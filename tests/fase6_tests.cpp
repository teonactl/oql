// Test standalone per le correzioni della "Fase 6" del piano di revisione
// (deduplicazione: R4, R5, R7, R8). Nessuna GUI, nessun device audio/rete
// reale. R1-R3, R6, R9, R11 non hanno un test dedicato qui: sono o pura
// struttura C++ già esercitata indirettamente dai test delle Fasi 1-4
// (AudioCue::go()/setUserRate()/setPitchPreserve()/applyPluginChain() via
// R1+R2, tutti ripassati senza regressioni), o richiedono una GUI reale
// (R3, R6 — MainWindow), o sono YAML di CI validato con `actionlint`
// (R9, fuori da questo harness).
//
// R8 scrive/legge AppSettings (QSettings "OQL"/"OQL" reale su questa
// macchina): il valore originale viene salvato e ripristinato a fine test,
// anche se qualche check() fallisce, per non lasciare traccia.

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>
#include <memory>

#include "test_harness.h"

#include "engine/CueList.h"
#include "engine/AudioCue.h"
#include "engine/GroupCue.h"
#include "engine/BuiltinPlugins.h"
#include "engine/AppSettings.h"

// ── R4 — CueList::fromJson(): tabella stringa→factory ────────────────────────

static void test_r4_fromjson_factory_table_all_types() {
    SECTION("R4 — CueList::fromJson(): la tabella stringa->factory risolve "
            "correttamente ogni cueType conosciuto, nell'ordine giusto, e "
            "salta silenziosamente un tipo sconosciuto");

    QJsonArray arr;
    auto addType = [&](const QString &t) {
        QJsonObject o;
        o["cueType"] = t;
        o["number"]  = t; // solo per distinguere le cue nell'asserzione sull'ordine
        arr.append(o);
    };

    const QStringList types = {
        "audio", "video", "stop", "fade", "pause",
#ifndef OQL_BASE
        "speed",
#endif
        "play", "mic", "group", "label", "text", "image",
#ifndef OQL_BASE
        "effect", "reseteffect", "script", "record",
#endif
    };
    for (const auto &t : types) addType(t);
    addType("tipo-sconosciuto-xyz"); // deve essere saltato, non crashare

    CueList list;
    list.fromJson(arr);

    check(list.count() == types.size(),
          "il tipo sconosciuto viene saltato: il conteggio corrisponde ai soli tipi validi");

    bool orderOk = true;
    for (int i = 0; i < types.size() && i < list.count(); ++i) {
        if (list.cueAt(i)->number() != types[i]) { orderOk = false; break; }
    }
    check(orderOk, "le cue risultanti sono nell'ordine giusto (numero == tipo richiesto)");

    // Spot-check sul tipo concreto per qualche voce (type() enum, non solo il numero).
    check(list.count() > 0 && list.cueAt(0)->type() == Cue::Type::Audio,
          "'audio' produce davvero una AudioCue");
    const int groupIdx = types.indexOf("group");
    check(groupIdx >= 0 && list.cueAt(groupIdx)->type() == Cue::Type::Group,
          "'group' produce davvero una GroupCue");
}

// ── R5 — CueList::indexOfCue() dopo un cambio strutturale ───────────────────

static void test_r5_indexofcue_tracks_after_removal() {
    SECTION("R5 — indexOfCue() (dietro cuePropertyChanged) resta corretto "
            "dopo che una cue precedente è stata rimossa dalla lista");

    CueList list;
    list.addCue(std::make_unique<AudioCue>()); // index 0, verrà rimossa
    auto ac = std::make_unique<AudioCue>();
    Cue *target = ac.get();
    list.addCue(std::move(ac)); // index 1 ora, index 0 dopo la removeCue sotto

    int lastIdx = -1;
    QObject::connect(&list, &CueList::cuePropertyChanged, [&](int idx) { lastIdx = idx; });

    list.removeCue(0); // target ora è all'indice 0
    target->setName("rinominata");
    check(lastIdx == 0,
          "dopo la removeCue(0), un propertyChanged sulla cue superstite riporta "
          "il suo indice AGGIORNATO (0), non quello pre-rimozione (1)");
}

// ── R7 — BuiltinAudioPlugin: type()/isValid()/toJson() condivisi ────────────

static void test_r7_builtin_audio_plugin_shared_base() {
    SECTION("R7 — i plugin builtin derivano da BuiltinAudioPlugin: type()/isValid() "
            "ereditati, toJson() porta il builtin_id giusto, round-trip ok");

    GainPlugin gain;
    check(gain.type() == "builtin", "GainPlugin::type() ereditato da BuiltinAudioPlugin");
    check(gain.isValid(), "GainPlugin::isValid() ereditato da BuiltinAudioPlugin");
    check(gain.toJson()["builtin_id"].toString() == "gain",
          "GainPlugin::toJson() porta il proprio builtin_id tramite baseJson()");

    EqPlugin eq;
    check(eq.type() == "builtin" && eq.isValid(), "EqPlugin eredita type()/isValid()");
    eq.setParam(0, 6.0f);   // bass
    eq.setParam(1, -3.0f);  // mid
    eq.setParam(2, 2.0f);   // treble
    const QJsonObject eqJson = eq.toJson();
    check(eqJson["builtin_id"].toString() == "eq3", "EqPlugin::toJson() porta 'eq3'");
    check(qFuzzyCompare(float(eqJson["bass"].toDouble()) + 1.0f, 6.0f + 1.0f)
       && qFuzzyCompare(float(eqJson["mid"].toDouble()) + 1.0f, -3.0f + 1.0f)
       && qFuzzyCompare(float(eqJson["treble"].toDouble()) + 1.0f, 2.0f + 1.0f),
          "EqPlugin::toJson() conserva i parametri impostati (bass/mid/treble)");

    EqPlugin eq2;
    eq2.fromJson(eqJson);
    check(eq2.toJson() == eqJson, "EqPlugin: round-trip toJson()->fromJson()->toJson() identico");

    MufflePlugin muffle;
    check(muffle.type() == "builtin" && muffle.isValid(), "MufflePlugin eredita type()/isValid()");
    check(muffle.toJson()["builtin_id"].toString() == "muffle", "MufflePlugin::toJson() porta 'muffle'");

    // Il Biquad condiviso (lowShelf/highShelf/peaking per EqPlugin, lowPass per
    // MufflePlugin) deve filtrare davvero: niente NaN/Inf, e un impulso non
    // deve passare inalterato (il filtro fa qualcosa).
    eq.prepare(48000, 64);
    muffle.prepare(48000, 64);
    std::vector<float> inL(64, 0.f), inR(64, 0.f);
    inL[0] = inR[0] = 1.0f; // impulso
    std::vector<float> outL(64), outR(64);
    float *in[2]  = { inL.data(), inR.data() };
    float *out[2] = { outL.data(), outR.data() };
    eq.process(in, out, 64);
    bool eqFinite = true, eqChangedSomething = false;
    for (int i = 0; i < 64; ++i) {
        if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) eqFinite = false;
        if (std::abs(outL[i] - inL[i]) > 1e-6f) eqChangedSomething = true;
    }
    check(eqFinite, "EqPlugin::process() su un impulso produce solo valori finiti (niente NaN/Inf)");
    check(eqChangedSomething, "EqPlugin::process() modifica davvero il segnale (il Biquad filtra)");

    muffle.process(in, out, 64);
    bool muffleFinite = true;
    for (int i = 0; i < 64; ++i)
        if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) muffleFinite = false;
    check(muffleFinite, "MufflePlugin::process() su un impulso produce solo valori finiti");
}

// ── R8 — AppSettings: recentFiles()/ultraDarkHintShown() ────────────────────

static void test_r8_appsettings_recent_and_hint_roundtrip() {
    SECTION("R8 — AppSettings::recentFiles()/setRecentFiles() e "
            "ultraDarkHintShown()/setUltraDarkHintShown() leggono/scrivono "
            "correttamente (sostituiscono i QSettings(\"OQL\",\"OQL\") ad-hoc)");

    // Salva lo stato reale di questa macchina per ripristinarlo a fine test:
    // questi test toccano AppSettings::instance(), che scrive sul QSettings
    // reale dell'utente, non un'istanza isolata.
    const QStringList originalRecent = AppSettings::instance().recentFiles();
    const bool originalHint          = AppSettings::instance().ultraDarkHintShown();

    AppSettings::instance().setRecentFiles({"/tmp/a.oql", "/tmp/b.oql"});
    check(AppSettings::instance().recentFiles() == QStringList{"/tmp/a.oql", "/tmp/b.oql"},
          "setRecentFiles()/recentFiles(): round-trip corretto");

    AppSettings::instance().setUltraDarkHintShown(!originalHint);
    check(AppSettings::instance().ultraDarkHintShown() == !originalHint,
          "setUltraDarkHintShown()/ultraDarkHintShown(): round-trip corretto");

    // Ripristino, sempre (non solo se i check sopra sono passati).
    AppSettings::instance().setRecentFiles(originalRecent);
    AppSettings::instance().setUltraDarkHintShown(originalHint);
    check(AppSettings::instance().recentFiles() == originalRecent,
          "stato originale di recentFiles() ripristinato correttamente a fine test");
    check(AppSettings::instance().ultraDarkHintShown() == originalHint,
          "stato originale di ultraDarkHintShown() ripristinato correttamente a fine test");
}

void run_fase6_tests() {
    test_r4_fromjson_factory_table_all_types();
    test_r5_indexofcue_tracks_after_removal();
    test_r7_builtin_audio_plugin_shared_base();
    test_r8_appsettings_recent_and_hint_roundtrip();
}
