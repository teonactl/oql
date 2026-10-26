#pragma once
#include "CueList.h"
#include "ProjectionWarp.h"
#include <QObject>
#include <QString>

class Workspace : public QObject {
    Q_OBJECT
public:
    explicit Workspace(QObject *parent = nullptr);

    QString   name()            const { return m_name; }
    QString   filePath()        const { return m_filePath; }
    bool      isModified()      const { return m_modified; }
    CueList*  cueList()               { return &m_cueList; }

    // Project settings
    bool   showCueNumbers()  const { return m_showCueNumbers; }
    bool   autoFadeOnStop()  const { return m_autoFadeOnStop; }

    // Calibrazione dell'area di proiezione (warp), condivisa da Video e
    // Image Output Window perché in pratica puntano allo stesso proiettore/
    // schermo fisico. Salvata nel file di progetto — venue diverse hanno
    // tipicamente setup di proiezione diversi.
    ProjectionWarp&       projectionWarp()       { return m_projectionWarp; }
    const ProjectionWarp& projectionWarp() const { return m_projectionWarp; }
    void setProjectionWarp(const ProjectionWarp &warp);

    void setName(const QString &n) { m_name = n; }
    void setShowCueNumbers(bool v);
    void setAutoFadeOnStop(bool v);

    bool save(const QString &path = {});
    bool load(const QString &path);
    void reset();

signals:
    void modifiedChanged(bool modified);
    void filePathChanged(const QString &path);
    void projectSettingsChanged();
    void projectionWarpChanged();

private:
    void setModified(bool v);

    CueList m_cueList;
    QString m_name          = "Nuovo workspace";
    QString m_filePath;
    bool    m_modified      = false;
    bool    m_showCueNumbers = true;
    bool    m_autoFadeOnStop = false;
    ProjectionWarp m_projectionWarp;
};
