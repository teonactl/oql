#include "ImageOutputWindow.h"
#include "engine/ImageCue.h"
#include <QLabel>
#include <QGraphicsOpacityEffect>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>

ImageOutputWindow::ImageOutputWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Image Output — OQL");
    setStyleSheet("background: black;");
    resize(1280, 720);

    m_backgroundLabel = new QLabel(this);
    m_backgroundLabel->setAlignment(Qt::AlignCenter);
    m_backgroundLabel->setStyleSheet("background: black;");
    m_backgroundLabel->setGeometry(rect());

    m_container = new QWidget(this);
    m_container->setStyleSheet("background: transparent;");

    m_bottomLabel = new QLabel(m_container);
    m_bottomLabel->setAlignment(Qt::AlignCenter);
    m_bottomLabel->setStyleSheet("background: transparent;");

    m_topLabel = new QLabel(m_container);
    m_topLabel->setAlignment(Qt::AlignCenter);
    m_topLabel->setStyleSheet("background: transparent;");
    m_topOpacity = new QGraphicsOpacityEffect(m_topLabel);
    m_topLabel->setGraphicsEffect(m_topOpacity);
    m_topLabel->hide();

    m_blackOverlay = new QWidget(this);
    m_blackOverlay->setStyleSheet("background: black;");
    m_blackOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_overlayOpacity = new QGraphicsOpacityEffect(m_blackOverlay);
    m_overlayOpacity->setOpacity(0.0);
    m_blackOverlay->setGraphicsEffect(m_overlayOpacity);
    m_blackOverlay->setGeometry(rect());
    m_blackOverlay->raise();
}

QPixmap ImageOutputWindow::pixmapFor(const QString &path) {
    auto it = m_pixmapCache.find(path);
    if (it != m_pixmapCache.end()) return it.value();
    QPixmap px(path);
    m_pixmapCache.insert(path, px);
    return px;
}

void ImageOutputWindow::setLabelPixmap(QLabel *label, const QString &path, QString &lastKey) {
    const QSize size = label->size();
    const QString key = path + '@' + QString::number(size.width()) + 'x' + QString::number(size.height());
    if (key == lastKey) return;
    lastKey = key;
    if (path.isEmpty()) { label->clear(); return; }
    const QPixmap src = pixmapFor(path);
    if (src.isNull()) { label->clear(); return; }
    label->setPixmap(src.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ImageOutputWindow::showCue(ImageCue *cue) {
    if (m_connection) disconnect(m_connection);
    m_cue = cue;
    if (!cue) { clearImage(); return; }
    m_connection = connect(cue, &Cue::displayChanged, this, &ImageOutputWindow::refresh);
    refresh();
    show();
    raise();
}

void ImageOutputWindow::clearImage() {
    if (m_connection) disconnect(m_connection);
    m_cue = nullptr;
    m_bottomLabel->clear();
    m_topLabel->clear();
    m_topLabel->hide();
    m_lastBottomKey.clear();
    m_lastTopKey.clear();
    m_lastBackgroundKey.clear();
    m_backgroundLabel->clear();
    m_backgroundLabel->setStyleSheet("background: black;");
    m_overlayOpacity->setOpacity(0.0);
}

void ImageOutputWindow::updateBackground() {
    if (!m_cue) return;

    if (m_cue->background() == ImageCue::Background::Image) {
        const QString path = m_cue->backgroundImagePath();
        const QString key = path + '@' + QString::number(width()) + 'x' + QString::number(height());
        if (key != m_lastBackgroundKey) {
            m_lastBackgroundKey = key;
            const QPixmap src = path.isEmpty() ? QPixmap() : pixmapFor(path);
            if (src.isNull()) {
                m_backgroundLabel->clear();
                m_backgroundLabel->setStyleSheet("background: black;");
            } else {
                m_backgroundLabel->setStyleSheet("background: transparent;");
                m_backgroundLabel->setPixmap(src.scaled(
                    size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            }
        }
    } else {
        m_lastBackgroundKey.clear();
        m_backgroundLabel->clear();
        m_backgroundLabel->setStyleSheet(
            m_cue->background() == ImageCue::Background::White ? "background: white;"
                                                                  : "background: black;");
    }
}

void ImageOutputWindow::refresh() {
    if (!m_cue) return;

    updateBackground();
    setLabelPixmap(m_bottomLabel, m_cue->currentImagePath(), m_lastBottomKey);
    m_overlayOpacity->setOpacity(1.0 - m_cue->visualLevel());

    const bool transitioning = m_cue->isTransitioning()
                             && m_cue->transitionType() != ImageCue::Transition::Cut;
    if (transitioning) {
        setLabelPixmap(m_topLabel, m_cue->nextImagePath(), m_lastTopKey);
        m_topLabel->show();
        const double progress = m_cue->transitionProgress();
        if (m_cue->transitionType() == ImageCue::Transition::Crossfade) {
            m_topLabel->move(0, 0);
            m_topOpacity->setOpacity(progress);
        } else { // SlideHorizontal
            m_topOpacity->setOpacity(1.0);
            const int x = int(width() * (1.0 - progress));
            m_topLabel->move(x, 0);
        }
    } else {
        m_topLabel->hide();
    }
}

void ImageOutputWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    m_backgroundLabel->setGeometry(rect());
    m_container->setGeometry(rect());
    m_bottomLabel->setGeometry(0, 0, width(), height());
    m_topLabel->resize(width(), height());
    m_blackOverlay->setGeometry(rect());
    m_lastBottomKey.clear();
    m_lastTopKey.clear();
    m_lastBackgroundKey.clear();
    if (m_cue) refresh();
}

void ImageOutputWindow::toggleFullScreen() {
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void ImageOutputWindow::mouseDoubleClickEvent(QMouseEvent *) {
    toggleFullScreen();
}

void ImageOutputWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullScreen())
        showNormal();
    else
        QWidget::keyPressEvent(event);
}
