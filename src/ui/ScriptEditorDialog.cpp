#include "ScriptEditorDialog.h"
#include "engine/ScriptCue.h"
#include "engine/ScriptEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QFont>
#include <QMessageBox>
#include <QTimer>

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
   sleep(2000)                 → attendi 2 secondi (ms)
   ──────────────────────────────────────────────────────── */

)";

ScriptEditorDialog::ScriptEditorDialog(ScriptCue *cue, QWidget *parent)
    : QDialog(parent), m_cue(cue)
{
    setWindowTitle(QString("Script — %1").arg(cue->name().isEmpty() ? "Cue" : cue->name()));
    setMinimumSize(620, 520);
    resize(720, 580);
    setModal(true);

    QFont mono("Monospace");
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSize(11);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(6);

    // ── Editor ────────────────────────────────────────────────
    m_editor = new QPlainTextEdit;
    m_editor->setFont(mono);
    m_editor->setPlaceholderText("// Scrivi qui il tuo script JavaScript\n// Premi ? API per vedere i comandi disponibili");
    m_editor->setPlainText(cue->script());
    lay->addWidget(m_editor, 3);

    // ── Buttons row ───────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;

    auto *helpBtn = new QPushButton("? API");
    helpBtn->setAutoDefault(false);
    helpBtn->setDefault(false);
    helpBtn->setToolTip("Mostra la documentazione dell'API");
    connect(helpBtn, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, "API Script", QString::fromUtf8(kApiDoc));
    });
    btnRow->addWidget(helpBtn);
    btnRow->addStretch();

    m_runBtn = new QPushButton("▶  Esegui");
    m_runBtn->setAutoDefault(false);
    m_runBtn->setDefault(false);
    connect(m_runBtn, &QPushButton::clicked, this, [this]() {
        m_console->clear();
        ScriptEngine::instance().evaluate(m_editor->toPlainText());
    });
    btnRow->addWidget(m_runBtn);

    auto *okBtn = new QPushButton("Salva");
    okBtn->setAutoDefault(false);
    okBtn->setDefault(false);
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        m_cue->setScript(m_editor->toPlainText());
        accept();
    });
    btnRow->addWidget(okBtn);

    auto *cancelBtn = new QPushButton("Annulla");
    cancelBtn->setAutoDefault(false);
    cancelBtn->setDefault(false);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);

    lay->addLayout(btnRow);

    // ── Console ───────────────────────────────────────────────
    lay->addWidget(new QLabel("Output:"));

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

    // Forza focus sull'editor dopo che il dialog è visibile
    QTimer::singleShot(0, m_editor, [this]() {
        m_editor->setFocus();
        m_editor->moveCursor(QTextCursor::End);
    });
}
