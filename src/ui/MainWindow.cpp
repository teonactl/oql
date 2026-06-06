#include "MainWindow.h"
#include "CueListModel.h"
#include "CueListView.h"
#include "InspectorPanel.h"
#include "ActiveCuesPanel.h"
#include "CueInfoBar.h"
#include "SettingsDialog.h"
#include "engine/AudioCue.h"
#include "engine/VideoCue.h"
#include "engine/ControlCues.h"
#include "engine/MicCue.h"
#include "engine/GroupCue.h"
#include "engine/LabelCue.h"
#include "engine/AppSettings.h"
#include <QApplication>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QSplitter>
#include <QScrollArea>
#include <QFrame>
#include <QVBoxLayout>
#include <QLabel>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QVideoWidget>
#include <QFileInfo>
#include <QSettings>
#include <QPainter>
#include <QToolButton>
#include <QUndoStack>

// ── Undo/redo ─────────────────────────────────────────────────────────────────

class SnapshotCommand : public QUndoCommand {
    CueList        *m_list;
    QJsonArray      m_before, m_after;
    std::function<void()> m_pre;   // called BEFORE fromJson (clear inspector)
    std::function<void()> m_post;  // called AFTER  fromJson (reassign sinks)
    bool            m_firstRedo = true;
public:
    SnapshotCommand(const QString &text, CueList *list,
                    QJsonArray before, QJsonArray after,
                    std::function<void()> pre,
                    std::function<void()> post)
        : QUndoCommand(text), m_list(list),
          m_before(std::move(before)), m_after(std::move(after)),
          m_pre(std::move(pre)), m_post(std::move(post)) {}

    void undo() override { m_pre(); m_list->fromJson(m_before); m_post(); }
    void redo() override {
        if (m_firstRedo) { m_firstRedo = false; return; }
        m_pre(); m_list->fromJson(m_after); m_post();
    }
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("OpenQLab");
    resize(1100, 650);

    m_videoOut  = new VideoOutputWindow(this);
    m_undoStack = new QUndoStack(this);

    buildUi();
    buildMenus();
    buildToolBar();

    connect(&m_workspace, &Workspace::modifiedChanged,     this, &MainWindow::updateTitle);
    connect(&m_workspace, &Workspace::filePathChanged,     this, &MainWindow::updateTitle);
    connect(&m_workspace, &Workspace::projectSettingsChanged, this, &MainWindow::applyProjectSettings);
    connect(m_undoStack,  &QUndoStack::cleanChanged,       this, &MainWindow::updateTitle);

    applyProjectSettings();

    connect(m_workspace.cueList(), &CueList::cueAdded, this, [this](int index) {
        if (m_workspace.cueList()->cueAt(index)->type() == Cue::Type::Video)
            setupNewVideoCue(index);
    });

    updateTitle();
}

void MainWindow::buildUi() {
    m_model      = new CueListModel(m_workspace.cueList(), this);
    m_cueView    = new CueListView(m_model, this);
    m_inspector  = new InspectorPanel(m_workspace.cueList(), this);
    m_infoBar    = new CueInfoBar(this);
    m_activeCues = new ActiveCuesPanel(m_workspace.cueList(), this);

    // Top: active cues panel (left) + cue list (center/right)
    auto *topSplit = new QSplitter(Qt::Horizontal);
    topSplit->addWidget(m_activeCues);
    topSplit->addWidget(m_cueView);
    topSplit->setStretchFactor(0, 0);
    topSplit->setStretchFactor(1, 1);
    topSplit->setHandleWidth(3);
    topSplit->setSizes({230, 800});

    // Inspector in a scroll area at bottom (for narrow windows)
    auto *inspScroll = new QScrollArea;
    inspScroll->setWidgetResizable(true);
    inspScroll->setFrameShape(QFrame::NoFrame);
    inspScroll->setWidget(m_inspector);
    inspScroll->setMinimumHeight(260);
    inspScroll->setMaximumHeight(400);

    // Vertical split: content area (top) + inspector (bottom)
    auto *mainSplit = new QSplitter(Qt::Vertical);
    mainSplit->addWidget(topSplit);
    mainSplit->addWidget(inspScroll);
    mainSplit->setStretchFactor(0, 1);
    mainSplit->setStretchFactor(1, 0);
    mainSplit->setHandleWidth(4);

    auto *central = new QWidget;
    auto *vlay    = new QVBoxLayout(central);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);
    vlay->addWidget(mainSplit, 1);
    vlay->addWidget(m_infoBar);
    setCentralWidget(central);

    m_statusLbl = new QLabel("Pronto");
    statusBar()->addWidget(m_statusLbl);

    connect(m_cueView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onSelectionChanged);
    connect(m_cueView, &CueListView::addAudioRequested, this, &MainWindow::addAudioCue);
    connect(m_cueView, &CueListView::addVideoRequested, this, &MainWindow::addVideoCue);
    connect(m_cueView, &CueListView::addStopRequested,  this, &MainWindow::addStopCue);
    connect(m_cueView, &CueListView::addFadeRequested,  this, &MainWindow::addFadeCue);
    connect(m_cueView, &CueListView::addPauseRequested, this, &MainWindow::addPauseCue);
    connect(m_cueView, &CueListView::addMicRequested,   this, &MainWindow::addMicCue);
    connect(m_cueView, &CueListView::addGroupRequested, this, &MainWindow::addGroupCue);
    connect(m_cueView, &CueListView::addLabelRequested, this, &MainWindow::addLabelCue);
    connect(m_cueView, &CueListView::deleteRequested,   this, &MainWindow::deleteSelectedCue);
    connect(m_cueView, &CueListView::groupToggleRequested, this, [this](int row) {
        m_model->toggleGroupAt(row);
    });
    connect(m_cueView, &CueListView::moveRequested, this, [this](int fromVis, int toVis) {
        const int from = m_model->actualRowForVisible(fromVis);
        const int to   = m_model->actualRowForVisible(toVis);
        if (from < 0 || to < 0) return;
        doUndoable("Sposta cue", [&] {
            m_workspace.cueList()->moveCue(from, to);
        });
    });
    connect(m_cueView, &CueListView::targetAssignRequested, this, [this](int srcRow, int ctrlRow) {
        Cue *src  = m_model->cueForRow(srcRow);
        Cue *ctrl = m_model->cueForRow(ctrlRow);
        if (!src || !ctrl) return;
        if (auto *cc = dynamic_cast<ControlCue*>(ctrl)) {
            doUndoable("Assegna target", [&] {
                cc->setTargetId(src->id());
                const QString tgtName = src->name().isEmpty() ? src->number() : src->name();
                ctrl->setName(ctrl->typeName() + " " + tgtName);
            });
        }
    });
    connect(m_cueView, &QAbstractItemView::clicked, this, [this](const QModelIndex &idx) {
        if (idx.column() != CueListModel::ColContinue) return;
        Cue *cue = m_model->cueForRow(idx.row());
        if (!cue) return;
        if      (cue->autoContinue()) { cue->setAutoContinue(false); cue->setAutoFollow(true); }
        else if (cue->autoFollow())   { cue->setAutoFollow(false); }
        else                          { cue->setAutoContinue(true); }
    });

    connect(m_cueView, &CueListView::cueDoubleClicked, this, [this](int row) {
        Cue *cue = m_model->cueForRow(row);
        if (!cue) return;
        if (cue->state() == Cue::State::Playing)
            cue->stop();
        else
            cue->go();
    });

    connect(m_cueView, &CueListView::filePickRequested, this, [this](int row) {
        Cue *cue = m_model->cueForRow(row);
        if (!cue) return;

        QString filter;
        if (cue->type() == Cue::Type::Audio)
            filter = "Audio (*.mp3 *.wav *.flac *.ogg *.aac *.m4a *.opus);;Tutti i file (*)";
        else if (cue->type() == Cue::Type::Video)
            filter = "Video (*.mp4 *.mkv *.avi *.mov *.webm *.m4v);;Tutti i file (*)";
        else
            return;

        const QString path = QFileDialog::getOpenFileName(this, "Seleziona file", {}, filter);
        if (path.isEmpty()) return;

        doUndoable("Assegna file", [&] {
            if (auto *ac = dynamic_cast<AudioCue*>(cue))
                ac->setFilePath(path);
            else if (auto *vc = dynamic_cast<VideoCue*>(cue))
                vc->setFilePath(path);
        });
    });
}

static QAction* menuAction(QMenu *menu, const QString &text,
                            const QKeySequence &key, auto *recv, auto slot) {
    auto *a = new QAction(text, menu);
    a->setShortcut(key);
    QObject::connect(a, &QAction::triggered, recv, slot);
    menu->addAction(a);
    return a;
}

void MainWindow::buildMenus() {
    // File
    auto *file = menuBar()->addMenu("&File");
    menuAction(file, "&Nuovo",       QKeySequence::New,    this, &MainWindow::newWorkspace);
    menuAction(file, "&Apri…",       QKeySequence::Open,   this, &MainWindow::openWorkspace);
    m_recentMenu = file->addMenu("Apri &recenti");
    rebuildRecentMenu();
    file->addSeparator();
    menuAction(file, "&Salva",       QKeySequence::Save,   this, &MainWindow::saveWorkspace);
    menuAction(file, "Salva &come…", QKeySequence::SaveAs, this, &MainWindow::saveWorkspaceAs);
    file->addSeparator();
    menuAction(file, "&Esci",        QKeySequence::Quit,   qApp, &QApplication::quit);

    // Modifica
    auto *edit = menuBar()->addMenu("&Modifica");

    auto *undoAct = m_undoStack->createUndoAction(this, "&Annulla");
    undoAct->setShortcut(QKeySequence::Undo);
    edit->addAction(undoAct);

    auto *redoAct = m_undoStack->createRedoAction(this, "&Ripeti");
    redoAct->setShortcut(QKeySequence("Ctrl+Shift+Z"));
    edit->addAction(redoAct);

    edit->addSeparator();

    menuAction(edit, "Aggiungi &Audio Cue", QKeySequence("Ctrl+Shift+A"), this, &MainWindow::addAudioCue);
    menuAction(edit, "Aggiungi &Video Cue", QKeySequence("Ctrl+Shift+V"), this, &MainWindow::addVideoCue);
    edit->addSeparator();
    menuAction(edit, "Aggiungi &Stop Cue",         QKeySequence("Ctrl+Shift+S"), this, &MainWindow::addStopCue);
    menuAction(edit, "Aggiungi &Fade Cue",         QKeySequence("Ctrl+Shift+F"), this, &MainWindow::addFadeCue);
    menuAction(edit, "Aggiungi &Pause Cue",        QKeySequence("Ctrl+Shift+P"), this, &MainWindow::addPauseCue);
    menuAction(edit, "Aggiungi cue &Velocizza",    QKeySequence("Ctrl+Shift+U"), this, &MainWindow::addSpeedUpCue);
    menuAction(edit, "Aggiungi cue &Rallenta",     QKeySequence("Ctrl+Shift+D"), this, &MainWindow::addSpeedDownCue);
    menuAction(edit, "Aggiungi cue &Play",         QKeySequence("Ctrl+Shift+L"), this, &MainWindow::addPlayCue);
    menuAction(edit, "Aggiungi cue &Microfono",    QKeySequence("Ctrl+Shift+M"), this, &MainWindow::addMicCue);
    edit->addSeparator();
    menuAction(edit, "Aggiungi &Gruppo",           QKeySequence("Ctrl+Shift+G"), this, &MainWindow::addGroupCue);
    menuAction(edit, "Aggiungi &Etichetta",        QKeySequence("Ctrl+Shift+E"), this, &MainWindow::addLabelCue);
    edit->addSeparator();
    menuAction(edit, "&Elimina cue", QKeySequence::Delete, this, &MainWindow::deleteSelectedCue);

    // Finestra
    auto *win = menuBar()->addMenu("&Finestra");
    menuAction(win, "Mostra/nascondi output video", QKeySequence("Ctrl+W"), this, &MainWindow::toggleVideoOutput);

    // Strumenti
    auto *tools = menuBar()->addMenu("&Strumenti");
    menuAction(tools, "&Impostazioni…", QKeySequence("Ctrl+,"), this, &MainWindow::openSettings);
}

void MainWindow::buildToolBar() {
    auto *tb = addToolBar("Principale");
    tb->setMovable(false);
    tb->setIconSize({20, 20});

    m_goAction = tb->addAction("▶  GO");
    m_goAction->setShortcut(Qt::Key_Space);
    m_goAction->setToolTip("Vai (Spazio)");
    auto goFont = m_goAction->font();
    goFont.setPointSize(13);
    goFont.setBold(true);
    m_goAction->setFont(goFont);
    connect(m_goAction, &QAction::triggered, this, &MainWindow::go);

    tb->addSeparator();

    m_stopAction = tb->addAction("■  Stop All");
    m_stopAction->setShortcut(Qt::Key_Escape);
    m_stopAction->setToolTip("Ferma tutto (Esc)");
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopAll);

    tb->addSeparator();

    auto makeTbIcon = [](const QColor &bg, auto drawFn) -> QIcon {
        QPixmap px(20, 20);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(0, 0, 20, 20, 4, 4);
        p.setBrush(Qt::white);
        drawFn(p);
        return QIcon(px);
    };

    // Audio: play triangle
    auto audioIcon = makeTbIcon(QColor(0x2a, 0x6d, 0xcc), [](QPainter &p) {
        QPolygon t; t << QPoint(5,3) << QPoint(5,17) << QPoint(16,10); p.drawPolygon(t);
    });
    // Video: frame + play triangle
    auto videoIcon = makeTbIcon(QColor(0x2a, 0x88, 0x44), [](QPainter &p) {
        p.setBrush(Qt::NoBrush); p.setPen(QPen(Qt::white, 1.5));
        p.drawRoundedRect(2,4,16,12,2,2);
        p.setPen(Qt::NoPen); p.setBrush(Qt::white);
        QPolygon t; t << QPoint(7,6) << QPoint(7,14) << QPoint(14,10); p.drawPolygon(t);
    });
    // Stop: filled square
    auto stopIcon = makeTbIcon(QColor(0xcc, 0x33, 0x33), [](QPainter &p) {
        p.drawRect(4,4,12,12);
    });
    // Fade: fade envelope (right triangle)
    auto fadeIcon = makeTbIcon(QColor(0xcc, 0x77, 0x22), [](QPainter &p) {
        QPolygon e; e << QPoint(3,17) << QPoint(17,3) << QPoint(17,17); p.drawPolygon(e);
    });
    // Pause: two vertical bars
    auto pauseIcon = makeTbIcon(QColor(0x88, 0x55, 0xcc), [](QPainter &p) {
        p.drawRect(4,4,5,12); p.drawRect(11,4,5,12);
    });
    // SpeedUp: two forward triangles >>
    auto speedUpIcon = makeTbIcon(QColor(0x11, 0x99, 0x99), [](QPainter &p) {
        QPolygon t1, t2;
        t1 << QPoint(2,5) << QPoint(2,15) << QPoint(9,10);
        t2 << QPoint(10,5) << QPoint(10,15) << QPoint(17,10);
        p.drawPolygon(t1); p.drawPolygon(t2);
        p.drawRect(18,5,2,10);
    });
    // SpeedDown: two back triangles <<
    auto speedDownIcon = makeTbIcon(QColor(0x11, 0x77, 0x77), [](QPainter &p) {
        QPolygon t1, t2;
        t1 << QPoint(18,5) << QPoint(18,15) << QPoint(11,10);
        t2 << QPoint(10,5) << QPoint(10,15) << QPoint(3,10);
        p.drawPolygon(t1); p.drawPolygon(t2);
        p.drawRect(0,5,2,10);
    });

    auto addCueBtn = [&](const QIcon &icon, const QString &tooltip, auto slot) {
        auto *btn = new QToolButton;
        btn->setIcon(icon);
        btn->setIconSize({20, 20});
        btn->setToolTip(tooltip);
        connect(btn, &QToolButton::clicked, this, slot);
        tb->addWidget(btn);
    };

    // Play: |▶ (green #22aa55)
    auto playCueIcon = makeTbIcon(QColor(0x22, 0xaa, 0x55), [](QPainter &p) {
        p.drawRect(2, 3, 3, 14);   // vertical bar
        QPolygon t; t << QPoint(7,3) << QPoint(7,17) << QPoint(17,10); p.drawPolygon(t);
    });

    addCueBtn(audioIcon,     "+ Audio Cue",    &MainWindow::addAudioCue);
    addCueBtn(videoIcon,     "+ Video Cue",    &MainWindow::addVideoCue);
    addCueBtn(stopIcon,      "+ Stop Cue",     &MainWindow::addStopCue);
    addCueBtn(fadeIcon,      "+ Fade Cue",     &MainWindow::addFadeCue);
    addCueBtn(pauseIcon,     "+ Pause Cue",    &MainWindow::addPauseCue);
    addCueBtn(playCueIcon,   "+ Play Cue",     &MainWindow::addPlayCue);
    addCueBtn({},            "+ Mic Cue",      &MainWindow::addMicCue);
    addCueBtn(speedUpIcon,   "+ Velocizza",    &MainWindow::addSpeedUpCue);
    addCueBtn(speedDownIcon, "+ Rallenta",     &MainWindow::addSpeedDownCue);

    tb->addSeparator();

    auto *videoWin = tb->addAction("📺 Video Out");
    connect(videoWin, &QAction::triggered, this, &MainWindow::toggleVideoOutput);
}

// ── Transport ─────────────────────────────────────────────────────────────────

void MainWindow::go() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    if (!sel.isEmpty()) {
        const int actual = m_model->actualRowForVisible(sel.first().row());
        Cue *selCue = actual >= 0 ? m_workspace.cueList()->cueAt(actual) : nullptr;
        if (!selCue || selCue->state() == Cue::State::Idle)
            m_workspace.cueList()->setPlayhead(actual >= 0 ? actual : 0);
    }
    m_workspace.cueList()->go();
}

void MainWindow::stopAll() {
    m_workspace.cueList()->stopAll();
}

// ── Cue management ────────────────────────────────────────────────────────────

void MainWindow::addAudioCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Audio Cue", [&] {
        auto cue = std::make_unique<AudioCue>();
        cue->setNumber(nextCueNumber());
        cue->setFadeInDuration(AppSettings::instance().defaultFadeInDuration());
        cue->setFadeOutDuration(AppSettings::instance().defaultFadeOutDuration());
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::addVideoCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Video Cue", [&] {
        auto cue = std::make_unique<VideoCue>();
        cue->setNumber(nextCueNumber());
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::addStopCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Stop Cue", [&] {
        auto cue = std::make_unique<StopCue>();
        cue->setNumber(nextCueNumber());
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::addFadeCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Fade Cue", [&] {
        auto cue = std::make_unique<FadeCue>();
        cue->setNumber(nextCueNumber());
        cue->setFadeDuration(AppSettings::instance().defaultFadeDuration());
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::addPauseCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Pause Cue", [&] {
        auto cue = std::make_unique<PauseCue>();
        cue->setNumber(nextCueNumber());
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::addPlayCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Play Cue", [&] {
        auto cue = std::make_unique<PlayCue>();
        cue->setNumber(nextCueNumber());
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::addMicCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Microfono Cue", [&] {
        auto cue = std::make_unique<MicCue>();
        cue->setNumber(nextCueNumber());
        cue->setName("Microfono");
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::addGroupCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Gruppo", [&] {
        auto cue = std::make_unique<GroupCue>();
        cue->setNumber(nextCueNumber());
        cue->setName("Gruppo");
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::addLabelCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Etichetta", [&] {
        auto cue = std::make_unique<LabelCue>();
        cue->setNumber(nextCueNumber());
        cue->setName("Etichetta");
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::addSpeedUpCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Velocizza Cue", [&] {
        auto cue = std::make_unique<SpeedCue>(1.5);
        cue->setNumber(nextCueNumber());
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::addSpeedDownCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Rallenta Cue", [&] {
        auto cue = std::make_unique<SpeedCue>(0.5);
        cue->setNumber(nextCueNumber());
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::setupNewVideoCue(int index) {
    auto *cue = qobject_cast<VideoCue*>(m_workspace.cueList()->cueAt(index));
    if (cue)
        cue->setVideoSink(m_videoOut->videoWidget()->videoSink());
}

void MainWindow::openSettings() {
    SettingsDialog dlg(&m_workspace, this);
    dlg.exec();
    applyProjectSettings();
}

void MainWindow::applyProjectSettings() {
    m_cueView->setColumnHidden(CueListModel::ColNumber, !m_workspace.showCueNumbers());
}

QString MainWindow::nextCueNumber() {

    if (!AppSettings::instance().autoNumberNewCues()) return {};
    int maxNum = 0;
    const auto *list = m_workspace.cueList();
    for (int i = 0; i < list->count(); ++i) {
        bool ok;
        const int n = list->cueAt(i)->number().toInt(&ok);
        if (ok && n > maxNum) maxNum = n;
    }
    return QString::number(maxNum + 1);
}

void MainWindow::assignVideoSinkToAll() {
    auto *list = m_workspace.cueList();
    for (int i = 0; i < list->count(); ++i) {
        auto *cue = qobject_cast<VideoCue*>(list->cueAt(i));
        if (cue)
            cue->setVideoSink(m_videoOut->videoWidget()->videoSink());
    }
}

void MainWindow::deleteSelectedCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    const int actual = m_model->actualRowForVisible(sel.first().row());
    if (actual < 0) return;
    m_inspector->setCue(nullptr);
    m_infoBar->setCue(nullptr);
    doUndoable("Elimina cue", [&] {
        m_workspace.cueList()->removeCue(actual);
    });
}

// ── Selection ─────────────────────────────────────────────────────────────────

void MainWindow::onSelectionChanged() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    if (!sel.isEmpty()) {
        const int actual = m_model->actualRowForVisible(sel.first().row());
        if (actual >= 0)
            m_workspace.cueList()->setPlayhead(actual);
    }
    Cue *cue = sel.isEmpty() ? nullptr : m_model->cueForRow(sel.first().row());
    m_inspector->setCue(cue);
    m_infoBar->setCue(cue);
}

// ── File I/O ──────────────────────────────────────────────────────────────────

void MainWindow::newWorkspace() {
    if (!confirmUnsaved()) return;
    m_workspace.reset();
    m_inspector->setCue(nullptr);
    m_undoStack->clear();
}

void MainWindow::openWorkspace() {
    if (!confirmUnsaved()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, "Apri workspace", {}, "OpenQLab (*.oqlab);;JSON (*.json)");
    if (path.isEmpty()) return;
    if (!m_workspace.load(path)) {
        QMessageBox::warning(this, "Errore", "Impossibile aprire il file.");
        return;
    }
    assignVideoSinkToAll();
    m_inspector->setCue(nullptr);
    m_undoStack->clear();
    addToRecentFiles(path);
}

bool MainWindow::saveWorkspace() {
    if (m_workspace.filePath().isEmpty()) return saveWorkspaceAs();
    const bool ok = m_workspace.save();
    if (ok) m_undoStack->setClean();
    return ok;
}

bool MainWindow::saveWorkspaceAs() {
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(
        this, "Salva workspace", {},
        "OpenQLab (*.oqlab);;JSON (*.json)",
        &selectedFilter);
    if (path.isEmpty()) return false;

    const QFileInfo fi(path);
    if (fi.suffix().isEmpty())
        path += selectedFilter.contains("json") ? ".json" : ".oqlab";

    const bool ok = m_workspace.save(path);
    if (ok) {
        m_undoStack->setClean();
        addToRecentFiles(path);
    }
    return ok;
}

// ── Video output ──────────────────────────────────────────────────────────────

void MainWindow::toggleVideoOutput() {
    m_videoOut->setVisible(!m_videoOut->isVisible());
}

// ── Helpers ───────────────────────────────────────────────────────────────────

bool MainWindow::confirmUnsaved() {
    if (!m_workspace.isModified()) return true;
    const auto ans = QMessageBox::question(
        this, "Modifiche non salvate",
        "Ci sono modifiche non salvate. Vuoi salvarle prima di continuare?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (ans == QMessageBox::Save)    return saveWorkspace();
    if (ans == QMessageBox::Discard) return true;
    return false;
}

void MainWindow::doUndoable(const QString &desc, std::function<void()> fn) {
    QJsonArray before = m_workspace.cueList()->toJson();
    fn();
    QJsonArray after = m_workspace.cueList()->toJson();
    if (before == after) return;
    m_undoStack->push(new SnapshotCommand(
        desc,
        m_workspace.cueList(),
        std::move(before),
        std::move(after),
        // pre: clear inspector BEFORE cues are destroyed by fromJson
        [this] {
            m_inspector->setCue(nullptr);
            m_infoBar->setCue(nullptr);
        },
        // post: reassign video sinks to the newly created cue objects
        [this] {
            assignVideoSinkToAll();
        }
    ));
}

void MainWindow::addToRecentFiles(const QString &path) {
    QSettings s("OpenQLab", "OpenQLab");
    QStringList recent = s.value("recentFiles").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > kMaxRecent) recent.removeLast();
    s.setValue("recentFiles", recent);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    QSettings s("OpenQLab", "OpenQLab");
    const QStringList recent = s.value("recentFiles").toStringList();
    for (const QString &path : recent) {
        const QString label = QFileInfo(path).fileName();
        m_recentMenu->addAction(label, this, [this, path]() { openRecentFile(path); });
    }
    if (recent.isEmpty())
        m_recentMenu->addAction("(nessun file recente)")->setEnabled(false);
    m_recentMenu->addSeparator();
    m_recentMenu->addAction("Svuota recenti", this, [this]() {
        QSettings("OpenQLab", "OpenQLab").setValue("recentFiles", QStringList{});
        rebuildRecentMenu();
    });
}

void MainWindow::openRecentFile(const QString &path) {
    if (!confirmUnsaved()) return;
    if (!m_workspace.load(path)) {
        QMessageBox::warning(this, "Errore", "Impossibile aprire il file:\n" + path);
        QSettings s("OpenQLab", "OpenQLab");
        QStringList recent = s.value("recentFiles").toStringList();
        recent.removeAll(path);
        s.setValue("recentFiles", recent);
        rebuildRecentMenu();
        return;
    }
    assignVideoSinkToAll();
    m_inspector->setCue(nullptr);
    m_undoStack->clear();
    addToRecentFiles(path);
}

void MainWindow::updateTitle() {
    QString title = "OpenQLab";
    const QString fp = m_workspace.filePath();
    if (!fp.isEmpty()) title += " — " + QFileInfo(fp).fileName();
    if (!m_undoStack->isClean() || m_workspace.isModified()) title += " *";
    setWindowTitle(title);
}

// ── Events ────────────────────────────────────────────────────────────────────

void MainWindow::closeEvent(QCloseEvent *event) {
    if (confirmUnsaved()) event->accept();
    else                  event->ignore();
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    // Spacebar go is handled by QAction shortcut — pass others through
    QMainWindow::keyPressEvent(event);
}
