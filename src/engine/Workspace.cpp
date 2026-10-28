#include "Workspace.h"
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

Workspace::Workspace(QObject *parent) : QObject(parent) {
    auto mark = [this](auto...) { setModified(true); };
    connect(&m_cueList, &CueList::cueAdded,          this, mark);
    connect(&m_cueList, &CueList::cueRemoved,         this, mark);
    connect(&m_cueList, &CueList::cueMoved,           this, mark);
    connect(&m_cueList, &CueList::cuePropertyChanged, this, mark);
    ensureAtLeastOneScreen();
}

void Workspace::setShowCueNumbers(bool v) {
    if (m_showCueNumbers == v) return;
    m_showCueNumbers = v;
    setModified(true);
    emit projectSettingsChanged();
}

void Workspace::setAutoFadeOnStop(bool v) {
    if (m_autoFadeOnStop == v) return;
    m_autoFadeOnStop = v;
    setModified(true);
    emit projectSettingsChanged();
}

void Workspace::ensureAtLeastOneScreen() {
    if (!m_physicalOutputs.isEmpty()) return;
    PhysicalOutput out;
    m_physicalOutputs.append(out);
    VirtualScreen scr;
    scr.setPhysicalOutputId(out.id());
    m_virtualScreens.append(scr);
}

PhysicalOutput* Workspace::physicalOutputById(const QString &id) {
    for (PhysicalOutput &o : m_physicalOutputs)
        if (o.id() == id) return &o;
    return nullptr;
}

VirtualScreen* Workspace::virtualScreenById(const QString &id) {
    for (VirtualScreen &s : m_virtualScreens)
        if (s.id() == id) return &s;
    return nullptr;
}

QList<VirtualScreen*> Workspace::virtualScreensFor(const QString &physicalOutputId) {
    QList<VirtualScreen*> result;
    for (VirtualScreen &s : m_virtualScreens)
        if (s.physicalOutputId() == physicalOutputId) result.append(&s);
    return result;
}

QString Workspace::addPhysicalOutput(const QString &name) {
    PhysicalOutput out;
    if (!name.isEmpty()) out.setName(name);
    else out.setName(QObject::tr("Output %1").arg(m_physicalOutputs.size() + 1));
    const QString id = out.id();
    m_physicalOutputs.append(out);
    notifyScreensChanged();
    return id;
}

void Workspace::removePhysicalOutput(const QString &id) {
    if (m_physicalOutputs.size() <= 1) return; // deve restarne sempre almeno uno
    for (int i = 0; i < m_virtualScreens.size(); ) {
        if (m_virtualScreens[i].physicalOutputId() == id) m_virtualScreens.removeAt(i);
        else ++i;
    }
    for (int i = 0; i < m_physicalOutputs.size(); ++i) {
        if (m_physicalOutputs[i].id() == id) { m_physicalOutputs.removeAt(i); break; }
    }
    ensureAtLeastOneScreen();
    notifyScreensChanged();
}

QString Workspace::addVirtualScreen(const QString &physicalOutputId, const QString &name) {
    VirtualScreen scr;
    scr.setPhysicalOutputId(physicalOutputId);
    if (!name.isEmpty()) scr.setName(name);
    else scr.setName(QObject::tr("Schermo %1").arg(virtualScreensFor(physicalOutputId).size() + 1));
    const QString id = scr.id();
    m_virtualScreens.append(scr);
    notifyScreensChanged();
    return id;
}

void Workspace::removeVirtualScreen(const QString &id) {
    // Deve restarne sempre almeno uno sul suo output, altrimenti le cue
    // assegnate a quello schermo non avrebbero più nessuna destinazione.
    for (int i = 0; i < m_virtualScreens.size(); ++i) {
        if (m_virtualScreens[i].id() != id) continue;
        if (virtualScreensFor(m_virtualScreens[i].physicalOutputId()).size() <= 1) return;
        m_virtualScreens.removeAt(i);
        break;
    }
    notifyScreensChanged();
}

void Workspace::replaceVirtualScreensWithGrid(const QString &physicalOutputId, int cols, int rows) {
    if (!physicalOutputById(physicalOutputId)) return;
    cols = qMax(1, cols);
    rows = qMax(1, rows);

    for (int i = 0; i < m_virtualScreens.size(); ) {
        if (m_virtualScreens[i].physicalOutputId() == physicalOutputId) m_virtualScreens.removeAt(i);
        else ++i;
    }

    int n = 1;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c, ++n) {
            const QRectF tile(double(c) / cols, double(r) / rows, 1.0 / cols, 1.0 / rows);
            VirtualScreen scr;
            scr.setPhysicalOutputId(physicalOutputId);
            scr.setName(cols * rows == 1 ? QObject::tr("Schermo 1") : QObject::tr("Zona %1").arg(n));
            scr.setMaskPolygon(QPolygonF(tile));
            scr.setCanvasRect(tile);
            m_virtualScreens.append(scr);
        }
    }
    notifyScreensChanged();
}

void Workspace::notifyScreensChanged() {
    setModified(true);
    emit screensChanged();
}

void Workspace::setModified(bool v) {
    if (m_modified == v) return;
    m_modified = v;
    emit modifiedChanged(v);
}

bool Workspace::save(const QString &path) {
    const QString target = path.isEmpty() ? m_filePath : path;
    if (target.isEmpty()) return false;

    QJsonObject root;
    root["name"]           = m_name;
    root["cues"]           = m_cueList.toJson();
    root["showCueNumbers"] = m_showCueNumbers;
    root["autoFadeOnStop"] = m_autoFadeOnStop;

    QJsonArray outs;
    for (const PhysicalOutput &o : m_physicalOutputs) outs.append(o.toJson());
    root["physicalOutputs"] = outs;

    QJsonArray screens;
    for (const VirtualScreen &s : m_virtualScreens) screens.append(s.toJson());
    root["virtualScreens"] = screens;

    const QByteArray data = QJsonDocument(root).toJson();

    // QSaveFile scrive su un file temporaneo nella stessa cartella e lo
    // sostituisce atomicamente a commit() riuscito: un crash, un disco pieno
    // o un errore di scrittura a metà salvataggio non possono più troncare
    // il file .oql esistente (prima si scriveva direttamente sul target).
    QSaveFile f(target);
    if (!f.open(QIODevice::WriteOnly)) return false;
    if (f.write(data) != data.size()) { f.cancelWriting(); return false; }
    if (!f.commit()) return false;

    m_filePath = target;
    m_name     = QFileInfo(target).completeBaseName();
    setModified(false);
    emit filePathChanged(m_filePath);
    return true;
}

bool Workspace::load(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;

    const auto doc = QJsonDocument::fromJson(f.readAll());
    // doc.object() su un JSON valido ma non-oggetto (array, numero, ecc.)
    // restituirebbe silenziosamente un oggetto vuoto: senza isObject() un
    // file .oql corrotto/non valido svuoterebbe il workspace senza errore.
    if (doc.isNull() || !doc.isObject()) return false;

    const auto root = doc.object();
    m_name           = root["name"].toString(QFileInfo(path).completeBaseName());
    m_showCueNumbers = root["showCueNumbers"].toBool(true);
    m_autoFadeOnStop = root["autoFadeOnStop"].toBool(false);

    m_physicalOutputs.clear();
    m_virtualScreens.clear();

    if (root.contains("physicalOutputs") || root.contains("virtualScreens")) {
        for (const QJsonValue &v : root["physicalOutputs"].toArray()) {
            PhysicalOutput o;
            o.fromJson(v.toObject());
            m_physicalOutputs.append(o);
        }
        for (const QJsonValue &v : root["virtualScreens"].toArray()) {
            VirtualScreen s;
            s.fromJson(v.toObject());
            m_virtualScreens.append(s);
        }
    } else if (root.contains("projectionWarp")) {
        // Migrazione da un progetto salvato prima dell'introduzione di
        // output/schermi multipli: un solo output di default con la vecchia
        // calibrazione, e un solo schermo virtuale che copre tutto il suo
        // canvas — riproduce esattamente il comportamento precedente.
        PhysicalOutput out;
        out.setName(QObject::tr("Output 1"));
        ProjectionWarp warp;
        warp.fromJson(root["projectionWarp"].toObject());
        out.setWarp(warp);
        m_physicalOutputs.append(out);

        VirtualScreen scr;
        scr.setPhysicalOutputId(out.id());
        m_virtualScreens.append(scr);
    }
    ensureAtLeastOneScreen();

    m_cueList.fromJson(root["cues"].toArray());
    m_filePath = path;
    setModified(false);
    emit filePathChanged(m_filePath);
    emit projectSettingsChanged();
    emit screensChanged();
    return true;
}

void Workspace::reset() {
    m_cueList.stopAll();
    m_cueList.fromJson({});
    m_name           = "Nuovo workspace";
    m_filePath.clear();
    m_showCueNumbers = true;
    m_autoFadeOnStop = false;
    m_physicalOutputs.clear();
    m_virtualScreens.clear();
    ensureAtLeastOneScreen();
    setModified(false);
    emit filePathChanged(m_filePath);
    emit projectSettingsChanged();
    emit screensChanged();
}
