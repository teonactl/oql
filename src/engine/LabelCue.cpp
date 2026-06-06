#include "LabelCue.h"

LabelCue::LabelCue(QObject *parent) : Cue(parent) {}

void LabelCue::go() {
    setState(State::Playing);
    setState(State::Idle);
    emit finished();
}

QJsonObject LabelCue::toJson() const {
    auto obj = Cue::toJson();
    obj["cueType"] = "label";
    return obj;
}

void LabelCue::fromJson(const QJsonObject &o) {
    Cue::fromJson(o);
}
