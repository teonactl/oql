#include "ScriptEditorDialog.h"
#include "engine/ScriptCue.h"
#include "engine/ScriptEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QFont>
#include <QDialog>
#include <QMessageBox>

static const char *kApiDoc = R"(/* ── API disponibile ──────────────────────────────────────
   workspace.count()            → numero di cue
   workspace.at(i)              → cue all'indice i (0-based)
   workspace.byNumber("1")      → cue con numero "1"
   workspace.byName("Intro")    → prima cue con quel nome
   workspace.byId("uuid")       → cue per UUID
   workspace.go()               → GO globale (avanza playhead)
   workspace.stopAll()          → ferma tutte le cue

   cue.go()  / cue.stop()  / cue.pause()
   cue.name    (lettura/scrittura)
   cue.number  (lettura/scrittura)
   cue.type    (lettura)  es. "audio", "video", "script"
   cue.state   (lettura)  "idle" | "playing" | "paused" | "waiting"

   print("messaggio")  /  log("messaggio")
   ──────────────────────────────────────────────────────── */

)";

ScriptEditorDialog::ScriptEditorDialog(ScriptCue *cue, QWidget *parent)
    : QDialog(parent, Qt::Dialog), m_cue(cue)
{
    setWindowTitle(QString("Script — %1").arg(cue->name().isEmpty() ? "Cue" : cue->name()));
    setMinimumSize(620, 520);
    resize(720, 580);

    QFont mono("Monospace");
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSize(11);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(6);

    // ── Editor ────────────────────────────────────────────
    m_editor = new QPlainTextEdit;
    m_editor->setFont(mono);
    m_editor->setPlaceholderText(kApiDoc);
    const QString existing = cue->script();
    m_editor->setPlainText(existing.isEmpty() ? "" : existing);
    lay->addWidget(m_editor, 3);

    // ── Buttons row ───────────────────────────────────────
    auto *btnRow = new QHBoxLayout;

    auto *helpBtn = new QPushButton("? API");
    helpBtn->setToolTip("Mostra la documentazione dell'API");
    connect(helpBtn, &QPushButton::clicked, this, [this]() {
        if (m_editor->toPlainText().isEmpty())
            m_editor->setPlainText(kApiDoc);
        else
            QMessageBox::information(this, "API Script", QString::fromLatin1(kApiDoc));
    });
    btnRow->addWidget(helpBtn);
    btnRow->addStretch();

    auto *runBtn = new QPushButton("▶  Esegui");
    runBtn->setDefault(false);
    connect(runBtn, &QPushButton::clicked, this, &ScriptEditorDialog::run);
    btnRow->addWidget(runBtn);

    auto *okBtn = new QPushButton("Salva");
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        m_cue->setScript(m_editor->toPlainText());
        accept();
    });
    btnRow->addWidget(okBtn);

    auto *cancelBtn = new QPushButton("Annulla");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);

    lay->addLayout(btnRow);

    // ── Console ───────────────────────────────────────────
    auto *consLbl = new QLabel("Output:");
    lay->addWidget(consLbl);

    m_console = new QPlainTextEdit;
    m_console->setReadOnly(true);
    m_console->setMaximumHeight(100);
    m_console->setFont(mono);
    m_console->setPlaceholderText("Output degli script…");
    lay->addWidget(m_console, 1);

    connect(&ScriptEngine::instance(), &ScriptEngine::outputLine,
            this, [this](const QString &line) {
        m_console->appendPlainText(line);
    });
}

void ScriptEditorDialog::run() {
    m_console->clear();
    ScriptEngine::instance().evaluate(m_editor->toPlainText());
}
