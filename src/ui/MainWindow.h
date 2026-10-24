#pragma once
#include <QMainWindow>
#include <QUndoStack>
#include <QVector>
#include <QMap>
#include <QString>
#include <QJsonArray>
#include <QTimer>
#include <functional>
#include <memory>
#include "engine/Workspace.h"
#include "engine/Cue.h"
#include "ui/VideoOutputWindow.h"
#include "ui/TextOutputWindow.h"
#include "ui/ImageOutputWindow.h"
#include "ui/CueInfoBar.h"

class CueListModel;
class CueListView;
class InspectorPanel;
class ActiveCuesPanel;
class WebServer;
class VideoCue;
class QLabel;
class QAction;
class QMenu;
class QSplitter;
class QToolButton;
class QPushButton;
class QShortcut;
class QToolBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Apre un workspace passato da riga di comando (doppio clic / "Apri con"
    // su un .oql). Chiamato da main() subito dopo la costruzione, prima di
    // show(): niente conferma di modifiche non salvate, non ce ne sono ancora.
    void openFileAtStartup(const QString &path);

protected:
    void closeEvent(QCloseEvent *event)         override;
    void keyPressEvent(QKeyEvent *event)        override;
    void changeEvent(QEvent *event)             override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private slots:
    void newWorkspace();
    void openWorkspace();
    void openRecentFile(const QString &path);
    bool saveWorkspace();
    bool saveWorkspaceAs();
    void addAudioCue();
    void addVideoCue();
    void addStopCue();
    void addFadeCue();
    void addPauseCue();
    void addMicCue();
    void addGroupCue();
    void addLabelCue();
    void addTextCue();
    void addImageCue();
    void addPlayCue();
#ifndef OQL_BASE
    void addSpeedUpCue();
    void addSpeedDownCue();
    void addEffectCue();
    void addResetEffectCue();
    void addScriptCue();
    void addRecordCue();
#endif
    void deleteSelectedCue();
    void moveSelectedCueUp();
    void moveSelectedCueDown();
    void groupSelectedCues(const QVector<int> &visRows);
    void go();
    void stopAll();
    void goToFirstCue();
    void applyShortcuts();
    void toggleVideoOutput();
    void toggleTextOutput();
    void toggleImageOutput();
    void openSettings();
    void showAbout();
    void onSelectionChanged();
    void updateTitle();
    void assignVideoSinkToAll();
    void applyProjectSettings();

private:
    void buildUi();
    void buildMenus();
    void buildToolBar();
    bool confirmUnsaved();
    void setupNewVideoCue(int index);
    void wireVideoCueOpacity(VideoCue *cue);
    void addToRecentFiles(const QString &path);
    // Sequenza comune dopo un load() riuscito (assegna i video sink, pulisce
    // l'inspector/undo stack, aggiorna i recenti) — condivisa da
    // openWorkspace(), openRecentFile() e openFileAtStartup().
    void refreshAfterLoad(const QString &path);
    void rebuildRecentMenu();
    QString nextCueNumber();

    Workspace          m_workspace;
    CueListModel      *m_model           = nullptr;
    CueListView       *m_cueView         = nullptr;
    QSplitter         *m_mainSplit       = nullptr;
    InspectorPanel    *m_inspector       = nullptr;
    ActiveCuesPanel   *m_activeCues      = nullptr;
    VideoOutputWindow *m_videoOut        = nullptr;
    TextOutputWindow  *m_textOut         = nullptr;
    ImageOutputWindow *m_imageOut        = nullptr;
    CueInfoBar        *m_infoBar         = nullptr;
    QLabel            *m_statusLbl       = nullptr;
    QAction           *m_goAction        = nullptr;
    QPushButton       *m_goBtn           = nullptr;
    QAction           *m_stopAction      = nullptr;
    QAction           *m_firstCueAction  = nullptr;
    QMenu             *m_recentMenu      = nullptr;
    QUndoStack        *m_undoStack       = nullptr;
    WebServer         *m_webServer       = nullptr;
    QAction           *m_webAction        = nullptr;
    QAction           *m_videoAction      = nullptr;
    QAction           *m_textAction       = nullptr;
    QAction           *m_imageAction      = nullptr;
    QLabel            *m_webUrlLabel      = nullptr;
    QToolButton       *m_webCopyBtn       = nullptr;
    QLabel            *m_dropoutLabel     = nullptr;
    QString            m_currentWebUrl;
    bool               m_showMode         = false;
    QWidget           *m_showModeBtn      = nullptr;  // PillToggle*
    QShortcut         *m_showModeShortcut = nullptr;
    bool               m_ultraDark        = false;
    QWidget           *m_ultraDarkBtn     = nullptr;  // PillToggle*
    QString            m_normalQss;
    QToolBar          *m_toolBar          = nullptr;
    QList<int>         m_normalMainSplit;
    QList<int>         m_normalTopSplit;
    QVector<QToolButton*> m_cueAddBtns;
    QSplitter         *m_topSplit        = nullptr;
    QMap<QString, QShortcut*> m_addCueShortcuts;

    static constexpr int kMaxRecent = 8;

    void doUndoable(const QString &desc, std::function<void()> fn);
    void flushUndoPropChange();
    void addControlCueImpl(Cue::Type type, double extra = 0.0);

    // R3: le add*Cue() per le cue "semplici" (non di controllo, già servite
    // da addControlCueImpl) ripetevano la stessa logica di inserimento
    // (indice dalla selezione, doUndoable, numero, selezione della nuova
    // riga) differendo solo per il tipo concreto, il nome di default e
    // un'eventuale configurazione aggiuntiva.
    void addSimpleCue(const QString &undoLabel, const QString &defaultName,
                       const std::function<std::unique_ptr<Cue>()> &factory,
                       const std::function<void(Cue*)> &extraSetup = nullptr);

    // R6: toggleVideo/Text/ImageOutput() erano identiche a meno della
    // finestra e dell'azione di menu coinvolte.
    void toggleOutputWindow(QWidget *win, QAction *action);
    void showMultiControlDialog(const QVector<int> &visRows, Cue::Type type, double extra = 0.0);

    bool        m_programmaticSelect    = false;
    bool        m_suppressUndoTracking  = false;
    QTimer     *m_undoPropTimer         = nullptr;
    QJsonArray  m_undoLastPushed;

    void applyWebServer();
    void applyPanelLayout();
    void applyUltraDark(bool on);
    void applyOutputScreen();
};
