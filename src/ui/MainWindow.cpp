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
#include "engine/TextCue.h"
#include "TextOutputWindow.h"
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
#include <QTimer>
#include <QDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QShortcut>
#include <algorithm>

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::unique_ptr<ControlCue> makeCtrlCue(Cue::Type type, double extra) {
    switch (type) {
    case Cue::Type::Stop:        return std::make_unique<StopCue>();
    case Cue::Type::Fade: {
        auto c = std::make_unique<FadeCue>();
        c->setFadeDuration(AppSettings::instance().defaultFadeDuration());
        return c;
    }
    case Cue::Type::Pause:       return std::make_unique<PauseCue>();
    case Cue::Type::Play:        return std::make_unique<PlayCue>();
    case Cue::Type::Speed:       return std::make_unique<SpeedCue>(extra > 0.0 ? extra : 1.5);
    case Cue::Type::Effect:      return std::make_unique<EffectCue>();
    case Cue::Type::ResetEffect: return std::make_unique<ResetEffectCue>();
    default:                     return nullptr;
    }
}

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
    m_textOut   = new TextOutputWindow(this);
    m_undoStack = new QUndoStack(this);

    buildUi();
    buildMenus();
    buildToolBar();
    applyShortcuts();

    connect(&m_workspace, &Workspace::modifiedChanged,     this, &MainWindow::updateTitle);
    connect(&m_workspace, &Workspace::filePathChanged,     this, &MainWindow::updateTitle);
    connect(&m_workspace, &Workspace::projectSettingsChanged, this, &MainWindow::applyProjectSettings);
    connect(m_undoStack,  &QUndoStack::cleanChanged,       this, &MainWindow::updateTitle);

    applyProjectSettings();

    connect(m_workspace.cueList(), &CueList::cueAdded, this, [this](int index) {
        if (m_workspace.cueList()->cueAt(index)->type() == Cue::Type::Video)
            setupNewVideoCue(index);
    });

    connect(m_workspace.cueList(), &CueList::cueStateChanged, this, [this](int index, Cue::State state) {
        Cue *cue = m_workspace.cueList()->cueAt(index);
        if (!cue || cue->type() != Cue::Type::Text) return;
        if (state == Cue::State::Playing)
            m_textOut->showCue(static_cast<TextCue*>(cue));
        else if (state == Cue::State::Idle)
            m_textOut->clearText();
    });

    connect(m_workspace.cueList(), &CueList::playheadChanged, this, [this](int actualIdx) {
        const int visRow = m_model->visibleRowForActual(actualIdx);
        if (visRow >= 0) {
            m_programmaticSelect = true;
            m_cueView->selectRow(visRow);
            m_programmaticSelect = false;
        }
    });

    connect(m_model, &QAbstractTableModel::modelReset, this, [this]() {
        QTimer::singleShot(0, this, [this]() {
            const int visRow = m_model->visibleRowForActual(
                m_workspace.cueList()->playheadIndex());
            if (visRow >= 0) {
                m_programmaticSelect = true;
                m_cueView->selectRow(visRow);
                m_programmaticSelect = false;
            }
        });
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
    connect(m_cueView, &CueListView::addTextRequested,         this, &MainWindow::addTextCue);
    connect(m_cueView, &CueListView::addEffectRequested,       this, &MainWindow::addEffectCue);
    connect(m_cueView, &CueListView::addResetEffectRequested,  this, &MainWindow::addResetEffectCue);
    connect(m_cueView, &CueListView::deleteRequested,   this, &MainWindow::deleteSelectedCue);
    connect(m_cueView, &CueListView::groupToggleRequested, this, [this](int row) {
        m_model->toggleGroupAt(row);
    });
    connect(m_cueView, &CueListView::groupAssignRequested, this, [this](int cueVis, int grpVis) {
        Cue *cue = m_model->cueForRow(cueVis);
        Cue *grp = m_model->cueForRow(grpVis);
        if (!cue || !grp || grp->type() != Cue::Type::Group) return;
        doUndoable("Assegna a gruppo", [&] {
            cue->setParentGroupId(grp->id());
        });
    });
    connect(m_cueView, &CueListView::ungroupRequested, this, [this](int srcVis, int dstVis) {
        Cue *cue = m_model->cueForRow(srcVis);
        if (!cue || cue->parentGroupId().isEmpty()) return;
        const int from = m_model->actualRowForVisible(srcVis);
        const int to   = m_model->actualRowForVisible(dstVis);
        if (from < 0) return;
        doUndoable("Rimuovi da gruppo", [&] {
            cue->setParentGroupId({});
            if (to >= 0 && from != to)
                m_workspace.cueList()->moveCue(from, to);
        });
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

    connect(m_cueView, &CueListView::groupSelectionRequested,
            this, &MainWindow::groupSelectedCues);
    connect(m_cueView, &CueListView::multiGroupAssignRequested,
            this, [this](QVector<int> rows, int grpVis) {
        Cue *grp = m_model->cueForRow(grpVis);
        if (!grp || grp->type() != Cue::Type::Group) return;
        // Resolve cue pointers BEFORE the loop: each setParentGroupId triggers
        // a model reset that reshuffles visible row indices, so we can't use
        // visible indices inside the mutation loop.
        QVector<Cue*> cues;
        for (int vis : rows) {
            Cue *c = m_model->cueForRow(vis);
            if (c && c->type() != Cue::Type::Group)
                cues.append(c);
        }
        doUndoable("Aggiungi a gruppo", [&] {
            for (Cue *c : cues)
                c->setParentGroupId(grp->id());
        });
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
    menuAction(edit, "Aggiungi &Testo",            QKeySequence("Ctrl+Shift+T"), this, &MainWindow::addTextCue);
    edit->addSeparator();
    menuAction(edit, "Aggiungi cue E&ffetto",      QKeySequence("Ctrl+Shift+X"), this, &MainWindow::addEffectCue);
    menuAction(edit, "Aggiungi cue &Reset Effetti",QKeySequence("Ctrl+Shift+R"), this, &MainWindow::addResetEffectCue);
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

    QFont bigFont;
    bigFont.setPointSize(13);
    bigFont.setBold(true);

    m_goAction = tb->addAction("▶  GO");
    m_goAction->setFont(bigFont);
    m_goAction->setToolTip("Vai");
    connect(m_goAction, &QAction::triggered, this, &MainWindow::go);

    tb->addSeparator();

    m_stopAction = tb->addAction("■");
    m_stopAction->setFont(bigFont);
    m_stopAction->setToolTip("Ferma tutto");
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopAll);

    m_firstCueAction = tb->addAction("");
    m_firstCueAction->setToolTip("Torna alla prima cue");
    connect(m_firstCueAction, &QAction::triggered, this, &MainWindow::goToFirstCue);

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

    // ⏮ drawn as pixel icon: left bar + two left-pointing triangles
    m_firstCueAction->setIcon(makeTbIcon(QColor(0x44, 0x48, 0x58), [](QPainter &p) {
        p.drawRect(2, 3, 2, 14);                                          // left bar
        QPolygon t1; t1 << QPoint(11, 3) << QPoint(11, 17) << QPoint(5, 10);
        QPolygon t2; t2 << QPoint(18, 3) << QPoint(18, 17) << QPoint(12, 10);
        p.drawPolygon(t1);
        p.drawPolygon(t2);
    }));

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

    // Group: folder shape
    auto groupIcon = makeTbIcon(QColor(0x44, 0x48, 0x58), [](QPainter &p) {
        p.drawRect(1, 5, 7, 3);    // tab
        p.drawRect(1, 7, 18, 11);  // body
    });

    // Label: three horizontal lines (note lines)
    auto labelIcon = makeTbIcon(QColor(0x60, 0x55, 0x10), [](QPainter &p) {
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(Qt::NoBrush);
        p.drawLine(3, 6,  17, 6);
        p.drawLine(3, 10, 17, 10);
        p.drawLine(3, 14, 12, 14);
    });

    // Text: bold "T"
    auto textIcon = makeTbIcon(QColor(0x0a, 0x72, 0x8a), [](QPainter &p) {
        p.drawRect(2, 3, 16, 3);   // top bar
        p.drawRect(8, 3, 4, 13);   // stem
    });

    // Mic: capsule + stand
    auto micIcon = makeTbIcon(QColor(0xcc, 0x22, 0x88), [](QPainter &p) {
        p.setBrush(Qt::white);
        p.drawRoundedRect(7, 2, 6, 9, 3, 3);
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawArc(4, 6, 12, 8, 0, -180 * 16);
        p.drawLine(10, 14, 10, 17);
        p.drawLine(6, 17, 14, 17);
    });

    addCueBtn(audioIcon,     "+ Audio Cue",    &MainWindow::addAudioCue);
    addCueBtn(videoIcon,     "+ Video Cue",    &MainWindow::addVideoCue);
    addCueBtn(stopIcon,      "+ Stop Cue",     &MainWindow::addStopCue);
    addCueBtn(fadeIcon,      "+ Fade Cue",     &MainWindow::addFadeCue);
    addCueBtn(pauseIcon,     "+ Pause Cue",    &MainWindow::addPauseCue);
    addCueBtn(playCueIcon,   "+ Play Cue",     &MainWindow::addPlayCue);
    addCueBtn(micIcon,       "+ Mic Cue",      &MainWindow::addMicCue);
    addCueBtn(groupIcon,     "+ Gruppo",       &MainWindow::addGroupCue);
    addCueBtn(labelIcon,     "+ Etichetta",    &MainWindow::addLabelCue);
    addCueBtn(textIcon,      "+ Testo",        &MainWindow::addTextCue);
    addCueBtn(speedUpIcon,   "+ Velocizza",    &MainWindow::addSpeedUpCue);
    addCueBtn(speedDownIcon, "+ Rallenta",     &MainWindow::addSpeedDownCue);

    // Effect: lightning bolt (purple #7b359e)
    auto effectIcon = makeTbIcon(QColor(0x7b, 0x35, 0x9e), [](QPainter &p) {
        QPolygon bolt;
        bolt << QPoint(11, 2) << QPoint(5, 11) << QPoint(9, 11)
             << QPoint(7, 18) << QPoint(15, 9) << QPoint(11, 9);
        p.drawPolygon(bolt);
    });
    // ResetEffect: circular undo arrow (blue-gray #357b9e)
    auto resetEffectIcon = makeTbIcon(QColor(0x35, 0x7b, 0x9e), [](QPainter &p) {
        p.setPen(QPen(Qt::white, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawArc(3, 3, 14, 14, 50*16, 250*16);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QPolygon arr;
        arr << QPoint(3, 6) << QPoint(8, 2) << QPoint(8, 10);
        p.drawPolygon(arr);
    });

    addCueBtn(effectIcon,      "+ Effetto",      &MainWindow::addEffectCue);
    addCueBtn(resetEffectIcon, "+ Reset Effetti",&MainWindow::addResetEffectCue);

    tb->addSeparator();

    auto *videoWin = tb->addAction("📺 Video Out");
    connect(videoWin, &QAction::triggered, this, &MainWindow::toggleVideoOutput);
    auto *textWin = tb->addAction("📝 Text Out");
    connect(textWin, &QAction::triggered, this, [this]() {
        if (m_textOut->isVisible()) m_textOut->hide();
        else                        m_textOut->show();
    });
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

    // Safety net: select the new playhead row immediately if visible.
    // Covers cases where playheadChanged fired but the row was not yet visible.
    const int ph    = m_workspace.cueList()->playheadIndex();
    const int phVis = m_model->visibleRowForActual(ph);
    if (phVis >= 0) {
        m_programmaticSelect = true;
        m_cueView->selectRow(phVis);
        m_programmaticSelect = false;
    }
}

void MainWindow::stopAll() {
    m_workspace.cueList()->stopAll();
}

void MainWindow::goToFirstCue() {
    m_workspace.cueList()->setPlayhead(0);
    const int vis = m_model->visibleRowForActual(0);
    if (vis >= 0) {
        m_cueView->selectRow(vis);
        m_cueView->scrollTo(m_model->index(vis, 0));
    }
}

void MainWindow::applyShortcuts() {
    const auto &s = AppSettings::instance();
    m_goAction->setShortcut(s.keyGo());
    m_goAction->setToolTip(QString("Vai (%1)").arg(s.keyGo().toString()));
    m_stopAction->setShortcut(s.keyStopAll());
    m_stopAction->setToolTip(QString("Ferma tutto (%1)").arg(s.keyStopAll().toString()));
    m_firstCueAction->setShortcut(s.keyFirstCue());
    m_firstCueAction->setToolTip(QString("Torna alla prima cue (%1)").arg(s.keyFirstCue().toString()));
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

void MainWindow::addStopCue()    { addControlCueImpl(Cue::Type::Stop); }
void MainWindow::addFadeCue()    { addControlCueImpl(Cue::Type::Fade); }
void MainWindow::addPauseCue()   { addControlCueImpl(Cue::Type::Pause); }
void MainWindow::addPlayCue()    { addControlCueImpl(Cue::Type::Play); }

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

void MainWindow::addTextCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Testo Cue", [&] {
        auto cue = std::make_unique<TextCue>();
        cue->setNumber(nextCueNumber());
        cue->setName("Testo");
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::addSpeedUpCue()   { addControlCueImpl(Cue::Type::Speed, 1.5); }
void MainWindow::addSpeedDownCue() { addControlCueImpl(Cue::Type::Speed, 0.5); }
void MainWindow::addEffectCue()      { addControlCueImpl(Cue::Type::Effect); }
void MainWindow::addResetEffectCue() { addControlCueImpl(Cue::Type::ResetEffect); }

void MainWindow::setupNewVideoCue(int index) {
    auto *cue = qobject_cast<VideoCue*>(m_workspace.cueList()->cueAt(index));
    if (cue)
        cue->setVideoSink(m_videoOut->videoWidget()->videoSink());
}

void MainWindow::openSettings() {
    SettingsDialog dlg(&m_workspace, this);
    dlg.exec();
    applyProjectSettings();
    applyShortcuts();
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
    m_inspector->setCue(nullptr);
    m_infoBar->setCue(nullptr);

    // Collect and sort actual indices in descending order to avoid shift issues
    QVector<int> actuals;
    for (const auto &idx : sel) {
        const int a = m_model->actualRowForVisible(idx.row());
        if (a >= 0) actuals.append(a);
    }
    std::sort(actuals.begin(), actuals.end(), std::greater<int>());

    doUndoable(actuals.size() > 1 ? "Elimina cue selezionate" : "Elimina cue", [&] {
        for (int a : actuals)
            m_workspace.cueList()->removeCue(a);
    });
}

void MainWindow::addControlCueImpl(Cue::Type type, double extra) {
    const auto sel = m_cueView->selectionModel()->selectedRows();

    // Collect targetable rows (exclude Group, Label, Text)
    QVector<int> targetVisRows;
    for (const auto &idx : sel) {
        Cue *c = m_model->cueForRow(idx.row());
        if (c && c->type() != Cue::Type::Group
               && c->type() != Cue::Type::Label
               && c->type() != Cue::Type::Text)
            targetVisRows.append(idx.row());
    }
    if (targetVisRows.size() >= 2) {
        showMultiControlDialog(targetVisRows, type, extra);
        return;
    }

    const int idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    auto tmp = makeCtrlCue(type, extra);
    if (!tmp) return;
    const QString desc = "Aggiungi " + tmp->typeName() + " Cue";
    doUndoable(desc, [&] {
        auto cue = makeCtrlCue(type, extra);
        cue->setNumber(nextCueNumber());
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::showMultiControlDialog(const QVector<int> &visRows, Cue::Type type, double extra) {
    const int N = visRows.size();
    auto sample = makeCtrlCue(type, extra);
    if (!sample) return;
    const QString typeName = sample->typeName();

    auto *dlg = new QDialog(this, Qt::Dialog);
    dlg->setWindowTitle("Selezione multipla");
    dlg->setModal(true);
    auto *lay = new QVBoxLayout(dlg);
    lay->setSpacing(6);

    auto *title = new QLabel(
        QString("Creare %1 Cue per %2 cue selezionate?").arg(typeName).arg(N));
    QFont f = title->font();
    f.setPointSize(11);
    title->setFont(f);
    lay->addWidget(title);
    lay->addSpacing(4);

    auto makeBtn = [&](int num, const QString &text) {
        auto *row   = new QWidget;
        auto *rl    = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(8);
        auto *numLbl = new QLabel(QString::number(num) + ".");
        numLbl->setFixedWidth(18);
        auto *btn = new QPushButton(text);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        btn->setStyleSheet("text-align: left; padding: 5px 10px;");
        QObject::connect(btn, &QPushButton::clicked, dlg, [dlg, num]() { dlg->done(num); });
        rl->addWidget(numLbl);
        rl->addWidget(btn, 1);
        lay->addWidget(row);
        return btn;
    };

    makeBtn(1, QString("Aggiungi %1 %2 Cue dopo l'ultima selezionata").arg(N).arg(typeName));
    makeBtn(2, QString("Inserisci una %1 Cue dopo ogni cue selezionata").arg(typeName));
    makeBtn(3, QString("Aggiungi una %1 Cue senza target").arg(typeName));

    lay->addSpacing(4);
    auto *cancelBtn = new QPushButton("Annulla");
    QObject::connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    lay->addWidget(cancelBtn);

    auto *hint = new QLabel("I numeri 1–3 sono tasti rapidi.");
    hint->setStyleSheet("color: #888; font-size: 9pt;");
    lay->addWidget(hint);

    // Keyboard shortcuts
    for (int k = 1; k <= 3; ++k) {
        auto *sc = new QShortcut(QKeySequence(Qt::Key_0 + k), dlg);
        QObject::connect(sc, &QShortcut::activated, dlg, [dlg, k]() { dlg->done(k); });
    }

    const int choice = dlg->exec();
    delete dlg;
    if (choice <= 0) return;

    // Collect target IDs and display names BEFORE any insertions
    struct TargetInfo { int actualRow; QString id; QString displayName; };
    QVector<TargetInfo> targets;
    for (int vis : visRows) {
        Cue *c = m_model->cueForRow(vis);
        if (!c) continue;
        const QString name = c->name().isEmpty() ? c->number() : c->name();
        targets.append({ m_model->actualRowForVisible(vis), c->id(), name });
    }
    std::sort(targets.begin(), targets.end(), [](const auto &a, const auto &b) {
        return a.actualRow < b.actualRow;
    });

    if (choice == 1) {
        // Add N cues after the last selected
        const int insertBase = targets.last().actualRow + 1;
        doUndoable(QString("Aggiungi %1 %2 Cue").arg(N).arg(typeName), [&] {
            for (int i = 0; i < targets.size(); ++i) {
                auto cue = makeCtrlCue(type, extra);
                cue->setNumber(nextCueNumber());
                cue->setTargetId(targets[i].id);
                cue->setName(typeName + " " + targets[i].displayName);
                m_workspace.cueList()->addCue(std::move(cue), insertBase + i);
            }
        });
    } else if (choice == 2) {
        // Insert one after each selected cue (process in descending actual row order)
        QVector<TargetInfo> rev = targets;
        std::sort(rev.begin(), rev.end(), [](const auto &a, const auto &b) {
            return a.actualRow > b.actualRow;
        });
        doUndoable(QString("Inserisci %1 Cue per ognuna").arg(typeName), [&] {
            for (const auto &t : rev) {
                auto cue = makeCtrlCue(type, extra);
                cue->setNumber(nextCueNumber());
                cue->setTargetId(t.id);
                cue->setName(typeName + " " + t.displayName);
                m_workspace.cueList()->addCue(std::move(cue), t.actualRow + 1);
            }
        });
    } else if (choice == 3) {
        // Single cue with no target
        const auto sel = m_cueView->selectionModel()->selectedRows();
        const int idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.last().row()) + 1;
        doUndoable("Aggiungi " + typeName + " Cue", [&] {
            auto cue = makeCtrlCue(type, extra);
            cue->setNumber(nextCueNumber());
            m_workspace.cueList()->addCue(std::move(cue), idx);
        });
    }
    m_cueView->setFocus();
}

void MainWindow::groupSelectedCues(const QVector<int> &visRows) {
    if (visRows.size() < 2) return;

    // Convert visible → actual, sorted ascending
    QVector<int> actuals;
    for (int vis : visRows) {
        const int a = m_model->actualRowForVisible(vis);
        if (a >= 0) actuals.append(a);
    }
    if (actuals.size() < 2) return;
    std::sort(actuals.begin(), actuals.end());

    doUndoable("Raggruppa cue selezionate", [&] {
        auto grp = std::make_unique<GroupCue>();
        grp->setNumber(nextCueNumber());
        grp->setName("Gruppo");
        const QString grpId = grp->id();
        const int insertAt  = actuals.first();
        m_workspace.cueList()->addCue(std::move(grp), insertAt);
        // All actual rows >= insertAt shifted by 1 after the insert
        for (int &a : actuals)
            if (a >= insertAt) ++a;
        for (int a : actuals) {
            Cue *c = m_workspace.cueList()->cueAt(a);
            if (c && c->type() != Cue::Type::Group)
                c->setParentGroupId(grpId);
        }
    });
    m_cueView->setFocus();
}

// ── Selection ─────────────────────────────────────────────────────────────────

void MainWindow::onSelectionChanged() {
    if (m_programmaticSelect) return;
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
