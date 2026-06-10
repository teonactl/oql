#include "ScriptEngine.h"
#include "CueList.h"
#include "Cue.h"
#include <QJSValue>
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>

static QString cueTypeStr(Cue::Type t) {
    switch (t) {
    case Cue::Type::Audio:       return "audio";
    case Cue::Type::Video:       return "video";
    case Cue::Type::Text:        return "text";
    case Cue::Type::Mic:         return "mic";
    case Cue::Type::Stop:        return "stop";
    case Cue::Type::Pause:       return "pause";
    case Cue::Type::Play:        return "play";
    case Cue::Type::Fade:        return "fade";
    case Cue::Type::Speed:       return "speed";
    case Cue::Type::Effect:      return "effect";
    case Cue::Type::ResetEffect: return "reseteffect";
    case Cue::Type::Group:       return "group";
    case Cue::Type::Label:       return "label";
    case Cue::Type::Script:      return "script";
    }
    return "unknown";
}

static QString cueStateStr(Cue::State s) {
    switch (s) {
    case Cue::State::Idle:    return "idle";
    case Cue::State::Waiting: return "waiting";
    case Cue::State::Playing: return "playing";
    case Cue::State::Paused:  return "paused";
    }
    return "idle";
}

// ── JsCue — thin QObject wrapper around a Cue* ───────────────────────────────
class JsCue : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id     READ id       CONSTANT)
    Q_PROPERTY(QString name   READ name     WRITE setName)
    Q_PROPERTY(QString number READ number   WRITE setNumber)
    Q_PROPERTY(QString type   READ typeStr  CONSTANT)
    Q_PROPERTY(QString state  READ stateStr)
public:
    explicit JsCue(Cue *cue, QObject *parent = nullptr) : QObject(parent), m_cue(cue) {}

    QString id()       const { return m_cue ? m_cue->id()     : QString(); }
    QString name()     const { return m_cue ? m_cue->name()   : QString(); }
    QString number()   const { return m_cue ? m_cue->number() : QString(); }
    QString typeStr()  const { return m_cue ? cueTypeStr(m_cue->type())   : QString(); }
    QString stateStr() const { return m_cue ? cueStateStr(m_cue->state()) : QString(); }

    void setName(const QString &v)   { if (m_cue) m_cue->setName(v); }
    void setNumber(const QString &v) { if (m_cue) m_cue->setNumber(v); }

public slots:
    void go()    { if (m_cue) m_cue->go(); }
    void stop()  { if (m_cue) m_cue->stop(); }
    void pause() { if (m_cue) m_cue->pause(); }

private:
    Cue *m_cue;
};

// ── JsWorkspace — exposes CueList to JS ──────────────────────────────────────
class JsWorkspace : public QObject {
    Q_OBJECT
public:
    explicit JsWorkspace(CueList *cues, QJSEngine *engine, QObject *parent = nullptr)
        : QObject(parent), m_cues(cues), m_engine(engine) {}

public slots:
    void go()      { m_cues->go(); }
    void stopAll() { m_cues->stopAll(); }
    int  count()   { return m_cues->count(); }

    QJSValue at(int i) { return wrap(m_cues->cueAt(i)); }

    QJSValue byId(const QString &id) { return wrap(m_cues->findCueById(id)); }

    QJSValue byNumber(const QString &num) {
        for (int i = 0; i < m_cues->count(); ++i) {
            Cue *c = m_cues->cueAt(i);
            if (c && c->number() == num) return wrap(c);
        }
        return m_engine->evaluate("null");
    }

    QJSValue byName(const QString &name) {
        for (int i = 0; i < m_cues->count(); ++i) {
            Cue *c = m_cues->cueAt(i);
            if (c && c->name() == name) return wrap(c);
        }
        return m_engine->evaluate("null");
    }

private:
    QJSValue wrap(Cue *c) {
        if (!c) return m_engine->evaluate("null");
        return m_engine->newQObject(new JsCue(c, this));
    }
    CueList   *m_cues;
    QJSEngine *m_engine;
};

// ── JsSleep — blocking sleep that keeps the UI alive via processEvents ────────
class JsSleep : public QObject {
    Q_OBJECT
public:
    explicit JsSleep(QObject *parent = nullptr) : QObject(parent) {}
public slots:
    void sleep(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
};

// ── JsPrint — bridges JS print() to ScriptEngine::outputLine ─────────────────
class JsPrint : public QObject {
    Q_OBJECT
public:
    explicit JsPrint(ScriptEngine *parent) : QObject(parent), m_engine(parent) {}
public slots:
    void print(const QString &msg) { emit m_engine->outputLine(msg); }
private:
    ScriptEngine *m_engine;
};

// ── ScriptEngine ─────────────────────────────────────────────────────────────
ScriptEngine &ScriptEngine::instance() {
    static ScriptEngine eng;
    return eng;
}

ScriptEngine::ScriptEngine(QObject *parent) : QObject(parent) {}

void ScriptEngine::init(CueList *cues) {
    m_cues = cues;
    setup();
}

void ScriptEngine::setup() {
    if (!m_cues) return;
    auto *workspace = new JsWorkspace(m_cues, &m_engine, this);
    auto *printer   = new JsPrint(this);
    auto *sleeper   = new JsSleep(this);
    m_engine.globalObject().setProperty("workspace",  m_engine.newQObject(workspace));
    m_engine.globalObject().setProperty("__printer",  m_engine.newQObject(printer));
    m_engine.globalObject().setProperty("__sleeper",  m_engine.newQObject(sleeper));
    m_engine.evaluate("function print(msg){ __printer.print(String(msg)); }");
    m_engine.evaluate("var log = print;");
    m_engine.evaluate("function sleep(ms){ __sleeper.sleep(ms); }");
}

QString ScriptEngine::evaluate(const QString &script) {
    if (!m_cues) return "ScriptEngine not initialised";
    const QJSValue result = m_engine.evaluate(script);
    if (result.isError()) {
        const QString err = QString("Line %1: %2")
            .arg(result.property("lineNumber").toInt())
            .arg(result.toString());
        emit outputLine("ERROR: " + err);
        return err;
    }
    return {};
}

#include "ScriptEngine.moc"
