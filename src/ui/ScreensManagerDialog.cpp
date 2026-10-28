#include "ScreensManagerDialog.h"
#include "PhysicalOutputZonesCanvas.h"
#include "engine/Workspace.h"
#include <QApplication>
#include <QScreen>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QLineEdit>

ScreensManagerDialog::ScreensManagerDialog(Workspace *workspace, QWidget *parent)
    : QDialog(parent), m_workspace(workspace)
{
    setWindowTitle(tr("Gestione schermi — OQL"));
    setModal(false);
    resize(920, 620);

    auto *mainLay = new QVBoxLayout(this);
    auto *columns = new QHBoxLayout;
    mainLay->addLayout(columns, 1);

    // ── Colonna sinistra: output fisici ─────────────────────────────────
    auto *outGroup = new QGroupBox(tr("Output fisici (proiettori/monitor)"));
    auto *outLay   = new QVBoxLayout(outGroup);

    m_outputList = new QListWidget;
    m_outputList->setMaximumHeight(110);
    outLay->addWidget(m_outputList);

    auto *outBtnRow = new QHBoxLayout;
    m_addOutputBtn    = new QPushButton(tr("Aggiungi"));
    m_removeOutputBtn = new QPushButton(tr("Rimuovi"));
    m_renameOutputBtn = new QPushButton(tr("Rinomina…"));
    outBtnRow->addWidget(m_addOutputBtn);
    outBtnRow->addWidget(m_removeOutputBtn);
    outBtnRow->addWidget(m_renameOutputBtn);
    outLay->addLayout(outBtnRow);

    auto *outForm = new QFormLayout;
    m_outputScreenCombo = new QComboBox;
    outForm->addRow(tr("Schermo reale:"), m_outputScreenCombo);
    outLay->addLayout(outForm);

    m_realScreenInfoLabel = new QLabel;
    m_realScreenInfoLabel->setStyleSheet("color:#9c9; font-size:11px;");
    m_realScreenInfoLabel->setWordWrap(true);
    outLay->addWidget(m_realScreenInfoLabel);

    auto *resGroup = new QGroupBox(tr("Risoluzione del canvas"));
    auto *resLay   = new QVBoxLayout(resGroup);
    m_autoResolutionCheck = new QCheckBox(tr("Automatica (dallo schermo assegnato)"));
    resLay->addWidget(m_autoResolutionCheck);
    auto *resForm = new QFormLayout;
    m_resWSpin = new QSpinBox; m_resWSpin->setRange(1, 16384); m_resWSpin->setSuffix(" px");
    m_resHSpin = new QSpinBox; m_resHSpin->setRange(1, 16384); m_resHSpin->setSuffix(" px");
    auto *resRow = new QWidget;
    auto *resRowLay = new QHBoxLayout(resRow);
    resRowLay->setContentsMargins(0, 0, 0, 0);
    resRowLay->addWidget(m_resWSpin);
    resRowLay->addWidget(new QLabel("×"));
    resRowLay->addWidget(m_resHSpin);
    resForm->addRow(tr("Dimensione:"), resRow);
    resLay->addLayout(resForm);
    m_detectResolutionBtn = new QPushButton(tr("Rileva dallo schermo reale"));
    resLay->addWidget(m_detectResolutionBtn);
    outLay->addWidget(resGroup);
    outLay->addStretch();

    columns->addWidget(outGroup, 1);

    // ── Colonna destra: schermi virtuali dell'output selezionato ────────
    auto *scrGroup = new QGroupBox(tr("Schermi virtuali"));
    auto *scrLay   = new QVBoxLayout(scrGroup);

    auto *presetGroup = new QGroupBox(tr("Dividi in…"));
    auto *presetLay = new QHBoxLayout(presetGroup);
    m_presetCombo = new QComboBox;
    m_presetCombo->addItem(tr("Intero (nessuna divisione)"), QSize(1, 1));
    m_presetCombo->addItem(tr("Verticale — 2 fianco a fianco"), QSize(2, 1));
    m_presetCombo->addItem(tr("Orizzontale — 2 sopra/sotto"), QSize(1, 2));
    m_presetCombo->addItem(tr("Verticale — 3 fianco a fianco"), QSize(3, 1));
    m_presetCombo->addItem(tr("Orizzontale — 3 sopra/sotto"), QSize(1, 3));
    m_presetCombo->addItem(tr("Mosaico 2×2"), QSize(2, 2));
    m_presetCombo->addItem(tr("Mosaico 3×3"), QSize(3, 3));
    m_presetCombo->addItem(tr("Mosaico 4×4"), QSize(4, 4));
    presetLay->addWidget(m_presetCombo, 1);
    m_applyPresetBtn = new QPushButton(tr("Applica"));
    presetLay->addWidget(m_applyPresetBtn);
    scrLay->addWidget(presetGroup);
    scrLay->addWidget(new QLabel(
        "<small style='color:#888'>" +
        tr("Sostituisce tutti gli schermi virtuali di questo output con una griglia di rettangoli "
           "(poligono e area contenuto già tassellati, stile video-wall) — poi modellabili liberamente.") +
        "</small>"));

    m_screenList = new QListWidget;
    m_screenList->setMaximumHeight(90);
    scrLay->addWidget(m_screenList);

    auto *scrBtnRow = new QHBoxLayout;
    m_addScreenBtn    = new QPushButton(tr("Aggiungi"));
    m_removeScreenBtn = new QPushButton(tr("Rimuovi"));
    m_renameScreenBtn = new QPushButton(tr("Rinomina…"));
    scrBtnRow->addWidget(m_addScreenBtn);
    scrBtnRow->addWidget(m_removeScreenBtn);
    scrBtnRow->addWidget(m_renameScreenBtn);
    scrLay->addLayout(scrBtnRow);

    auto *canvasGroup = new QGroupBox(tr("Area contenuto mostrata (canvasRect, 0–1)"));
    auto *canvasForm   = new QFormLayout(canvasGroup);
    auto mkSpin = [] {
        auto *s = new QDoubleSpinBox;
        s->setRange(-0.5, 1.5);
        s->setSingleStep(0.01);
        s->setDecimals(3);
        return s;
    };
    m_canvasXSpin = mkSpin();
    m_canvasYSpin = mkSpin();
    m_canvasWSpin = mkSpin();
    m_canvasHSpin = mkSpin();
    auto *canvasRow1 = new QWidget; auto *canvasRow1Lay = new QHBoxLayout(canvasRow1);
    canvasRow1Lay->setContentsMargins(0,0,0,0);
    canvasRow1Lay->addWidget(new QLabel(tr("X:"))); canvasRow1Lay->addWidget(m_canvasXSpin);
    canvasRow1Lay->addWidget(new QLabel(tr("Y:"))); canvasRow1Lay->addWidget(m_canvasYSpin);
    canvasForm->addRow(canvasRow1);
    auto *canvasRow2 = new QWidget; auto *canvasRow2Lay = new QHBoxLayout(canvasRow2);
    canvasRow2Lay->setContentsMargins(0,0,0,0);
    canvasRow2Lay->addWidget(new QLabel(tr("L:"))); canvasRow2Lay->addWidget(m_canvasWSpin);
    canvasRow2Lay->addWidget(new QLabel(tr("A:"))); canvasRow2Lay->addWidget(m_canvasHSpin);
    canvasForm->addRow(canvasRow2);

    m_fitModeCombo = new QComboBox;
    m_fitModeCombo->addItem(tr("Adatta (mantiene le proporzioni, può lasciare bande)"), int(VirtualScreen::FitMode::Contain));
    m_fitModeCombo->addItem(tr("Riempi (mantiene le proporzioni, ritaglia l'eccesso)"), int(VirtualScreen::FitMode::Cover));
    m_fitModeCombo->addItem(tr("Stirato (riempie esattamente, può deformare)"), int(VirtualScreen::FitMode::Stretch));
    m_fitModeCombo->addItem(tr("Centrato (dimensione originale)"), int(VirtualScreen::FitMode::Center));
    m_fitModeCombo->addItem(tr("Manuale (zoom + posizione libera)"), int(VirtualScreen::FitMode::Manual));
    canvasForm->addRow(tr("Adattamento:"), m_fitModeCombo);

    m_anchorXSpin = new QDoubleSpinBox; m_anchorXSpin->setRange(0.0, 1.0); m_anchorXSpin->setSingleStep(0.05); m_anchorXSpin->setDecimals(2);
    m_anchorYSpin = new QDoubleSpinBox; m_anchorYSpin->setRange(0.0, 1.0); m_anchorYSpin->setSingleStep(0.05); m_anchorYSpin->setDecimals(2);
    auto *anchorRow = new QWidget; auto *anchorRowLay = new QHBoxLayout(anchorRow);
    anchorRowLay->setContentsMargins(0,0,0,0);
    anchorRowLay->addWidget(new QLabel(tr("X:"))); anchorRowLay->addWidget(m_anchorXSpin);
    anchorRowLay->addWidget(new QLabel(tr("Y:"))); anchorRowLay->addWidget(m_anchorYSpin);
    canvasForm->addRow(tr("Posizione (0=in alto/sx, 1=in basso/dx):"), anchorRow);

    scrLay->addWidget(canvasGroup);

    auto *maskGroup = new QGroupBox(tr("Forma (maschera poligonale) — clicca un poligono per selezionarlo"));
    auto *maskLay   = new QVBoxLayout(maskGroup);
    m_zonesCanvas = new PhysicalOutputZonesCanvas;
    maskLay->addWidget(m_zonesCanvas, 1);
    auto *maskBtnRow = new QHBoxLayout;
    m_addVertexBtn    = new QPushButton(tr("Aggiungi vertice"));
    m_removeVertexBtn = new QPushButton(tr("Rimuovi vertice"));
    m_resetMaskBtn    = new QPushButton(tr("Reset (rettangolo)"));
    maskBtnRow->addWidget(m_addVertexBtn);
    maskBtnRow->addWidget(m_removeVertexBtn);
    maskBtnRow->addWidget(m_resetMaskBtn);
    maskLay->addLayout(maskBtnRow);
    scrLay->addWidget(maskGroup, 1);

    columns->addWidget(scrGroup, 2);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::close);
    mainLay->addWidget(btns);

    connect(m_outputList, &QListWidget::currentItemChanged, this, &ScreensManagerDialog::onOutputSelectionChanged);
    connect(m_screenList,  &QListWidget::currentItemChanged, this, &ScreensManagerDialog::onScreenSelectionChanged);
    connect(m_addOutputBtn,    &QPushButton::clicked, this, &ScreensManagerDialog::onAddOutput);
    connect(m_removeOutputBtn, &QPushButton::clicked, this, &ScreensManagerDialog::onRemoveOutput);
    connect(m_renameOutputBtn, &QPushButton::clicked, this, &ScreensManagerDialog::onRenameOutput);
    connect(m_outputScreenCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ScreensManagerDialog::onOutputScreenNameChanged);
    connect(m_autoResolutionCheck, &QCheckBox::toggled, this, &ScreensManagerDialog::onAutoResolutionToggled);
    connect(m_resWSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ScreensManagerDialog::onResolutionSpinChanged);
    connect(m_resHSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ScreensManagerDialog::onResolutionSpinChanged);
    connect(m_detectResolutionBtn, &QPushButton::clicked, this, &ScreensManagerDialog::onDetectResolution);
    connect(m_applyPresetBtn, &QPushButton::clicked, this, &ScreensManagerDialog::onApplyPreset);
    connect(m_addScreenBtn,    &QPushButton::clicked, this, &ScreensManagerDialog::onAddScreen);
    connect(m_removeScreenBtn, &QPushButton::clicked, this, &ScreensManagerDialog::onRemoveScreen);
    connect(m_renameScreenBtn, &QPushButton::clicked, this, &ScreensManagerDialog::onRenameScreen);
    connect(m_canvasXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ScreensManagerDialog::onCanvasRectChanged);
    connect(m_canvasYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ScreensManagerDialog::onCanvasRectChanged);
    connect(m_canvasWSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ScreensManagerDialog::onCanvasRectChanged);
    connect(m_canvasHSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ScreensManagerDialog::onCanvasRectChanged);
    connect(m_fitModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ScreensManagerDialog::onFitModeChanged);
    connect(m_anchorXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ScreensManagerDialog::onAnchorChanged);
    connect(m_anchorYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ScreensManagerDialog::onAnchorChanged);
    connect(m_addVertexBtn,    &QPushButton::clicked, m_zonesCanvas, &PhysicalOutputZonesCanvas::addVertex);
    connect(m_removeVertexBtn, &QPushButton::clicked, m_zonesCanvas, &PhysicalOutputZonesCanvas::removeSelectedVertex);
    connect(m_resetMaskBtn,    &QPushButton::clicked, m_zonesCanvas, &PhysicalOutputZonesCanvas::resetPolygon);
    connect(m_zonesCanvas, &PhysicalOutputZonesCanvas::maskEdited, this, &ScreensManagerDialog::onMaskEdited);
    connect(m_zonesCanvas, &PhysicalOutputZonesCanvas::selectionChangedByClick, this, &ScreensManagerDialog::onCanvasSelectionChanged);

    refreshAll();
}

QString ScreensManagerDialog::currentOutputId() const {
    auto *item = m_outputList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QString ScreensManagerDialog::currentScreenId() const {
    auto *item = m_screenList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void ScreensManagerDialog::refreshAll() {
    refreshOutputList(currentOutputId());
}

void ScreensManagerDialog::refreshOutputList(const QString &selectId) {
    m_loading = true;
    m_outputList->clear();
    for (const PhysicalOutput &o : m_workspace->physicalOutputs()) {
        auto *item = new QListWidgetItem(o.name());
        item->setData(Qt::UserRole, o.id());
        m_outputList->addItem(item);
        if (o.id() == selectId) m_outputList->setCurrentItem(item);
    }
    if (!m_outputList->currentItem() && m_outputList->count() > 0)
        m_outputList->setCurrentRow(0);
    m_loading = false;
    onOutputSelectionChanged();
}

void ScreensManagerDialog::onOutputSelectionChanged() {
    if (m_loading) return;
    const QString outId = currentOutputId();
    m_zonesCanvas->setOutput(m_workspace, outId);

    m_loading = true;
    m_outputScreenCombo->clear();
    m_outputScreenCombo->addItem(tr("Schermo principale (predefinito)"), QString{});
    for (QScreen *scr : QApplication::screens()) {
        const QString name  = scr->name();
        const QString label = QString("%1  (%2×%3)")
            .arg(name).arg(scr->geometry().width()).arg(scr->geometry().height());
        m_outputScreenCombo->addItem(label, name);
    }
    PhysicalOutput *out = m_workspace->physicalOutputById(outId);
    const bool hasOut = (out != nullptr);
    m_outputScreenCombo->setEnabled(hasOut);
    m_removeOutputBtn->setEnabled(hasOut && m_workspace->physicalOutputs().size() > 1);
    m_renameOutputBtn->setEnabled(hasOut);
    m_autoResolutionCheck->setEnabled(hasOut);
    m_detectResolutionBtn->setEnabled(hasOut);
    m_presetCombo->setEnabled(hasOut);
    m_applyPresetBtn->setEnabled(hasOut);
    if (out) {
        const int idx = m_outputScreenCombo->findData(out->physicalScreenName());
        m_outputScreenCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        m_autoResolutionCheck->setChecked(out->autoResolution());
        m_resWSpin->setValue(out->resolution().width());
        m_resHSpin->setValue(out->resolution().height());
        m_resWSpin->setEnabled(!out->autoResolution());
        m_resHSpin->setEnabled(!out->autoResolution());
    }
    m_loading = false;

    refreshRealScreenInfo();
    refreshScreenList();
}

void ScreensManagerDialog::refreshRealScreenInfo() {
    PhysicalOutput *out = m_workspace->physicalOutputById(currentOutputId());
    if (!out) { m_realScreenInfoLabel->setText({}); return; }

    QScreen *target = nullptr;
    if (!out->physicalScreenName().isEmpty()) {
        for (QScreen *s : QApplication::screens())
            if (s->name() == out->physicalScreenName()) { target = s; break; }
    } else {
        target = QApplication::primaryScreen();
    }
    if (!target) {
        m_realScreenInfoLabel->setText(tr("Schermo reale non rilevato."));
        return;
    }
    const QRect g = target->geometry();
    m_realScreenInfoLabel->setText(
        tr("Rilevato: %1×%2 px, densità %3x (%4 DPI)")
            .arg(g.width()).arg(g.height())
            .arg(target->devicePixelRatio(), 0, 'f', 2)
            .arg(int(target->logicalDotsPerInch())));
}

void ScreensManagerDialog::onOutputScreenNameChanged(int idx) {
    if (m_loading) return;
    PhysicalOutput *out = m_workspace->physicalOutputById(currentOutputId());
    if (!out) return;
    out->setPhysicalScreenName(m_outputScreenCombo->itemData(idx).toString());
    m_workspace->notifyScreensChanged();
    refreshRealScreenInfo();
}

void ScreensManagerDialog::onAutoResolutionToggled(bool on) {
    if (m_loading) return;
    PhysicalOutput *out = m_workspace->physicalOutputById(currentOutputId());
    if (!out) return;
    out->setAutoResolution(on);
    m_resWSpin->setEnabled(!on);
    m_resHSpin->setEnabled(!on);
    m_workspace->notifyScreensChanged();
}

void ScreensManagerDialog::onResolutionSpinChanged() {
    if (m_loading) return;
    PhysicalOutput *out = m_workspace->physicalOutputById(currentOutputId());
    if (!out) return;
    out->setResolution(QSize(m_resWSpin->value(), m_resHSpin->value()));
    m_workspace->notifyScreensChanged();
}

void ScreensManagerDialog::onDetectResolution() {
    PhysicalOutput *out = m_workspace->physicalOutputById(currentOutputId());
    if (!out) return;
    QScreen *target = nullptr;
    if (!out->physicalScreenName().isEmpty()) {
        for (QScreen *s : QApplication::screens())
            if (s->name() == out->physicalScreenName()) { target = s; break; }
    }
    if (!target) target = QApplication::primaryScreen();
    if (!target) return;
    m_loading = true;
    m_resWSpin->setValue(target->geometry().width());
    m_resHSpin->setValue(target->geometry().height());
    m_loading = false;
    out->setResolution(target->geometry().size());
    m_workspace->notifyScreensChanged();
}

void ScreensManagerDialog::onApplyPreset() {
    const QString outId = currentOutputId();
    if (outId.isEmpty()) return;
    const QSize grid = m_presetCombo->currentData().toSize();
    if (m_workspace->virtualScreensFor(outId).size() > 1
        || (m_workspace->virtualScreensFor(outId).size() == 1
            && m_workspace->virtualScreensFor(outId).first()->maskPolygon() != QPolygonF(QRectF(0,0,1,1)))) {
        const auto reply = QMessageBox::question(this, tr("Applica divisione"),
            tr("Questo sostituisce tutti gli schermi virtuali esistenti di questo output "
               "(e le forme/ritagli già impostati) con la nuova griglia %1×%2. Continuare?")
                .arg(grid.width()).arg(grid.height()));
        if (reply != QMessageBox::Yes) return;
    }
    m_workspace->replaceVirtualScreensWithGrid(outId, grid.width(), grid.height());
    refreshScreenList();
}

void ScreensManagerDialog::onAddOutput() {
    const QString id = m_workspace->addPhysicalOutput();
    refreshOutputList(id);
}

void ScreensManagerDialog::onRemoveOutput() {
    const QString id = currentOutputId();
    if (id.isEmpty()) return;
    m_workspace->removePhysicalOutput(id);
    refreshOutputList();
}

void ScreensManagerDialog::onRenameOutput() {
    PhysicalOutput *out = m_workspace->physicalOutputById(currentOutputId());
    if (!out) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rinomina output"), tr("Nome:"),
                                                 QLineEdit::Normal, out->name(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    out->setName(name.trimmed());
    m_workspace->notifyScreensChanged();
    refreshOutputList(out->id());
}

void ScreensManagerDialog::refreshScreenList(const QString &selectId) {
    m_loading = true;
    m_screenList->clear();
    const QString outId = currentOutputId();
    for (VirtualScreen *s : m_workspace->virtualScreensFor(outId)) {
        auto *item = new QListWidgetItem(s->name());
        item->setData(Qt::UserRole, s->id());
        m_screenList->addItem(item);
        if (s->id() == selectId) m_screenList->setCurrentItem(item);
    }
    if (!m_screenList->currentItem() && m_screenList->count() > 0)
        m_screenList->setCurrentRow(0);
    m_loading = false;
    onScreenSelectionChanged();
}

void ScreensManagerDialog::onScreenSelectionChanged() {
    if (m_loading) return;
    const QString scrId = currentScreenId();
    const bool hasScreen = !scrId.isEmpty();
    m_removeScreenBtn->setEnabled(hasScreen && m_workspace->virtualScreensFor(currentOutputId()).size() > 1);
    m_renameScreenBtn->setEnabled(hasScreen);
    m_zonesCanvas->setSelectedScreen(scrId);
    refreshScreenFields();
}

void ScreensManagerDialog::onCanvasSelectionChanged(const QString &screenId) {
    // Click diretto su un poligono nel canvas: sincronizza la selezione
    // nella lista (che a sua volta aggiornerà i campi canvasRect).
    for (int i = 0; i < m_screenList->count(); ++i) {
        if (m_screenList->item(i)->data(Qt::UserRole).toString() == screenId) {
            m_screenList->setCurrentRow(i);
            return;
        }
    }
    if (screenId.isEmpty()) m_screenList->setCurrentItem(nullptr);
}

void ScreensManagerDialog::refreshScreenFields() {
    VirtualScreen *vs = m_workspace->virtualScreenById(currentScreenId());
    m_loading = true;
    const QRectF r = vs ? vs->canvasRect() : QRectF(0, 0, 1, 1);
    m_canvasXSpin->setValue(r.x());
    m_canvasYSpin->setValue(r.y());
    m_canvasWSpin->setValue(r.width());
    m_canvasHSpin->setValue(r.height());
    const int fitIdx = m_fitModeCombo->findData(int(vs ? vs->fitMode() : VirtualScreen::FitMode::Contain));
    m_fitModeCombo->setCurrentIndex(fitIdx >= 0 ? fitIdx : 0);
    const QPointF anchor = vs ? vs->contentAnchor() : QPointF(0.5, 0.5);
    m_anchorXSpin->setValue(anchor.x());
    m_anchorYSpin->setValue(anchor.y());
    const bool enabled = (vs != nullptr);
    m_canvasXSpin->setEnabled(enabled);
    m_canvasYSpin->setEnabled(enabled);
    m_canvasWSpin->setEnabled(enabled);
    m_canvasHSpin->setEnabled(enabled);
    m_fitModeCombo->setEnabled(enabled);
    m_anchorXSpin->setEnabled(enabled);
    m_anchorYSpin->setEnabled(enabled);
    m_addVertexBtn->setEnabled(enabled);
    m_removeVertexBtn->setEnabled(enabled);
    m_resetMaskBtn->setEnabled(enabled);
    m_loading = false;
}

void ScreensManagerDialog::onAddScreen() {
    const QString outId = currentOutputId();
    if (outId.isEmpty()) return;
    const QString id = m_workspace->addVirtualScreen(outId);
    refreshScreenList(id);
}

void ScreensManagerDialog::onRemoveScreen() {
    const QString id = currentScreenId();
    if (id.isEmpty()) return;
    m_workspace->removeVirtualScreen(id);
    refreshScreenList();
}

void ScreensManagerDialog::onRenameScreen() {
    VirtualScreen *vs = m_workspace->virtualScreenById(currentScreenId());
    if (!vs) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rinomina schermo"), tr("Nome:"),
                                                 QLineEdit::Normal, vs->name(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    vs->setName(name.trimmed());
    m_workspace->notifyScreensChanged();
    refreshScreenList(vs->id());
}

void ScreensManagerDialog::onCanvasRectChanged() {
    if (m_loading) return;
    VirtualScreen *vs = m_workspace->virtualScreenById(currentScreenId());
    if (!vs) return;
    vs->setCanvasRect(QRectF(m_canvasXSpin->value(), m_canvasYSpin->value(),
                               m_canvasWSpin->value(), m_canvasHSpin->value()));
    m_workspace->notifyScreensChanged();
}

void ScreensManagerDialog::onFitModeChanged(int idx) {
    if (m_loading) return;
    VirtualScreen *vs = m_workspace->virtualScreenById(currentScreenId());
    if (!vs) return;
    vs->setFitMode(VirtualScreen::FitMode(m_fitModeCombo->itemData(idx).toInt()));
    m_workspace->notifyScreensChanged();
}

void ScreensManagerDialog::onAnchorChanged() {
    if (m_loading) return;
    VirtualScreen *vs = m_workspace->virtualScreenById(currentScreenId());
    if (!vs) return;
    vs->setContentAnchor(QPointF(m_anchorXSpin->value(), m_anchorYSpin->value()));
    m_workspace->notifyScreensChanged();
}

void ScreensManagerDialog::onMaskEdited() {
    m_workspace->notifyScreensChanged();
}
