// Test standalone per le correzioni della "Fase 5" del piano di revisione
// (prestazioni UI: E1, E2, E3, E4, E12, E13). Nessuna GUI, nessun device
// audio/rete reale necessario (E13 usa un refused-connection locale, non un
// host esterno).
//
// Non testati qui (richiedono l'app vera / l'orecchio e l'occhio
// dell'utente): E5 (decodifica della waveform in streaming — verificata a
// compilazione + dal non-regresso degli altri test; serve un file audio
// lungo e un confronto visivo della forma d'onda risultante), E6 (il
// QThread della waveform viene davvero liberato — osservabile solo con
// strumenti esterni al processo, es. valgrind/heaptrack, non con asserzioni
// in-process), E7 (ActiveCuesPanel — richiede QApplication/QPA, non
// disponibile in questo harness headless; verificato a compilazione e a
// vista nella checklist GUI), E11 (cache pixmap limitata — stesso motivo:
// QPixmap/QLabel richiedono un QGuiApplication con platform plugin).

#include <QCoreApplication>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <memory>

#include "test_harness.h"

#include "engine/CueList.h"
#include "engine/AudioCue.h"
#include "engine/RecordCue.h"
#include "engine/ControlCues.h"
#include "engine/GroupCue.h"
#include "engine/ImageCue.h"
#include "engine/ScriptEngine.h"
#include "ui/CueListModel.h"

// ── E1 — colonna Target: cache invece di scansioni O(n) per cella ───────────

static void test_e1_target_column_caches() {
    SECTION("E1 — CueListModel: colonna Target usa cache invece di scansionare "
            "la lista per ogni cella, e le cache si invalidano correttamente");

    CueList list;

    auto ac1 = std::make_unique<AudioCue>();
    Cue *ac1Raw = ac1.get();
    list.addCue(std::move(ac1));                      // index 0

    auto rc1 = std::make_unique<RecordCue>();
    RecordCue *rc1Raw = rc1.get();
    rc1Raw->setLinkedAudioCueId(ac1Raw->id());
    list.addCue(std::move(rc1));                       // index 1

    auto cc1 = std::make_unique<StopCue>();
    cc1->setNumber("CC1");
    cc1->setTargetId(ac1Raw->id());
    Cue *cc1Raw = cc1.get();
    list.addCue(std::move(cc1));                        // index 2

    auto cc2 = std::make_unique<StopCue>();
    cc2->setTargetId("non-esiste");
    list.addCue(std::move(cc2));                         // index 3

    CueListModel model(&list);

    // ac1 non ha filePath ma è collegata a rc1: deve mostrare la label di registrazione.
    const QModelIndex idxAc1 = model.index(0, CueListModel::ColTarget);
    check(model.data(idxAc1, Qt::DisplayRole).toString().startsWith("⏺"),
          "AudioCue senza file ma con RecordCue collegata: mostra 'Reg. #...' (cache RecordCue)");

    // cc1 punta ad ac1: deve risolvere correttamente tramite la cache id->Cue.
    const QModelIndex idxCc1 = model.index(2, CueListModel::ColTarget);
    check(model.data(idxCc1, Qt::DisplayRole).toString().contains("CC1") == false
       && model.data(idxCc1, Qt::DisplayRole).toString().startsWith("→"),
          "ControlCue con target valido: risolto tramite la cache id->Cue");
    check(model.data(idxCc1, Qt::ForegroundRole).value<QColor>() == QColor(160, 210, 160),
          "...colore verde per un target trovato");

    // cc2 punta a un id inesistente.
    const QModelIndex idxCc2 = model.index(3, CueListModel::ColTarget);
    check(model.data(idxCc2, Qt::DisplayRole).toString().contains("non trovato"),
          "ControlCue con target inesistente: '? non trovato' (cache id->Cue, non crash)");

    // Sgancia rc1 da ac1: la cache RecordCue deve invalidarsi su questo
    // specifico propertyChanged (rc1 è una RecordCue) e tornare corretta.
    rc1Raw->setLinkedAudioCueId(QString());
    check(model.data(idxAc1, Qt::DisplayRole).toString().contains("nessun file"),
          "dopo aver sganciato il collegamento, la colonna Target di ac1 torna "
          "a '✗ nessun file' (cache RecordCue invalidata correttamente)");

    // Rimuove ac1 dalla lista: la cache id->Cue deve rigenerarsi sul cambio
    // strutturale, così cc1 (ora in una posizione diversa) vede il target sparito.
    list.removeCue(0);
    const QModelIndex idxCc1After = model.index(1, CueListModel::ColTarget); // cc1 era index2, ora index1
    check(model.cueForRow(1) == cc1Raw, "dopo la removeCue(0), cc1 è ora alla riga visibile 1");
    check(model.data(idxCc1After, Qt::DisplayRole).toString().contains("non trovato"),
          "dopo aver rimosso il target, cc1 mostra '? non trovato' (cache id->Cue "
          "rigenerata sulla removeCue, non lascia un puntatore stantio)");
}

// ── E2 — playhead: dataChanged tocca solo le righe interessate ──────────────

static void test_e2_playhead_minimal_datachanged() {
    SECTION("E2 — spostare il playhead non ridisegna più l'intera tabella, "
            "solo la riga che perde ed eventualmente quella che riceve l'evidenziazione");

    CueList list;
    for (int i = 0; i < 5; ++i)
        list.addCue(std::make_unique<AudioCue>());

    CueListModel model(&list);
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

    list.setPlayhead(2);
    check(spy.count() == 1, "primo spostamento del playhead: una sola emissione di dataChanged");
    if (spy.count() == 1) {
        const auto args = spy.takeFirst();
        const QModelIndex tl = args.at(0).toModelIndex();
        const QModelIndex br = args.at(1).toModelIndex();
        check(tl.row() == 2 && br.row() == 2,
              "...e riguarda solo la riga 2 (quella appena evidenziata), non tutta la tabella");
    } else {
        spy.clear();
    }

    list.setPlayhead(4);
    check(spy.count() == 2,
          "secondo spostamento (2→4): due emissioni distinte (una per liberare la riga 2, "
          "una per evidenziare la riga 4) — mai un dataChanged che copre 0..rowCount()-1");
    bool sawRow2 = false, sawRow4 = false, sawWholeTable = false;
    for (const auto &args : spy) {
        const QModelIndex tl = args.at(0).toModelIndex();
        const QModelIndex br = args.at(1).toModelIndex();
        if (tl.row() == 2 && br.row() == 2) sawRow2 = true;
        if (tl.row() == 4 && br.row() == 4) sawRow4 = true;
        if (tl.row() == 0 && br.row() == model.rowCount() - 1) sawWholeTable = true;
    }
    check(sawRow2 && sawRow4, "...precisamente le righe 2 e 4, ciascuna da sola");
    check(!sawWholeTable, "...e non (anche) un'emissione che copre tutta la tabella");
}

// ── E3 — property changed su una cue qualsiasi non ricalcola le righe visibili ──

static void test_e3_property_change_skips_layout_recompute() {
    SECTION("E3 — cambiare una proprietà qualsiasi (nome) NON ricostruisce il "
            "layout delle righe visibili; cambiare davvero il parentGroupId SÌ");

    CueList list;
    auto ac1 = std::make_unique<AudioCue>();
    Cue *ac1Raw = ac1.get();
    list.addCue(std::move(ac1));
    auto ac2 = std::make_unique<AudioCue>();
    Cue *ac2Raw = ac2.get();
    list.addCue(std::move(ac2));

    CueListModel model(&list);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);

    ac1Raw->setName("Nome qualsiasi");
    check(resetSpy.count() == 0,
          "un cambio di proprietà generico (nome) non innesca un ricalcolo/reset del layout");
    check(dataSpy.count() >= 1,
          "...ma la riga interessata riceve comunque un dataChanged (resta reattiva)");

    resetSpy.clear();
    dataSpy.clear();

    auto gc = std::make_unique<GroupCue>();
    Cue *gcRaw = gc.get();
    list.addCue(std::move(gc)); // index 2, non ancora genitore di nessuno

    resetSpy.clear(); // l'aggiunta stessa passa da onCueAdded (reset legittimo)

    ac2Raw->setParentGroupId(gcRaw->id());
    check(resetSpy.count() == 1,
          "assegnare ac2 a un gruppo (parentGroupId cambia davvero) RICALCOLA il layout, "
          "perché questa è l'unica proprietà che incide sulla disposizione visibile");
}

// ── E4 — ImageCue emette displayChanged solo quando qualcosa cambia davvero ──

static void test_e4_imagecue_selective_display_changed() {
    SECTION("E4 — ImageCue non emette displayChanged() ad ogni tick durante "
            "Holding (nulla cambia visivamente); solo durante/alla fine di una transizione");

    ImageCue ic;
    ic.setImagePaths({"a.png", "b.png"});
    ic.setSlideDuration(0.1);
    ic.setTransitionDuration(0.1);
    ic.setTransitionType(ImageCue::Transition::Crossfade);
    ic.setLoop(false);

    ic.go(); // emette displayChanged() una volta per conto proprio, non lo contiamo

    QSignalSpy spy(&ic, &Cue::displayChanged);

    auto tick = [&]() {
        QMetaObject::invokeMethod(&ic, "onTick");
    };

    tick(); tick(); tick(); // 3 tick da 0.033s: 0.099s < 0.1s, resta in Holding
    check(spy.count() == 0,
          "durante Holding, prima che scatti la transizione, nessun displayChanged emesso");

    tick(); // 4° tick: supera slideDuration, scatta la transizione in questo stesso tick
    check(spy.count() == 1,
          "il tick che fa scattare la transizione emette displayChanged (il frame è cambiato)");

    tick(); tick(); tick(); // durante la transizione: deve emettere ad ogni tick
    check(spy.count() == 4,
          "ogni tick DURANTE la transizione emette (la dissolvenza avanza visivamente)");

    tick(); // completa la transizione (torna a Holding in questo tick)
    check(spy.count() == 5,
          "il tick che conclude la transizione emette una volta per mostrare lo stato finale");

    tick(); tick();
    check(spy.count() == 5,
          "tick successivi di nuovo in Holding (nulla cambia): nessuna emissione aggiuntiva");
}

// ── E12 — JsWorkspace::wrap() non appende più JsCue senza fine a sé stesso ──

static void test_e12_jscue_no_parent_leak() {
    SECTION("E12 — uno script che chiama workspace.at()/byId() migliaia di volte "
            "in loop non deve crashare né rallentare in modo osservabile "
            "(i wrapper JsCue non restano più appesi per sempre a JsWorkspace)");

    CueList list;
    auto ac = std::make_unique<AudioCue>();
    ac->setName("probe");
    list.addCue(std::move(ac));

    ScriptEngine::instance().init(&list);
    const QString err = ScriptEngine::instance().evaluate(
        "var total = 0;"
        "for (var i = 0; i < 5000; i++) {"
        "  var c = workspace.at(0);"
        "  if (c.name === 'probe') total++;"
        "}"
        "print(String(total));");
    check(err.isEmpty(), "5000 workspace.at(0) in loop: nessun errore JS");
}

// ── E13 — http.get()/post() non si blocca a tempo indeterminato ─────────────

static void test_e13_http_fails_fast_no_hang() {
    SECTION("E13 — http.get() su una connessione rifiutata fallisce rapidamente "
            "(non resta bloccato in attesa del timeout di sicurezza di 15s)");

    CueList list;
    ScriptEngine::instance().init(&list);

    QString captured;
    QObject::connect(&ScriptEngine::instance(), &ScriptEngine::outputLine,
                      [&](const QString &line) { captured = line; });

    QElapsedTimer t;
    t.start();
    const QString err = ScriptEngine::instance().evaluate(
        "print(http.get('http://127.0.0.1:1/'));");
    const qint64 ms = t.elapsed();

    check(err.isEmpty(), "evaluate() non segnala errori JS per una richiesta HTTP fallita");
    const QByteArray msg = QString(
        "la connessione rifiutata fallisce in pochi secondi, non attende il timeout di 15s (%1 ms)")
        .arg(ms).toUtf8();
    check(ms < 5000, msg.constData());
    check(captured.isEmpty(),
          "il corpo stampato è vuoto: niente crash su reply->readAll() dopo un errore di rete");
}

void run_fase5_tests() {
    test_e1_target_column_caches();
    test_e2_playhead_minimal_datachanged();
    test_e3_property_change_skips_layout_recompute();
    test_e4_imagecue_selective_display_changed();
    test_e12_jscue_no_parent_leak();
    test_e13_http_fails_fast_no_hang();
}
