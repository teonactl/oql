#pragma once
#include <QMainWindow>
#include <QUndoStack>
#include <QVector>
#include <QMap>
#include <QString>
#include <QJsonArray>
#include <QTimer>
#include <functional>
#include "engine/Workspace.h"
#include "engine/Cue.h"
#include "ui/VideoOutputWindow.h"
#include "ui/TextOutputWindow.h"
#include "ui/CueInfoBar.h"

class CueListModel;
class CueListView;
class InspectorPanel;
class ActiveCuesPanel;
class WebServer;
class QLabel;
class QAction;
class QMenu;
class QSplitter;
class QToolButton;
class QPushButton;
class QShortcut;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event)  override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event)      override;

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
    void groupSelectedCues(const QVector<int> &visRows);
    void go();
    void stopAll();
    void goToFirstCue();
    void applyShortcuts();
    void toggleVideoOutput();
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
    void addToRecentFiles(const QString &path);
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
    QLabel            *m_webUrlLabel      = nullptr;
    bool               m_showMode         = false;
    QToolButton       *m_showModeBtn      = nullptr;
    QShortcut         *m_showModeShortcut = nullptr;
    QVector<QToolButton*> m_cueAddBtns;
    QSplitter         *m_topSplit        = nullptr;
    QMap<QString, QShortcut*> m_addCueShortcuts;

    static constexpr int kMaxRecent = 8;

    void doUndoable(const QString &desc, std::function<void()> fn);
    void flushUndoPropChange();
    void addControlCueImpl(Cue::Type type, double extra = 0.0);
    void showMultiControlDialog(const QVector<int> &visRows, Cue::Type type, double extra = 0.0);

    bool        m_programmaticSelect    = false;
    bool        m_suppressUndoTracking  = false;
    QTimer     *m_undoPropTimer         = nullptr;
    QJsonArray  m_undoLastPushed;

    void applyWebServer();
    void applyPanelLayout();
};
