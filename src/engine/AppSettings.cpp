#include "AppSettings.h"
#include <QCoreApplication>
#include <QKeySequence>
#include <QTranslator>

AppSettings &AppSettings::instance() {
    static AppSettings inst;
    return inst;
}

AppSettings::AppSettings() : m_s("OQL", "OQL") {}

double AppSettings::defaultFadeDuration() const {
    return m_s.value("defaultFadeDuration", 2.0).toDouble();
}
void AppSettings::setDefaultFadeDuration(double s) {
    m_s.setValue("defaultFadeDuration", qMax(0.1, s));
}

double AppSettings::defaultFadeInDuration() const {
    return m_s.value("defaultFadeInDuration", 0.0).toDouble();
}
void AppSettings::setDefaultFadeInDuration(double s) {
    m_s.setValue("defaultFadeInDuration", qMax(0.0, s));
}

double AppSettings::defaultFadeOutDuration() const {
    return m_s.value("defaultFadeOutDuration", 0.0).toDouble();
}
void AppSettings::setDefaultFadeOutDuration(double s) {
    m_s.setValue("defaultFadeOutDuration", qMax(0.0, s));
}

bool AppSettings::autoNumberNewCues() const {
    return m_s.value("autoNumberNewCues", true).toBool();
}
void AppSettings::setAutoNumberNewCues(bool v) {
    m_s.setValue("autoNumberNewCues", v);
}

QKeySequence AppSettings::keyGo() const {
    const QString s = m_s.value("keyGo").toString();
    return s.isEmpty() ? QKeySequence(Qt::Key_Space) : QKeySequence(s);
}
void AppSettings::setKeyGo(const QKeySequence &k) { m_s.setValue("keyGo", k.toString()); }

QKeySequence AppSettings::keyStopAll() const {
    const QString s = m_s.value("keyStopAll").toString();
    return s.isEmpty() ? QKeySequence(Qt::Key_Escape) : QKeySequence(s);
}
void AppSettings::setKeyStopAll(const QKeySequence &k) { m_s.setValue("keyStopAll", k.toString()); }

QKeySequence AppSettings::keyFirstCue() const {
    const QString s = m_s.value("keyFirstCue").toString();
    return s.isEmpty() ? QKeySequence(Qt::Key_Home) : QKeySequence(s);
}
void AppSettings::setKeyFirstCue(const QKeySequence &k) { m_s.setValue("keyFirstCue", k.toString()); }

bool AppSettings::webEnabled() const { return m_s.value("webEnabled", false).toBool(); }
void AppSettings::setWebEnabled(bool v) { m_s.setValue("webEnabled", v); }
quint16 AppSettings::webPort() const { return quint16(m_s.value("webPort", 8080).toUInt()); }
void AppSettings::setWebPort(quint16 p) { m_s.setValue("webPort", p); }

QList<int> AppSettings::cueListColumnWidths() const {
    const auto v = m_s.value("cueListColumnWidths").toList();
    QList<int> out;
    for (const auto &val : v) out.append(val.toInt());
    return out;
}
void AppSettings::setCueListColumnWidths(const QList<int> &widths) {
    QVariantList v;
    for (int w : widths) v.append(w);
    m_s.setValue("cueListColumnWidths", v);
}

int AppSettings::cueListRowHeight() const {
    return m_s.value("cueListRowHeight", 28).toInt();
}
void AppSettings::setCueListRowHeight(int h) {
    m_s.setValue("cueListRowHeight", qMax(16, h));
}

int AppSettings::cueListFontSize() const {
    return m_s.value("cueListFontSize", 9).toInt();
}
void AppSettings::setCueListFontSize(int pt) {
    m_s.setValue("cueListFontSize", qMax(7, pt));
}

QString AppSettings::cueListFontFamily() const {
    return m_s.value("cueListFontFamily", QString()).toString();
}
void AppSettings::setCueListFontFamily(const QString &family) {
    m_s.setValue("cueListFontFamily", family);
}

int AppSettings::activeCuePanelSide() const {
    return m_s.value("activeCuePanelSide", 0).toInt();  // 0=left, 1=right
}
void AppSettings::setActiveCuePanelSide(int side) {
    m_s.setValue("activeCuePanelSide", side);
}

int AppSettings::waveformBuckets() const {
    return m_s.value("waveformBuckets", 8000).toInt();
}
void AppSettings::setWaveformBuckets(int b) {
    m_s.setValue("waveformBuckets", b);
}

QString AppSettings::appLanguage() const {
    return m_s.value("appLanguage", "it").toString();
}
void AppSettings::setAppLanguage(const QString &lang) {
    m_s.setValue("appLanguage", lang);
}

QKeySequence AppSettings::keyShowMode() const {
    const QString s = m_s.value("keyShowMode").toString();
    return s.isEmpty() ? QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H) : QKeySequence(s);
}
void AppSettings::setKeyShowMode(const QKeySequence &k) { m_s.setValue("keyShowMode", k.toString()); }

static QKeySequence defaultAddCueKey(const QString &typeKey) {
    static const QMap<QString,QKeySequence> defs = {
        {"audio",       QKeySequence(Qt::CTRL | Qt::Key_1)},
        {"video",       QKeySequence(Qt::CTRL | Qt::Key_2)},
        {"text",        QKeySequence(Qt::CTRL | Qt::Key_3)},
        {"mic",         QKeySequence(Qt::CTRL | Qt::Key_4)},
        {"record",      QKeySequence(Qt::CTRL | Qt::Key_5)},
        {"stop",        QKeySequence(Qt::CTRL | Qt::Key_Q)},
        {"fade",        QKeySequence(Qt::CTRL | Qt::Key_F)},
        {"pause",       QKeySequence(Qt::CTRL | Qt::Key_P)},
        {"play",        QKeySequence(Qt::CTRL | Qt::Key_R)},
        {"effect",      QKeySequence(Qt::CTRL | Qt::Key_E)},
        {"reseteffect", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)},
        {"script",      QKeySequence(Qt::CTRL | Qt::Key_J)},
        {"group",       QKeySequence(Qt::CTRL | Qt::Key_G)},
        {"label",       QKeySequence(Qt::CTRL | Qt::Key_L)},
    };
    return defs.value(typeKey, QKeySequence());
}

QKeySequence AppSettings::keyAddCue(const QString &typeKey) const {
    const QString stored = m_s.value("keyAddCue/" + typeKey).toString();
    return stored.isEmpty() ? defaultAddCueKey(typeKey) : QKeySequence(stored);
}
void AppSettings::setKeyAddCue(const QString &typeKey, const QKeySequence &k) {
    m_s.setValue("keyAddCue/" + typeKey, k.toString());
}

void AppSettings::applyLanguage() {
    static QTranslator *s_tr = nullptr;
    if (s_tr) {
        QCoreApplication::removeTranslator(s_tr);
        delete s_tr;
        s_tr = nullptr;
    }
    const QString lang = AppSettings::instance().appLanguage();
    if (lang == "it") return;
    s_tr = new QTranslator;
    if (!s_tr->load(":/i18n/oql_" + lang + ".qm")) {
        delete s_tr;
        s_tr = nullptr;
    } else {
        QCoreApplication::installTranslator(s_tr);
    }
}

QStringList AppSettings::lv2ExtraPaths() const {
    return m_s.value("lv2ExtraPaths", QStringList{}).toStringList();
}
void AppSettings::setLv2ExtraPaths(const QStringList &paths) {
    m_s.setValue("lv2ExtraPaths", paths);
}

QStringList AppSettings::vstExtraPaths() const {
    return m_s.value("vstExtraPaths", QStringList{}).toStringList();
}
void AppSettings::setVstExtraPaths(const QStringList &paths) {
    m_s.setValue("vstExtraPaths", paths);
}
