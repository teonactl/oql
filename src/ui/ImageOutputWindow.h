#pragma once
#include <QWidget>
#include <QHash>
#include <QPixmap>
#include <QMetaObject>
#include <QStringList>

class ImageCue;
class QLabel;
class QGraphicsOpacityEffect;

class ImageOutputWindow : public QWidget {
    Q_OBJECT
public:
    explicit ImageOutputWindow(QWidget *parent = nullptr);

    void showCue(ImageCue *cue);
    void clearImage();
    void toggleFullScreen();

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)            override;
    void resizeEvent(QResizeEvent *event)           override;

private slots:
    void refresh();

private:
    QPixmap pixmapFor(const QString &path);
    void    setLabelPixmap(QLabel *label, const QString &path, QString &lastKey);
    void    updateBackground();

    QLabel  *m_backgroundLabel;
    QWidget *m_container;
    QLabel  *m_bottomLabel;
    QLabel  *m_topLabel;
    QGraphicsOpacityEffect *m_topOpacity;

    // Fade-to-black via overlay nero sopra il container (non nidificato
    // sotto un altro QGraphicsEffect — Qt non compone bene effetti annidati).
    QWidget                *m_blackOverlay;
    QGraphicsOpacityEffect *m_overlayOpacity;

    ImageCue *m_cue = nullptr;
    QMetaObject::Connection m_connection;
    // E11: niente limite di dimensione — ogni path mai mostrato restava in
    // cache per sempre (una QPixmap intera a piena risoluzione per voce).
    // Cache bounded in stile LRU: m_pixmapOrder tiene l'ordine di accesso,
    // le voci più vecchie vengono espulse oltre kMaxCachedPixmaps.
    static constexpr int kMaxCachedPixmaps = 48;
    QHash<QString, QPixmap> m_pixmapCache;
    QStringList m_pixmapOrder;
    QString m_lastBottomKey;
    QString m_lastTopKey;
    QString m_lastBackgroundKey;
};
