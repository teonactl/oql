#include "WaveformView.h"
#include "engine/miniaudio.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QThread>
#include <QLinearGradient>
#include <QPolygon>
#include <algorithm>

static constexpr double kMarginY = 8.0;  // px top/bottom for volume line
static constexpr double kScrollH = 5.0;  // px height of zoom scroll indicator

WaveformView::WaveformView(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setStyleSheet("WaveformView { background:#0c0c10; border-radius:3px; }");
}

WaveformView::~WaveformView() {
    if (m_decodeThread) {
        ++m_decodeGeneration;  // invalidate any pending queued lambda
        m_decodeThread->requestInterruption();
        m_decodeThread->wait();
        delete m_decodeThread;
    }
}

// ── Public setters ────────────────────────────────────────────────────────────

void WaveformView::setFilePath(const QString &path) {
    if (m_filePath == path) return;
    m_filePath   = path;
    m_waveform.clear();
    m_zoom       = 1.0;
    m_viewOffset = 0.0;
    m_trimStart  = -1.0;
    m_trimEnd    = -1.0;
    update();
    startDecode(path);
}

void WaveformView::setDuration(double secs) { m_duration = secs; update(); }
void WaveformView::setFadeIn  (double secs) { m_fadeIn   = secs; update(); }
void WaveformView::setFadeOut (double secs) { m_fadeOut  = secs; update(); }

void WaveformView::setVolumePoints(const QVector<QPointF> &pts) {
    m_points = pts;
    update();
}

void WaveformView::setPlayPosition(double secs) {
    m_playPos = secs;
    // Auto-scroll: keep playhead in view when zoomed
    if (m_zoom > 1.01 && m_duration > 0.01) {
        const double t = secs / m_duration;
        const double viewEnd = m_viewOffset + 1.0 / m_zoom;
        if (t < m_viewOffset || t > viewEnd) {
            m_viewOffset = qBound(0.0, t - 0.1 / m_zoom, 1.0 - 1.0 / m_zoom);
        }
    }
    update();
}

void WaveformView::setTrimStart(double secs) {
    if (secs <= 0.001) {
        m_trimStart = -1.0;
    } else {
        const double maxT = (m_trimEnd >= 0.0) ? m_trimEnd - 0.001 : 0.999;
        m_trimStart = m_duration > 0.01 ? qBound(0.0, secs / m_duration, maxT) : -1.0;
    }
    update();
}

void WaveformView::setTrimEnd(double secs) {
    if (secs <= 0.001) {
        m_trimEnd = -1.0;
    } else {
        const double minT = (m_trimStart >= 0.0) ? m_trimStart + 0.001 : 0.001;
        m_trimEnd = m_duration > 0.01 ? qBound(minT, secs / m_duration, 1.0) : -1.0;
    }
    update();
}

// ── Decode ────────────────────────────────────────────────────────────────────

void WaveformView::startDecode(const QString &path) {
    if (m_decodeThread) {
        ++m_decodeGeneration;
        m_decodeThread->requestInterruption();
        m_decodeThread->wait();
        delete m_decodeThread;
        m_decodeThread = nullptr;
    }
    m_waveform.clear();
    update();

    if (path.isEmpty()) return;

    const int gen = ++m_decodeGeneration;
    const QByteArray pathUtf8 = path.toUtf8();

    m_decodeThread = QThread::create([this, gen, pathUtf8]() {
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0);
        ma_decoder dec;
        if (ma_decoder_init_file(pathUtf8.constData(), &cfg, &dec) != MA_SUCCESS)
            return;

        constexpr int kBufFrames = 8192;
        float buf[kBufFrames];
        QVector<float> raw;

        while (!QThread::currentThread()->isInterruptionRequested()) {
            ma_uint64 framesRead = 0;
            ma_decoder_read_pcm_frames(&dec, buf, kBufFrames, &framesRead);
            for (ma_uint64 i = 0; i < framesRead; ++i)
                raw.append(qAbs(buf[i]));
            if (framesRead < static_cast<ma_uint64>(kBufFrames)) break;
        }
        ma_decoder_uninit(&dec);

        if (QThread::currentThread()->isInterruptionRequested()) return;

        const int N = raw.size();
        QVector<float> waveform;
        if (N > 0) {
            constexpr int kBuckets = 2000;
            waveform.resize(kBuckets);
            for (int i = 0; i < kBuckets; ++i) {
                const int s = int(qint64(i)   * N / kBuckets);
                const int e = int(qint64(i+1) * N / kBuckets);
                float peak = 0.0f;
                for (int j = s; j < e && j < N; ++j)
                    peak = qMax(peak, raw[j]);
                waveform[i] = peak;
            }
        }

        QMetaObject::invokeMethod(this, [this, gen, wv = std::move(waveform)]() mutable {
            if (gen != m_decodeGeneration) return;
            m_waveform    = std::move(wv);
            m_decodeThread = nullptr;
            update();
        }, Qt::QueuedConnection);
    });

    m_decodeThread->start();
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void WaveformView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int W = width();
    const int H = height();
    // Reserve bottom strip for the zoom scroll indicator
    const int waveH = m_zoom > 1.01 ? H - int(kScrollH) - 1 : H;
    const int midY  = waveH / 2;

    // Background
    p.fillRect(rect(), QColor(0x0c, 0x0c, 0x10));

    // ── Trim region (greyed-out zones outside active range) ───────────────────
    if (m_duration > 0.01) {
        if (m_trimStart >= 0.0) {
            const int xTS = int(normToX(m_trimStart));
            if (xTS > 0)
                p.fillRect(0, 0, qMin(xTS, W), waveH, QColor(0, 0, 0, 130));
        }
        if (m_trimEnd >= 0.0) {
            const int xTE = int(normToX(m_trimEnd));
            if (xTE < W)
                p.fillRect(qMax(0, xTE), 0, W - qMax(0, xTE), waveH, QColor(0, 0, 0, 130));
        }
    }

    // ── Waveform ──────────────────────────────────────────────────────────────
    if (!m_waveform.isEmpty()) {
        const int N = m_waveform.size();
        for (int x = 0; x < W; ++x) {
            const double t = xToNorm(x);
            if (t < 0.0 || t > 1.0) continue;
            const int idx = qBound(0, int(t * N), N - 1);
            const int h   = qMax(1, int(m_waveform[idx] * (midY - 2)));
            p.fillRect(x, midY - h, 1, 2 * h, QColor(0x1e, 0x55, 0x38));
        }
    }

    // ── Fade overlays ─────────────────────────────────────────────────────────
    if (m_duration > 0.01) {
        if (m_fadeIn > 0.01) {
            const double tFI = qMin(m_fadeIn / m_duration, 1.0);
            const int xFI = int(normToX(tFI));
            if (xFI > 0 && xFI < W) {
                QLinearGradient g(0, 0, xFI, 0);
                g.setColorAt(0.0, QColor(0, 0, 0, 160));
                g.setColorAt(1.0, QColor(0, 0, 0, 0));
                p.fillRect(0, 0, xFI, waveH, g);
                p.setPen(QPen(QColor(200, 200, 200, 100), 1, Qt::DashLine));
                p.drawLine(xFI, 0, xFI, waveH);
                QPolygon tri;
                tri << QPoint(xFI - 1, midY - 7) << QPoint(xFI + 7, midY) << QPoint(xFI - 1, midY + 7);
                p.setBrush(QColor(220, 220, 220, 180));
                p.setPen(Qt::NoPen);
                p.drawPolygon(tri);
            }
        }
        if (m_fadeOut > 0.01) {
            const double tFO = qMax(0.0, 1.0 - m_fadeOut / m_duration);
            const int xFO = int(normToX(tFO));
            if (xFO > 0 && xFO < W) {
                QLinearGradient g(xFO, 0, W, 0);
                g.setColorAt(0.0, QColor(0, 0, 0, 0));
                g.setColorAt(1.0, QColor(0, 0, 0, 160));
                p.fillRect(xFO, 0, W - xFO, waveH, g);
                p.setPen(QPen(QColor(200, 200, 200, 100), 1, Qt::DashLine));
                p.drawLine(xFO, 0, xFO, waveH);
                QPolygon tri;
                tri << QPoint(xFO + 1, midY - 7) << QPoint(xFO - 7, midY) << QPoint(xFO + 1, midY + 7);
                p.setBrush(QColor(220, 220, 220, 180));
                p.setPen(Qt::NoPen);
                p.drawPolygon(tri);
            }
        }
    }

    // ── Volume line (automation + fade envelope combined) ─────────────────────
    {
        // Returns the fade envelope multiplier at normalised position t
        auto fadeEnv = [&](double t) -> double {
            if (m_duration <= 0.01) return 1.0;
            double v = 1.0;
            if (m_fadeIn  > 0.01) v *= qMin(1.0, t       * m_duration / m_fadeIn);
            if (m_fadeOut > 0.01) v *= qMin(1.0, (1.0-t) * m_duration / m_fadeOut);
            return v;
        };
        // Returns the user-automation multiplier at normalised t (linear interpolation)
        auto userVol = [&](double t) -> double {
            if (m_points.isEmpty()) return 1.0;
            if (t <= m_points.first().x()) return m_points.first().y();
            if (t >= m_points.last().x())  return m_points.last().y();
            for (int i = 0; i + 1 < m_points.size(); ++i) {
                if (t >= m_points[i].x() && t <= m_points[i+1].x()) {
                    const double a = (t - m_points[i].x()) / (m_points[i+1].x() - m_points[i].x());
                    return m_points[i].y()*(1-a) + m_points[i+1].y()*a;
                }
            }
            return 1.0;
        };

        const bool hasFade = (m_duration > 0.01 && (m_fadeIn > 0.01 || m_fadeOut > 0.01));

        // Build line sampling the combined curve at every pixel column
        QVector<QPointF> line;
        line.reserve(W + 2);
        if (hasFade || !m_points.isEmpty()) {
            for (int x = 0; x <= W; x += qMax(1, W / 400)) {
                const double t = xToNorm(x);
                if (t < 0.0 || t > 1.0) continue;
                line << QPointF(double(x), volToY(userVol(t) * fadeEnv(t)));
            }
            // Ensure endpoints are included
            if (line.isEmpty() || line.first().x() > 0)
                line.prepend(QPointF(normToX(0.0), volToY(userVol(0.0) * fadeEnv(0.0))));
            if (line.last().x() < W)
                line.append(QPointF(normToX(1.0), volToY(userVol(1.0) * fadeEnv(1.0))));
        } else {
            line << QPointF(normToX(0.0), volToY(1.0)) << QPointF(normToX(1.0), volToY(1.0));
        }

        p.setPen(QPen(QColor(0xf0, 0xc0, 0x40, 200), 2));
        p.drawPolyline(line.data(), line.size());

        QPolygonF poly;
        poly << QPointF(normToX(0.0), volToY(0));
        for (const auto &pt : line) poly << pt;
        poly << QPointF(normToX(1.0), volToY(0));
        p.setBrush(QColor(0xf0, 0xc0, 0x40, 28));
        p.setPen(Qt::NoPen);
        p.drawPolygon(poly);

        // Draw user-defined automation points on top
        p.setPen(QPen(QColor(0xf0, 0xc0, 0x40), 2));
        p.setBrush(QColor(0x0c, 0x0c, 0x10));
        for (const auto &pt : m_points)
            p.drawEllipse(QPointF(normToX(pt.x()), volToY(pt.y())), 5.0, 5.0);
    }

    // ── Trim bars (cesure) ────────────────────────────────────────────────────
    if (m_duration > 0.01) {
        if (m_trimStart >= 0.0) {
            const int xTS = int(normToX(m_trimStart));
            p.setPen(QPen(QColor(0x44, 0xee, 0x66), 2));
            p.drawLine(xTS, 0, xTS, waveH);
            QPolygon tri;
            tri << QPoint(xTS, midY - 8) << QPoint(xTS + 10, midY) << QPoint(xTS, midY + 8);
            p.setBrush(QColor(0x44, 0xee, 0x66, 220));
            p.setPen(Qt::NoPen);
            p.drawPolygon(tri);
            // Label "IN"
            p.setPen(QColor(0x44, 0xee, 0x66));
            QFont lf; lf.setPixelSize(9); lf.setBold(true); p.setFont(lf);
            p.drawText(xTS + 3, 12, "IN");
        }
        if (m_trimEnd >= 0.0) {
            const int xTE = int(normToX(m_trimEnd));
            p.setPen(QPen(QColor(0xee, 0x44, 0x44), 2));
            p.drawLine(xTE, 0, xTE, waveH);
            QPolygon tri;
            tri << QPoint(xTE, midY - 8) << QPoint(xTE - 10, midY) << QPoint(xTE, midY + 8);
            p.setBrush(QColor(0xee, 0x44, 0x44, 220));
            p.setPen(Qt::NoPen);
            p.drawPolygon(tri);
            // Label "OUT"
            p.setPen(QColor(0xee, 0x44, 0x44));
            QFont lf; lf.setPixelSize(9); lf.setBold(true); p.setFont(lf);
            p.drawText(xTE - 22, 12, "OUT");
        }
    }

    // ── Playback cursor ───────────────────────────────────────────────────────
    if (m_duration > 0.01 && m_playPos > 0.001) {
        const double t  = qBound(0.0, m_playPos / m_duration, 1.0);
        const int    xPH = int(normToX(t));
        if (xPH >= 0 && xPH < W) {
            // Bright vertical line
            p.setPen(QPen(QColor(255, 220, 60, 230), 2));
            p.drawLine(xPH, 0, xPH, waveH);
            // Small downward triangle at top
            p.setRenderHint(QPainter::Antialiasing, true);
            QPolygon tri;
            tri << QPoint(xPH - 5, 0) << QPoint(xPH + 5, 0) << QPoint(xPH, 8);
            p.setBrush(QColor(255, 220, 60, 220));
            p.setPen(Qt::NoPen);
            p.drawPolygon(tri);
            // Time label
            if (m_duration > 0.01) {
                const double rem = qMax(0.0, m_duration - m_playPos);
                const QString lbl = QString("-%1s").arg(rem, 0, 'f', 1);
                p.setRenderHint(QPainter::Antialiasing, false);
                QFont f; f.setPixelSize(9); p.setFont(f);
                const int lblX = (xPH + 4 + 30 < W) ? xPH + 4 : xPH - 34;
                p.setPen(QColor(255, 220, 60, 200));
                p.drawText(lblX, 20, lbl);
            }
        }
    }

    // ── Zoom scroll indicator ─────────────────────────────────────────────────
    if (m_zoom > 1.01) {
        const int barY = H - int(kScrollH);
        p.fillRect(0, barY, W, int(kScrollH), QColor(0x18, 0x18, 0x22));
        const int vx = int(m_viewOffset * W);
        const int vw = qMax(4, int(W / m_zoom));
        p.fillRect(vx, barY + 1, vw, int(kScrollH) - 2, QColor(0x60, 0x60, 0x99, 220));
    }

    // Border
    p.setPen(QColor(0x28, 0x28, 0x30));
    p.setBrush(Qt::NoBrush);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

// ── Mouse ─────────────────────────────────────────────────────────────────────

void WaveformView::mousePressEvent(QMouseEvent *event) {
    const QPointF pos = event->position();

    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStartX = pos.x();
        m_panStartOffset = m_viewOffset;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    if (hitTrimStart(pos)) { m_dragTrimStart = true; setCursor(Qt::SizeHorCursor); return; }
    if (hitTrimEnd  (pos)) { m_dragTrimEnd   = true; setCursor(Qt::SizeHorCursor); return; }
    if (hitFadeIn   (pos)) { m_dragFadeIn    = true; setCursor(Qt::SizeHorCursor); return; }
    if (hitFadeOut  (pos)) { m_dragFadeOut   = true; setCursor(Qt::SizeHorCursor); return; }

    const int idx = hitPoint(pos);
    if (idx >= 0) {
        m_dragIdx = idx;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    // Add new volume point
    const double t = qBound(0.001, xToNorm(pos.x()), 0.999);
    const double v = qBound(0.0,   yToVol(pos.y()),  1.0);
    m_points.append({t, v});
    std::sort(m_points.begin(), m_points.end(),
              [](const QPointF &a, const QPointF &b){ return a.x() < b.x(); });
    for (int i = 0; i < m_points.size(); ++i) {
        if (qAbs(m_points[i].x() - t) < 0.0001) { m_dragIdx = i; break; }
    }
    update();
    emit volumePointsChanged(m_points);
}

void WaveformView::mouseMoveEvent(QMouseEvent *event) {
    const QPointF pos = event->position();

    if (m_panning) {
        if (m_zoom > 1.01) {
            const double delta = (m_panStartX - pos.x()) / (m_zoom * width());
            m_viewOffset = qBound(0.0, m_panStartOffset + delta, 1.0 - 1.0 / m_zoom);
        }
        update();
        return;
    }

    if (m_dragTrimStart) {
        const double maxT = (m_trimEnd >= 0.0) ? m_trimEnd - 0.001 : 1.0;
        m_trimStart = qBound(0.0, xToNorm(pos.x()), maxT);
        update();
        if (m_duration > 0.01)
            emit trimStartChanged(m_trimStart * m_duration);
        return;
    }
    if (m_dragTrimEnd) {
        const double minT = (m_trimStart >= 0.0) ? m_trimStart + 0.001 : 0.0;
        m_trimEnd = qBound(minT, xToNorm(pos.x()), 1.0);
        update();
        if (m_duration > 0.01)
            emit trimEndChanged(m_trimEnd * m_duration);
        return;
    }

    if (m_dragFadeIn) {
        const double maxT = m_duration > 0.01
            ? qMax(0.0, 1.0 - m_fadeOut / m_duration) : 1.0;
        m_fadeIn = qBound(0.0, xToNorm(pos.x()), maxT) * m_duration;
        update();
        emit fadeInChanged(m_fadeIn);
        return;
    }
    if (m_dragFadeOut) {
        const double minT = m_duration > 0.01 ? m_fadeIn / m_duration : 0.0;
        m_fadeOut = qBound(0.0, 1.0 - xToNorm(pos.x()), 1.0 - minT) * m_duration;
        update();
        emit fadeOutChanged(m_fadeOut);
        return;
    }
    if (m_dragIdx >= 0 && m_dragIdx < m_points.size()) {
        const double minX = m_dragIdx > 0
            ? m_points[m_dragIdx - 1].x() + 0.001 : 0.001;
        const double maxX = m_dragIdx + 1 < m_points.size()
            ? m_points[m_dragIdx + 1].x() - 0.001 : 0.999;
        const double t = qBound(minX, xToNorm(pos.x()), maxX);
        const double v = qBound(0.0,  yToVol(pos.y()), 1.0);
        m_points[m_dragIdx] = {t, v};
        update();
        emit volumePointsChanged(m_points);
        return;
    }

    // Cursor hints when idle
    if      (hitTrimStart(pos) || hitTrimEnd(pos)) setCursor(Qt::SizeHorCursor);
    else if (hitFadeIn   (pos) || hitFadeOut(pos)) setCursor(Qt::SizeHorCursor);
    else if (hitPoint    (pos) >= 0)               setCursor(Qt::OpenHandCursor);
    else                                            setCursor(Qt::CrossCursor);
}

void WaveformView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        m_panning = false;
        setCursor(Qt::CrossCursor);
        return;
    }
    m_dragIdx       = -1;
    m_dragFadeIn    = false;
    m_dragFadeOut   = false;
    m_dragTrimStart = false;
    m_dragTrimEnd   = false;
    setCursor(Qt::CrossCursor);
}

void WaveformView::contextMenuEvent(QContextMenuEvent *event) {
    const QPointF pos = event->pos();
    const int volIdx  = hitPoint(pos);
    const double t    = xToNorm(pos.x());

    QMenu menu(this);

    if (volIdx >= 0) {
        QAction *del = menu.addAction("Rimuovi punto volume");
        menu.addSeparator();
        if (menu.exec(event->globalPos()) == del) {
            m_points.removeAt(volIdx);
            update();
            emit volumePointsChanged(m_points);
        }
        return;
    }

    // Trim start
    if (m_trimStart >= 0.0) {
        menu.addAction("Rimuovi inizio cue (IN)", this, [this]() {
            m_trimStart = -1.0;
            update();
            emit trimStartChanged(0.0);
        });
    } else {
        menu.addAction("Imposta inizio cue (IN) qui", this, [this, t]() {
            const double maxT = (m_trimEnd >= 0.0) ? m_trimEnd - 0.001 : 0.999;
            m_trimStart = qBound(0.0, t, maxT);
            update();
            if (m_duration > 0.01)
                emit trimStartChanged(m_trimStart * m_duration);
        });
    }

    // Trim end
    if (m_trimEnd >= 0.0) {
        menu.addAction("Rimuovi fine cue (OUT)", this, [this]() {
            m_trimEnd = -1.0;
            update();
            emit trimEndChanged(0.0);
        });
    } else {
        menu.addAction("Imposta fine cue (OUT) qui", this, [this, t]() {
            const double minT = (m_trimStart >= 0.0) ? m_trimStart + 0.001 : 0.001;
            m_trimEnd = qBound(minT, t, 1.0);
            update();
            if (m_duration > 0.01)
                emit trimEndChanged(m_trimEnd * m_duration);
        });
    }

    menu.exec(event->globalPos());
}

void WaveformView::wheelEvent(QWheelEvent *event) {
    const double cursorNorm = xToNorm(event->position().x());
    const double factor     = event->angleDelta().y() > 0 ? 1.25 : (1.0 / 1.25);

    m_zoom = qBound(1.0, m_zoom * factor, 20.0);

    if (m_zoom <= 1.01) {
        m_zoom       = 1.0;
        m_viewOffset = 0.0;
    } else {
        m_viewOffset = qBound(0.0,
            cursorNorm - event->position().x() / (m_zoom * width()),
            1.0 - 1.0 / m_zoom);
    }

    update();
    event->accept();
}

// ── Coordinates ───────────────────────────────────────────────────────────────

double WaveformView::xToNorm(double px) const {
    return width() > 0 ? m_viewOffset + px / (m_zoom * width()) : 0.0;
}

double WaveformView::normToX(double t) const {
    return (t - m_viewOffset) * m_zoom * width();
}

double WaveformView::yToVol(double py) const {
    const double h = height() - 2 * kMarginY;
    return h > 0 ? 1.0 - (py - kMarginY) / h : 1.0;
}

double WaveformView::volToY(double v) const {
    return kMarginY + (1.0 - v) * (height() - 2 * kMarginY);
}

void WaveformView::clampView() {
    if (m_zoom <= 1.01) { m_zoom = 1.0; m_viewOffset = 0.0; return; }
    m_viewOffset = qBound(0.0, m_viewOffset, 1.0 - 1.0 / m_zoom);
}

int WaveformView::hitPoint(const QPointF &pos) const {
    for (int i = 0; i < m_points.size(); ++i) {
        const QPointF c(normToX(m_points[i].x()), volToY(m_points[i].y()));
        const double dx = pos.x() - c.x(), dy = pos.y() - c.y();
        if (dx*dx + dy*dy < 64.0) return i;
    }
    return -1;
}

bool WaveformView::hitFadeIn(const QPointF &pos) const {
    if (m_duration <= 0.01 || m_fadeIn <= 0.01) return false;
    return qAbs(pos.x() - normToX(m_fadeIn / m_duration)) < 8.0;
}

bool WaveformView::hitFadeOut(const QPointF &pos) const {
    if (m_duration <= 0.01 || m_fadeOut <= 0.01) return false;
    return qAbs(pos.x() - normToX(1.0 - m_fadeOut / m_duration)) < 8.0;
}

bool WaveformView::hitTrimStart(const QPointF &pos) const {
    if (m_duration <= 0.01 || m_trimStart < 0.0) return false;
    const double x = normToX(m_trimStart);
    return x > -10 && x < width() + 10 && qAbs(pos.x() - x) < 8.0;
}

bool WaveformView::hitTrimEnd(const QPointF &pos) const {
    if (m_duration <= 0.01 || m_trimEnd < 0.0) return false;
    const double x = normToX(m_trimEnd);
    return x > -10 && x < width() + 10 && qAbs(pos.x() - x) < 8.0;
}
