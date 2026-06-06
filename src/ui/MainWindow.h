#pragma once
#include <QMainWindow>
#include <QUndoStack>
#include <functional>
#include "engine/Workspace.h"
#include "ui/VideoOutputWindow.h"
#include "ui/TextOutputWindow.h"
#include "ui/CueInfoBar.h"

class CueListModel;
class CueListView;
class InspectorPanel;
class ActiveCuesPanel;
class QLabel;
class QAction;
class QMenu;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event)  override;
    void keyPressEvent(QKeyEvent *event) override;

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
    void addSpeedUpCue();
    void addSpeedDownCue();
    void addPlayCue();
    void deleteSelectedCue();
    void go();
    void stopAll();
    void toggleVideoOutput();
    void openSettings();
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
    InspectorPanel    *m_inspector       = nullptr;
    ActiveCuesPanel   *m_activeCues      = nullptr;
    VideoOutputWindow *m_videoOut        = nullptr;
    TextOutputWindow  *m_textOut         = nullptr;
    CueInfoBar        *m_infoBar         = nullptr;
    QLabel            *m_statusLbl       = nullptr;
    QAction           *m_goAction        = nullptr;
    QAction           *m_stopAction      = nullptr;
    QMenu             *m_recentMenu      = nullptr;
    QUndoStack        *m_undoStack       = nullptr;

    static constexpr int kMaxRecent = 8;

    void doUndoable(const QString &desc, std::function<void()> fn);
};
