// Test standalone per le correzioni della "Fase 4" del piano di revisione
// (thread audio real-time safe: A4, A5, A7, P2, P3). Nessuna GUI, nessun
// device audio reale toccato.
//
// A5 (il vettore toFinish riusato in AudioEngine) e P8 (mic/video sull'uscita
// configurata) non hanno un test qui: il primo richiederebbe accesso a uno
// struct privato di AudioEngine solo per osservarlo, il secondo richiede un
// vero device audio con cui confrontare le due enumerazioni (miniaudio vs
// QMediaDevices) — sono nella checklist dei test GUI del doc del piano.
// P4 (SEH su Windows) non è testabile qui per natura: nessun modo di
// compilare/eseguire codice MSVC-specifico su questa macchina Linux — lo
// verifica solo la build CI (compilazione) e, a runtime, un vero Windows
// (vedi la stessa checklist).

#include <QCoreApplication>
#include <vector>
#include <atomic>
#include <thread>
#include <cstdio>
#include <memory>

#include "test_harness.h"

#include "engine/PluginChain.h"
#include "engine/VstPlugin.h"
#include "engine/Lv2Plugin.h"

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#include <pmmintrin.h>
#define OQL_TEST_HAS_X86_DENORMAL_TEST 1
#endif

// ── A4 + A5 — process() e toJson()/param() concorrenti su un VST2 reale ─────

static void test_a4_a5_vst_concurrent_access() {
    SECTION("A4+A5 — process() (try_lock) e toJson()/param() (lock \"bounded\") "
            "concorrenti su un VST2 reale: niente deadlock, niente crash");
    const std::string path = "/usr/lib/vst/ZaMaximX2-vst.so";

    auto plug = std::make_unique<VstPlugin>(path);
    if (!plug->isValid()) {
        printf("  [SKIP] %s non trovato/caricabile: installa zam-plugins-vst per questo test\n", path.c_str());
        return;
    }
    plug->prepare(48000, 64);

    // Un vero thread audio-like: chiama process() in loop stretto, esattamente
    // il pattern che prima dell'A5 bloccava (m_effectMtx.lock()) se il main
    // thread stava facendo un toJson()/param() — qui deve solo saltare il
    // blocco occasionale (passthrough), mai bloccarsi.
    std::atomic<bool> stop{false};
    std::atomic<long> blocksProcessed{0};
    std::thread audioThread([&]{
        std::vector<float> L(64, 0.3f), R(64, 0.3f);
        float *buf[2] = { L.data(), R.data() };
        while (!stop.load(std::memory_order_relaxed)) {
            plug->process(buf, buf, 64);
            blocksProcessed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Sul main thread: tanti toJson()/param() mentre l'altro thread elabora
    // senza sosta. Se ci fosse un deadlock qui il test non finirebbe mai
    // (il task runner/CI lo segnalerebbe come timeout); se ci fosse una race
    // sulla memoria del plugin, ASan la segnalerebbe a fine processo.
    for (int i = 0; i < 2000; ++i) {
        const auto json = plug->toJson();
        (void)json;
        const PluginParam p = plug->param(0);
        (void)p;
    }

    stop.store(true, std::memory_order_relaxed);
    audioThread.join();

    check(true, "2000 toJson()/param() concorrenti con process() in loop stretto: nessun deadlock");
    check(blocksProcessed.load() > 0, "il thread audio-like ha effettivamente elaborato blocchi nel frattempo");
}

// ── A7 — FTZ/DAZ azzerano davvero i denormali (verifica diretta, x86) ───────

#ifdef OQL_TEST_HAS_X86_DENORMAL_TEST
static void test_a7_denormal_protection() {
    SECTION("A7 — FTZ/DAZ (le stesse macro usate in ensureDenormalProtectionOnThisThread) "
            "azzerano davvero un denormale invece di lasciarlo propagare");

    // ensureDenormalProtectionOnThisThread() è static in AudioEngine.cpp, non
    // esportata: qui si verifica l'effetto delle STESSE due macro che usa,
    // non la funzione in sé — a conferma che l'approccio è quello giusto.
    const unsigned savedFtz = _MM_GET_FLUSH_ZERO_MODE();
    const unsigned savedDaz = _MM_GET_DENORMALS_ZERO_MODE();

    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_OFF);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_OFF);
    volatile float denorm1 = 1e-40f;  // sotto FLT_MIN: un denormale
    volatile float withoutProtection = denorm1 * 2.0f;
    check(withoutProtection != 0.0f,
          "setup: senza FTZ/DAZ un denormale moltiplicato per 2 resta non-zero (precondizione del test)");

    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    check(_MM_GET_FLUSH_ZERO_MODE() == _MM_FLUSH_ZERO_ON, "FTZ risulta acceso dopo averlo impostato");
    check(_MM_GET_DENORMALS_ZERO_MODE() == _MM_DENORMALS_ZERO_ON, "DAZ risulta acceso dopo averlo impostato");

    volatile float denorm2 = 1e-40f;
    volatile float withProtection = denorm2 * 2.0f;
    check(withProtection == 0.0f,
          "con FTZ/DAZ attivi lo stesso identico calcolo dà esattamente zero, non un denormale");

    // Ripristina lo stato di prima: questo test non deve alterare il resto
    // della suite (altri test numerici potrebbero aspettarsi il default).
    _MM_SET_FLUSH_ZERO_MODE(savedFtz);
    _MM_SET_DENORMALS_ZERO_MODE(savedDaz);
}
#endif

// ── P2 + P3 — LV2_PATH: separatore giusto, default non persi ────────────────

static void test_p2_p3_lv2_path_value() {
    SECTION("P2+P3 — Lv2Plugin::buildLv2PathValue(): separatore corretto per piattaforma, "
            "default di sistema non persi se LV2_PATH era vuota");

    check(Lv2Plugin::buildLv2PathValue("/qualcosa/esistente", {}).isEmpty() == false
       && Lv2Plugin::buildLv2PathValue("/qualcosa/esistente", {}) == QByteArray("/qualcosa/esistente"),
          "senza path extra configurati, LV2_PATH non viene toccata");

    check(Lv2Plugin::buildLv2PathValue(QByteArray(), {}).isEmpty(),
          "senza path extra e senza LV2_PATH preesistente: resta vuota (nessuna qputenv())");

#ifdef Q_OS_WIN
    const QByteArray withExtra = Lv2Plugin::buildLv2PathValue("C:\\LV2", {"D:\\MoreLV2"});
    check(withExtra == QByteArray("C:\\LV2;D:\\MoreLV2"),
          "Windows: separatore ';' tra il valore esistente e il path extra");
#else
    const QByteArray withExtra = Lv2Plugin::buildLv2PathValue("/usr/lib/lv2", {"/opt/extra/lv2"});
    check(withExtra == QByteArray("/usr/lib/lv2:/opt/extra/lv2"),
          "Linux/macOS: separatore ':' tra il valore esistente e il path extra");
#endif

    // Il caso che prima di P3 perdeva i default: LV2_PATH non impostata (qui
    // simulata con QByteArray() vuota) + un path extra configurato dall'utente.
    const QByteArray fromEmpty = Lv2Plugin::buildLv2PathValue(QByteArray(), {"/opt/extra/lv2"});
    check(!fromEmpty.isEmpty(), "LV2_PATH vuota + un path extra: il risultato non è vuoto");
    check(fromEmpty.contains("/opt/extra/lv2"), "...e contiene il path extra richiesto");
#ifndef Q_OS_WIN
    check(fromEmpty.contains("/usr/lib/lv2"),
          "...e contiene ANCHE il default di sistema /usr/lib/lv2 — "
          "prima di P3 sarebbe sparito, lasciando solo il path extra");
#endif

    // Due path extra: entrambi presenti, nell'ordine, con il separatore giusto.
    const QByteArray twoExtras = Lv2Plugin::buildLv2PathValue("/base", {"/extra1", "/extra2"});
#ifdef Q_OS_WIN
    check(twoExtras == QByteArray("/base;/extra1;/extra2"), "due path extra su Windows: entrambi separati da ';'");
#else
    check(twoExtras == QByteArray("/base:/extra1:/extra2"), "due path extra su Linux/macOS: entrambi separati da ':'");
#endif
}

void run_fase4_tests() {
    test_a4_a5_vst_concurrent_access();
#ifdef OQL_TEST_HAS_X86_DENORMAL_TEST
    test_a7_denormal_protection();
#endif
    test_p2_p3_lv2_path_value();
}
