// Test per l'assegnazione cue→schermo (VideoCue/ImageCue::assignedScreenIds,
// CueList::activeVideoCueForScreen/activeImageCueForScreen) e per
// Workspace::replaceVirtualScreensWithGrid() (preset di divisione rapida).
// Nessuna GUI.

#include "test_harness.h"
#include "engine/Workspace.h"
#include "engine/VideoCue.h"
#include "engine/ImageCue.h"

static void test_replace_grid_tiles_cover_whole_canvas() {
    printf("\n== V1 — replaceVirtualScreensWithGrid(2,2) crea 4 zone che tassellano l'intero canvas ==\n");
    Workspace ws;
    const QString outId = ws.physicalOutputs().first().id();
    ws.replaceVirtualScreensWithGrid(outId, 2, 2);
    const auto screens = ws.virtualScreensFor(outId);
    check(screens.size() == 4, "4 schermi virtuali creati per una griglia 2x2");

    bool foundTopLeft = false, foundBottomRight = false;
    for (VirtualScreen *s : screens) {
        if (s->canvasRect() == QRectF(0.0, 0.0, 0.5, 0.5)) foundTopLeft = true;
        if (s->canvasRect() == QRectF(0.5, 0.5, 0.5, 0.5)) foundBottomRight = true;
        check(s->maskPolygon() == QPolygonF(s->canvasRect()),
              "il poligono di ciascuna zona coincide col suo canvasRect (tassellatura video-wall)");
    }
    check(foundTopLeft,     "la zona in alto a sinistra esiste con le coordinate attese");
    check(foundBottomRight, "la zona in basso a destra esiste con le coordinate attese");
}

static void test_replace_grid_removes_previous_screens_of_that_output_only() {
    printf("\n== V2 — replaceVirtualScreensWithGrid rimuove solo gli schermi DI QUELL'output ==\n");
    Workspace ws;
    const QString out1 = ws.physicalOutputs().first().id();
    const QString out2 = ws.addPhysicalOutput("Secondo");
    ws.addVirtualScreen(out2, "Zona fissa");
    check(ws.virtualScreensFor(out2).size() == 1, "schermo del secondo output presente prima della griglia");

    ws.replaceVirtualScreensWithGrid(out1, 3, 1);
    check(ws.virtualScreensFor(out1).size() == 3, "il primo output ha ora 3 zone verticali");
    check(ws.virtualScreensFor(out2).size() == 1, "il secondo output non è stato toccato");
}

static void test_replace_grid_clamps_to_at_least_one() {
    printf("\n== V3 — replaceVirtualScreensWithGrid con cols/rows <= 0 si comporta come 1x1 ==\n");
    Workspace ws;
    const QString outId = ws.physicalOutputs().first().id();
    ws.replaceVirtualScreensWithGrid(outId, 0, -3);
    check(ws.virtualScreensFor(outId).size() == 1, "almeno una zona viene sempre creata");
}

static void test_active_video_cue_for_screen_respects_assignment() {
    printf("\n== V4 — CueList::activeVideoCueForScreen rispetta assignedScreenIds ==\n");
    CueList list;
    auto a = std::make_unique<VideoCue>();
    auto b = std::make_unique<VideoCue>();
    VideoCue *va = a.get();
    VideoCue *vb = b.get();
    va->setAssignedScreenIds({"schermo-A"});
    vb->setAssignedScreenIds({"schermo-B"});
    list.addCue(std::move(a));
    list.addCue(std::move(b));

    check(list.activeVideoCueForScreen("schermo-A") == nullptr,
          "nessuna cue 'attiva' finché nessuna è in stato Playing");

    va->go();
    vb->go();
    check(list.activeVideoCueForScreen("schermo-A") == va, "la cue assegnata a schermo-A è quella giusta");
    check(list.activeVideoCueForScreen("schermo-B") == vb, "la cue assegnata a schermo-B è l'altra");
    check(list.activeVideoCueForScreen("schermo-C") == nullptr,
          "nessuna cue è assegnata a uno schermo-C inesistente");
}

static void test_empty_assignment_means_any_screen() {
    printf("\n== V5 — assignedScreenIds vuota = nessuna restrizione (compatibilità coi progetti vecchi) ==\n");
    CueList list;
    auto a = std::make_unique<ImageCue>();
    ImageCue *ia = a.get();
    list.addCue(std::move(a));
    ia->setContentMode(ImageCue::ContentMode::SolidColor); // va in Playing senza bisogno di immagini
    ia->go();
    check(list.activeImageCueForScreen("qualunque-schermo") == ia,
          "una cue senza schermi assegnati esplicitamente si mostra su qualunque schermo richiesto");
}

static void test_assigned_screen_ids_json_roundtrip() {
    printf("\n== V6 — assignedScreenIds sopravvive a toJson()/fromJson() (VideoCue e ImageCue) ==\n");
    VideoCue vc;
    vc.setAssignedScreenIds({"s1", "s2"});
    VideoCue vc2;
    vc2.fromJson(vc.toJson());
    check(vc2.assignedScreenIds() == QStringList({"s1", "s2"}), "VideoCue::assignedScreenIds sopravvive");

    ImageCue ic;
    ic.setAssignedScreenIds({"s3"});
    ImageCue ic2;
    ic2.fromJson(ic.toJson());
    check(ic2.assignedScreenIds() == QStringList({"s3"}), "ImageCue::assignedScreenIds sopravvive");
}

static void test_screens_on_different_outputs_are_fully_independent() {
    printf("\n== V7 — due schermi creati su output diversi hanno poligoni/id/nomi indipendenti ==\n");
    Workspace ws;
    const QString out1 = ws.physicalOutputs().first().id();
    const QString out2 = ws.addPhysicalOutput("Secondo");

    const QString screenOnOut2A = ws.addVirtualScreen(out2, "A su output 2");
    const QString screenOnOut2B = ws.addVirtualScreen(out2, "B su output 2");

    QPolygonF shapeA; shapeA << QPointF(0.0,0.0) << QPointF(0.3,0.0) << QPointF(0.3,1.0) << QPointF(0.0,1.0);
    QPolygonF shapeB; shapeB << QPointF(0.7,0.0) << QPointF(1.0,0.0) << QPointF(1.0,1.0) << QPointF(0.7,1.0);
    ws.virtualScreenById(screenOnOut2A)->setMaskPolygon(shapeA);
    ws.virtualScreenById(screenOnOut2B)->setMaskPolygon(shapeB);

    const auto screensOut1 = ws.virtualScreensFor(out1);
    const auto screensOut2 = ws.virtualScreensFor(out2);
    check(screensOut1.size() == 1, "output 1 ha ancora solo il suo schermo di default");
    check(screensOut2.size() == 2, "output 2 ha i suoi due nuovi schermi");
    check(screensOut1.first()->id() != screenOnOut2A && screensOut1.first()->id() != screenOnOut2B,
          "lo schermo di output 1 è un oggetto diverso, non uno di quelli creati su output 2");
    check(screensOut1.first()->maskPolygon() == QPolygonF(QRectF(0.0, 0.0, 1.0, 1.0)),
          "lo schermo di output 1 ha ancora il suo poligono originale (rettangolo pieno), non quello di A/B");
    check(ws.virtualScreenById(screenOnOut2A)->maskPolygon() == shapeA, "la forma di A è quella impostata su di lui");
    check(ws.virtualScreenById(screenOnOut2B)->maskPolygon() == shapeB, "la forma di B è quella impostata su di lui, diversa da A");
}

void run_fase11_tests() {
    test_replace_grid_tiles_cover_whole_canvas();
    test_replace_grid_removes_previous_screens_of_that_output_only();
    test_replace_grid_clamps_to_at_least_one();
    test_active_video_cue_for_screen_respects_assignment();
    test_empty_assignment_means_any_screen();
    test_assigned_screen_ids_json_roundtrip();
    test_screens_on_different_outputs_are_fully_independent();
}
