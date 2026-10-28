#include "ProjectionCalibrationWindow.h"
#include "WarpCalibrationOverlay.h"
#include "ProjectionCalibrationToolbar.h"
#include <QPainter>
#include <QPushButton>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QMoveEvent>

ProjectionCalibrationWindow::ProjectionCalibrationWindow(QWidget *parent)
    // WindowStaysOnTopHint: altrimenti avviare una cue video/immagine
    // mentre la calibrazione è aperta la fa finire dietro la finestra di
    // output appena portata in primo piano (ActiveCuesPanel ecc. restano
    // comunque usabili, è solo rispetto alle finestre di output che deve
    // restare sopra) — stessa scelta già fatta per la sua toolbar.
    : QWidget(parent, Qt::Window | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(tr("Calibrazione proiezione — OQL"));
    resize(1280, 720);

    m_gridOverlay = new WarpCalibrationOverlay(this);
    m_gridOverlay->setGeometry(rect());
    connect(m_gridOverlay, &WarpCalibrationOverlay::warpEdited,  this, [this] { update(); });
    connect(m_gridOverlay, &WarpCalibrationOverlay::warpCommitted, this, [this] {
        update();
        emit calibrationCommitted();
    });

    // Finestra flottante separata per i pulsanti: niente parent nel senso
    // di widget-contenitore (altrimenti Qt::Tool la clippa dentro il
    // rettangolo del genitore), ma la passiamo comunque per farla
    // distruggere insieme a questa finestra.
    m_toolbar = new ProjectionCalibrationToolbar(this);
    connect(m_toolbar->addColumnButton(),    &QPushButton::clicked, m_gridOverlay, &WarpCalibrationOverlay::addColumn);
    connect(m_toolbar->removeColumnButton(), &QPushButton::clicked, m_gridOverlay, &WarpCalibrationOverlay::removeColumn);
    connect(m_toolbar->addRowButton(),       &QPushButton::clicked, m_gridOverlay, &WarpCalibrationOverlay::addRow);
    connect(m_toolbar->removeRowButton(),    &QPushButton::clicked, m_gridOverlay, &WarpCalibrationOverlay::removeRow);
    connect(m_toolbar->resetButton(),        &QPushButton::clicked, m_gridOverlay, &WarpCalibrationOverlay::resetWarp);
    connect(m_toolbar->loadImageButton(),    &QPushButton::clicked, this, &ProjectionCalibrationWindow::chooseCustomImage);
    connect(m_toolbar->useBuiltinButton(),   &QPushButton::clicked, this, &ProjectionCalibrationWindow::useBuiltinPattern);
    connect(m_toolbar->closeButton(),        &QPushButton::clicked, this, [this] { setCalibrationVisible(false); });
    // La griglia cambia anche premendo i pulsanti (non solo trascinando):
    // ridisegna il contenuto warpato di conseguenza.
    connect(m_gridOverlay, &WarpCalibrationOverlay::warpCommitted, this, [this] { update(); });
}

void ProjectionCalibrationWindow::setProjectionWarp(ProjectionWarp *warp) {
    m_warp = warp;
    m_gridOverlay->setWarp(warp);
    update();
}

void ProjectionCalibrationWindow::setCalibrationVisible(bool visible) {
    setVisible(visible);
    if (visible) {
        raise();
        positionToolbar();
        m_toolbar->show();
        m_toolbar->raise();
    } else {
        m_toolbar->hide();
    }
}

void ProjectionCalibrationWindow::positionToolbar() {
    // Appena sopra la finestra di calibrazione, allineata a sinistra: fuori
    // dall'area del contenuto, così non copre mai nessun handle. Se non
    // c'è spazio sopra (finestra vicina al bordo superiore dello schermo),
    // la mette appena sotto invece.
    const QPoint topLeft = mapToGlobal(QPoint(0, 0));
    m_toolbar->adjustSize();
    int y = topLeft.y() - m_toolbar->height() - 4;
    if (y < 0) y = topLeft.y() + height() + 4;
    m_toolbar->move(topLeft.x(), y);
}

void ProjectionCalibrationWindow::chooseCustomImage() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Scegli immagine di riferimento per la calibrazione"),
        QString(), tr("Immagini (*.png *.jpg *.jpeg *.bmp *.gif)"));
    if (path.isEmpty()) return;
    const QImage img(path);
    if (img.isNull()) return;
    m_customImage = img;
    m_toolbar->useBuiltinButton()->show();
    m_toolbar->adjustSize();
    update();
}

void ProjectionCalibrationWindow::useBuiltinPattern() {
    m_customImage = QImage();
    m_toolbar->useBuiltinButton()->hide();
    m_toolbar->adjustSize();
    update();
}

QImage ProjectionCalibrationWindow::currentSourceImage() const {
    const QSize sz = size();
    if (m_customImage.isNull())
        return projectionTestPattern(sz);

    // Immagine utente scalata "expanding" (riempie la finestra, eventuale
    // ritaglio) e composta su sfondo nero — stesso trattamento di uno sfondo
    // immagine nelle finestre di output, per coerenza visiva.
    QImage canvas(sz, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);
    if (sz.isEmpty()) return canvas;
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QImage scaled = m_customImage.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint topLeft((sz.width() - scaled.width()) / 2, (sz.height() - scaled.height()) / 2);
    p.drawImage(topLeft, scaled);
    return canvas;
}

void ProjectionCalibrationWindow::paintEvent(QPaintEvent *) {
    const QImage source = currentSourceImage();
    QPainter painter(this);
    if (m_warp)
        painter.drawImage(0, 0, m_warp->renderWarped(source, size()));
    else
        painter.drawImage(0, 0, source);
}

void ProjectionCalibrationWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    m_gridOverlay->setGeometry(rect());
    if (isVisible()) positionToolbar();
}

void ProjectionCalibrationWindow::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    if (isVisible()) positionToolbar();
}

void ProjectionCalibrationWindow::toggleFullScreen() {
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void ProjectionCalibrationWindow::mouseDoubleClickEvent(QMouseEvent *) {
    toggleFullScreen();
}

void ProjectionCalibrationWindow::keyPressEvent(QKeyEvent *event) {
    // Chiude del tutto (non solo esce dal fullscreen): aperta a schermo
    // intero come di default non c'è nessuna barra del titolo da cui
    // chiuderla altrimenti — un'uscita parziale lascerebbe comunque una
    // finestra "in giro" invece di liberare lo schermo.
    if (event->key() == Qt::Key_Escape)
        setCalibrationVisible(false);
    else
        QWidget::keyPressEvent(event);
}
