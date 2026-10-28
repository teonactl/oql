// Test per applyVirtualScreenMask() (engine/VirtualScreen.h): ritaglio
// canvasRect, fitMode/contentAnchor (come il contenuto riempie l'area dello
// schermo) e maschera poligonale applicati a un frame già composto.
// Nessuna GUI.

#include "test_harness.h"
#include "engine/VirtualScreen.h"
#include <QPainter>

static QImage makeHalfRedHalfBlue(QSize sz) {
    QImage img(sz, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    p.fillRect(QRect(0, 0, sz.width() / 2, sz.height()), Qt::red);
    p.fillRect(QRect(sz.width() / 2, 0, sz.width() - sz.width() / 2, sz.height()), Qt::blue);
    return img;
}

static void test_default_canvasRect_and_polygon_is_passthrough() {
    printf("\n== U1 — canvasRect e maskPolygon di default (tutto il rettangolo) non alterano il frame ==\n");
    VirtualScreen s; // default: canvasRect/maskPolygon = [0,1]x[0,1], fitMode = Contain
    const QImage src = makeHalfRedHalfBlue(QSize(100, 50));
    const QImage out = applyVirtualScreenMask(src, src.size(), s);
    check(out.size() == src.size(), "la dimensione resta invariata");
    check(out.pixelColor(10, 25) == QColor(255, 0, 0), "metà sinistra resta rossa");
    check(out.pixelColor(90, 25) == QColor(0, 0, 255), "metà destra resta blu");
}

static void test_canvasRect_crops_left_half() {
    printf("\n== U2 — canvasRect = metà sinistra (fitMode Stretch): il risultato mostra solo quella porzione ==\n");
    VirtualScreen s;
    s.setCanvasRect(QRectF(0.0, 0.0, 0.5, 1.0));
    s.setFitMode(VirtualScreen::FitMode::Stretch); // isola il ritaglio dal fitMode
    const QImage src = makeHalfRedHalfBlue(QSize(100, 50));
    const QImage out = applyVirtualScreenMask(src, src.size(), s);
    check(out.pixelColor(10, 25) == QColor(255, 0, 0), "estremo sinistro del ritaglio è rosso");
    check(out.pixelColor(90, 25) == QColor(255, 0, 0),
          "con Stretch la metà sinistra ritagliata riempie tutto il canvas, rossa anche vicino al bordo destro");
}

static void test_canvasRect_crops_right_half() {
    printf("\n== U3 — canvasRect = metà destra (fitMode Stretch): il risultato mostra solo quella porzione ==\n");
    VirtualScreen s;
    s.setCanvasRect(QRectF(0.5, 0.0, 0.5, 1.0));
    s.setFitMode(VirtualScreen::FitMode::Stretch);
    const QImage src = makeHalfRedHalfBlue(QSize(100, 50));
    const QImage out = applyVirtualScreenMask(src, src.size(), s);
    check(out.pixelColor(10, 25) == QColor(0, 0, 255), "il ritaglio della metà destra è tutto blu, anche a sinistra");
}

static void test_polygon_mask_leaves_outside_transparent() {
    printf("\n== U4 — maskPolygon più piccolo del canvas: fuori dal poligono il risultato è trasparente ==\n");
    VirtualScreen s;
    s.setFitMode(VirtualScreen::FitMode::Stretch); // riempie tutta l'area del poligono, niente bande
    QPolygonF smallSquare;
    smallSquare << QPointF(0.1, 0.1) << QPointF(0.4, 0.1) << QPointF(0.4, 0.4) << QPointF(0.1, 0.4);
    s.setMaskPolygon(smallSquare);
    const QImage src = makeHalfRedHalfBlue(QSize(100, 100));
    const QImage out = applyVirtualScreenMask(src, src.size(), s);
    // (15,25) invece di (25,25): con Stretch l'intero frame 100x100 (metà
    // rossa/metà blu) viene compresso nella bounding box piccola del
    // poligono (10,10)-(40,40) — un punto troppo vicino al centro di quella
    // box cade sul confine rosso/blu rimappato; (15,25) resta saldamente
    // nel quarto sinistro, sempre rosso qualunque sia l'arrotondamento.
    check(out.pixelColor(15, 25).alpha() > 200, "dentro il poligono il pixel è opaco");
    check(out.pixelColor(15, 25).red() > 200, "dentro il poligono il colore è quello sorgente (rosso)");
    check(out.pixelColor(90, 90).alpha() < 20, "fuori dal poligono (angolo opposto) il pixel è trasparente");
}

static void test_empty_frame_returns_unchanged() {
    printf("\n== U5 — un frame vuoto non manda in crash applyVirtualScreenMask ==\n");
    VirtualScreen s;
    const QImage empty;
    const QImage out = applyVirtualScreenMask(empty, QSize(), s);
    check(out.isNull(), "un frame nullo resta nullo (nessun crash, nessuna allocazione spuria)");
}

static QImage makeSolid(QSize sz, QColor c) {
    QImage img(sz, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}

// L'area di destinazione (targetPx) è il bounding box del maskPolygon, NON
// la dimensione del frame sorgente — per avere un disallineamento di
// proporzioni da osservare serve un poligono la cui bounding box non
// coincida con la dimensione del contenuto ritagliato (qui sempre l'intero
// frame, canvasRect di default): una striscia orizzontale stretta è un caso
// chiaro (100x100 di contenuto "quadrato" dentro un'area 100x20 "larga").
static QPolygonF horizontalStripPolygon() {
    return QPolygonF(QRectF(0.0, 0.4, 1.0, 0.2)); // bbox pixel su un canvas 100x100: (0,40)-(100,60)
}

static void test_fitmode_contain_letterboxes_without_deforming() {
    printf("\n== U6 — fitMode Contain: il contenuto scala mantenendo le proporzioni, lasciando bande nell'area ==\n");
    VirtualScreen s;
    s.setFitMode(VirtualScreen::FitMode::Contain);
    s.setMaskPolygon(horizontalStripPolygon());
    const QImage src = makeSolid(QSize(100, 100), Qt::red); // quadrato, dentro una striscia 100x20
    const QImage out = applyVirtualScreenMask(src, src.size(), s);
    check(out.pixelColor(50, 50) == QColor(255, 0, 0),
          "il centro della striscia (dentro il quadrato scalato, 20x20) è rosso");
    check(out.pixelColor(10, 50).alpha() < 20,
          "ai lati della striscia (fuori dal quadrato scalato ma dentro il poligono) resta una banda trasparente");
}

static void test_fitmode_cover_fills_without_bands_cropping_excess() {
    printf("\n== U7 — fitMode Cover: il contenuto scala per riempire tutta l'area, niente bande (eccesso ritagliato) ==\n");
    VirtualScreen s;
    s.setFitMode(VirtualScreen::FitMode::Cover);
    s.setMaskPolygon(horizontalStripPolygon());
    const QImage src = makeSolid(QSize(100, 100), Qt::red);
    const QImage out = applyVirtualScreenMask(src, src.size(), s);
    check(out.pixelColor(10, 50) == QColor(255, 0, 0), "con Cover anche vicino al bordo della striscia è pieno");
    check(out.pixelColor(90, 50) == QColor(255, 0, 0), "...su tutta la larghezza della striscia, niente bande");
}

static void test_fitmode_center_keeps_native_size() {
    printf("\n== U8 — fitMode Center: nessuna scala, il contenuto ritagliato resta alla sua dimensione, centrato ==\n");
    VirtualScreen s;
    s.setFitMode(VirtualScreen::FitMode::Center);
    s.setCanvasRect(QRectF(0.25, 0.25, 0.5, 0.5)); // ritaglia un quadrato 50x50 da un canvas 100x100
    // maskPolygon resta il default (tutto il canvas, bbox 100x100): l'area
    // disponibile è più grande del contenuto ritagliato, che a sua volta
    // NON viene scalato per riempirla.
    const QImage src = makeSolid(QSize(100, 100), Qt::red);
    const QImage out = applyVirtualScreenMask(src, src.size(), s);
    check(out.pixelColor(50, 50) == QColor(255, 0, 0), "il centro del canvas (dentro il ritaglio 50x50) è rosso");
    check(out.pixelColor(5, 5).alpha() < 20, "l'angolo, ben fuori dal ritaglio 50x50 centrato, resta trasparente");
}

static void test_content_anchor_moves_within_available_space() {
    printf("\n== U9 — contentAnchor sposta il contenuto entro lo spazio libero invece di centrarlo ==\n");
    VirtualScreen s;
    s.setFitMode(VirtualScreen::FitMode::Contain);
    s.setMaskPolygon(horizontalStripPolygon());
    s.setContentAnchor(QPointF(0.0, 0.5)); // tutto a sinistra invece che centrato
    const QImage src = makeSolid(QSize(100, 100), Qt::red);
    const QImage out = applyVirtualScreenMask(src, src.size(), s);
    check(out.pixelColor(10, 50) == QColor(255, 0, 0), "con anchor a sinistra il quadrato tocca il bordo sinistro della striscia");
    check(out.pixelColor(90, 50).alpha() < 20, "il lato destro della striscia resta libero/trasparente");
}

static void test_fitmode_manual_zoom_one_equals_contain() {
    printf("\n== U10 — fitMode Manual con zoom=1.0 si comporta come Contain (stessa scala, stesse bande) ==\n");
    VirtualScreen s;
    s.setFitMode(VirtualScreen::FitMode::Manual);
    s.setZoom(1.0);
    s.setMaskPolygon(horizontalStripPolygon());
    const QImage src = makeSolid(QSize(100, 100), Qt::red);
    const QImage out = applyVirtualScreenMask(src, src.size(), s);
    check(out.pixelColor(50, 50) == QColor(255, 0, 0), "il centro della striscia è rosso, come con Contain");
    check(out.pixelColor(10, 50).alpha() < 20, "ai lati resta una banda trasparente, come con Contain");
}

static void test_fitmode_manual_zoom_enough_removes_bands_like_cover() {
    printf("\n== U11 — fitMode Manual con zoom sufficiente ritaglia le bande, comportandosi come Cover ==\n");
    // Con contentSize=100x100 e targetPx=100x20, containScale=0.2: uno zoom
    // di 5 (0.2*5=1.0) porta la scala a riempire esattamente la larghezza
    // (come farebbe Cover) pur partendo da Manual/Contain — è esattamente
    // la leva che serve per eliminare a mano le bande di un contenuto con
    // proporzioni diverse dalla zona, invece di restare bloccati su un
    // margine di trascinamento nullo come può capitare con Contain/Cover.
    VirtualScreen s;
    s.setFitMode(VirtualScreen::FitMode::Manual);
    s.setZoom(5.0);
    s.setMaskPolygon(horizontalStripPolygon());
    const QImage src = makeSolid(QSize(100, 100), Qt::red);
    const QImage out = applyVirtualScreenMask(src, src.size(), s);
    check(out.pixelColor(10, 50) == QColor(255, 0, 0), "con zoom sufficiente anche vicino al bordo è pieno, come Cover");
    check(out.pixelColor(90, 50) == QColor(255, 0, 0), "...su tutta la larghezza della striscia, niente bande");
}

static void test_zoom_json_roundtrip_and_clamped() {
    printf("\n== U12 — VirtualScreen::zoom sopravvive a toJson()/fromJson() ed è limitato a [1,8] ==\n");
    VirtualScreen s;
    s.setZoom(3.5);
    VirtualScreen s2;
    s2.fromJson(s.toJson());
    check(qFuzzyCompare(s2.zoom(), 3.5), "zoom sopravvive al round-trip");
    VirtualScreen s3;
    s3.setZoom(0.2);
    check(qFuzzyCompare(s3.zoom(), 1.0), "un valore sotto 1.0 viene limitato a 1.0 (Manual non può essere più \"largo\" di Contain)");
    s3.setZoom(50.0);
    check(qFuzzyCompare(s3.zoom(), 8.0), "un valore eccessivo viene limitato a 8.0");
}

void run_fase10_tests() {
    test_default_canvasRect_and_polygon_is_passthrough();
    test_canvasRect_crops_left_half();
    test_canvasRect_crops_right_half();
    test_polygon_mask_leaves_outside_transparent();
    test_empty_frame_returns_unchanged();
    test_fitmode_contain_letterboxes_without_deforming();
    test_fitmode_cover_fills_without_bands_cropping_excess();
    test_fitmode_center_keeps_native_size();
    test_content_anchor_moves_within_available_space();
    test_fitmode_manual_zoom_one_equals_contain();
    test_fitmode_manual_zoom_enough_removes_bands_like_cover();
    test_zoom_json_roundtrip_and_clamped();
}
