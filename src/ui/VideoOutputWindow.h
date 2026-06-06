#pragma once
#include <QWidget>
#include <QVideoWidget>

class VideoOutputWindow : public QWidget {
    Q_OBJECT
public:
    explicit VideoOutputWindow(QWidget *parent = nullptr);

    QVideoWidget* videoWidget() { return m_videoWidget; }

    void toggleFullScreen();

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)            override;

private:
    QVideoWidget *m_videoWidget;
};
