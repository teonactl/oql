#include "VirtualScreen.h"
#include <QUuid>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>

static QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

static QJsonArray rectToJson(QRectF r) {
    QJsonArray a;
    a.append(r.x()); a.append(r.y()); a.append(r.width()); a.append(r.height());
    return a;
}

static QRectF rectFromJson(const QJsonArray &a, QRectF fallback) {
    if (a.size() != 4) return fallback;
    return QRectF(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble());
}

// ───────────────────────── PhysicalOutput ─────────────────────────

PhysicalOutput::PhysicalOutput()
    : m_id(newId())
    , m_name(QStringLiteral("Output 1"))
{
}

PhysicalOutput::PhysicalOutput(QString id)
    : m_id(std::move(id))
{
}

QJsonObject PhysicalOutput::toJson() const {
    QJsonObject o;
    o["id"]                 = m_id;
    o["name"]                = m_name;
    o["physicalScreenName"]  = m_physicalScreenName;
    o["autoResolution"]      = m_autoResolution;
    o["resolutionW"]         = m_resolution.width();
    o["resolutionH"]         = m_resolution.height();
    o["warp"]                = m_warp.toJson();
    return o;
}

void PhysicalOutput::fromJson(const QJsonObject &o) {
    m_id = o["id"].toString();
    if (m_id.isEmpty()) m_id = newId();
    m_name               = o["name"].toString(QStringLiteral("Output"));
    m_physicalScreenName = o["physicalScreenName"].toString();
    m_autoResolution      = o["autoResolution"].toBool(true);
    m_resolution          = QSize(o["resolutionW"].toInt(1920), o["resolutionH"].toInt(1080));
    m_warp.fromJson(o["warp"].toObject());
}

// ───────────────────────── VirtualScreen ─────────────────────────

VirtualScreen::VirtualScreen()
    : m_id(newId())
    , m_name(QStringLiteral("Schermo 1"))
{
}

VirtualScreen::VirtualScreen(QString id)
    : m_id(std::move(id))
{
}

QJsonObject VirtualScreen::toJson() const {
    QJsonObject o;
    o["id"]               = m_id;
    o["name"]              = m_name;
    o["physicalOutputId"]  = m_physicalOutputId;
    o["canvasRect"]        = rectToJson(m_canvasRect);

    QJsonArray poly;
    for (const QPointF &p : m_maskPolygon) {
        poly.append(p.x());
        poly.append(p.y());
    }
    o["maskPolygon"] = poly;
    o["fitMode"]      = static_cast<int>(m_fitMode);
    o["anchorX"]      = m_contentAnchor.x();
    o["anchorY"]      = m_contentAnchor.y();
    o["zoom"]         = m_zoom;
    return o;
}

void VirtualScreen::fromJson(const QJsonObject &o) {
    m_id = o["id"].toString();
    if (m_id.isEmpty()) m_id = newId();
    m_name              = o["name"].toString(QStringLiteral("Schermo"));
    m_physicalOutputId  = o["physicalOutputId"].toString();
    m_canvasRect        = rectFromJson(o["canvasRect"].toArray(), QRectF(0.0, 0.0, 1.0, 1.0));

    const QJsonArray poly = o["maskPolygon"].toArray();
    if (poly.size() >= 6 && poly.size() % 2 == 0) { // almeno 3 punti, coppie x,y
        QPolygonF p;
        p.reserve(poly.size() / 2);
        for (int i = 0; i < poly.size(); i += 2)
            p.append(QPointF(poly[i].toDouble(), poly[i + 1].toDouble()));
        m_maskPolygon = p;
    } else {
        m_maskPolygon = QPolygonF(QRectF(0.0, 0.0, 1.0, 1.0));
    }

    m_fitMode = static_cast<FitMode>(o["fitMode"].toInt(static_cast<int>(FitMode::Contain)));
    m_contentAnchor = QPointF(o["anchorX"].toDouble(0.5), o["anchorY"].toDouble(0.5));
    setZoom(o["zoom"].toDouble(1.0));
}

QRectF virtualScreenTargetRect(const VirtualScreen &screen, QSize canvasSize) {
    const QPolygonF poly = screen.maskPolygon();
    if (poly.size() >= 3) {
        QPolygonF pixelPoly;
        pixelPoly.reserve(poly.size());
        for (const QPointF &n : poly)
            pixelPoly << QPointF(n.x() * canvasSize.width(), n.y() * canvasSize.height());
        const QRectF bbox = pixelPoly.boundingRect();
        if (bbox.width() >= 1.0 && bbox.height() >= 1.0) return bbox;
    }
    return QRectF(0, 0, canvasSize.width(), canvasSize.height());
}

QRectF fitDestRect(QSizeF contentSize, QRectF targetPx, VirtualScreen::FitMode mode, QPointF anchor, double zoom) {
    if (contentSize.width() <= 0 || contentSize.height() <= 0) return targetPx;
    auto alignedRect = [&](double w, double h) {
        const double x = targetPx.left() + (targetPx.width()  - w) * anchor.x();
        const double y = targetPx.top()  + (targetPx.height() - h) * anchor.y();
        return QRectF(x, y, w, h);
    };
    switch (mode) {
    case VirtualScreen::FitMode::Stretch:
        return targetPx;
    case VirtualScreen::FitMode::Contain: {
        const double s = qMin(targetPx.width() / contentSize.width(), targetPx.height() / contentSize.height());
        return alignedRect(contentSize.width() * s, contentSize.height() * s);
    }
    case VirtualScreen::FitMode::Cover: {
        const double s = qMax(targetPx.width() / contentSize.width(), targetPx.height() / contentSize.height());
        return alignedRect(contentSize.width() * s, contentSize.height() * s);
    }
    case VirtualScreen::FitMode::Center:
        return alignedRect(contentSize.width(), contentSize.height());
    case VirtualScreen::FitMode::Manual: {
        // Come Contain ma con uno zoom extra: a differenza di Contain/Cover,
        // il cui margine di trascinamento dipende (e può anche annullarsi)
        // dalle proporzioni del contenuto rispetto alla zona, qui lo zoom
        // garantisce sempre un margine proporzionale alla propria scelta —
        // serve anche a ritagliare le bande di un contenuto con proporzioni
        // diverse scegliendo a mano quale porzione restare visibile.
        const double containScale = qMin(targetPx.width() / contentSize.width(), targetPx.height() / contentSize.height());
        const double s = containScale * qMax(1.0, zoom);
        return alignedRect(contentSize.width() * s, contentSize.height() * s);
    }
    }
    return targetPx;
}

void applyVirtualScreenMask(QPainter &painter, const QImage &content, QSize canvasSize, const VirtualScreen &screen) {
    const QSize sz = canvasSize;
    if (sz.isEmpty() || content.isNull()) return;

    // 1. canvasRect: ritaglia la sotto-regione normalizzata del contenuto
    // NATIVO (non del canvas) che questo schermo mostra — con canvasRect =
    // tutto (default) equivale a mostrare il contenuto intero; con
    // rettangoli diversi per ciascuno schermo si ottiene una divisione in
    // stile video-wall DEL SORGENTE REALE, non di una sua versione già
    // adattata/letterboxata (altrimenti il fitMode al passo 3 lavorerebbe su
    // bande nere già presenti nei pixel invece che sul contenuto vero).
    const QRectF cr = screen.canvasRect();
    const QRect cropPx(
        qRound(cr.x() * content.width()),      qRound(cr.y() * content.height()),
        qRound(cr.width() * content.width()),  qRound(cr.height() * content.height()));
    const QRect validCrop = cropPx.intersected(content.rect());
    // Niente copy() quando il ritaglio è tutto il contenuto (il caso più
    // comune, canvasRect di default): QImage::copy() duplica i pixel, e
    // farlo a ogni frame su un'immagine inutilmente già intera è uno spreco
    // — QImage condivide i dati (copy-on-write) finché non viene modificata,
    // quindi qui `cropped` può restare un riferimento leggero a `content`.
    const QImage cropped = (validCrop.isEmpty() || validCrop == content.rect())
        ? content
        : content.copy(validCrop);

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 2. maskPolygon: l'area di destinazione è il bounding box del poligono
    // (non più sempre l'intero canvas) — è lì dentro che il contenuto va
    // posizionato secondo il fitMode; fuori dal poligono non si disegna
    // nulla (resta quel che c'era già nel canvas condiviso, tipicamente
    // nero — stesso risultato visivo di un'area trasparente compositata su
    // nero, senza bisogno di un buffer a parte da riempire di trasparente).
    const QRectF targetPx = virtualScreenTargetRect(screen, sz);
    const QPolygonF poly = screen.maskPolygon();
    if (poly.size() >= 3) {
        QPolygonF pixelPoly;
        pixelPoly.reserve(poly.size());
        for (const QPointF &n : poly)
            pixelPoly << QPointF(n.x() * sz.width(), n.y() * sz.height());
        QPainterPath clip;
        clip.addPolygon(pixelPoly);
        clip.closeSubpath();
        painter.setClipPath(clip);
    }

    // 3. fitMode: come il contenuto ritagliato riempie targetPx. contentAnchor
    // (0..1 su entrambi gli assi) decide dove si posiziona lo spazio libero
    // (Contain) o quale porzione resta visibile dell'eccesso (Cover/Center) —
    // (0.5,0.5) = centrato, (0,0) = in alto a sinistra, ecc.
    if (!cropped.isNull() && cropped.width() > 0 && cropped.height() > 0) {
        const QRectF destRect = fitDestRect(cropped.size(), targetPx, screen.fitMode(), screen.contentAnchor(), screen.zoom());
        painter.drawImage(destRect, cropped, QRectF(cropped.rect()));
    }
    painter.restore();
}

QImage applyVirtualScreenMask(const QImage &content, QSize canvasSize, const VirtualScreen &screen) {
    if (canvasSize.isEmpty() || content.isNull())
        return QImage(canvasSize, QImage::Format_ARGB32_Premultiplied);
    QImage result(canvasSize, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter p(&result);
    applyVirtualScreenMask(p, content, canvasSize, screen);
    return result;
}
