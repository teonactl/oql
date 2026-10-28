// Test per il fade automatico di VideoCue (fadeIn/fadeOut) e per il canale
// selettivo di FadeCue (Both/VideoOnly/AudioOnly) quando il target è una
// cue video. Nessuna GUI.

#include "test_harness.h"
#include "engine/VideoCue.h"
#include "engine/ControlCues.h"

static void test_video_fade_durations_json_roundtrip() {
    printf("\n== W1 — VideoCue::fadeInDuration/fadeOutDuration sopravvivono a toJson()/fromJson() ==\n");
    VideoCue vc;
    vc.setFadeInDuration(1.5);
    vc.setFadeOutDuration(2.5);
    VideoCue vc2;
    vc2.fromJson(vc.toJson());
    check(qFuzzyCompare(vc2.fadeInDuration(), 1.5), "fadeInDuration sopravvive");
    check(qFuzzyCompare(vc2.fadeOutDuration(), 2.5), "fadeOutDuration sopravvive");
}

static void test_visual_and_audio_playback_volume_are_independent() {
    printf("\n== W2 — setVisualPlaybackVolume/setAudioPlaybackVolume agiscono su canali indipendenti ==\n");
    VideoCue vc;
    vc.setVisualPlaybackVolume(0.3);
    check(qFuzzyCompare(vc.visualLevel(), 0.3), "setVisualPlaybackVolume cambia il livello visivo");
    vc.setVisualPlaybackVolume(1.0); // reset per il controllo successivo
    vc.setAudioPlaybackVolume(0.2);
    check(qFuzzyCompare(vc.visualLevel(), 1.0), "setAudioPlaybackVolume NON tocca il livello visivo");
}

static void test_fadecue_video_only_channel_affects_visual() {
    printf("\n== W3 — FadeCue con canale VideoOnly su un target video fa scendere visualLevel() ==\n");
    VideoCue vc;
    vc.go();
    FadeCue fc;
    fc.setTarget(&vc);
    fc.setFadeChannel(FadeCue::FadeChannel::VideoOnly);
    fc.setTargetVolume(0.0);
    fc.setFadeDuration(0.15);
    fc.go();
    pumpEvents(250);
    check(vc.visualLevel() < 0.5, "il livello visivo è sceso verso il target (fade video applicato)");
}

static void test_fadecue_audio_only_channel_does_not_affect_visual() {
    printf("\n== W4 — FadeCue con canale AudioOnly su un target video NON tocca visualLevel() ==\n");
    VideoCue vc;
    vc.go();
    FadeCue fc;
    fc.setTarget(&vc);
    fc.setFadeChannel(FadeCue::FadeChannel::AudioOnly);
    fc.setTargetVolume(0.0);
    fc.setFadeDuration(0.15);
    fc.go();
    pumpEvents(250);
    check(qFuzzyCompare(vc.visualLevel(), 1.0), "il livello visivo resta invariato con canale Solo audio");
}

static void test_fadecue_both_channel_affects_visual_too() {
    printf("\n== W5 — FadeCue con canale di default (Both) fa scendere visualLevel() come prima ==\n");
    VideoCue vc;
    vc.go();
    FadeCue fc;
    fc.setTarget(&vc);
    check(fc.fadeChannel() == FadeCue::FadeChannel::Both, "il canale di default è Both");
    fc.setTargetVolume(0.0);
    fc.setFadeDuration(0.15);
    fc.go();
    pumpEvents(250);
    check(vc.visualLevel() < 0.5, "con canale Both il video sfuma come il comportamento precedente");
}

static void test_fadecue_channel_json_roundtrip() {
    printf("\n== W6 — FadeCue::fadeChannel sopravvive a toJson()/fromJson() ==\n");
    FadeCue fc;
    fc.setFadeChannel(FadeCue::FadeChannel::AudioOnly);
    FadeCue fc2;
    fc2.fromJson(fc.toJson());
    check(fc2.fadeChannel() == FadeCue::FadeChannel::AudioOnly, "fadeChannel sopravvive");
}

static void test_fadecue_target_visual_level_json_roundtrip() {
    printf("\n== W7 — FadeCue::targetVisualLevel sopravvive a toJson()/fromJson(), indipendente da targetVolume ==\n");
    FadeCue fc;
    fc.setTargetVolume(0.8);
    fc.setTargetVisualLevel(0.25);
    FadeCue fc2;
    fc2.fromJson(fc.toJson());
    check(qFuzzyCompare(fc2.targetVolume(), 0.8), "targetVolume (audio) sopravvive");
    check(qFuzzyCompare(fc2.targetVisualLevel(), 0.25), "targetVisualLevel (video) sopravvive separatamente");
}

static void test_fadecue_both_channel_uses_independent_targets() {
    printf("\n== W8 — FadeCue Both usa traguardi indipendenti per audio e video ==\n");
    VideoCue vc;
    vc.go();
    FadeCue fc;
    fc.setTarget(&vc);
    fc.setTargetVolume(1.0);       // target audio resta a piena scala
    fc.setTargetVisualLevel(0.0);  // target video scende a 0
    fc.setFadeDuration(0.15);
    fc.go();
    pumpEvents(250);
    check(vc.visualLevel() < 0.5, "il video scende verso il suo target indipendente anche se l'audio non scende");
    check(qFuzzyCompare(fc.targetVolume(), 1.0), "targetVolume resta quello impostato (audio)");
    check(qFuzzyCompare(fc.targetVisualLevel(), 0.0), "targetVisualLevel resta quello impostato (video)");
}

static void test_video_muted_json_roundtrip() {
    printf("\n== W9 — VideoCue::muted sopravvive a toJson()/fromJson(), indipendente dal volume ==\n");
    VideoCue vc;
    check(!vc.muted(), "una VideoCue nuova non è muta di default");
    vc.setVolume(0.7);
    vc.setMuted(true);
    check(vc.muted(), "setMuted(true) imposta il muto");
    check(qFuzzyCompare(vc.volume(), 0.7), "il volume di base resta quello impostato, il muto non lo azzera");

    VideoCue vc2;
    vc2.fromJson(vc.toJson());
    check(vc2.muted(), "muted sopravvive al round-trip");
    check(qFuzzyCompare(vc2.volume(), 0.7), "volume sopravvive insieme al muto, separatamente");
}

void run_fase12_tests() {
    test_video_fade_durations_json_roundtrip();
    test_visual_and_audio_playback_volume_are_independent();
    test_fadecue_video_only_channel_affects_visual();
    test_fadecue_audio_only_channel_does_not_affect_visual();
    test_fadecue_both_channel_affects_visual_too();
    test_fadecue_channel_json_roundtrip();
    test_fadecue_target_visual_level_json_roundtrip();
    test_fadecue_both_channel_uses_independent_targets();
    test_video_muted_json_roundtrip();
}
