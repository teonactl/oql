#pragma once
#include <QWidget>
#include <QMetaObject>

class TextCue;
class QLabel;
class QGraphicsOpacityEffect;

class TextOutputWindow : public QWidget {
    Q_OBJECT
public:
    explicit TextOutputWindow(QWidget *parent = nullptr);
    void showCue(const TextCue *cue);
    void clearText();
    void toggleFullScreen();

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)            override;

private:
    QLabel *m_label;
    QGraphicsOpacityEffect *m_opacity;
    QMetaObject::Connection m_connection;
};
