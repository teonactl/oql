#pragma once
#include <QWidget>
#include <QVideoWidget>
#include <QHash>
#include <QPixmap>

class QGraphicsOpacityEffect;
class QLabel;

class VideoOutputWindow : public QWidget {
    Q_OBJECT
public:
    enum class Background { Black, White, Image };

    explicit VideoOutputWindow(QWidget *parent = nullptr);

    QVideoWidget* videoWidget() { return m_videoWidget; }

    void setOpacity(double level);
    void setBackground(Background mode, const QString &imagePath);
    void toggleFullScreen();

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)            override;
    void resizeEvent(QResizeEvent *event)           override;

private:
    void refreshOverlayBackground();

    QVideoWidget *m_videoWidget;
    // QGraphicsOpacityEffect non funziona in modo affidabile se applicato
    // direttamente a un QVideoWidget (rendering via path che bypassa il
    // compositing normale dei widget): il fade usa invece un overlay
    // sovrapposto (nero/bianco/immagine a scelta), la cui opacità è
    // pilotata dal fade — stesso identico schema di ImageOutputWindow.
    QLabel                  *m_overlay;
    QGraphicsOpacityEffect  *m_overlayOpacity;
    Background               m_bgMode = Background::Black;
    QString                  m_bgImagePath;
    QHash<QString, QPixmap>  m_pixmapCache;
};
