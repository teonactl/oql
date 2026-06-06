#include "Cue.h"
#include <QUuid>

Cue::Cue(QObject *parent) : QObject(parent) {
    m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void Cue::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

QJsonObject Cue::toJson() const {
    return {
        {"id",            m_id},
        {"number",        m_number},
        {"name",          m_name},
        {"notes",         m_notes},
        {"parentGroupId", m_parentGroupId},
        {"preWait",       m_preWait},
        {"postWait",      m_postWait},
        {"autoContinue",  m_autoContinue},
        {"autoFollow",    m_autoFollow},
    };
}

void Cue::fromJson(const QJsonObject &o) {
    m_id            = o["id"].toString(m_id);
    m_number        = o["number"].toString();
    m_name          = o["name"].toString();
    m_notes         = o["notes"].toString();
    m_parentGroupId = o["parentGroupId"].toString();
    m_preWait       = o["preWait"].toDouble();
    m_postWait      = o["postWait"].toDouble();
    m_autoContinue  = o["autoContinue"].toBool();
    m_autoFollow    = o["autoFollow"].toBool();
}
