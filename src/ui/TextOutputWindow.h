#pragma once
#include <QWidget>
#include <QMetaObject>

class TextCue;
class QLabel;
class QGraphicsOpacityEffect;

class TextOutputWindow : public QWidget {
    Q_OBJECT
public:
    // embedded=true: niente Qt::Window/titolo/resize iniziale e sfondo
    // trasparente finché nessuna cue è attiva (invece di nero) — usata
    // quando questa finestra vive come layer dentro CombinedOutputWindow
    // invece che come finestra separata (vedi quel file).
    explicit TextOutputWindow(QWidget *parent = nullptr, bool embedded = false);
    void showCue(const TextCue *cue);
    void clearText();
    void toggleFullScreen();

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event)        override;
    void mouseMoveEvent(QMouseEvent *event)         override;
    void mouseReleaseEvent(QMouseEvent *event)      override;
    void keyPressEvent(QKeyEvent *event)            override;

private:
    QLabel *m_label;
    QGraphicsOpacityEffect *m_opacity;
    QMetaObject::Connection m_connection;

    // Trascinamento manuale della finestra — vedi lo stesso commento in
    // VideoOutputWindow.h. Essendo il layer più in alto nella modalità
    // combinata (vedi CombinedOutputWindow), è quello che riceve i click
    // nella pratica più spesso.
    bool   m_draggingWindow = false;
    QPoint m_dragStartGlobalPos;
    QPoint m_dragStartWindowPos;
};
