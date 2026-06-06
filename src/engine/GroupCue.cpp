#include "GroupCue.h"

GroupCue::GroupCue(QObject *parent) : Cue(parent) {}

void GroupCue::go() {
    setState(State::Playing);
    setState(State::Idle);
    emit finished();
}

QJsonObject GroupCue::toJson() const {
    auto obj = Cue::toJson();
    obj["cueType"]   = "group";
    obj["collapsed"] = m_collapsed;
    return obj;
}

void GroupCue::fromJson(const QJsonObject &o) {
    Cue::fromJson(o);
    m_collapsed = o["collapsed"].toBool(false);
}
