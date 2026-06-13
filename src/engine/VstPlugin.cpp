#include "VstPlugin.h"
#include <cstring>
#include <QJsonArray>
#include <QByteArray>
#include <QDebug>
#include <signal.h>

#ifdef Q_OS_WIN
#include <windows.h>
static void *oql_dlopen(const char *path) { return static_cast<void*>(LoadLibraryA(path)); }
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

struct VstGuard {
    struct sigaction oldSegv, oldAbrt, oldIll, oldBus;
};

static void vstGuardInstall(VstGuard &g) {
    struct sigaction sa = {};
    sa.sa_handler = vstCrashHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g.oldSegv);
    sigaction(SIGABRT, &sa, &g.oldAbrt);
    sigaction(SIGILL,  &sa, &g.oldIll);
    sigaction(SIGBUS,  &sa, &g.oldBus);
}
static void vstGuardRestore(const VstGuard &g) {
    sigaction(SIGSEGV, &g.oldSegv, nullptr);
    sigaction(SIGABRT, &g.oldAbrt, nullptr);
    sigaction(SIGILL,  &g.oldIll,  nullptr);
    sigaction(SIGBUS,  &g.oldBus,  nullptr);
}
#else
static volatile sig_atomic_t tl_vstProcProtected = 0;
struct VstGuard {};
static void vstGuardInstall(VstGuard &) {}
static void vstGuardRestore(const VstGuard &) {}
#endif

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

VstPlugin::VstPlugin(const std::string &path) : m_path(path) {
    m_lib = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!m_lib) return;

    auto mainFn = reinterpret_cast<VstPluginMain>(dlsym(m_lib, "VSTPluginMain"));
    if (!mainFn)
        mainFn = reinterpret_cast<VstPluginMain>(dlsym(m_lib, "main"));
    if (!mainFn) { dlclose(m_lib); m_lib = nullptr; return; }

    VstGuard g;
    vstGuardInstall(g);

    s_vstProtected = 1;
#ifndef Q_OS_WIN
    if (sigsetjmp(s_vstJump, 1) != 0) {
        qWarning("VST2: plugin crashed during initialization, skipping: %s", path.c_str());
        s_vstProtected = 0;
        vstGuardRestore(g);
        m_effect = nullptr;
        dlclose(m_lib); m_lib = nullptr;
        return;
    }
#endif

    m_effect = mainFn(hostCallback);
    if (!m_effect || m_effect->magic != 0x56737450 /*'VstP'*/) {
        s_vstProtected = 0;
        vstGuardRestore(g);
        m_effect = nullptr;
        dlclose(m_lib); m_lib = nullptr;
        return;
    }

    m_effect->dispatcher(m_effect, effOpen, 0, 0, nullptr, 0.0f);
    char buf[256] = {};
    m_effect->dispatcher(m_effect, effGetEffectName, 0, 0, buf, 0.0f);
    m_name = (buf[0] != '\0') ? buf : path.substr(path.rfind('/') + 1);

    s_vstProtected = 0;
    vstGuardRestore(g);
}

VstPlugin::~VstPlugin() {
    if (m_effect) {
        VstGuard g;
        vstGuardInstall(g);
        s_vstProtected = 1;
#ifndef Q_OS_WIN
        if (sigsetjmp(s_vstJump, 1) == 0) {
#endif
            if (m_active)
                m_effect->dispatcher(m_effect, effMainsChanged, 0, 0, nullptr, 0.0f);
            if (!m_editorCrashed)
                m_effect->dispatcher(m_effect, effEditClose, 0, 0, nullptr, 0.0f);
            m_effect->dispatcher(m_effect, effClose, 0, 0, nullptr, 0.0f);
#ifndef Q_OS_WIN
        }
#endif
        s_vstProtected = 0;
        vstGuardRestore(g);
    }
    if (m_lib) dlclose(m_lib);
}

// ── prepare ───────────────────────────────────────────────────────────────────

void VstPlugin::prepare(int sampleRate, int blockSize) {
    if (!m_effect) return;
    m_sr    = sampleRate;
    m_block = blockSize;

    VstGuard g;
    vstGuardInstall(g);

    s_vstProtected = 1;
#ifndef Q_OS_WIN
    if (sigsetjmp(s_vstJump, 1) != 0) {
        qWarning("VST2: plugin crashed during prepare, disabling");
        s_vstProtected = 0;
        vstGuardRestore(g);
        m_effect = nullptr;
        return;
    }
#endif

    m_effect->dispatcher(m_effect, effSetSampleRate, 0, 0, nullptr, float(sampleRate));
    m_effect->dispatcher(m_effect, effSetBlockSize,  0, blockSize, nullptr, 0.0f);
    if (!m_active) {
        m_effect->dispatcher(m_effect, effMainsChanged, 0, 1, nullptr, 0.0f);
        m_active = true;

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

    s_vstProtected = 0;
    vstGuardRestore(g);

    m_inL.assign(blockSize, 0.0f); m_inR.assign(blockSize, 0.0f);
    m_outL.assign(blockSize, 0.0f); m_outR.assign(blockSize, 0.0f);
}

// ── process ───────────────────────────────────────────────────────────────────

void VstPlugin::process(float* const* inputs, float** outputs, int frames) {
    if (!m_effect || !AudioPlugin::isActive()) {
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

    VstGuard g;
    vstGuardInstall(g);

    tl_vstProcProtected = 1;
#ifndef Q_OS_WIN
    if (sigsetjmp(tl_vstProcJump, 1) != 0) {
        // tl_vstProcProtected already cleared by the handler
        qWarning("VST2: plugin crashed during processReplacing, disabling");
        vstGuardRestore(g);
        m_effect = nullptr;
        std::copy(inputs[0], inputs[0] + frames, outputs[0]);
        std::copy(inputs[1], inputs[1] + frames, outputs[1]);
        return;
    }
#endif

    // Acquire before processReplacing to block concurrent setParam from main thread.
    // Raw lock/unlock (not RAII): if the plugin crashes and siglongjmp fires, the
    // destructor is skipped and the mutex stays locked — but m_effect is set to null
    // in the crash path, so no future caller will ever try to acquire this lock again.
    m_effectMtx.lock();
    m_effect->processReplacing(m_effect, vstIn, vstOut, frames);
    m_effectMtx.unlock();
    tl_vstProcProtected = 0;
    vstGuardRestore(g);

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

    VstGuard g;
    vstGuardInstall(g);

    s_vstProtected = 1;
#ifndef Q_OS_WIN
    if (sigsetjmp(s_vstJump, 1) != 0) {
        qWarning("VST2: plugin crashed opening editor");
        s_vstProtected = 0;
        vstGuardRestore(g);
        m_editorCrashed = true;
        return false;
    }
#endif
    m_effect->dispatcher(m_effect, effEditOpen, 0, 0, parentWinId, 0.0f);
    s_vstProtected = 0;
    vstGuardRestore(g);
    return true;
}

void VstPlugin::closeEditor() {
    if (!m_effect || m_editorCrashed) return;

    VstGuard g;
    vstGuardInstall(g);

    s_vstProtected = 1;
#ifndef Q_OS_WIN
    if (sigsetjmp(s_vstJump, 1) != 0) {
        qWarning("VST2: plugin crashed during closeEditor");
        s_vstProtected = 0;
        vstGuardRestore(g);
        m_editorCrashed = true;
        return;
    }
#endif
    m_effect->dispatcher(m_effect, effEditClose, 0, 0, nullptr, 0.0f);
    s_vstProtected = 0;
    vstGuardRestore(g);
}

bool VstPlugin::getEditorRect(ERect **rect) const {
    if (!m_effect || !hasEditor() || !rect) return false;

    VstGuard g;
    vstGuardInstall(g);

    s_vstProtected = 1;
#ifndef Q_OS_WIN
    if (sigsetjmp(s_vstJump, 1) != 0) {
        qWarning("VST2: plugin crashed during getEditorRect");
        s_vstProtected = 0;
        vstGuardRestore(g);
        return false;
    }
#endif

    const bool ok = m_effect->dispatcher(m_effect, effEditGetRect, 0, 0, rect, 0.0f) != 0;
    s_vstProtected = 0;
    vstGuardRestore(g);
    return ok;
}

void VstPlugin::editorIdle() {
    if (!m_effect || m_editorCrashed) return;

    VstGuard g;
    vstGuardInstall(g);

    s_vstProtected = 1;
#ifndef Q_OS_WIN
    if (sigsetjmp(s_vstJump, 1) != 0) {
        qWarning("VST2: plugin crashed during editorIdle");
        s_vstProtected = 0;
        vstGuardRestore(g);
        m_editorCrashed = true;
        return;
    }
#endif
    m_effect->dispatcher(m_effect, effEditIdle, 0, 0, nullptr, 0.0f);
    s_vstProtected = 0;
    vstGuardRestore(g);
}

// ── JSON ──────────────────────────────────────────────────────────────────────

QJsonObject VstPlugin::toJson() const {
    auto obj = AudioPlugin::toJson();
    obj["path"] = QString::fromStdString(m_path);

    if (m_effect && (m_effect->flags & effFlagsProgramChunks)) {
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
        params.append(m_effect ? m_effect->getParameter(m_effect, i) : 0.0);
    obj["params"] = params;
    return obj;
}

void VstPlugin::fromJson(const QJsonObject &o) {
    AudioPlugin::fromJson(o);

    if (!o["chunk"].isUndefined() && m_effect && (m_effect->flags & effFlagsProgramChunks)) {
        const QByteArray raw = QByteArray::fromBase64(o["chunk"].toString().toLatin1());
        if (!raw.isEmpty()) {
            m_effect->dispatcher(m_effect, effSetChunk, 1 /*preset*/,
                                 intptr_t(raw.size()),
                                 const_cast<char*>(raw.constData()), 0.0f);
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
        setParam(i, v);
        m_pendingParams.push_back(v);
    }
}
