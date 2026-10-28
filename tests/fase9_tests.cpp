// Test per il contenuto generato di ImageCue (colore pieno / gradiente),
// aggiunto come alternativa alla slideshow basata su file. Nessuna GUI.

#include "test_harness.h"
#include "engine/ImageCue.h"

static void test_default_content_mode_is_file() {
    printf("\n== T1 — una ImageCue nuova usa di default il contenuto da file ==\n");
    ImageCue ic;
    check(ic.contentMode() == ImageCue::ContentMode::File, "contentMode di default è File");
}

static void test_solid_color_renders_fill() {
    printf("\n== T2 — contentMode SolidColor: renderGeneratedContent restituisce un riempimento uniforme ==\n");
    ImageCue ic;
    ic.setContentMode(ImageCue::ContentMode::SolidColor);
    ic.setSolidColor(QColor(10, 200, 50));
    const QImage img = ic.renderGeneratedContent(QSize(64, 48));
    check(img.size() == QSize(64, 48), "l'immagine generata ha la dimensione richiesta");
    check(img.pixelColor(0, 0)   == QColor(10, 200, 50), "angolo in alto a sinistra del colore richiesto");
    check(img.pixelColor(63, 47) == QColor(10, 200, 50), "angolo in basso a destra dello stesso colore");
    check(img.pixelColor(32, 24) == QColor(10, 200, 50), "centro dello stesso colore (riempimento uniforme)");
}

static void test_gradient_renders_endpoints_approx() {
    printf("\n== T3 — contentMode Gradient: gli estremi del gradiente sono vicini ai due colori impostati ==\n");
    ImageCue ic;
    ic.setContentMode(ImageCue::ContentMode::Gradient);
    ic.setGradientColor1(Qt::black);
    ic.setGradientColor2(Qt::white);
    ic.setGradientAngle(0.0); // sinistra → destra
    const QImage img = ic.renderGeneratedContent(QSize(200, 50));
    const int leftGray  = qGray(img.pixelColor(2, 25).rgb());
    const int rightGray = qGray(img.pixelColor(197, 25).rgb());
    check(leftGray < 40,  "estremo sinistro vicino al nero (colore1)");
    check(rightGray > 215, "estremo destro vicino al bianco (colore2)");
    check(rightGray > leftGray, "il gradiente cresce da sinistra a destra con angolo 0");
}

static void test_content_mode_json_roundtrip() {
    printf("\n== T4 — contentMode/colori/angolo sopravvivono a toJson()/fromJson() ==\n");
    ImageCue ic;
    ic.setContentMode(ImageCue::ContentMode::Gradient);
    ic.setGradientColor1(QColor(1, 2, 3));
    ic.setGradientColor2(QColor(250, 251, 252));
    ic.setGradientAngle(135.0);

    ImageCue restored;
    restored.fromJson(ic.toJson());
    check(restored.contentMode() == ImageCue::ContentMode::Gradient, "contentMode sopravvive");
    check(restored.gradientColor1() == QColor(1, 2, 3), "gradientColor1 sopravvive");
    check(restored.gradientColor2() == QColor(250, 251, 252), "gradientColor2 sopravvive");
    check(qFuzzyCompare(restored.gradientAngle(), 135.0), "gradientAngle sopravvive");
}

static void test_generated_content_go_has_no_slideshow_duration() {
    printf("\n== T5 — go() su colore/gradiente avvia la cue senza timer di slideshow (durata indefinita) ==\n");
    ImageCue ic;
    ic.setContentMode(ImageCue::ContentMode::SolidColor);
    check(ic.duration() == 0.0, "duration() è 0 (indefinita) per contenuto generato");
    ic.go();
    check(ic.state() == Cue::State::Playing, "go() porta la cue in stato Playing anche senza immagini");
    ic.stop();
    check(ic.state() == Cue::State::Idle, "stop() riporta la cue a Idle");
}

void run_fase9_tests() {
    test_default_content_mode_is_file();
    test_solid_color_renders_fill();
    test_gradient_renders_endpoints_approx();
    test_content_mode_json_roundtrip();
    test_generated_content_go_has_no_slideshow_duration();
}
