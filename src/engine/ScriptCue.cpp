#include "ScriptCue.h"
#include "ScriptEngine.h"
#include <QJsonObject>

ScriptCue::ScriptCue(QObject *parent) : Cue(parent) {}

void ScriptCue::go() {
    if (state() == State::Playing) return;
    setState(State::Playing);
    ScriptEngine::instance().evaluate(m_script);
    setState(State::Idle);
    emit finished();
}

void ScriptCue::stop() {
    setState(State::Idle);
}

QJsonObject ScriptCue::toJson() const {
    auto o = Cue::toJson();
    o["cueType"] = "script";
    o["script"]  = m_script;
    return o;
}

void ScriptCue::fromJson(const QJsonObject &o) {
    Cue::fromJson(o);
    m_script = o["script"].toString();
}
