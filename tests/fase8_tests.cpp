// Test per il modello multi-output/multi-schermo-virtuale (PhysicalOutput,
// VirtualScreen, Workspace) e la migrazione da un progetto con la vecchia
// calibrazione singola. Nessuna GUI.

#include "test_harness.h"
#include "engine/Workspace.h"
#include <QJsonDocument>
#include <QTemporaryDir>

static void test_workspace_has_default_screen() {
    printf("\n== S1 — un Workspace appena creato ha sempre almeno un output e uno schermo virtuale ==\n");
    Workspace ws;
    check(ws.physicalOutputs().size() == 1, "un output fisico di default esiste");
    check(ws.virtualScreens().size() == 1, "uno schermo virtuale di default esiste");
    check(ws.virtualScreens().first().physicalOutputId() == ws.physicalOutputs().first().id(),
          "lo schermo di default appartiene all'output di default");
    check(ws.virtualScreens().first().canvasRect() == QRectF(0.0, 0.0, 1.0, 1.0),
          "lo schermo di default mostra tutto il contenuto (mirror)");
}

static void test_add_remove_physical_output_cascades_screens() {
    printf("\n== S2 — removePhysicalOutput() rimuove anche i suoi schermi virtuali, mai l'ultimo output ==\n");
    Workspace ws;
    const QString firstId = ws.physicalOutputs().first().id();
    const QString secondId = ws.addPhysicalOutput("Proiettore 2");
    check(ws.physicalOutputs().size() == 2, "il secondo output è stato aggiunto");

    const QString screenId = ws.addVirtualScreen(secondId, "Zona A");
    check(ws.virtualScreensFor(secondId).size() == 1, "lo schermo è assegnato al secondo output");

    ws.removePhysicalOutput(secondId);
    check(ws.physicalOutputs().size() == 1, "il secondo output è stato rimosso");
    check(ws.virtualScreenById(screenId) == nullptr,
          "lo schermo virtuale del secondo output è stato rimosso a cascata");
    check(ws.physicalOutputById(firstId) != nullptr, "il primo output è sopravvissuto");

    // Non deve mai restare senza nessun output.
    ws.removePhysicalOutput(firstId);
    check(ws.physicalOutputs().size() == 1, "non si può rimuovere l'ultimo output rimasto");
}

static void test_add_multiple_virtual_screens_same_output() {
    printf("\n== S3 — più schermi virtuali possono condividere lo stesso output fisico ==\n");
    Workspace ws;
    const QString outId = ws.physicalOutputs().first().id();
    const int before = ws.virtualScreensFor(outId).size();
    ws.addVirtualScreen(outId, "Zona sinistra");
    ws.addVirtualScreen(outId, "Zona destra");
    check(ws.virtualScreensFor(outId).size() == before + 2,
          "due nuovi schermi virtuali si aggiungono allo stesso output senza rimuovere quello di default");
}

static void test_remove_last_virtual_screen_of_an_output_is_noop() {
    printf("\n== S4 — removeVirtualScreen() non rimuove l'ultimo schermo di un output ==\n");
    Workspace ws;
    const QString outId = ws.physicalOutputs().first().id();
    const QString onlyScreenId = ws.virtualScreensFor(outId).first()->id();
    ws.removeVirtualScreen(onlyScreenId);
    check(ws.virtualScreenById(onlyScreenId) != nullptr,
          "l'unico schermo rimasto su un output non viene rimosso");
}

static void test_virtualscreen_polygon_json_roundtrip() {
    printf("\n== S5 — VirtualScreen::toJson()/fromJson(): poligono arbitrario e canvasRect sopravvivono ==\n");
    VirtualScreen s;
    s.setName("Finestra a forma di losanga");
    s.setPhysicalOutputId("output-123");
    s.setCanvasRect(QRectF(0.25, 0.0, 0.5, 1.0));
    QPolygonF diamond;
    diamond << QPointF(0.5, 0.0) << QPointF(1.0, 0.5) << QPointF(0.5, 1.0) << QPointF(0.0, 0.5);
    s.setMaskPolygon(diamond);

    VirtualScreen restored;
    restored.fromJson(s.toJson());
    check(restored.name() == s.name(), "il nome sopravvive al round-trip");
    check(restored.physicalOutputId() == s.physicalOutputId(), "il riferimento all'output sopravvive");
    check(restored.canvasRect() == s.canvasRect(), "il canvasRect sopravvive");
    check(restored.maskPolygon().size() == 4, "il poligono ha ancora 4 vertici");
    check(restored.maskPolygon() == s.maskPolygon(), "il poligono (non rettangolare) sopravvive esattamente");
}

static void test_migration_from_old_single_warp_project() {
    printf("\n== S6 — un progetto salvato prima del multi-output viene migrato correttamente al caricamento ==\n");
    QTemporaryDir dir;
    check(dir.isValid(), "directory temporanea creata");
    const QString path = dir.filePath("vecchio.oql");

    // Simula un progetto nel formato vecchio: solo "projectionWarp", niente
    // "physicalOutputs"/"virtualScreens".
    QJsonObject oldWarp;
    oldWarp["cols"] = 2;
    oldWarp["rows"] = 1;
    QJsonArray pts;
    for (double v : {0.0, 0.0,  0.5, 0.0,  1.0, 0.0,  0.0, 1.0,  0.5, 1.0,  1.0, 1.0})
        pts.append(v);
    oldWarp["points"] = pts;

    QJsonObject root;
    root["name"] = "vecchio";
    root["cues"] = QJsonArray();
    root["showCueNumbers"] = true;
    root["autoFadeOnStop"] = false;
    root["projectionWarp"] = oldWarp;

    QFile f(path);
    check(f.open(QIODevice::WriteOnly), "file di progetto vecchio scritto su disco");
    f.write(QJsonDocument(root).toJson());
    f.close();

    Workspace ws;
    check(ws.load(path), "il progetto vecchio si carica senza errori");
    check(ws.physicalOutputs().size() == 1, "migrato a un solo output fisico");
    check(ws.virtualScreens().size() == 1, "migrato a un solo schermo virtuale");
    check(ws.physicalOutputs().first().warp().cols() == 2,
          "la vecchia calibrazione (griglia 2x1) è stata trasferita all'output migrato");
    check(ws.virtualScreens().first().physicalOutputId() == ws.physicalOutputs().first().id(),
          "lo schermo migrato è assegnato correttamente al nuovo output");
}

void run_fase8_tests() {
    test_workspace_has_default_screen();
    test_add_remove_physical_output_cascades_screens();
    test_add_multiple_virtual_screens_same_output();
    test_remove_last_virtual_screen_of_an_output_is_noop();
    test_virtualscreen_polygon_json_roundtrip();
    test_migration_from_old_single_warp_project();
}
