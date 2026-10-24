#include "VstPlugin.h"
#include <cstring>
#include <QJsonArray>
#include <QByteArray>
#include <QDebug>
#include <signal.h>
#include <mutex>
#include <thread>
#include <chrono>

#ifdef Q_OS_WIN
#include <windows.h>
#include <QString>
static void *oql_dlopen(const char *path) {
    // `path` arriva come UTF-8 (QString::toStdString()); LoadLibraryA usa la
    // code page ANSI e non trova silenziosamente un file con caratteri
    // accentati nel percorso — serve LoadLibraryW su una stringa UTF-16.
    const QString wpath = QString::fromUtf8(path);
    return static_cast<void*>(LoadLibraryW(reinterpret_cast<const wchar_t*>(wpath.utf16())));
}
static void *oql_dlsym(void *lib, const char *sym) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), sym));
}
static void  oql_dlclose(void *lib) { FreeLibrary(static_cast<HMODULE>(lib)); }
#define RTLD_LAZY  0
#define RTLD_LOCAL 0
#define dlopen(p, f) oql_dlopen(p)
#define dlsym(l, s)  oql_dlsym(l, s)
#define dlclose(l)   oql_dlclose(l)
#else
#include <dlfcn.h>
#include <setjmp.h>
#endif

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>

// I VST2 su macOS sono bundle (<Nome>.vst/Contents/MacOS/<Nome>), non un
// singolo file: dlopen() non apre la cartella del bundle direttamente, va
// risolto il binario Mach-O interno tramite CFBundle — lo stesso approccio
// usato da qualunque host VST su macOS. Ritorna una stringa vuota se
// `bundlePath` non è un bundle valido (es. già un binario diretto).
static std::string resolveMacVstBinaryPath(const std::string &bundlePath) {
    CFURLRef bundleUrl = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(bundlePath.c_str()),
        static_cast<CFIndex>(bundlePath.size()), true /*isDirectory*/);
    if (!bundleUrl) return {};

    CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, bundleUrl);
    CFRelease(bundleUrl);
    if (!bundle) return {};

    std::string result;
    if (CFURLRef execUrl = CFBundleCopyExecutableURL(bundle)) {
        char buf[1024] = {};
        if (CFURLGetFileSystemRepresentation(execUrl, true, reinterpret_cast<UInt8*>(buf), sizeof(buf)))
            result = buf;
        CFRelease(execUrl);
    }
    CFRelease(bundle);
    return result;
}
#endif

typedef AEffect* (*VstPluginMain)(VstHostCallback);

// ── Main-thread crash protection (loading / editor ops) ──────────────────────

static volatile sig_atomic_t s_vstProtected = 0;

// ── Audio-thread crash protection (process()) — thread-local ─────────────────

#ifndef Q_OS_WIN
static sigjmp_buf            s_vstJump;
static thread_local sigjmp_buf            tl_vstProcJump;
static thread_local volatile sig_atomic_t tl_vstProcProtected = 0;

static void vstCrashHandler(int sig) {
    if (tl_vstProcProtected) {
        tl_vstProcProtected = 0;
        siglongjmp(tl_vstProcJump, 1);
    }
    if (s_vstProtected)
        siglongjmp(s_vstJump, 1);
    struct sigaction sa = {};
    sa.sa_handler = SIG_DFL;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    raise(sig);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// Installa l'handler UNA VOLTA per processo, non ad ogni chiamata.
// sigaction() è una risorsa di PROCESSO, non di thread: installarla/
// ripristinarla ad ogni process() (audio thread) e ad ogni operazione sul
// main thread (prepare/openEditor/...) costava 8 syscall per blocco audio
// E creava una race reale fra i due thread che si sovrascrivevano a vicenda
// il "vecchio" handler salvato — con la finestra apertasi, un crash poteva
// restare senza protezione, o un siglongjmp finire nel jmp_buf sbagliato.
// L'handler resta installato per tutta la vita del processo: quando non c'è
// nessuna chiamata VST "protetta" (s_vstProtected/tl_vstProcProtected a 0) si
// comporta come se non ci fosse — ripristina SIG_DFL e ri-rilancia — quindi
// non altera il comportamento per crash non legati a plugin.
static void installVstCrashHandlerOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        struct sigaction sa = {};
        sa.sa_handler = vstCrashHandler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGILL,  &sa, nullptr);
        sigaction(SIGBUS,  &sa, nullptr);
    });
}
#else
// Windows non ha sigaction/sigsetjmp: l'equivalente nativo è SEH
// (Structured Exception Handling, __try/__except), che a differenza del
// meccanismo POSIX non richiede un "handler" installato a parte — la
// protezione è interamente strutturale, un __try per ogni chiamata a rischio
// (vedi VST_SEH_TRY/VST_SEH_CATCH sotto). installVstCrashHandlerOnce() resta
// quindi un no-op qui, mantenuto solo per non dover diramare ogni call site
// con un ulteriore #ifdef.
//
// ATTENZIONE: percorso scritto senza poter compilare con MSVC su questa
// macchina (solo Linux disponibile) — verificato dalla build Windows in CI,
// ma il comportamento A RUNTIME va confermato su una macchina Windows reale
// (vedi la checklist dei test nel doc del piano).
static volatile sig_atomic_t tl_vstProcProtected = 0;
static void installVstCrashHandlerOnce() {}

// Eccezioni hardware che vale la pena intercettare (equivalente Windows di
// SIGSEGV/SIGILL/SIGBUS/SIGFPE). EXCEPTION_STACK_OVERFLOW è esclusa
// deliberatamente: a quel punto lo stack è già corrotto e continuare in
// sicurezza richiederebbe _resetstkoflw() — qui si lascia che il processo
// termini, come capiterebbe comunque nella pratica anche per un SIGSEGV da
// stack overflow su Linux/macOS.
static int vstSehFilter(unsigned int code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return EXCEPTION_EXECUTE_HANDLER;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
}
// Ogni call site usa questi due macro per restare leggibile e simmetrico
// rispetto al ramo POSIX (sigsetjmp prima del lavoro, gestione del crash
// nel corpo dell'if): VST_SEH_TRY apre il __try, VST_SEH_CATCH apre
// l'__except — il corpo tra i due è lo stesso lavoro "a rischio" di sempre.
#define VST_SEH_TRY    __try {
#define VST_SEH_CATCH  } __except (vstSehFilter(GetExceptionCode())) {
#define VST_SEH_END    }
#endif

// Prova ad acquisire il lock per un tempo limitato invece di bloccare per
// sempre: nel caso normale il thread audio lo tiene solo per la durata di
// un singolo blocco (microsecondi), quindi pochi tentativi bastano quasi
// sempre; se restasse bloccato per sempre (il thread audio crashato mentre
// lo teneva, saltando l'unlock via siglongjmp — un caso raro, comunque
// protetto a parte dal controllo "m_effect è null?" prima di ogni lock),
// questo non blocca comunque il main thread all'infinito. Usata da
// toJson()/fromJson() — chiamate rare (salvataggio, snapshot undo) dove
// perdere lo stato del plugin per colpa di una race non è accettabile,
// a differenza di setParam() (tipicamente a frequenza di drag di uno
// slider) che resta un try_lock() a singolo tentativo apposta.
static bool tryLockBounded(std::mutex &m, int maxWaitUs = 10000) {
    for (int waited = 0; waited < maxWaitUs; waited += 50) {
        if (m.try_lock()) return true;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    return false;
}

// Minimal host callback — VST2 plugins call this to query host capabilities.
intptr_t VstPlugin::hostCallback(AEffect* /*effect*/, int32_t opcode,
                                  int32_t /*index*/, intptr_t /*value*/,
                                  void* /*ptr*/, float /*opt*/)
{
    switch (opcode) {
    case 1:  return 2400;   // audioMasterVersion → VST 2.4
    case 2:  return 0;      // audioMasterCurrentId
    case 6:  return 1;      // audioMasterWantMidi (ignored)
    default: return 0;
    }
}

// ── Constructor / destructor ──────────────────────────────────────────────────

#ifdef Q_OS_WIN
// Isolato dal costruttore: MSVC vieta __try in un costruttore/distruttore
// (C2712 — richiedono object unwinding implicito per i membri, incompatibile
// con SEH nella stessa funzione). Nessun parametro/locale con distruttore
// non banale qui dentro, solo puntatori e un char buffer POD.
static AEffect* vstSehInitPlugin(VstPluginMain mainFn, VstHostCallback cb,
                                  char *nameBufOut, bool *crashed)
{
    AEffect *effect = nullptr;
    VST_SEH_TRY
        effect = mainFn(cb);
        if (effect && effect->magic == 0x56737450 /*'VstP'*/) {
            effect->dispatcher(effect, effOpen, 0, 0, nullptr, 0.0f);
            effect->dispatcher(effect, effGetEffectName, 0, 0, nameBufOut, 0.0f);
        }
    VST_SEH_CATCH
        effect = nullptr;
        *crashed = true;
    VST_SEH_END
    return effect;
}
#endif

VstPlugin::VstPlugin(const std::string &path) : m_path(path) {
    std::string loadPath = path;
#ifdef Q_OS_MACOS
    if (const std::string resolved = resolveMacVstBinaryPath(path); !resolved.empty())
        loadPath = resolved;
#endif
    m_lib = dlopen(loadPath.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!m_lib) return;

    auto mainFn = reinterpret_cast<VstPluginMain>(dlsym(m_lib, "VSTPluginMain"));
    if (!mainFn)
        mainFn = reinterpret_cast<VstPluginMain>(dlsym(m_lib, "main"));
    if (!mainFn) { dlclose(m_lib); m_lib = nullptr; return; }

    installVstCrashHandlerOnce();
    s_vstProtected = 1;
    char nameBuf[256] = {};
#ifdef Q_OS_WIN
    bool crashed = false;
    m_effect = vstSehInitPlugin(mainFn, hostCallback, nameBuf, &crashed);
    if (crashed) {
        qWarning("VST2: plugin crashed during initialization, skipping: %s", path.c_str());
        s_vstProtected = 0;
        m_effect = nullptr;
        dlclose(m_lib); m_lib = nullptr;
        return;
    }
#else
    if (sigsetjmp(s_vstJump, 1) != 0) {
        qWarning("VST2: plugin crashed during initialization, skipping: %s", path.c_str());
        s_vstProtected = 0;
        m_effect = nullptr;
        dlclose(m_lib); m_lib = nullptr;
        return;
    }
    m_effect = mainFn(hostCallback);
    if (m_effect && m_effect->magic == 0x56737450 /*'VstP'*/) {
        m_effect->dispatcher(m_effect, effOpen, 0, 0, nullptr, 0.0f);
        m_effect->dispatcher(m_effect, effGetEffectName, 0, 0, nameBuf, 0.0f);
    }
#endif

    if (!m_effect || m_effect->magic != 0x56737450 /*'VstP'*/) {
        s_vstProtected = 0;
        m_effect = nullptr;
        dlclose(m_lib); m_lib = nullptr;
        return;
    }

    if (nameBuf[0] != '\0') {
        m_name = nameBuf;
    } else {
        // path.rfind('/') su un percorso Windows (solo '\') darebbe npos+1==0,
        // cioè l'intero percorso invece del solo nome file: si cerca entrambi.
        const size_t slash = path.find_last_of("/\\");
        m_name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    }

    s_vstProtected = 0;
}

#ifdef Q_OS_WIN
// Isolato dal distruttore per lo stesso motivo del costruttore sopra
// (C2712). Nessun locale con distruttore non banale: solo il puntatore e
// due bool passati per valore.
static void vstSehTeardown(AEffect *effect, bool mainsOn, bool editorCrashed) {
    VST_SEH_TRY
        if (mainsOn)
            effect->dispatcher(effect, effMainsChanged, 0, 0, nullptr, 0.0f);
        if (!editorCrashed)
            effect->dispatcher(effect, effEditClose, 0, 0, nullptr, 0.0f);
        effect->dispatcher(effect, effClose, 0, 0, nullptr, 0.0f);
    VST_SEH_CATCH
        // Durante il teardown non c'è molto da "recuperare": si ignora,
        // come fa già il ramo POSIX in questo stesso caso (nessun log,
        // nessuna azione — si prosegue con dlclose() comunque sotto).
    VST_SEH_END
}
#endif

VstPlugin::~VstPlugin() {
    if (m_effect) {
        installVstCrashHandlerOnce();
        s_vstProtected = 1;
#ifdef Q_OS_WIN
        vstSehTeardown(m_effect, m_mainsOn, m_editorCrashed);
#else
        if (sigsetjmp(s_vstJump, 1) == 0) {
            if (m_mainsOn)
                m_effect->dispatcher(m_effect, effMainsChanged, 0, 0, nullptr, 0.0f);
            if (!m_editorCrashed)
                m_effect->dispatcher(m_effect, effEditClose, 0, 0, nullptr, 0.0f);
            m_effect->dispatcher(m_effect, effClose, 0, 0, nullptr, 0.0f);
        }
#endif
        s_vstProtected = 0;
    }
    if (m_lib) dlclose(m_lib);
}

// ── prepare ───────────────────────────────────────────────────────────────────

void VstPlugin::prepare(int sampleRate, int blockSize) {
    if (!m_effect) return;
    m_sr    = sampleRate;
    m_block = blockSize;

    installVstCrashHandlerOnce();
    s_vstProtected = 1;
#ifdef Q_OS_WIN
    VST_SEH_TRY
#else
    if (sigsetjmp(s_vstJump, 1) != 0) {
        qWarning("VST2: plugin crashed during prepare, disabling");
        s_vstProtected = 0;
        m_effect = nullptr;
        return;
    }
#endif

    m_effect->dispatcher(m_effect, effSetSampleRate, 0, 0, nullptr, float(sampleRate));
    m_effect->dispatcher(m_effect, effSetBlockSize,  0, blockSize, nullptr, 0.0f);
    if (!m_mainsOn) {
        m_effect->dispatcher(m_effect, effMainsChanged, 0, 1, nullptr, 0.0f);
        m_mainsOn = true;

        // Re-apply params after effMainsChanged: ZynFX resets internal state on activation
        for (int i = 0; i < int(m_pendingParams.size()) && i < m_effect->numParams; ++i)
            m_effect->setParameter(m_effect, i, m_pendingParams[i]);
        m_pendingParams.clear();

        // Apply deferred chunk state (chunk-based plugins, after effMainsChanged)
        if (!m_pendingChunk.isEmpty()) {
            m_effect->dispatcher(m_effect, effSetChunk, 1 /*preset*/,
                                 intptr_t(m_pendingChunk.size()),
                                 m_pendingChunk.data(), 0.0f);
            m_pendingChunk.clear();
        }
    }

#ifdef Q_OS_WIN
    VST_SEH_CATCH
        qWarning("VST2: plugin crashed during prepare, disabling");
        s_vstProtected = 0;
        m_effect = nullptr;
        return;
    VST_SEH_END
#endif

    s_vstProtected = 0;

    m_inL.assign(blockSize, 0.0f); m_inR.assign(blockSize, 0.0f);
    m_outL.assign(blockSize, 0.0f); m_outR.assign(blockSize, 0.0f);
}

// ── process ───────────────────────────────────────────────────────────────────

void VstPlugin::process(float* const* inputs, float** outputs, int frames) {
    // Il bypass (isActive()) è gestito centralmente da PluginChain::process(),
    // che copia input→output senza chiamare questo metodo quando il plugin è
    // disattivato: qui serve solo il controllo "il plugin non è mai stato
    // caricato correttamente".
    if (!m_effect) {
        if (inputs != outputs) {
            std::copy(inputs[0], inputs[0] + frames, outputs[0]);
            std::copy(inputs[1], inputs[1] + frames, outputs[1]);
        }
        return;
    }
    if (!m_effect->processReplacing) return;

    if (int(m_inL.size()) < frames) {
        m_inL.resize(frames); m_inR.resize(frames);
        m_outL.resize(frames); m_outR.resize(frames);
    }
    std::copy(inputs[0], inputs[0] + frames, m_inL.data());
    std::copy(inputs[1], inputs[1] + frames, m_inR.data());

    float* vstIn[2]  = { m_inL.data(), m_inR.data()  };
    float* vstOut[2] = { m_outL.data(), m_outR.data() };

    // try_lock, non lock(): bloccare qui se il main thread sta facendo un
    // setParam()/toJson()/fromJson() (vedi A4) trasformerebbe una breve
    // attesa in un vero dropout udibile sul thread audio. Se occupato, si
    // passa il segnale invariato per questo solo blocco — impercettibile, a
    // differenza di un drop-out.
    if (!m_effectMtx.try_lock()) {
        std::copy(inputs[0], inputs[0] + frames, outputs[0]);
        std::copy(inputs[1], inputs[1] + frames, outputs[1]);
        return;
    }

    // L'handler è già installato (vedi installVstCrashHandlerOnce): a
    // differenza di prima, qui non si tocca più sigaction() a ogni blocco —
    // solo il flag/jmp_buf thread-local di questa chiamata.
    tl_vstProcProtected = 1;
#ifdef Q_OS_WIN
    VST_SEH_TRY
#else
    if (sigsetjmp(tl_vstProcJump, 1) != 0) {
        // tl_vstProcProtected already cleared by the handler. Il mutex resta
        // bloccato (il salto ha scavalcato l'unlock qui sotto): va bene,
        // m_effect è già null quindi nessuno proverà più a riprenderlo
        // (ogni chiamante controlla m_effect prima di tentare il lock).
        qWarning("VST2: plugin crashed during processReplacing, disabling");
        m_effect = nullptr;
        std::copy(inputs[0], inputs[0] + frames, outputs[0]);
        std::copy(inputs[1], inputs[1] + frames, outputs[1]);
        return;
    }
#endif

    m_effect->processReplacing(m_effect, vstIn, vstOut, frames);
    m_effectMtx.unlock();
    tl_vstProcProtected = 0;

#ifdef Q_OS_WIN
    VST_SEH_CATCH
        // Mutex volutamente non rilasciato (vedi commento sul ramo POSIX
        // sopra): m_effect diventa null, nessuno proverà più a riprenderlo.
        qWarning("VST2: plugin crashed during processReplacing, disabling");
        m_effect = nullptr;
        std::copy(inputs[0], inputs[0] + frames, outputs[0]);
        std::copy(inputs[1], inputs[1] + frames, outputs[1]);
        return;
    VST_SEH_END
#endif

    std::copy(m_outL.data(), m_outL.data() + frames, outputs[0]);
    std::copy(m_outR.data(), m_outR.data() + frames, outputs[1]);
}

// ── params ────────────────────────────────────────────────────────────────────

int VstPlugin::numParams() const {
    return m_effect ? m_effect->numParams : 0;
}

PluginParam VstPlugin::param(int idx) const {
    PluginParam p;
    if (!m_effect || idx >= m_effect->numParams) return p;

    // A4: dispatcher()/getParameter() leggono lo stato del plugin, che il
    // thread audio può stare toccando dentro processReplacing() — senza il
    // lock era una lettura concorrente non protetta. Chiamato a frequenza di
    // drag di uno slider: try_lock a singolo tentativo, non la versione
    // "bounded" di toJson/fromJson — se occupato si ritorna il default per
    // questa sola chiamata (il prossimo refresh della UI lo richiama).
    if (!m_effectMtx.try_lock()) return p;
    std::lock_guard<std::mutex> lock(m_effectMtx, std::adopt_lock);
    if (!m_effect) return p;

    char buf[256] = {};
    m_effect->dispatcher(m_effect, effGetParamName, idx, 0, buf, 0.0f);
    p.name = buf;
    buf[0] = '\0';
    m_effect->dispatcher(m_effect, effGetParamLabel, idx, 0, buf, 0.0f);
    p.unit = buf;
    p.value       = m_effect->getParameter(m_effect, idx);
    p.defaultVal  = p.value;
    p.minVal      = 0.0f;
    p.maxVal      = 1.0f;
    return p;
}

void VstPlugin::setParam(int idx, float v) {
    if (!m_effect || idx >= m_effect->numParams) return;
    // try_lock: if the audio thread is mid-processReplacing, drop this update rather
    // than blocking. The slider will send another update on the next move.
    // Also safe post-crash: m_effect is null so we already returned above.
    if (!m_effectMtx.try_lock()) return;
    if (m_effect && idx < m_effect->numParams)
        m_effect->setParameter(m_effect, idx, v);
    m_effectMtx.unlock();
}

// ── editor ────────────────────────────────────────────────────────────────────

bool VstPlugin::hasEditor() const {
    return m_effect && (m_effect->flags & effFlagsHasEditor) && !m_editorCrashed;
}

bool VstPlugin::openEditor(void* parentWinId) {
    if (!hasEditor()) return false;

    installVstCrashHandlerOnce();
    s_vstProtected = 1;
#ifdef Q_OS_WIN
    VST_SEH_TRY
#else
    if (sigsetjmp(s_vstJump, 1) != 0) {
        qWarning("VST2: plugin crashed opening editor");
        s_vstProtected = 0;
        m_editorCrashed = true;
        return false;
    }
#endif
    m_effect->dispatcher(m_effect, effEditOpen, 0, 0, parentWinId, 0.0f);
#ifdef Q_OS_WIN
    VST_SEH_CATCH
        qWarning("VST2: plugin crashed opening editor");
        s_vstProtected = 0;
        m_editorCrashed = true;
        return false;
    VST_SEH_END
#endif
    s_vstProtected = 0;
    return true;
}

void VstPlugin::closeEditor() {
    if (!m_effect || m_editorCrashed) return;

    installVstCrashHandlerOnce();
    s_vstProtected = 1;
#ifdef Q_OS_WIN
    VST_SEH_TRY
#else
    if (sigsetjmp(s_vstJump, 1) != 0) {
        qWarning("VST2: plugin crashed during closeEditor");
        s_vstProtected = 0;
        m_editorCrashed = true;
        return;
    }
#endif
    m_effect->dispatcher(m_effect, effEditClose, 0, 0, nullptr, 0.0f);
#ifdef Q_OS_WIN
    VST_SEH_CATCH
        qWarning("VST2: plugin crashed during closeEditor");
        s_vstProtected = 0;
        m_editorCrashed = true;
        return;
    VST_SEH_END
#endif
    s_vstProtected = 0;
}

bool VstPlugin::getEditorRect(ERect **rect) const {
    if (!m_effect || !hasEditor() || !rect) return false;

    installVstCrashHandlerOnce();
    s_vstProtected = 1;
    bool ok = false;  // dichiarata fuori dal __try: MSVC chiude lo scope di __try come un blocco normale
#ifdef Q_OS_WIN
    VST_SEH_TRY
#else
    if (sigsetjmp(s_vstJump, 1) != 0) {
        qWarning("VST2: plugin crashed during getEditorRect");
        s_vstProtected = 0;
        return false;
    }
#endif

    ok = m_effect->dispatcher(m_effect, effEditGetRect, 0, 0, rect, 0.0f) != 0;
#ifdef Q_OS_WIN
    VST_SEH_CATCH
        qWarning("VST2: plugin crashed during getEditorRect");
        s_vstProtected = 0;
        return false;
    VST_SEH_END
#endif
    s_vstProtected = 0;
    return ok;
}

void VstPlugin::editorIdle() {
    if (!m_effect || m_editorCrashed) return;

    installVstCrashHandlerOnce();
    s_vstProtected = 1;
#ifdef Q_OS_WIN
    VST_SEH_TRY
#else
    if (sigsetjmp(s_vstJump, 1) != 0) {
        qWarning("VST2: plugin crashed during editorIdle");
        s_vstProtected = 0;
        m_editorCrashed = true;
        return;
    }
#endif
    m_effect->dispatcher(m_effect, effEditIdle, 0, 0, nullptr, 0.0f);
#ifdef Q_OS_WIN
    VST_SEH_CATCH
        qWarning("VST2: plugin crashed during editorIdle");
        s_vstProtected = 0;
        m_editorCrashed = true;
        return;
    VST_SEH_END
#endif
    s_vstProtected = 0;
}

// ── JSON ──────────────────────────────────────────────────────────────────────

QJsonObject VstPlugin::toJson() const {
    auto obj = AudioPlugin::toJson();
    obj["path"] = QString::fromStdString(m_path);
    if (!m_effect) return obj;

    // A4: effGetChunk/getParameter leggono lo stato del plugin mentre il
    // thread audio può stare dentro processReplacing() — senza il lock era
    // una lettura concorrente non protetta. toJson() è chiamato spesso
    // (ogni operazione "undoable" e ogni tick dell'auto-salvataggio delle
    // proprietà serializzano l'intera catena di plugin): a differenza di
    // param(), perdere lo stato qui vorrebbe dire salvare un preset
    // incompleto, quindi si usa il tentativo "bounded" invece del singolo
    // try_lock di param()/setParam().
    if (!tryLockBounded(m_effectMtx)) return obj;
    std::lock_guard<std::mutex> lock(m_effectMtx, std::adopt_lock);
    if (!m_effect) return obj;  // un crash concorrente può averlo appena azzerato

    if (m_effect->flags & effFlagsProgramChunks) {
        void *chunkPtr = nullptr;
        const intptr_t chunkSize =
            m_effect->dispatcher(m_effect, effGetChunk, 1 /*preset*/, 0, &chunkPtr, 0.0f);
        if (chunkSize > 0 && chunkPtr) {
            const QByteArray raw(static_cast<const char*>(chunkPtr), int(chunkSize));
            obj["chunk"] = QString::fromLatin1(raw.toBase64());
            return obj;
        }
    }

    QJsonArray params;
    for (int i = 0; i < numParams(); ++i)
        params.append(m_effect->getParameter(m_effect, i));
    obj["params"] = params;
    return obj;
}

void VstPlugin::fromJson(const QJsonObject &o) {
    AudioPlugin::fromJson(o);

    if (!o["chunk"].isUndefined() && m_effect && (m_effect->flags & effFlagsProgramChunks)) {
        const QByteArray raw = QByteArray::fromBase64(o["chunk"].toString().toLatin1());
        if (!raw.isEmpty()) {
            // A4: solo la chiamata diretta a dispatcher() va protetta (stesso
            // ragionamento di toJson()); il fallback qui sotto delega a
            // setParam(), che ha già il suo try_lock — prendere di nuovo lo
            // stesso mutex (non ricorsivo) in quel ramo farebbe deadlock.
            if (m_effect && tryLockBounded(m_effectMtx)) {
                std::lock_guard<std::mutex> lock(m_effectMtx, std::adopt_lock);
                if (m_effect)
                    m_effect->dispatcher(m_effect, effSetChunk, 1 /*preset*/,
                                         intptr_t(raw.size()),
                                         const_cast<char*>(raw.constData()), 0.0f);
            }
            m_pendingChunk = raw;
            return;
        }
    }

    // Fallback: float parameters — set immediately (for toJson on unprepared plugins)
    // and save as pending to re-apply after effMainsChanged (ZynFX resets on activation)
    const auto params = o["params"].toArray();
    m_pendingParams.clear();
    for (int i = 0; i < params.size() && i < numParams(); ++i) {
        const float v = float(params[i].toDouble());
        setParam(i, v);  // già protetto internamente (try_lock)
        m_pendingParams.push_back(v);
    }
}
