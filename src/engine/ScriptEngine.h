#pragma once
#include <QObject>
#include <QJSEngine>

class CueList;

class ScriptEngine : public QObject {
    Q_OBJECT
public:
    static ScriptEngine &instance();
    void init(CueList *cues);
    QString evaluate(const QString &script); // returns error string, empty = ok

signals:
    void outputLine(const QString &line);

private:
    explicit ScriptEngine(QObject *parent = nullptr);
    void setup();
    QJSEngine  m_engine;
    CueList   *m_cues = nullptr;
};
