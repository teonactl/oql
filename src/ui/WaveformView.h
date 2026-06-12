#pragma once
#include <QWidget>
#include <QVector>
#include <QPointF>

class QThread;

class WaveformView : public QWidget {
    Q_OBJECT
public:
    explicit WaveformView(QWidget *parent = nullptr);
    ~WaveformView() override;

    void setFilePath(const QString &path);
    void setDuration(double secs);
    void setFadeIn(double secs);
    void setFadeOut(double secs);
    void setVolumePoints(const QVector<QPointF> &pts);
    void setPlayPosition(double secs);
    void setTrimStart(double secs);   // 0 or negative = remove
    void setTrimEnd  (double secs);   // 0 or negative = remove

    QVector<QPointF> volumePoints() const { return m_points; }
    // Returns seconds, or 0 if not set
    double trimStart() const { return (m_trimStart >= 0.0 && m_duration > 0) ? m_trimStart * m_duration : 0.0; }
    double trimEnd()   const { return (m_trimEnd   >= 0.0 && m_duration > 0) ? m_trimEnd   * m_duration : 0.0; }
    bool hasTrimStart() const { return m_trimStart >= 0.0; }
    bool hasTrimEnd()   const { return m_trimEnd   >= 0.0; }

    void setSliceMarkers(const QVector<double> &normPositions); // normalized [0,1], sorted
    void setInteractive(bool interactive);

signals:
    void volumePointsChanged(const QVector<QPointF> &pts);
    void fadeInChanged(double secs);
    void fadeOutChanged(double secs);
    void trimStartChanged(double secs);
    void trimEndChanged(double secs);
    void sliceMarkersChanged(const QVector<double> &normPositions);

protected:
    void paintEvent(QPaintEvent *event)          override;
    void mousePressEvent(QMouseEvent *event)     override;
    void mouseMoveEvent(QMouseEvent *event)      override;
    void mouseReleaseEvent(QMouseEvent *event)   override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void wheelEvent(QWheelEvent *event)          override;

private:
    void startDecode(const QString &path);

    // Coordinate helpers — all account for zoom/pan
    double xToNorm(double px) const;
    double normToX(double t)  const;
    double yToVol(double py)  const;
    double volToY(double v)   const;

    int  hitPoint    (const QPointF &p) const;
    bool hitFadeIn   (const QPointF &p) const;
    bool hitFadeOut  (const QPointF &p) const;
    bool hitTrimStart(const QPointF &p) const;
    bool hitTrimEnd  (const QPointF &p) const;
    int  hitSlice    (const QPointF &p) const; // returns index or -1

    void clampView();

    QString          m_filePath;
    double           m_duration   = 0.0;
    double           m_fadeIn     = 3.0;
    double           m_fadeOut    = 3.0;
    double           m_playPos    = 0.0;
    double           m_trimStart  = -1.0; // normalised [0,1]; -1 = not set
    double           m_trimEnd    = -1.0; // normalised [0,1]; -1 = not set
    QVector<float>   m_wfMin;
    QVector<float>   m_wfMax;
    QVector<QPointF> m_points;  // x=normalised time [0,1], y=volume [0,1]

    // Zoom / pan
    double m_zoom       = 1.0;   // 1x – 20x
    double m_viewOffset = 0.0;   // normalised left edge of visible range

    // Slice markers (normalized [0,1], sorted)
    QVector<double> m_sliceMarkers;

    // Drag state
    int  m_dragIdx       = -1;
    int  m_dragSliceIdx  = -1;
    bool m_dragFadeIn    = false;
    bool m_dragFadeOut   = false;
    bool m_dragTrimStart = false;
    bool m_dragTrimEnd   = false;

    bool m_interactive      = true;

    // Pan state (middle button)
    bool   m_panning        = false;
    double m_panStartX      = 0.0;
    double m_panStartOffset = 0.0;

    QThread *m_decodeThread     = nullptr;
    int      m_decodeGeneration = 0;
};
