#pragma once
#include <QWidget>
#include <QString>

class VideoOutputWindow;
class ImageOutputWindow;
class TextOutputWindow;
class Workspace;

// Un'unica finestra top-level che ospita video, immagine e testo come layer
// sovrapposti (figli non top-level, vedi il parametro `embedded` nei
// rispettivi costruttori), invece di tre finestre separate — opzione
// "Impostazioni → Hardware", attiva di default. Video e immagine restano
// due QImage composte indipendentemente (ciascuna già gestisce il proprio
// per-schermo-virtuale + warp, vedi VideoOutputWindow/ImageOutputWindow) ma
// finiscono nella stessa finestra, una sopra l'altra per stacking naturale
// dei widget figli; il testo (mai deformato dal warp, come già accadeva a
// finestre separate) resta il layer più in alto.
class CombinedOutputWindow : public QWidget {
    Q_OBJECT
public:
    explicit CombinedOutputWindow(QWidget *parent = nullptr);

    void setOutput(Workspace *workspace, const QString &physicalOutputId);

    VideoOutputWindow *video() const { return m_video; }
    ImageOutputWindow *image() const { return m_image; }
    TextOutputWindow  *text()  const { return m_text; }

    void toggleFullScreen();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event)        override;
    void mouseMoveEvent(QMouseEvent *event)         override;
    void mouseReleaseEvent(QMouseEvent *event)      override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    VideoOutputWindow *m_video;
    ImageOutputWindow *m_image;
    TextOutputWindow  *m_text;

    // Trascinamento manuale (senza bordi/decorazioni) — ridondante con
    // quello nei layer figli (che intercettano la maggior parte dei click)
    // ma utile per qualunque pixel non coperto da nessuno dei tre.
    bool   m_draggingWindow = false;
    QPoint m_dragStartGlobalPos;
    QPoint m_dragStartWindowPos;
};
