// Test standalone per le correzioni della "Fase 1" del piano di revisione
// (vedi il doc del piano). Nessuna GUI, nessun device audio reale toccato:
// AudioEngine::instance().init() non viene MAI chiamato in questo file, così
// questi test sono sicuri da eseguire anche con un vero show in corso altrove
// sulla stessa macchina.
//
// Non è un framework di test — solo una sequenza di controlli con PASS/FAIL,
// pensata per essere letta ed estesa facilmente. Uscita 0 se tutto passa, 1
// altrimenti (comodo da usare in CI in futuro).

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <algorithm>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

#include "test_harness.h"

#include "engine/Cue.h"
#include "engine/CueList.h"
#include "engine/AudioCue.h"
#include "engine/ControlCues.h"
#include "engine/GroupCue.h"
#include "engine/TextCue.h"
#include "engine/Workspace.h"
#include "engine/AudioEngine.h"

void run_fase2_tests();  // tests/fase2_tests.cpp
void run_fase3_tests();  // tests/fase3_tests.cpp
void run_fase4_tests();  // tests/fase4_tests.cpp
void run_fase5_tests();  // tests/fase5_tests.cpp
void run_fase6_tests();  // tests/fase6_tests.cpp

// Scrive un WAV PCM16 minimale (silenzio) che miniaudio decodifica senza problemi.
static bool writeTestWav(const QString &path, double seconds = 1.0,
                          int sampleRate = 44100, int channels = 2) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;

    const uint32_t sr          = uint32_t(sampleRate);
    const uint16_t ch          = uint16_t(channels);
    const uint16_t bps         = 16;
    const uint16_t blockAlign  = uint16_t(ch * (bps / 8));
    const uint32_t byteRate    = sr * blockAlign;
    const uint32_t numFrames   = uint32_t(seconds * sr);
    const uint32_t dataSize    = numFrames * blockAlign;
    const uint32_t fileSize    = 36 + dataSize;

    auto w32 = [&](uint32_t v){ f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v){ f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4); w32(fileSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16);
    w16(1); w16(ch); w32(sr); w32(byteRate); w16(blockAlign); w16(bps);
    f.write("data", 4); w32(dataSize);

    const QByteArray zeros(8192, '\0');
    uint32_t written = 0;
    while (written < dataSize) {
        const uint32_t chunk = std::min<uint32_t>(uint32_t(zeros.size()), dataSize - written);
        f.write(zeros.constData(), chunk);
        written += chunk;
    }
    return true;
}

// ── C2 — AudioEngine non inizializzato ───────────────────────────────────────

static void test_audioEngine_uninitialized_safe() {
    SECTION("C2 — AudioEngine non inizializzato: addRenderer/removeRenderer non devono crashare");
    QTemporaryDir dir;
    check(dir.isValid(), "tempdir creata");
    const QString wav = dir.filePath("c2.wav");
    check(writeTestWav(wav), "wav di test scritto");

    AudioCue cue;
    cue.setFilePath(wav);
    cue.go();   // AudioEngine::instance().init() non è MAI stato chiamato in questo processo
    check(cue.state() == Cue::State::Playing,
          "go() imposta Playing anche senza device audio (il renderer viene scartato in silenzio, non crasha)");
    cue.stop();
    check(cue.state() == Cue::State::Idle, "stop() torna a Idle senza crash");
}

// ── Minori AudioCue — stato coerente dopo un reinit del decoder fallito ──────

static void test_audioCue_reinit_failure_sets_idle() {
    SECTION("Minori — AudioCue torna a Idle (non resta \"Playing\" fantasma) se il reinit del decoder fallisce");
    QTemporaryDir dir;
    const QString wav = dir.filePath("reinit.wav");

    AudioCue cue;

    cue.setFilePath(dir.filePath("non_esiste.wav"));
    cue.go();
    check(cue.state() == Cue::State::Idle, "go() con file inesistente: stato resta Idle");

    check(writeTestWav(wav), "wav di test scritto");
    cue.setFilePath(wav);
    cue.go();
    check(cue.state() == Cue::State::Playing, "go() con file valido: Playing");
    QFile::remove(wav);
    cue.go();  // richiamato mentre è "Playing" (es. PlayCue su una cue già in play): il reinit ora fallisce
    check(cue.state() == Cue::State::Idle,
          "go() richiamato su cue già Playing con file sparito: torna a Idle, non resta Playing fantasma");

    check(writeTestWav(wav), "wav di test riscritto");
    cue.setFilePath(wav);
    cue.go();
    check(cue.state() == Cue::State::Playing, "go() di nuovo: Playing");
    QFile::remove(wav);
    cue.setUserRate(2.0);
    check(cue.state() == Cue::State::Idle, "setUserRate() con file sparito: torna a Idle");

    check(writeTestWav(wav), "wav di test riscritto");
    cue.setFilePath(wav);
    cue.go();
    QFile::remove(wav);
    cue.setPlaybackRate(2.0);
    check(cue.state() == Cue::State::Idle, "setPlaybackRate() con file sparito: torna a Idle");

    check(writeTestWav(wav), "wav di test riscritto");
    cue.setFilePath(wav);
    cue.go();
    QFile::remove(wav);
    cue.setPitchPreserve(true);
    check(cue.state() == Cue::State::Idle, "setPitchPreserve() con file sparito: torna a Idle");
}

// ── Minore CueList::removeCue — notifica il playhead ─────────────────────────

static void test_cueList_removeCue_emits_playheadChanged() {
    SECTION("Minore — CueList::removeCue() emette playheadChanged quando sposta il playhead");
    CueList list;
    list.addCue(std::make_unique<TextCue>());
    list.addCue(std::make_unique<TextCue>());
    list.addCue(std::make_unique<TextCue>());
    list.setPlayhead(2);

    int signalCount = 0, lastValue = -1;
    QObject::connect(&list, &CueList::playheadChanged, [&](int idx) {
        ++signalCount; lastValue = idx;
    });

    list.removeCue(0);  // rimuove una cue PRIMA del playhead
    check(list.playheadIndex() == 1, "playhead scalato correttamente da 2 a 1");
    check(signalCount == 1, "playheadChanged emesso esattamente una volta");
    check(lastValue == 1, "playheadChanged porta il nuovo valore corretto");
}

// ── C4 — CueList::go(): catena normale vs. riferimento ciclico ──────────────

static void test_cueList_go_normal_chain_completes() {
    SECTION("C4 — una catena normale di auto-continue avanza fino in fondo e si ferma (nessun avviso)");
    clearWarnings();
    CueList list;
    for (int i = 0; i < 5; ++i) {
        auto c = std::make_unique<TextCue>();
        c->setAutoContinue(i < 4);  // l'ultima non continua: altrimenti wrapperebbe a inizio lista
        list.addCue(std::move(c));
    }
    list.setPlayhead(0);
    list.go();
    pumpEvents(300);

    check(list.playheadIndex() == list.count(), "la catena avanza fino in fondo alla lista");
    check(!anyWarningContains("interrotta"), "nessun avviso di circuit-breaker durante una catena normale");
}

static void test_cueList_go_breaks_self_referencing_group() {
    SECTION("C4 — un gruppo auto-parentato a se stesso non crasha (stack overflow) né gira all'infinito");
    clearWarnings();
    CueList list;
    auto grp = std::make_unique<GroupCue>();
    const QString gid = grp->id();
    grp->setParentGroupId(gid);  // riferimento ciclico deliberato: il gruppo è "figlio" di se stesso
    list.addCue(std::move(grp));
    list.setPlayhead(0);

    // Con la vecchia ricorsione sincrona questa chiamata avrebbe fatto stack
    // overflow prima ancora di tornare qui. Con il fix torna subito (ogni
    // passo è deferred via QTimer::singleShot) e il loop continua nell'event
    // loop sottostante.
    list.go();

    pumpEvents(3000);  // generoso: senza breaker girerebbe per sempre
    check(anyWarningContains("interrotta"),
          "il circuit-breaker interrompe la catena ciclica con un avviso, invece di girare per sempre");
}

// ── C6 — Workspace::save() atomico ───────────────────────────────────────────

static void test_workspace_atomicSave() {
    SECTION("C6 — Workspace::save() è atomico (QSaveFile) e load() valida il JSON");
    QTemporaryDir dir;
    check(dir.isValid(), "tempdir creata");
    const QString path = dir.filePath("show.oql");

    Workspace ws;
    ws.cueList()->addCue(std::make_unique<TextCue>());
    ws.cueList()->cueAt(0)->setName("Prova");
    check(ws.save(path), "save() su un path scrivibile riesce");
    check(QFile::exists(path), "il file esiste dopo save()");

    Workspace ws2;
    check(ws2.load(path), "load() rilegge quello che save() ha scritto");
    check(ws2.cueList()->count() == 1, "round-trip preserva il numero di cue");
    check(ws2.cueList()->cueAt(0)->name() == "Prova", "round-trip preserva i dati della cue");

    // JSON valido ma non un oggetto: load() deve fallire pulito, non svuotare in silenzio.
    const QString badPath = dir.filePath("bad.oql");
    { QFile f(badPath); const bool opened = f.open(QIODevice::WriteOnly); Q_UNUSED(opened); f.write("[1,2,3]"); }
    Workspace ws3;
    ws3.cueList()->addCue(std::make_unique<TextCue>());
    check(!ws3.load(badPath), "load() rifiuta un JSON valido ma non-oggetto, invece di svuotare il workspace");

#ifndef Q_OS_WIN
    if (geteuid() == 0) {
        printf("  [SKIP] test su directory non scrivibile (eseguito come root: i permessi non si applicano)\n");
        return;
    }

    // Un save() che deve fallire (directory non scrivibile) non deve toccare
    // il file target preesistente: è esattamente la garanzia che QSaveFile dà
    // e che la scrittura diretta precedente non dava.
    QDir(dir.path()).mkdir("locked");
    const QString lockedDir = dir.filePath("locked");
    const QString target    = lockedDir + "/show.oql";

    Workspace ws4;
    ws4.cueList()->addCue(std::make_unique<TextCue>());
    check(ws4.save(target), "primo save() (dir ancora scrivibile) riesce");

    QFile::setPermissions(lockedDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
    ws4.cueList()->cueAt(0)->setName("Modificato dopo il lock");
    const bool secondSaveOk = ws4.save(target);
    QFile::setPermissions(lockedDir,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    check(!secondSaveOk, "save() su directory non scrivibile ritorna false");

    Workspace ws5;
    check(ws5.load(target), "il file originale si ricarica ancora dopo il save() fallito");
    check(ws5.cueList()->count() == 1 && ws5.cueList()->cueAt(0)->name() != "Modificato dopo il lock",
          "il contenuto originale non è stato toccato dal save() fallito (nessuna scrittura parziale)");
#else
    printf("  [SKIP] test su directory non scrivibile (non eseguito su Windows)\n");
#endif
}

// ── C3 — FadeCue con target eliminato a metà fade ────────────────────────────

static void test_fadeCue_target_deleted_mid_fade() {
    SECTION("C3 — FadeCue non crasha se la cue target viene eliminata durante il fade");
    auto *target = new AudioCue();  // basta un Cue con volume(), non serve un file

    FadeCue fade;
    fade.setTarget(target);
    fade.setFadeDuration(1.0);
    fade.setTargetVolume(0.0);

    bool finishedFired = false;
    QObject::connect(&fade, &Cue::finished, [&]{ finishedFired = true; });

    fade.go();
    check(fade.state() == Cue::State::Playing, "il fade parte");

    delete target;  // la QPointer dentro FadeCue deve auto-azzerarsi qui, non restare pendente

    pumpEvents(1500);  // più del fadeDuration: lascia girare qualche tick del QTimer da 50ms

    check(fade.state() == Cue::State::Idle, "il fade si ferma da solo (niente crash, niente blocco a \"Playing\")");
    check(finishedFired, "finished() viene emesso comunque");
}

// ── C1+C3 — EffectCue + AudioCue eliminata subito dopo go() ──────────────────

static void test_effectCue_target_deleted_immediately_after_go() {
    SECTION("C1+C3 — eliminare la AudioCue target subito dopo EffectCue::go() non crasha "
            "(join del thread di applyPluginChain() + QPointer su m_activeTarget)");
    QTemporaryDir dir;
    const QString wav = dir.filePath("effect.wav");
    check(writeTestWav(wav, 1.0), "wav di test scritto");

    auto *ac = new AudioCue();
    ac->setFilePath(wav);
    ac->go();
    check(ac->state() == Cue::State::Playing, "la AudioCue target è in riproduzione");

    EffectCue effect;
    effect.setTarget(ac);
    effect.setEffectDuration(0.0);  // "permanente": applyPluginChain() parte comunque in background
    effect.go();
    check(effect.state() == Cue::State::Playing, "l'EffectCue parte");

    delete ac;  // ~AudioCue() deve joinare il thread di applyPluginChain() prima di liberare la memoria

    pumpEvents(500);  // lascia fluire l'eventuale invokeMethod già in coda (Qt lo scarta da solo: l'oggetto non c'è più)

    effect.stop();   // senza QPointer, questo dereferenzierebbe m_activeTarget pendente
    check(effect.state() == Cue::State::Idle, "stop() dopo la distruzione del target: nessun crash, torna a Idle");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    qInstallMessageHandler(testMessageHandler);

    printf("=== OQL — test Fase 1 ===\n");
    test_audioEngine_uninitialized_safe();
    test_audioCue_reinit_failure_sets_idle();
    test_cueList_removeCue_emits_playheadChanged();
    test_cueList_go_normal_chain_completes();
    test_cueList_go_breaks_self_referencing_group();
    test_workspace_atomicSave();
    test_fadeCue_target_deleted_mid_fade();
    test_effectCue_target_deleted_immediately_after_go();

    printf("\n=== OQL — test Fase 2 ===\n");
    run_fase2_tests();

    printf("\n=== OQL — test Fase 3 ===\n");
    run_fase3_tests();

    printf("\n=== OQL — test Fase 4 ===\n");
    run_fase4_tests();

    printf("\n=== OQL — test Fase 5 ===\n");
    run_fase5_tests();

    printf("\n=== OQL — test Fase 6 ===\n");
    run_fase6_tests();

    const int n = failureCount();
    printf("\n=== %s (%d fallimenti) ===\n", n == 0 ? "TUTTI I TEST OK" : "ALCUNI TEST FALLITI", n);
    return n == 0 ? 0 : 1;
}
