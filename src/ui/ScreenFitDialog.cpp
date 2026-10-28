#include "ScreenFitDialog.h"
#include "ScreenFitPreview.h"
#include "engine/Workspace.h"
#include "engine/VideoCue.h"
#include "engine/ImageCue.h"
#include <QVBoxLayout>
#include <QTimer>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QScrollArea>

// Immagine nativa rappresentativa della cue, per l'anteprima — stessa idea
// di VideoOutputWindow/ImageOutputWindow::composeCueFrame() ma semplificata
// (niente sfondo/transizione: qui serve solo a mostrare come il contenuto
// si posiziona nella zona, non a replicare ogni dettaglio della resa).
static QImage previewContentFor(Cue *cue) {
    if (!cue) return {};
    if (auto *vc = qobject_cast<VideoCue*>(cue)) {
        return vc->currentFrame(); // nullo se non in riproduzione: l'anteprima lo segnala
    }
    if (auto *ic = qobject_cast<ImageCue*>(cue)) {
        if (ic->contentMode() != ImageCue::ContentMode::File)
            return ic->renderGeneratedContent(QSize(1920, 1080));
        QString path = ic->currentImagePath();
        if (path.isEmpty() && ic->imageCount() > 0) path = ic->imagePaths().first();
        if (path.isEmpty()) return {};
        return QImage(path);
    }
    return {};
}

ScreenFitDialog::ScreenFitDialog(Workspace *workspace, Cue *cue, const QStringList &screenIds, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Adattamento negli schermi — OQL"));
    setModal(false);
    resize(520, 620);

    auto *mainLay = new QVBoxLayout(this);

    QStringList ids = screenIds;
    if (ids.isEmpty()) {
        mainLay->addWidget(new QLabel(
            "<small style='color:#888'>" +
            tr("Questa cue non è assegnata a schermi specifici: appare su tutti. "
               "L'adattamento scelto qui vale per ciascuno schermo elencato.") +
            "</small>"));
        for (const VirtualScreen &s : workspace->virtualScreens())
            ids.append(s.id());
    }

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget;
    auto *innerLay = new QVBoxLayout(inner);

    QList<ScreenFitPreview*> previews;
    for (const QString &id : ids) {
        VirtualScreen *vs = workspace->virtualScreenById(id);
        if (!vs) continue;
        PhysicalOutput *out = workspace->physicalOutputById(vs->physicalOutputId());
        const QString title = (out ? out->name() : tr("Output")) + " / " + vs->name();

        auto *group = new QGroupBox(title);
        auto *groupLay = new QVBoxLayout(group);
        auto *form  = new QFormLayout;

        auto *fitCombo = new QComboBox;
        fitCombo->addItem(tr("Adatta (mantiene le proporzioni, può lasciare bande)"), int(VirtualScreen::FitMode::Contain));
        fitCombo->addItem(tr("Riempi (mantiene le proporzioni, ritaglia l'eccesso)"), int(VirtualScreen::FitMode::Cover));
        fitCombo->addItem(tr("Stirato (riempie esattamente, può deformare)"), int(VirtualScreen::FitMode::Stretch));
        fitCombo->addItem(tr("Centrato (dimensione originale)"), int(VirtualScreen::FitMode::Center));
        fitCombo->addItem(tr("Manuale (zoom + posizione libera)"), int(VirtualScreen::FitMode::Manual));
        const int idx = fitCombo->findData(int(vs->fitMode()));
        fitCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        form->addRow(tr("Adattamento:"), fitCombo);

        // Rilevante solo per Manuale: con Adatta/Riempi il margine di
        // trascinamento dipende dalle proporzioni del contenuto rispetto
        // alla zona e può coincidere con zero (nessun bug, è la matematica
        // di quei modi) — con Manuale lo zoom garantisce sempre un margine
        // apprezzabile da trascinare, utile anche solo per ritagliare le
        // bande di un contenuto con proporzioni diverse dalla zona.
        auto *zoomSpin = new QDoubleSpinBox;
        zoomSpin->setRange(1.0, 8.0);
        zoomSpin->setSingleStep(0.1);
        zoomSpin->setDecimals(2);
        zoomSpin->setValue(vs->zoom());
        form->addRow(tr("Zoom (solo Manuale):"), zoomSpin);

        auto *anchorX = new QDoubleSpinBox; anchorX->setRange(0.0, 1.0); anchorX->setSingleStep(0.05); anchorX->setDecimals(2);
        auto *anchorY = new QDoubleSpinBox; anchorY->setRange(0.0, 1.0); anchorY->setSingleStep(0.05); anchorY->setDecimals(2);
        anchorX->setValue(vs->contentAnchor().x());
        anchorY->setValue(vs->contentAnchor().y());
        auto *anchorRow = new QWidget;
        auto *anchorRowLay = new QHBoxLayout(anchorRow);
        anchorRowLay->setContentsMargins(0, 0, 0, 0);
        anchorRowLay->addWidget(new QLabel(tr("X:"))); anchorRowLay->addWidget(anchorX);
        anchorRowLay->addWidget(new QLabel(tr("Y:"))); anchorRowLay->addWidget(anchorY);
        form->addRow(tr("Posizione (0=in alto/sx, 1=in basso/dx):"), anchorRow);
        groupLay->addLayout(form);

        auto *preview = new ScreenFitPreview;
        preview->setToolTip(tr("Trascina il contenuto per spostarlo — equivale a cambiare la posizione qui sopra."));
        groupLay->addWidget(preview);

        const QString screenId = vs->id();
        preview->setTarget(workspace, screenId, [cue] { return previewContentFor(cue); });

        connect(fitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [workspace, screenId, fitCombo, preview](int i) {
            if (VirtualScreen *s = workspace->virtualScreenById(screenId)) {
                s->setFitMode(VirtualScreen::FitMode(fitCombo->itemData(i).toInt()));
                workspace->notifyScreensChanged();
                preview->refresh();
            }
        });
        auto onAnchorSpinChanged = [workspace, screenId, anchorX, anchorY, preview](double) {
            if (VirtualScreen *s = workspace->virtualScreenById(screenId)) {
                s->setContentAnchor(QPointF(anchorX->value(), anchorY->value()));
                workspace->notifyScreensChanged();
                preview->refresh();
            }
        };
        connect(anchorX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, onAnchorSpinChanged);
        connect(anchorY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, onAnchorSpinChanged);

        connect(zoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [workspace, screenId, preview](double z) {
            if (VirtualScreen *s = workspace->virtualScreenById(screenId)) {
                s->setZoom(z);
                workspace->notifyScreensChanged();
                preview->refresh();
            }
        });

        // Il trascinamento nell'anteprima aggiorna l'anchor direttamente sul
        // VirtualScreen (vedi ScreenFitPreview) — qui si rilegge solo il
        // valore finale per tenere gli spinbox sincronizzati.
        connect(preview, &ScreenFitPreview::anchorEdited, this, [workspace, screenId, anchorX, anchorY] {
            VirtualScreen *s = workspace->virtualScreenById(screenId);
            if (!s) return;
            const QSignalBlocker bx(anchorX), by(anchorY);
            anchorX->setValue(s->contentAnchor().x());
            anchorY->setValue(s->contentAnchor().y());
            workspace->notifyScreensChanged();
        });

        previews.append(preview);
        innerLay->addWidget(group);
    }
    innerLay->addStretch();
    scroll->setWidget(inner);
    mainLay->addWidget(scroll, 1);

    // Il contenuto di un video in riproduzione cambia a ogni frame — senza
    // un ridisegno periodico l'anteprima resterebbe ferma al frame di quando
    // il dialogo è stato aperto.
    auto *previewTimer = new QTimer(this);
    previewTimer->setInterval(100);
    connect(previewTimer, &QTimer::timeout, this, [previews] {
        for (ScreenFitPreview *pv : previews) pv->refresh();
    });
    previewTimer->start();

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::close);
    mainLay->addWidget(btns);
}
