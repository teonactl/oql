#include "SettingsDialog.h"
#include "engine/AppSettings.h"
#include "engine/Workspace.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QLabel>
#include <QKeySequenceEdit>

SettingsDialog::SettingsDialog(Workspace *workspace, QWidget *parent)
    : QDialog(parent), m_workspace(workspace)
{
    setWindowTitle("Impostazioni");
    setMinimumWidth(400);

    auto *mainLay = new QVBoxLayout(this);

    auto *tabs = new QTabWidget;
    mainLay->addWidget(tabs);

    // ── Tab 1: Generale ──────────────────────────────────────────────────────
    auto *genWidget = new QWidget;
    auto *genForm   = new QFormLayout(genWidget);
    genForm->setSpacing(10);
    genForm->setContentsMargins(12, 12, 12, 12);

    auto makeSecSpin = [](double val) {
        auto *s = new QDoubleSpinBox;
        s->setRange(0.0, 60.0);
        s->setDecimals(1);
        s->setSingleStep(0.5);
        s->setSuffix(" s");
        s->setSpecialValueText("Off");
        s->setValue(val);
        return s;
    };

    m_fadeDurationSpin = makeSecSpin(AppSettings::instance().defaultFadeDuration());
    m_fadeDurationSpin->setRange(0.1, 60.0);  // FadeCue must be > 0
    genForm->addRow("Durata default Fade Cue:", m_fadeDurationSpin);

    m_fadeInSpin = makeSecSpin(AppSettings::instance().defaultFadeInDuration());
    genForm->addRow("Fade-in default (Audio Cue):", m_fadeInSpin);

    m_fadeOutSpin = makeSecSpin(AppSettings::instance().defaultFadeOutDuration());
    genForm->addRow("Fade-out default (Audio Cue):", m_fadeOutSpin);

    m_autoNumberCheck = new QCheckBox("Numera automaticamente le nuove cue");
    m_autoNumberCheck->setChecked(AppSettings::instance().autoNumberNewCues());
    genForm->addRow(m_autoNumberCheck);

    genForm->addRow(new QLabel(
        "<small style='color:#888'>Le impostazioni generali si applicano a tutti i progetti.</small>"));

    tabs->addTab(genWidget, "Generale");

    // ── Tab 2: Progetto ───────────────────────────────────────────────────────
    auto *prjWidget = new QWidget;
    auto *prjForm   = new QFormLayout(prjWidget);
    prjForm->setSpacing(10);
    prjForm->setContentsMargins(12, 12, 12, 12);

    m_projectNameEdit = new QLineEdit(workspace->name());
    prjForm->addRow("Nome progetto:", m_projectNameEdit);

    m_showCueNumbersCheck = new QCheckBox("Mostra la colonna numero (#)");
    m_showCueNumbersCheck->setChecked(workspace->showCueNumbers());
    prjForm->addRow(m_showCueNumbersCheck);

    m_autoFadeOnStopCheck = new QCheckBox("Auto-fade allo stop delle cue audio");
    m_autoFadeOnStopCheck->setChecked(workspace->autoFadeOnStop());
    prjForm->addRow(m_autoFadeOnStopCheck);

    prjForm->addRow(new QLabel(
        "<small style='color:#888'>Le impostazioni di progetto vengono salvate nel file .oql.</small>"));

    tabs->addTab(prjWidget, "Progetto");

    // ── Tab 3: Scorciatoie ────────────────────────────────────────────────────
    auto *keyWidget = new QWidget;
    auto *keyForm   = new QFormLayout(keyWidget);
    keyForm->setSpacing(10);
    keyForm->setContentsMargins(12, 12, 12, 12);

    m_keyGoEdit = new QKeySequenceEdit(AppSettings::instance().keyGo());
    keyForm->addRow("GO:", m_keyGoEdit);

    m_keyStopAllEdit = new QKeySequenceEdit(AppSettings::instance().keyStopAll());
    keyForm->addRow("Stop All:", m_keyStopAllEdit);

    m_keyFirstCueEdit = new QKeySequenceEdit(AppSettings::instance().keyFirstCue());
    keyForm->addRow("Prima cue:", m_keyFirstCueEdit);

    keyForm->addRow(new QLabel(
        "<small style='color:#888'>Fare clic sul campo e premere la combinazione desiderata.</small>"));

    tabs->addTab(keyWidget, "Scorciatoie");

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLay->addWidget(btns);

    connect(btns, &QDialogButtonBox::accepted, this, &SettingsDialog::apply);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::apply() {
    // App settings
    AppSettings::instance().setDefaultFadeDuration(m_fadeDurationSpin->value());
    AppSettings::instance().setDefaultFadeInDuration(m_fadeInSpin->value());
    AppSettings::instance().setDefaultFadeOutDuration(m_fadeOutSpin->value());
    AppSettings::instance().setAutoNumberNewCues(m_autoNumberCheck->isChecked());

    // Project settings
    m_workspace->setName(m_projectNameEdit->text().trimmed());
    m_workspace->setShowCueNumbers(m_showCueNumbersCheck->isChecked());
    m_workspace->setAutoFadeOnStop(m_autoFadeOnStopCheck->isChecked());

    // Shortcuts
    AppSettings::instance().setKeyGo(m_keyGoEdit->keySequence());
    AppSettings::instance().setKeyStopAll(m_keyStopAllEdit->keySequence());
    AppSettings::instance().setKeyFirstCue(m_keyFirstCueEdit->keySequence());

    accept();
}
