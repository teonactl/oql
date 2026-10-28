#pragma once
#include "CueList.h"
#include "VirtualScreen.h"
#include <QObject>
#include <QString>
#include <QList>

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

    // Output fisici (un proiettore/monitor reale ciascuno, con la propria
    // calibrazione) e schermi virtuali (una forma — poligono qualsiasi —
    // ritagliata dentro UN output fisico; più schermi virtuali possono
    // condividere lo stesso output). Le cue video/immagine si assegnano a
    // schermi virtuali, non direttamente agli output. Salvati nel progetto
    // — venue diverse hanno setup di proiezione diversi. C'è sempre almeno
    // un output e uno schermo virtuale di default (anche in un progetto
    // nuovo), così l'app ha sempre una destinazione sensata senza bisogno
    // di configurazione esplicita per il caso più comune (un solo output).
    QList<PhysicalOutput>&       physicalOutputs()       { return m_physicalOutputs; }
    const QList<PhysicalOutput>& physicalOutputs() const { return m_physicalOutputs; }
    QList<VirtualScreen>&        virtualScreens()        { return m_virtualScreens; }
    const QList<VirtualScreen>&  virtualScreens() const  { return m_virtualScreens; }

    PhysicalOutput* physicalOutputById(const QString &id);
    VirtualScreen*  virtualScreenById(const QString &id);
    // Tutti gli schermi virtuali che appartengono a un dato output fisico.
    QList<VirtualScreen*> virtualScreensFor(const QString &physicalOutputId);

    QString addPhysicalOutput(const QString &name = {});
    void    removePhysicalOutput(const QString &id); // rimuove anche i suoi schermi virtuali
    QString addVirtualScreen(const QString &physicalOutputId, const QString &name = {});
    void    removeVirtualScreen(const QString &id);

    // Sostituisce TUTTI gli schermi virtuali di un output con una griglia
    // cols×rows di rettangoli che tassellano l'intero canvas (sia come
    // maskPolygon che come canvasRect, quindi equivale a un video-wall vero
    // e proprio: ciascuna zona mostra solo la sua porzione di contenuto) —
    // i preset di divisione rapida (metà, mosaico, ecc.) nella UI passano
    // da qui. cols e rows vengono portati ad almeno 1.
    void replaceVirtualScreensWithGrid(const QString &physicalOutputId, int cols, int rows);

    // Da chiamare dopo aver modificato sul posto un PhysicalOutput/
    // VirtualScreen preso per riferimento (es. trascinando la calibrazione,
    // disegnando un poligono): segna il progetto modificato e notifica la UI.
    void notifyScreensChanged();

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
    void screensChanged();

private:
    void setModified(bool v);
    void ensureAtLeastOneScreen(); // crea output+schermo di default se la lista è vuota

    CueList m_cueList;
    QString m_name          = "Nuovo workspace";
    QString m_filePath;
    bool    m_modified      = false;
    bool    m_showCueNumbers = true;
    bool    m_autoFadeOnStop = false;

    QList<PhysicalOutput> m_physicalOutputs;
    QList<VirtualScreen>  m_virtualScreens;
};
