#pragma once
#include "Cue.h"
#include <QStringList>
#include <QTimer>

class ImageCue : public Cue {
    Q_OBJECT
public:
    enum class Transition { Cut, Crossfade, SlideHorizontal };
    enum class Background { Black, White, Image };

    explicit ImageCue(QObject *parent = nullptr);

    Type    type()     const override { return Type::Image; }
    QString typeName() const override { return tr("Immagine"); }

    void   go()             override;
    void   stop()           override;
    void   pause()          override {}
    double duration() const override;

    // Volume di base sempre 1.0 (nessun "volume" persistito per le immagini);
    // il fade visivo passa per setPlaybackVolume(), non per setVolume().
    void setPlaybackVolume(double v) override;

    QStringList imagePaths()        const { return m_imagePaths; }
    int         imageCount()        const { return m_imagePaths.size(); }
    void        setImagePaths(const QStringList &paths) { m_imagePaths = paths; emit propertyChanged(); }

    double     slideDuration()      const { return m_slideDuration; }
    double     transitionDuration() const { return m_transitionDuration; }
    Transition transitionType()     const { return m_transition; }
    bool       loop()               const { return m_loop; }
    double     visualLevel()        const { return m_visualLevel; }

    void setSlideDuration(double s)           { m_slideDuration = qMax(0.1, s); emit propertyChanged(); }
    void setTransitionDuration(double s)      { m_transitionDuration = qMax(0.0, s); emit propertyChanged(); }
    void setTransitionType(Transition t)      { m_transition = t; emit propertyChanged(); }
    void setLoop(bool v)                      { m_loop = v; emit propertyChanged(); }

    Background background()         const { return m_background; }
    QString    backgroundImagePath() const { return m_backgroundImagePath; }
    void setBackground(Background b)            { m_background = b; emit propertyChanged(); }
    void setBackgroundImagePath(const QString &p) { m_backgroundImagePath = p; emit propertyChanged(); }

    // Stato runtime per la finestra di output
    QString currentImagePath()  const;
    QString nextImagePath()     const;   // vuoto se non in transizione
    bool    isTransitioning()   const { return m_phase == Phase::Transitioning; }
    double  transitionProgress() const; // 0..1 entro la transizione corrente

    QJsonObject toJson()                  const override;
    void        fromJson(const QJsonObject &o)  override;

private slots:
    void onTick();

private:
    enum class Phase { Holding, Transitioning };

    void finishNaturally();

    QStringList m_imagePaths;
    double      m_slideDuration      = 3.0;
    double      m_transitionDuration = 1.0;
    Transition  m_transition         = Transition::Crossfade;
    bool        m_loop               = false;
    double      m_visualLevel        = 1.0;
    Background  m_background         = Background::Black;
    QString     m_backgroundImagePath;

    QTimer *m_timer = nullptr;
    int     m_currentIndex = 0;
    int     m_targetIndex  = 0;
    Phase   m_phase        = Phase::Holding;
    double  m_phaseElapsed = 0.0;
};
