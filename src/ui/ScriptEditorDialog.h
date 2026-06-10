#pragma once
#include <QDialog>

class ScriptCue;
class QPlainTextEdit;
class QPushButton;

class ScriptEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScriptEditorDialog(ScriptCue *cue, QWidget *parent = nullptr);

private:
    ScriptCue      *m_cue;
    QPlainTextEdit *m_editor;
    QPushButton    *m_runBtn;
    QPlainTextEdit *m_console;
};
