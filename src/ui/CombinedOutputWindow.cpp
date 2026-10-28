#include "CombinedOutputWindow.h"
#include "VideoOutputWindow.h"
#include "ImageOutputWindow.h"
#include "TextOutputWindow.h"
#include <QResizeEvent>
#include <QKeyEvent>
#include <QMouseEvent>

CombinedOutputWindow::CombinedOutputWindow(QWidget *parent)
    // Senza bordi/barra del titolo: questa finestra deve comportarsi come
    // uno schermo reale/virtuale (posizionata e dimensionata esattamente
    // sul monitor/proiettore assegnato), non come una finestra applicativa
    // normale da spostare o ridimensionare a mano.
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
{
    setWindowTitle("Output — OQL");
    setStyleSheet("background: black;");
    resize(1280, 720);

    // Ordine di creazione = ordine di stacking (i widget figli creati dopo
    // disegnano sopra quelli creati prima): video in basso, immagine sopra,
    // testo in cima.
    m_video = new VideoOutputWindow(this, true);
    m_image = new ImageOutputWindow(this, true);
    m_text  = new TextOutputWindow(this, true);
    m_video->setGeometry(rect());
    m_image->setGeometry(rect());
    m_text->setGeometry(rect());

    // I tre layer restano sempre "visibili" come figli (ereditano la
    // visibilità dal contenitore, che è l'unica cosa che MainWindow mostra/
    // nasconde — vedi MainWindow::toggleAllOutputs): se qui li si nascondesse
    // esplicitamente, mostrare poi solo il contenitore non li farebbe
    // comparire comunque (un figlio esplicitamente nascosto resta nascosto
    // anche se il genitore diventa visibile), lasciando la finestra
    // perennemente nera anche con una cue video/immagine in riproduzione.
}

void CombinedOutputWindow::setOutput(Workspace *workspace, const QString &physicalOutputId) {
    m_video->setOutput(workspace, physicalOutputId);
    m_image->setOutput(workspace, physicalOutputId);
}

void CombinedOutputWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    m_video->setGeometry(rect());
    m_image->setGeometry(rect());
    m_text->setGeometry(rect());
}

void CombinedOutputWindow::toggleFullScreen() {
    if (isFullScreen()) showNormal();
    else                showFullScreen();
}

void CombinedOutputWindow::mouseDoubleClickEvent(QMouseEvent *) {
    toggleFullScreen();
}

void CombinedOutputWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || isFullScreen()) return;
    m_draggingWindow     = true;
    m_dragStartGlobalPos = event->globalPosition().toPoint();
    m_dragStartWindowPos = pos();
}

void CombinedOutputWindow::mouseMoveEvent(QMouseEvent *event) {
    if (!m_draggingWindow) return;
    const QPoint delta = event->globalPosition().toPoint() - m_dragStartGlobalPos;
    move(m_dragStartWindowPos + delta);
}

void CombinedOutputWindow::mouseReleaseEvent(QMouseEvent *) {
    m_draggingWindow = false;
}

void CombinedOutputWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullScreen())
        showNormal();
    else
        QWidget::keyPressEvent(event);
}
