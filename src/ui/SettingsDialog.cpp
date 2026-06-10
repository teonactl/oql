#include "SettingsDialog.h"
#include "engine/AppSettings.h"
#include "engine/Workspace.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QLabel>
#include <QKeySequenceEdit>
#include <QFontComboBox>
#include <QHBoxLayout>

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

    m_rowHeightSpin = new QSpinBox;
    m_rowHeightSpin->setRange(16, 80);
    m_rowHeightSpin->setSingleStep(2);
    m_rowHeightSpin->setSuffix(" px");
    m_rowHeightSpin->setValue(AppSettings::instance().cueListRowHeight());
    genForm->addRow("Altezza righe cue list:", m_rowHeightSpin);

    m_fontFamilyCombo = new QFontComboBox;
    m_fontFamilyCombo->setEditable(false);
    {
        const QString saved = AppSettings::instance().cueListFontFamily();
        if (!saved.isEmpty()) m_fontFamilyCombo->setCurrentFont(QFont(saved));
    }

    m_fontSizeSpin = new QSpinBox;
    m_fontSizeSpin->setRange(7, 24);
    m_fontSizeSpin->setSingleStep(1);
    m_fontSizeSpin->setSuffix(" pt");
    m_fontSizeSpin->setValue(AppSettings::instance().cueListFontSize());
    m_fontSizeSpin->setFixedWidth(70);

    auto *fontRow = new QWidget;
    auto *fontLay = new QHBoxLayout(fontRow);
    fontLay->setContentsMargins(0, 0, 0, 0);
    fontLay->addWidget(m_fontFamilyCombo, 1);
    fontLay->addWidget(m_fontSizeSpin, 0);
    genForm->addRow("Font cue list:", fontRow);

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

    // ── Tab 4: Remote ──────────────────────────────────────────────────────────
    auto *webWidget = new QWidget;
    auto *webForm   = new QFormLayout(webWidget);
    webForm->setSpacing(10);
    webForm->setContentsMargins(12, 12, 12, 12);

    m_webEnabledCheck = new QCheckBox("Abilita Web Remote");
    m_webEnabledCheck->setChecked(AppSettings::instance().webEnabled());
    webForm->addRow(m_webEnabledCheck);

    m_webPortSpin = new QSpinBox;
    m_webPortSpin->setRange(1024, 65535);
    m_webPortSpin->setValue(AppSettings::instance().webPort());
    webForm->addRow("Porta HTTP:", m_webPortSpin);

    webForm->addRow(new QLabel(
        "<small style='color:#888'>Connettiti da smartphone: <b>http://&lt;IP&gt;:porta/</b><br>"
        "L'IP è mostrato nella barra di stato quando il server è attivo.</small>"));

    tabs->addTab(webWidget, "Remote");

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
    AppSettings::instance().setCueListRowHeight(m_rowHeightSpin->value());
    AppSettings::instance().setCueListFontSize(m_fontSizeSpin->value());
    AppSettings::instance().setCueListFontFamily(m_fontFamilyCombo->currentFont().family());

    // Project settings
    m_workspace->setName(m_projectNameEdit->text().trimmed());
    m_workspace->setShowCueNumbers(m_showCueNumbersCheck->isChecked());
    m_workspace->setAutoFadeOnStop(m_autoFadeOnStopCheck->isChecked());

    // Shortcuts
    AppSettings::instance().setKeyGo(m_keyGoEdit->keySequence());
    AppSettings::instance().setKeyStopAll(m_keyStopAllEdit->keySequence());
    AppSettings::instance().setKeyFirstCue(m_keyFirstCueEdit->keySequence());

    // Remote
    AppSettings::instance().setWebEnabled(m_webEnabledCheck->isChecked());
    AppSettings::instance().setWebPort(quint16(m_webPortSpin->value()));

    accept();
}
