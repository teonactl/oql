#include "TextCue.h"

TextCue::TextCue(QObject *parent) : Cue(parent) {}

void TextCue::go() {
    setState(State::Playing);
    // stays playing until stop() is called
}

void TextCue::stop() {
    if (m_state == State::Idle) return;
    setState(State::Idle);
    emit finished();
}

QJsonObject TextCue::toJson() const {
    auto obj = Cue::toJson();
    obj["cueType"]         = "text";
    obj["text"]            = m_text;
    obj["fontFamily"]      = m_fontFamily;
    obj["fontSize"]        = m_fontSize;
    obj["bold"]            = m_bold;
    obj["italic"]          = m_italic;
    obj["textColor"]       = m_textColor.name(QColor::HexArgb);
    obj["backgroundColor"] = m_backgroundColor.name(QColor::HexArgb);
    obj["alignment"]       = m_alignment;
    return obj;
}

void TextCue::fromJson(const QJsonObject &o) {
    Cue::fromJson(o);
    m_text            = o["text"].toString();
    m_fontFamily      = o["fontFamily"].toString("Sans Serif");
    m_fontSize        = o["fontSize"].toInt(48);
    m_bold            = o["bold"].toBool(false);
    m_italic          = o["italic"].toBool(false);
    m_textColor       = QColor(o["textColor"].toString("#ffffffff"));
    m_backgroundColor = QColor(o["backgroundColor"].toString("#ff000000"));
    m_alignment       = o["alignment"].toInt(Qt::AlignCenter);
}
