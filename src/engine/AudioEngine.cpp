#include "AudioEngine.h"
#include "miniaudio.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>
#include <QDebug>

// ── Protezione dai denormali (A7) ────────────────────────────────────────────
// Reverb/Delay/Phaser/Chorus (BuiltinPlugins.h) hanno cicli di feedback: quando
// una coda decade verso lo zero i valori entrano in denormali e il costo CPU
// di ogni operazione in virgola mobile sale di 10-100×, proprio nei silenzi
// tra le cue — quando meno ce lo si aspetta. FTZ/DAZ (azzera i denormali in
// uscita/ingresso dalle unità FP) va impostato una volta per thread, non è
// un flag di processo. Impostato qui perché ogni Output ha il proprio thread
// audio indipendente (vedi ma_device_config::dataCallback) e questa è la
// prima funzione chiamata su quel thread per ogni blocco.
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#include <pmmintrin.h>
static inline void ensureDenormalProtectionOnThisThread() {
    static thread_local bool done = false;
    if (done) return;
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    done = true;
}
#elif (defined(__aarch64__) || defined(__arm64__)) && (defined(__GNUC__) || defined(__clang__))
#include <cstdint>
static inline void ensureDenormalProtectionOnThisThread() {
    static thread_local bool done = false;
    if (done) return;
    uint64_t fpcr = 0;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24);  // FZ: tratta i denormali come zero (ARM AArch64)
    __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr));
    done = true;
}
#else
static inline void ensureDenormalProtectionOnThisThread() {}
#endif

// ── Bluetooth keepalive ping ────────────────────────────────────────────────
// Stessa tecnica/parametri di BlueCue (bt-multizone-audio): un tono a 18kHz,
// quasi impercettibile all'orecchio umano, ripetuto ogni 20s per 60ms (con
// fade in/out di 10ms per evitare click), mixato in ogni uscita aperta.
// Le casse Bluetooth spengono l'amplificatore/si disconnettono dopo un po'
// di silenzio: questo tono tiene il link "vivo" senza che si senta durante
// le pause tra le cue.
namespace {
constexpr double kPingFrequencyHz  = 18000.0;
constexpr float  kPingAmplitude    = 0.0015f;
constexpr double kPingDurationSec  = 0.06;
constexpr double kPingPeriodSec    = 20.0;
constexpr double kPingFadeSec      = 0.010;
}

// ── pimpl ─────────────────────────────────────────────────────────────────────

// Una uscita fisica: il suo ma_device, i renderer che vi sono instradati e lo
// stato (indipendente per ciascuna uscita) del tono di keepalive Bluetooth.
struct AudioEngine::Output {
    ma_device   device{};
    std::string name;              // "" = dispositivo di default di sistema
    bool        isMain = false;    // true solo per outputs[0] (vedi Impl)

    std::mutex                  mutex;   // protegge renderers durante add/remove/mix
    std::vector<AudioRenderer*> renderers;
    std::vector<float>          mixBuf;
    // Riusato ad ogni callback invece di un std::vector locale: prima si
    // allocava una nuova lista ogni volta che una cue finiva — rara, ma
    // un'allocazione nel thread audio resta un'allocazione (A5).
    std::vector<AudioRenderer*> finishedScratch;

    // Stato del generatore di keepalive — toccato solo dal thread audio di
    // questa uscita, nessuna sincronizzazione necessaria.
    double   pingPhase       = 0.0;
    uint64_t pingSampleClock = 0;
};

struct AudioEngine::Impl {
    ma_context context{};

    // outputs[0] è sempre l'uscita "principale" (quella di init()/reinitWithDevice()).
    // Le successive vengono create al volo da addRenderer() quando una cue
    // chiede un'uscita diversa da quella principale.
    std::mutex                                        outputsMutex;
    std::vector<std::unique_ptr<AudioEngine::Output>> outputs;
};

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { shutdown(); }

// ── Singleton ─────────────────────────────────────────────────────────────────

AudioEngine &AudioEngine::instance() {
    static AudioEngine inst;
    return inst;
}

// ── Mixing + keepalive (thread audio di UNA specifica uscita) ─────────────────

static void mixKeepAlivePing(AudioEngine::Output *o, float *out, int frameCount, int sampleRate) {
    const double rate = sampleRate > 0 ? sampleRate : 48000;
    const auto periodSamples = static_cast<uint64_t>(std::max(0.001, kPingPeriodSec) * rate);
    const auto pingSamples   = static_cast<uint64_t>(std::max(0.0, kPingDurationSec) * rate);
    const auto fadeSamples   = static_cast<uint64_t>(kPingFadeSec * rate);
    const double phaseIncrement = 2.0 * std::numbers::pi * kPingFrequencyHz / rate;

    for (int i = 0; i < frameCount; ++i) {
        const uint64_t phase = periodSamples > 0 ? (o->pingSampleClock % periodSamples) : 0;
        if (phase < pingSamples) {
            float envelope = 1.0f;
            if (phase < fadeSamples)
                envelope = float(phase) / float(fadeSamples);
            else if (phase >= pingSamples - fadeSamples)
                envelope = float(pingSamples - phase) / float(fadeSamples);
            const float sample = kPingAmplitude * envelope * float(std::sin(o->pingPhase));
            out[i * 2]     += sample;
            out[i * 2 + 1] += sample;
        }
        o->pingPhase += phaseIncrement;
        if (o->pingPhase > 2.0 * std::numbers::pi)
            o->pingPhase -= 2.0 * std::numbers::pi;
        ++o->pingSampleClock;
    }
}

// friend di AudioEngine (dichiarato in AudioEngine.h): accede a
// AudioEngine::instance()'s m_sampleRate/m_peakL/m_peakR/m_dropoutCount solo
// per l'uscita principale (out_->isMain).
void audioDeviceCallback(AudioEngine::Output *out_, void *pOutput, unsigned int frameCount) {
    ensureDenormalProtectionOnThisThread();
    AudioEngine &engine = AudioEngine::instance();
    float *out  = static_cast<float *>(pOutput);
    const int n = int(frameCount) * 2;  // stereo interleaved
    std::fill(out, out + n, 0.0f);

    std::unique_lock<std::mutex> lock(out_->mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        if (out_->isMain) engine.m_dropoutCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    auto &mb = out_->mixBuf;
    if (int(mb.size()) < n) mb.resize(n);

    auto &toFinish = out_->finishedScratch;
    toFinish.clear();  // non libera la capacità già allocata, solo la dimensione logica
    for (auto *r : out_->renderers) {
        std::fill(mb.begin(), mb.begin() + n, 0.0f);
        if (!r->renderAudio(mb.data(), int(frameCount), engine.m_sampleRate)) {
            toFinish.push_back(r);
        } else {
            for (int i = 0; i < n; ++i)
                out[i] += mb[i];
        }
    }

    mixKeepAlivePing(out_, out, int(frameCount), engine.m_sampleRate);

    if (out_->isMain) {
        float pkL = 0.0f, pkR = 0.0f;
        for (int i = 0; i < n; i += 2) {
            const float l = std::abs(out[i]),   r = std::abs(out[i + 1]);
            if (l > pkL) pkL = l;
            if (r > pkR) pkR = r;
        }
        engine.m_peakL.store(pkL, std::memory_order_relaxed);
        engine.m_peakR.store(pkR, std::memory_order_relaxed);
    }

    // Rimuove e notifica i renderer terminati — sempre con il lock tenuto,
    // così se il distruttore di un renderer chiama removeRenderer() si blocca
    // finché questo giro (onRenderFinished incluso) non è completo, evitando
    // use-after-free sul puntatore.
    for (auto *r : toFinish) {
        auto it = std::find(out_->renderers.begin(), out_->renderers.end(), r);
        if (it != out_->renderers.end())
            out_->renderers.erase(it);
        r->onRenderFinished();  // safe: posta solo un evento Qt in coda
    }
}

// Trova l'id del device di sistema che corrisponde a `name` (name vuoto = default).
static bool resolveDeviceId(ma_context *ctx, const std::string &name, ma_device_id *outId) {
    if (name.empty()) return false;
    ma_device_info *pInfos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(ctx, &pInfos, &count, nullptr, nullptr) != MA_SUCCESS)
        return false;
    for (ma_uint32 i = 0; i < count; ++i) {
        if (std::string(pInfos[i].name) == name) {
            *outId = pInfos[i].id;
            return true;
        }
    }
    return false;
}

// Apre e avvia un nuovo Output. Per le uscite extra (isMain=false) forza lo
// stesso sample rate della principale (invece di lasciar scegliere al
// device) così tutte le cue possono preparare decoder/plugin chain usando
// AudioEngine::sampleRate()/blockSize() senza sapere quale uscita fisica
// finiranno per usare — miniaudio ricampiona se il device non lo supporta.
static bool startOutput(ma_context *ctx, AudioEngine::Output *out_,
                         const std::string &deviceName, int sampleRate, bool isMain)
{
    out_->name   = deviceName;
    out_->isMain = isMain;

    ma_device_id selectedId{};
    const bool hasId = resolveDeviceId(ctx, deviceName, &selectedId);
    // Diagnostica per il bug "l'uscita selezionata per la cue non cambia
    // davvero" (P8, segnalato dall'utente su una AudioCue normale): se il
    // nome non viene risolto a un ID device, miniaudio apre comunque il
    // device di SISTEMA predefinito e ma_device_init() ha successo lo
    // stesso — nessun errore visibile da nessuna parte, l'uscita sembra
    // "cambiata" ma suona dal device sbagliato. Questo avviso distingue
    // "nome non trovato nell'enumerazione" da "trovato ma non instrada
    // davvero" (un problema di miniaudio/backend audio, non di OQL).
    if (!deviceName.empty() && !hasId)
        qWarning("AudioEngine: uscita \"%s\" non trovata tra i device enumerati — "
                 "uso il device di sistema predefinito per questa uscita %s",
                 deviceName.c_str(), isMain ? "principale" : "extra");

    ma_device_config cfg   = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format    = ma_format_f32;
    cfg.playback.channels  = 2;
    cfg.sampleRate         = isMain ? 0 : ma_uint32(sampleRate);  // 0 = lascia scegliere al device
    cfg.periodSizeInFrames = 512;
    cfg.pUserData          = out_;
    cfg.dataCallback        = [](ma_device *d, void *pOut, const void *, ma_uint32 frames) {
        audioDeviceCallback(static_cast<AudioEngine::Output *>(d->pUserData), pOut, frames);
    };
    if (hasId) cfg.playback.pDeviceID = &selectedId;

    if (ma_device_init(ctx, &cfg, &out_->device) != MA_SUCCESS)
        return false;
    qWarning("AudioEngine: uscita %s aperta — richiesta \"%s\", id risolto: %s, "
             "device miniaudio effettivamente aperto: \"%s\"",
             isMain ? "principale" : "extra",
             deviceName.c_str(), hasId ? "sì" : "no",
             out_->device.playback.name);
    if (ma_device_start(&out_->device) != MA_SUCCESS) {
        ma_device_uninit(&out_->device);
        return false;
    }
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool AudioEngine::init(const std::string &deviceName) {
    if (m_initialized) return true;
    m_impl = std::make_unique<Impl>();

    if (ma_context_init(nullptr, 0, nullptr, &m_impl->context) != MA_SUCCESS)
        return false;

    auto main = std::make_unique<Output>();
    Output *mainPtr = main.get();
    m_impl->outputs.push_back(std::move(main));

    if (!startOutput(&m_impl->context, mainPtr, deviceName, m_sampleRate, true)) {
        ma_context_uninit(&m_impl->context);
        m_impl->outputs.clear();
        return false;
    }

    m_sampleRate        = int(mainPtr->device.sampleRate);
    m_blockSize         = int(mainPtr->device.playback.internalPeriodSizeInFrames);
    if (m_blockSize <= 0) m_blockSize = 512;
    m_currentDeviceName = deviceName;
    m_initialized       = true;
    return true;
}

std::vector<AudioDeviceInfo> AudioEngine::enumerateDevices() const {
    std::vector<AudioDeviceInfo> result;
    if (!m_impl) return result;
    ma_device_info *pInfos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&m_impl->context, &pInfos, &count, nullptr, nullptr) != MA_SUCCESS)
        return result;
    result.reserve(count);
    for (ma_uint32 i = 0; i < count; ++i) {
        AudioDeviceInfo di;
        di.name      = pInfos[i].name;
        di.isDefault = bool(pInfos[i].isDefault);
        result.push_back(std::move(di));
    }
    return result;
}

bool AudioEngine::reinitWithDevice(const std::string &deviceName) {
    if (!m_initialized) return false;

    std::lock_guard<std::mutex> outputsLock(m_impl->outputsMutex);

    // Bug segnalato dall'utente: cambiare l'uscita da Impostazioni →
    // Hardware non spostava davvero l'audio — confermato con un log
    // diagnostico che miniaudio riportava successo e il nome del device
    // corretto, ma il suono continuava a uscire da quello vecchio. A
    // differenza di questo, il percorso "uscita extra per cue"
    // (addRenderer(), più sotto) funziona correttamente, e la sola
    // differenza strutturale è che QUELLO crea sempre un ma_device NUOVO,
    // mentre qui si faceva stop+uninit+init sulla STESSA struct ma_device:
    // il backend audio reale (PipeWire/ALSA) non si ri-aggancia davvero
    // alla nuova uscita fisica se la struct viene riusata in-place. Fix:
    // costruisce una Output nuova da zero (stesso schema di addRenderer()),
    // trasferisce i renderer già in riproduzione, e solo se la nuova
    // apertura riesce sostituisce quella vecchia — se fallisce, quella
    // vecchia resta intatta e continua a suonare invece di restare morta.
    auto newMain = std::make_unique<Output>();
    if (!startOutput(&m_impl->context, newMain.get(), deviceName, m_sampleRate, true))
        return false;

    Output *oldMain = m_impl->outputs.front().get();
    {
        std::lock_guard<std::mutex> lkOld(oldMain->mutex);
        std::lock_guard<std::mutex> lkNew(newMain->mutex);
        newMain->renderers = std::move(oldMain->renderers);
    }

    // ma_device_stop blocca finché il thread della callback non esce. La
    // callback usa try_to_lock quindi rinuncia subito se il lock è tenuto;
    // a questo punto oldMain->renderers è già vuoto, quindi anche un ultimo
    // giro di mix sul device vecchio produrrebbe solo silenzio.
    ma_device_stop(&oldMain->device);
    ma_device_uninit(&oldMain->device);

    m_sampleRate        = int(newMain->device.sampleRate);
    m_blockSize         = int(newMain->device.playback.internalPeriodSizeInFrames);
    if (m_blockSize <= 0) m_blockSize = 512;
    m_currentDeviceName = deviceName;

    m_impl->outputs.front() = std::move(newMain);
    return true;
}

void AudioEngine::shutdown() {
    if (!m_initialized) return;
    for (auto &o : m_impl->outputs) {
        ma_device_stop(&o->device);
        ma_device_uninit(&o->device);
    }
    ma_context_uninit(&m_impl->context);
    m_impl->outputs.clear();
    m_initialized = false;
}

void AudioEngine::addRenderer(AudioRenderer *r, const std::string &deviceName) {
    // Se init() non è mai riuscito (device audio occupato/assente), m_impl è
    // comunque allocato ma con outputs vuoto: senza questa guardia la riga
    // sotto (outputs.front()) sarebbe undefined behaviour sul primo GO.
    if (!m_initialized) return;
    std::lock_guard<std::mutex> outputsLock(m_impl->outputsMutex);

    // Bug segnalato dall'utente: una cue con "Predefinita" (deviceName=="")
    // seguiva Impostazioni → Hardware solo finché l'uscita principale non
    // aveva mai un nome specifico impostato. Il confronto sotto
    // (`o->name == deviceName`) cercava un'Output con nome LETTERALMENTE
    // vuoto — ma `name` dell'uscita principale non è un flag "è il
    // default": è la stringa scelta in Impostazioni → Hardware (es. "JBL
    // Xtreme 3"). Appena l'utente sceglieva un device specifico lì, "" non
    // trovava più corrispondenza, e la cue si apriva una NUOVA uscita
    // puntata al device di sistema predefinito — ignorando in silenzio la
    // scelta fatta in Impostazioni (che "vinceva" solo se non toccata).
    // "" deve sempre significare "segui l'uscita principale, qualunque
    // essa sia", non "cerca un'uscita senza nome".
    Output *target = deviceName.empty() ? m_impl->outputs.front().get() : nullptr;
    if (!target) {
        for (auto &o : m_impl->outputs) {
            if (o->name == deviceName) { target = o.get(); break; }
        }
    }
    if (!target) {
        // Nuova uscita richiesta al volo: la apre e la avvia adesso.
        auto newOut = std::make_unique<Output>();
        target = newOut.get();
        if (!startOutput(&m_impl->context, target, deviceName, m_sampleRate, false)) {
            // Dispositivo non apribile (rimosso, occupato, ecc.): la cue va
            // sull'uscita principale piuttosto che restare senza audio.
            target = m_impl->outputs.front().get();
        } else {
            m_impl->outputs.push_back(std::move(newOut));
        }
    }

    std::lock_guard<std::mutex> lock(target->mutex);
    if (std::find(target->renderers.begin(), target->renderers.end(), r) == target->renderers.end())
        target->renderers.push_back(r);
}

void AudioEngine::removeRenderer(AudioRenderer *r) {
    if (!m_initialized) return;
    // L'uscita usata da una cue non viene tracciata dal chiamante: si cerca
    // su tutte (poche, tipicamente 1-2) finché non si trova.
    std::lock_guard<std::mutex> outputsLock(m_impl->outputsMutex);
    for (auto &o : m_impl->outputs) {
        std::lock_guard<std::mutex> lock(o->mutex);
        auto it = std::find(o->renderers.begin(), o->renderers.end(), r);
        if (it != o->renderers.end()) {
            o->renderers.erase(it);
            return;
        }
    }
}
