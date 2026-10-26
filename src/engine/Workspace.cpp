#include "Workspace.h"
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

Workspace::Workspace(QObject *parent) : QObject(parent) {
    auto mark = [this](auto...) { setModified(true); };
    connect(&m_cueList, &CueList::cueAdded,          this, mark);
    connect(&m_cueList, &CueList::cueRemoved,         this, mark);
    connect(&m_cueList, &CueList::cueMoved,           this, mark);
    connect(&m_cueList, &CueList::cuePropertyChanged, this, mark);
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

void Workspace::setProjectionWarp(const ProjectionWarp &warp) {
    m_projectionWarp = warp;
    setModified(true);
    emit projectionWarpChanged();
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
    root["projectionWarp"] = m_projectionWarp.toJson();
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
    m_projectionWarp.fromJson(root["projectionWarp"].toObject());
    m_cueList.fromJson(root["cues"].toArray());
    m_filePath = path;
    setModified(false);
    emit filePathChanged(m_filePath);
    emit projectSettingsChanged();
    emit projectionWarpChanged();
    return true;
}

void Workspace::reset() {
    m_cueList.stopAll();
    m_cueList.fromJson({});
    m_name           = "Nuovo workspace";
    m_filePath.clear();
    m_showCueNumbers = true;
    m_autoFadeOnStop = false;
    m_projectionWarp = ProjectionWarp();
    setModified(false);
    emit filePathChanged(m_filePath);
    emit projectSettingsChanged();
    emit projectionWarpChanged();
}
