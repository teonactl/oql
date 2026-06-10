#pragma once
#include <QDialog>

class ScriptCue;
class QPlainTextEdit;

class ScriptEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScriptEditorDialog(ScriptCue *cue, QWidget *parent = nullptr);

private:
    void run();

    ScriptCue      *m_cue;
    QPlainTextEdit *m_editor;
    QPlainTextEdit *m_console;
};
