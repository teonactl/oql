// Test standalone per le correzioni della "Fase 3" del piano di revisione
// (correttezza audio: A1, A2, A3, A6, A8, A9). Nessuna GUI, nessun device
// audio reale toccato — qui si esercitano solo PluginChain/AudioPlugin/
// AudioCue::fadeEnvelope direttamente, mai tramite AudioEngine.
//
// Due test usano plugin VST2/LV2 REALI già installati su questa macchina di
// sviluppo (pacchetti Arch `zam-plugins-vst` e `infamousplugins`) per
// una verifica end-to-end vera, non solo sintetica — si saltano con una nota
// (non falliscono) se quel plugin non è installato, così la suite resta
// portabile su un'altra macchina.
//
// A3 (sigaction installata una sola volta per processo, non ad ogni
// chiamata) non ha un test dedicato: ogni test qui sotto che carica un VST2/
// LV2 reale passa comunque dalla stessa guardia sigsetjmp/installXxxOnce, più
// volte e su più istanze nello stesso processo — se il refactor avesse rotto
// la protezione o introdotto un deadlock/race, lo si vedrebbe come un hang o
// un crash in QUESTI test, non in un test a parte.

#include <QCoreApplication>
#include <QTemporaryDir>
#include <vector>
#include <cmath>
#include <cstdio>
#include <memory>
#include <algorithm>

#include "test_harness.h"

#include "engine/PluginChain.h"
#include "engine/BuiltinPlugins.h"
#include "engine/VstPlugin.h"
#include "engine/Lv2Plugin.h"
#include "engine/AudioCue.h"

// ── A1 — bypass centralizzato: GainPlugin interno (sintetico, deterministico) ─

static void test_a1_bypass_builtin_plugin() {
    SECTION("A1 — il bypass (isActive) funziona anche sui plugin interni (prima: solo VST2/LV2)");

    PluginChain chain;
    chain.addPlugin(std::make_unique<GainPlugin>());
    chain.plugin(0)->setParam(0, -60.0f);  // -60dB: quasi silenzio se il gain è applicato
    chain.prepare(48000, 64);

    std::vector<float> L(64), R(64);
    float *buf[2] = { L.data(), R.data() };
    auto fillInput = [&]{ std::fill(L.begin(), L.end(), 0.5f); std::fill(R.begin(), R.end(), 0.5f); };

    fillInput();
    chain.process(buf, 64);
    check(std::abs(L[0]) < 0.01f && std::abs(R[0]) < 0.01f,
          "plugin attivo (default): -60dB attenua pesantemente il segnale");

    chain.plugin(0)->setActive(false);
    fillInput();
    chain.process(buf, 64);
    check(std::abs(L[0] - 0.5f) < 1e-4f && std::abs(R[0] - 0.5f) < 1e-4f,
          "plugin bypassato: il segnale passa invariato "
          "(prima del fix, i plugin interni ignoravano setActive(false))");
}

// ── A1 end-to-end — bypass su un VST2 reale già installato, via PluginChain ──

static void test_a1_bypass_real_vst2() {
    SECTION("A1 — bypass end-to-end su un VST2 reale (zam-plugins-vst), via PluginChain");
    const std::string path = "/usr/lib/vst/ZaMaximX2-vst.so";

    auto plug = std::make_unique<VstPlugin>(path);
    if (!plug->isValid()) {
        printf("  [SKIP] %s non trovato/caricabile: installa zam-plugins-vst per questo test\n", path.c_str());
        return;
    }
    AudioPlugin *raw = plug.get();

    PluginChain chain;
    chain.addPlugin(std::move(plug));
    chain.prepare(48000, 64);

    std::vector<float> L(64), R(64);
    float *buf[2] = { L.data(), R.data() };
    auto fillInput = [&]{ for (int i = 0; i < 64; ++i) { L[i] = 0.9f; R[i] = 0.9f; } };

    raw->setActive(false);
    fillInput();
    chain.process(buf, 64);
    bool unchanged = true;
    for (int i = 0; i < 64; ++i)
        if (std::abs(L[i] - 0.9f) > 1e-5f || std::abs(R[i] - 0.9f) > 1e-5f) { unchanged = false; break; }
    check(unchanged, "VST2 reale bypassato via PluginChain::process(): segnale passa invariato "
                      "(VstPlugin::process() stesso non controlla più isActive() — centralizzato in PluginChain)");

    // Riattivato: verifica solo che non crashi (il contenuto esatto dipende
    // dal plugin — un limiter sotto soglia può anche non alterare nulla).
    raw->setActive(true);
    fillInput();
    chain.process(buf, 64);
    check(true, "plugin riattivato ed elaborato di nuovo senza crash");
}

// ── A2 + A9 — LV2 reale: re-istanziazione al sample rate giusto, ────────────
//              porte connesse una volta in prepare(), niente crash multi-blocco

static void test_a2_a9_lv2_sample_rate_change() {
    SECTION("A2+A9 — LV2 reale (infamousplugins/Stuck): cambio di sample rate senza crash, output sempre sano");
    const std::string uri = "http://ssj71.github.io/infamousPlugins/plugs.html#stuck";

    Lv2Plugin plug(uri);
    if (!plug.isValid()) {
        printf("  [SKIP] %s non trovato/instanziabile: installa infamousplugins per questo test\n", uri.c_str());
        return;
    }

    auto runBlocks = [&](float *bufL, float *bufR) {
        float *buf[2] = { bufL, bufR };
        bool anyNonZero = false;
        for (int blk = 0; blk < 10; ++blk) {
            for (int i = 0; i < 64; ++i) { bufL[i] = 0.5f; bufR[i] = 0.5f; }
            plug.process(buf, buf, 64);
            for (int i = 0; i < 64; ++i)
                if (std::abs(bufL[i]) > 1e-6f || std::abs(bufR[i]) > 1e-6f) anyNonZero = true;
        }
        return anyNonZero;
    };

    std::vector<float> L(64), R(64);

    // Il costruttore istanzia sempre a 44100 (non conosce ancora il rate
    // reale): qui lo forziamo a 48000, il caso che prima di A2 restava
    // silenziosamente sbagliato (l'istanza restava a 44100 per sempre).
    plug.prepare(48000, 64);
    check(plug.isValid(), "resta valido dopo la re-istanziazione a 48000 Hz (nessun crash)");
    check(runBlocks(L.data(), R.data()), "produce output non nullo a 48000 Hz su più blocchi consecutivi");

    // Cambio rate a runtime (simula un cambio di device audio): deve
    // funzionare di nuovo, non solo la prima volta.
    plug.prepare(44100, 64);
    check(plug.isValid(), "resta valido dopo un secondo cambio di sample rate (44100 Hz)");
    check(runBlocks(L.data(), R.data()), "produce output non nullo anche dopo il secondo cambio di rate");
}

// ── A6 — un cambio di parametro EQ non azzera la memoria del filtro ─────────

static void test_a6_eq_param_change_no_click() {
    SECTION("A6 — un cambio di parametro EQ non azzera la memoria del filtro (niente click)");

    // Stessa sequenza di input per due istanze identiche; la seconda
    // richiama setParam() con lo STESSO valore subito prima dell'ultimo
    // campione (coefficienti risultano identici). Se reset() venisse ancora
    // chiamato (il bug), quel richiamo azzererebbe x1/x2/y1/y2 e l'ultimo
    // campione differirebbe da quello della prima istanza; con il fix i due
    // output devono coincidere esattamente.
    auto run = [](bool reapplySameParam) -> float {
        EqPlugin eq;
        eq.prepare(48000, 1);
        eq.setParam(0, 6.0f);   // bass +6dB: coefficienti non banali

        std::vector<float> L(1), R(1);
        float *buf[2] = { L.data(), R.data() };
        for (int i = 0; i < 500; ++i) {   // scalda la memoria del filtro
            L[0] = 0.5f; R[0] = 0.5f;
            eq.process(buf, buf, 1);
        }
        if (reapplySameParam)
            eq.setParam(0, 6.0f);  // stesso valore: ricalcola i coefficienti (identici)

        L[0] = 0.5f; R[0] = 0.5f;
        eq.process(buf, buf, 1);
        return L[0];
    };

    const float withoutReapply = run(false);
    const float withReapply    = run(true);
    check(std::abs(withoutReapply - withReapply) < 1e-5f,
          "ririchiamare setParam() con lo stesso valore non altera l'uscita successiva: "
          "la memoria del filtro non viene azzerata ad ogni ricalcolo dei coefficienti");
}

// ── A8 — il fade in/out è relativo al range trimmato, non all'intero file ────

static void test_a8_fade_envelope_respects_trim() {
    SECTION("A8 — AudioCue::fadeEnvelope() è relativo a trimStart/trimEnd, non a tutta la durata del file");

    const double full = 60.0;

    // Fade-out: file di 60s, trimEnd a 20s, fadeOut di 3s → la finestra di
    // fade deve essere 17s..20s (relativa al trim), non 57s..60s.
    {
        const double trimEnd = 20.0, fadeOut = 3.0;
        check(AudioCue::fadeEnvelope(16.0, 0.0, fadeOut, 0.0, trimEnd, full) > 0.99,
              "prima della finestra di fade-out (rispetto al trimEnd): volume ancora pieno");
        check(AudioCue::fadeEnvelope(18.5, 0.0, fadeOut, 0.0, trimEnd, full) < 0.6,
              "dentro la finestra di fade-out rispetto al trimEnd: volume già in calo");
        check(AudioCue::fadeEnvelope(20.0, 0.0, fadeOut, 0.0, trimEnd, full) < 0.01,
              "esattamente al trimEnd: volume vicino a zero");

        // Prima del fix (fade-out calcolato su m_fileDuration=60 invece che su
        // trimEnd=20) a 18.5s il fade-out sarebbe stato ancora "lontano"
        // (57s di finestra), cioè non si sarebbe mai sentito prima dei 20s.
        check(AudioCue::fadeEnvelope(18.5, 0.0, fadeOut, 0.0, 0.0 /*nessun trim*/, full) > 0.99,
              "senza trimEnd impostato, lo stesso calcolo (relativo alla fine dei 60s) a 18.5s dà ancora volume pieno — "
              "la differenza qui sopra è esattamente il bug: col trim, 18.5s è vicino alla fine del range attivo");
    }

    // Fade-in: trimStart a 10s, fadeIn di 3s → la finestra deve essere
    // 10s..13s (relativa al trim), non "posizione assoluta nel file".
    {
        const double trimStart = 10.0, fadeIn = 3.0;
        check(AudioCue::fadeEnvelope(trimStart, fadeIn, 0.0, trimStart, 0.0, full) < 0.01,
              "esattamente al trimStart: volume vicino a zero (inizio del fade-in)");
        const double mid = AudioCue::fadeEnvelope(trimStart + 1.5, fadeIn, 0.0, trimStart, 0.0, full);
        check(mid > 0.3 && mid < 0.7, "a metà del fade-in rispetto al trimStart: volume intermedio");
        check(AudioCue::fadeEnvelope(trimStart + fadeIn, fadeIn, 0.0, trimStart, 0.0, full) > 0.99,
              "alla fine del fade-in rispetto al trimStart: volume pieno");

        // Prima del fix, posS=10 (posizione assoluta) con fadeIn=3 dava
        // qMin(1.0, 10/3)=1.0 fin da subito: il fade-in non si sentiva mai.
    }
}

// ── A8b — renderAudio() applica davvero il fade-out, non solo fadeEnvelope()
//         in isolamento (bug segnalato dall'utente testando A8 a mano:
//         "il fade out non è percettibile") ─────────────────────────────────

static void test_a8b_renderaudio_actually_applies_trim_fade() {
    SECTION("A8b — renderAudio() applica il fade-out relativo al trim sul "
            "segnale REALMENTE renderizzato, non solo fadeEnvelope() isolata");

    QTemporaryDir dir;
    const QString wav = dir.filePath("loud.wav");
    check(writeLoudTestWav(wav, 5.0), "wav di test (segnale a piena scala, non silenzio) scritto");

    AudioCue ac;
    ac.setFilePath(wav);
    ac.setTrimStart(0.0);
    ac.setTrimEnd(2.0);          // file di 5s, trim-end a 2s
    ac.setFadeInDuration(0.0);
    ac.setFadeOutDuration(1.0);  // 1s di fade-out prima del trim-end (finestra 1.0s-2.0s)
    ac.go();
    check(ac.state() == Cue::State::Playing, "go() con trim+fadeOut impostati: Playing");

    constexpr int kSr = 48000;   // default AudioEngine::sampleRate() senza init()
    constexpr int kBlockFrames = 256;
    std::vector<float> buf(kBlockFrames * 2);

    // Renderizza blocchi finché la posizione di playback non supera `seconds`,
    // poi ritorna il picco di ampiezza assoluto nell'ULTIMO blocco renderizzato.
    auto peakAbsNear = [&](double seconds) -> float {
        float peak = 0.0f;
        for (int guard = 0; guard < 4000; ++guard) {
            std::fill(buf.begin(), buf.end(), 0.0f);
            if (!ac.renderAudioForTest(buf.data(), kBlockFrames, kSr)) break;
            peak = 0.0f;
            for (float v : buf) peak = std::max(peak, std::abs(v));
            if (ac.position() >= seconds) break;
        }
        return peak;
    };

    const float earlyPeak    = peakAbsNear(0.5);  // prima della finestra di fade (inizia a 1.0s)
    const float midFadePeak  = peakAbsNear(1.5);  // a metà della finestra di fade-out
    const float latePeak     = peakAbsNear(1.95); // appena prima del trim-end

    check(earlyPeak > 0.5f, "prima della finestra di fade-out: ampiezza in uscita piena");
    check(midFadePeak < earlyPeak * 0.8f,
          "a metà del fade-out: l'ampiezza REALMENTE in uscita è già visibilmente calata");
    check(latePeak < midFadePeak,
          "verso la fine del fade-out: l'ampiezza continua a scendere (non resta piena fino al trim-end)");
}

// ── A8c — fade e automazione del volume si combinano, non si escludono ──────

static void test_a8c_fade_and_automation_combine() {
    SECTION("A8c — un punto di automazione del volume non disattiva più "
            "silenziosamente il fade-out (prima erano if/else, ora si combinano)");

    QTemporaryDir dir;
    const QString wav = dir.filePath("loud.wav");
    check(writeLoudTestWav(wav, 5.0), "wav di test scritto");

    AudioCue ac;
    ac.setFilePath(wav);
    ac.setTrimStart(0.0);
    ac.setTrimEnd(2.0);
    ac.setFadeInDuration(0.0);
    ac.setFadeOutDuration(1.0);
    // Automazione "piatta" (sempre volume 1.0): prima di questo fix,
    // la sola presenza di QUALSIASI punto disattivava fadeEnvelope() del
    // tutto — esattamente lo scenario in cui un click sbagliato sulla
    // waveform (fuori dalla maniglia del fade) crea un punto per sbaglio.
    ac.setVolumePoints({QPointF(0.0, 1.0), QPointF(1.0, 1.0)});
    ac.go();
    check(ac.state() == Cue::State::Playing, "go() con automazione piatta + trim/fadeOut: Playing");

    constexpr int kSr = 48000;
    constexpr int kBlockFrames = 256;
    std::vector<float> buf(kBlockFrames * 2);

    auto peakAbsNear = [&](double seconds) -> float {
        float peak = 0.0f;
        for (int guard = 0; guard < 4000; ++guard) {
            std::fill(buf.begin(), buf.end(), 0.0f);
            if (!ac.renderAudioForTest(buf.data(), kBlockFrames, kSr)) break;
            peak = 0.0f;
            for (float v : buf) peak = std::max(peak, std::abs(v));
            if (ac.position() >= seconds) break;
        }
        return peak;
    };

    const float earlyPeak = peakAbsNear(0.5);
    const float latePeak  = peakAbsNear(1.95);

    check(earlyPeak > 0.5f, "con automazione piatta: ampiezza piena prima del fade");
    check(latePeak < earlyPeak * 0.5f,
          "con automazione piatta presente, il fade-out si sente ANCORA (prima di questo fix sarebbe "
          "rimasto ad ampiezza piena fino al trim-end, perché l'automazione disattivava il fade)");
}

// ── Loop count: deve avere effetto SUBITO se cambiato mentre la cue suona
//    (bug segnalato dall'utente: "cambiando il numero di loop ... quando la
//    cue è già partita non ha effetto") ──────────────────────────────────────

static void test_loopcount_live_update_while_playing() {
    SECTION("AudioCue::setLoopCount() mentre la cue è già in riproduzione ha "
            "effetto immediato (non solo al prossimo go())");

    QTemporaryDir dir;
    const QString wav = dir.filePath("loud.wav");
    check(writeLoudTestWav(wav, 1.0), "wav di test scritto");

    AudioCue ac;
    ac.setFilePath(wav);
    ac.setLoopCount(1); // nessun loop, inizialmente
    ac.go();
    check(ac.state() == Cue::State::Playing, "go(): Playing");

    // Cambia il loop count MENTRE la cue è già in riproduzione — deve
    // riflettersi subito, non solo se si rifà go().
    ac.setLoopCount(5);

    constexpr int kSr = 48000;
    constexpr int kBlockFrames = 256;
    std::vector<float> buf(kBlockFrames * 2);

    // Avanza oltre la fine naturale del file (1s): con 5 loop applicati dal
    // vivo, la cue deve continuare a suonare (si riavvolge), non fermarsi.
    bool stillPlaying = true;
    for (int i = 0; i < 400 && stillPlaying; ++i) { // ~400*256/48000 ≈ 2.1s > 1s del file
        std::fill(buf.begin(), buf.end(), 0.0f);
        stillPlaying = ac.renderAudioForTest(buf.data(), kBlockFrames, kSr);
    }
    check(stillPlaying,
          "dopo aver superato la durata di un singolo giro, la cue continua "
          "a renderizzare (il nuovo loopCount=5 impostato a runtime ha avuto effetto)");
}

void run_fase3_tests() {
    test_a1_bypass_builtin_plugin();
    test_a1_bypass_real_vst2();
    test_a2_a9_lv2_sample_rate_change();
    test_a6_eq_param_change_no_click();
    test_a8_fade_envelope_respects_trim();
    test_a8b_renderaudio_actually_applies_trim_fade();
    test_a8c_fade_and_automation_combine();
    test_loopcount_live_update_while_playing();
}
