// Test per ProjectionWarp (calibrazione area di proiezione / video mapping).
// Nessuna GUI: solo il modello dati e il motore di render, niente finestre.

#include "test_harness.h"
#include "engine/ProjectionWarp.h"
#include <QJsonDocument>

static void test_identity_grid_is_identity() {
    printf("\n== W1 — una ProjectionWarp appena creata è la griglia identità (nessun warp) ==\n");
    ProjectionWarp warp;
    check(warp.isIdentity(), "una ProjectionWarp di default è identità");
    check(warp.cols() == 1 && warp.rows() == 1, "la griglia di default è 1x1 (equivalente a 4 angoli)");
    check(warp.controlPoint(0, 0) == QPointF(0.0, 0.0), "angolo in alto a sinistra a (0,0)");
    check(warp.controlPoint(1, 1) == QPointF(1.0, 1.0), "angolo in basso a destra a (1,1)");
}

static void test_moving_a_point_breaks_identity() {
    printf("\n== W2 — spostare un punto di controllo rende la griglia non-identità ==\n");
    ProjectionWarp warp;
    warp.setControlPoint(0, 0, QPointF(0.05, 0.1));
    check(!warp.isIdentity(), "dopo setControlPoint() non è più identità");
    check(warp.controlPoint(0, 0) == QPointF(0.05, 0.1), "il punto spostato ha la nuova posizione");
    check(warp.controlPoint(1, 0) == QPointF(1.0, 0.0), "gli altri punti restano quelli di default");
}

static void test_grid_resize_preserves_shape() {
    printf("\n== W3 — setGridSize() ricampiona la calibrazione esistente invece di azzerarla ==\n");
    ProjectionWarp warp;
    // Keystone semplice: il lato destro è "più vicino" (angoli destri spostati verso il centro).
    warp.setControlPoint(1, 0, QPointF(0.9, 0.1));
    warp.setControlPoint(1, 1, QPointF(0.9, 0.9));

    warp.setGridSize(2, 2);
    check(warp.cols() == 2 && warp.rows() == 2, "la griglia è ora 2x2");
    // Gli angoli esterni (0,0)/(2,0)/(0,2)/(2,2) corrispondono esattamente ai
    // vecchi angoli (0,0)/(1,0)/(0,1)/(1,1): il resize non deve spostarli.
    check(warp.controlPoint(0, 0) == QPointF(0.0, 0.0), "angolo TL preservato dopo il resize");
    check(QLineF(warp.controlPoint(2, 0), QPointF(0.9, 0.1)).length() < 1e-9,
          "angolo TR preservato dopo il resize");
    check(QLineF(warp.controlPoint(2, 2), QPointF(0.9, 0.9)).length() < 1e-9,
          "angolo BR preservato dopo il resize");
    // Il punto centrale (1,1) nella nuova griglia 2x2 deve essere una via di
    // mezzo plausibile tra i 4 angoli originali (bilineare), non un valore
    // arbitrario: controlliamo solo che sia dentro il rettangolo [0,1].
    const QPointF mid = warp.controlPoint(1, 1);
    check(mid.x() >= 0.0 && mid.x() <= 1.0 && mid.y() >= 0.0 && mid.y() <= 1.0,
          "il nuovo punto centrale ricampionato resta in un range plausibile");
}

static void test_shrink_grid_keeps_corners() {
    printf("\n== W4 — restringere la griglia (setGridSize più piccola) non crasha e mantiene gli angoli ==\n");
    ProjectionWarp warp;
    warp.setGridSize(3, 3);
    warp.setControlPoint(0, 0, QPointF(0.02, 0.03));
    warp.setGridSize(1, 1);
    check(warp.cols() == 1 && warp.rows() == 1, "la griglia è tornata a 1x1");
    check(QLineF(warp.controlPoint(0, 0), QPointF(0.02, 0.03)).length() < 1e-9,
          "l'angolo TL modificato è sopravvissuto al restringimento della griglia");
}

static void test_nearest_control_point() {
    printf("\n== W5 — nearestControlPoint() trova l'handle giusto entro il raggio, altrimenti fallisce ==\n");
    ProjectionWarp warp;
    const QSize win(1000, 500);
    int col = -1, row = -1;
    // (1,0) normalizzato -> (1000,0) in pixel sulla finestra 1000x500.
    const bool found = warp.nearestControlPoint(QPointF(995, 5), win, 14.0, &col, &row);
    check(found, "un click vicino all'angolo TR (entro il raggio) lo trova");
    check(col == 1 && row == 0, "trova esattamente l'indice (1,0)");

    int col2 = -1, row2 = -1;
    const bool notFound = warp.nearestControlPoint(QPointF(500, 250), win, 14.0, &col2, &row2);
    check(!notFound, "un click al centro della finestra (lontano da ogni handle) non trova nulla");
}

static void test_render_warped_identity_is_passthrough() {
    printf("\n== W6 — renderWarped() con griglia identità restituisce l'immagine di sorgente inalterata ==\n");
    ProjectionWarp warp;
    QImage src(64, 32, QImage::Format_ARGB32_Premultiplied);
    src.fill(QColor(10, 20, 30, 255));
    const QImage out = warp.renderWarped(src, src.size());
    check(out.size() == src.size(), "l'immagine deformata ha la stessa dimensione della sorgente");
    check(out.pixelColor(32, 16) == QColor(10, 20, 30, 255),
          "con la griglia identità il colore al centro non cambia");
}

static void test_json_roundtrip() {
    printf("\n== W7 — toJson()/fromJson(): round-trip esatto per griglia e punti ==\n");
    ProjectionWarp warp;
    warp.setGridSize(2, 3);
    warp.setControlPoint(1, 1, QPointF(0.33, 0.77));
    warp.setControlPoint(0, 0, QPointF(-0.1, -0.05)); // margine extra consentito

    const QJsonObject json = warp.toJson();
    ProjectionWarp restored;
    restored.fromJson(json);

    check(restored.cols() == warp.cols() && restored.rows() == warp.rows(),
          "dimensioni della griglia ripristinate correttamente");
    bool allMatch = true;
    for (int r = 0; r <= warp.rows() && allMatch; ++r)
        for (int c = 0; c <= warp.cols() && allMatch; ++c)
            if (QLineF(warp.controlPoint(c, r), restored.controlPoint(c, r)).length() > 1e-9)
                allMatch = false;
    check(allMatch, "tutti i punti di controllo ripristinati esattamente");
}

static void test_fromjson_missing_or_corrupt_defaults_to_identity() {
    printf("\n== W8 — fromJson() su JSON vuoto/corrotto non crasha e ricade sulla griglia identità ==\n");
    ProjectionWarp warp;
    warp.setGridSize(4, 4);
    warp.setControlPoint(2, 2, QPointF(0.4, 0.4));

    // Un progetto vecchio senza la chiave "projectionWarp" produce un
    // QJsonObject vuoto: non deve crashare né lasciare lo stato precedente
    // (che potrebbe avere una dimensione di griglia diversa).
    warp.fromJson(QJsonObject());
    check(warp.isIdentity(), "JSON vuoto -> griglia identità, non lo stato precedente");
    check(warp.cols() == 1 && warp.rows() == 1, "JSON vuoto -> dimensione di griglia di default 1x1");
}

void run_fase7_tests() {
    test_identity_grid_is_identity();
    test_moving_a_point_breaks_identity();
    test_grid_resize_preserves_shape();
    test_shrink_grid_keeps_corners();
    test_nearest_control_point();
    test_render_warped_identity_is_passthrough();
    test_json_roundtrip();
    test_fromjson_missing_or_corrupt_defaults_to_identity();
}
