#include "MainWindow.h"
#include "CueListModel.h"
#include "CueListView.h"
#include "InspectorPanel.h"
#include "ActiveCuesPanel.h"
#include "CueInfoBar.h"
#include "SettingsDialog.h"
#include "AboutDialog.h"
#include "WebServer.h"
#include "engine/AudioCue.h"
#include "engine/VideoCue.h"
#include "engine/ControlCues.h"
#include "engine/MicCue.h"
#include "engine/GroupCue.h"
#include "engine/LabelCue.h"
#include "engine/TextCue.h"
#ifndef OQL_BASE
#include "engine/ScriptCue.h"
#include "engine/RecordCue.h"
#endif
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
#include <QEvent>
#include <QKeyEvent>
#include <QVideoWidget>
#include <QFileInfo>
#include <QSettings>
#include <QPainter>
#include <QPainterPath>
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
#ifndef OQL_BASE
    case Cue::Type::Speed:       return std::make_unique<SpeedCue>(extra > 0.0 ? extra : 1.5);
    case Cue::Type::Effect:      return std::make_unique<EffectCue>();
    case Cue::Type::ResetEffect: return std::make_unique<ResetEffectCue>();
#endif
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
    setWindowTitle("OQL");
    resize(1440, 900);
    setWindowState(Qt::WindowMaximized);

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

    // Web remote server
    m_webServer = new WebServer(m_workspace.cueList(), this);
    connect(m_webServer, &WebServer::started, this, [this](quint16) {
        const QString url = m_webServer->localUrl();
        m_webUrlLabel->setText("  🌐 " + url);
        m_webUrlLabel->show();
        const QSignalBlocker b(m_webAction);
        m_webAction->setChecked(true);
        QMessageBox::information(this, "Web Remote",
            "Server remoto avviato!\n\nConnettiti da smartphone o tablet:\n" + url);
    });
    connect(m_webServer, &WebServer::stopped, this, [this]() {
        m_webUrlLabel->hide();
        const QSignalBlocker b(m_webAction);
        m_webAction->setChecked(false);
    });
    connect(m_webServer, &WebServer::errorOccurred, this, [this](const QString &msg) {
        QMessageBox::warning(this, "Web Remote", "Impossibile avviare il server:\n" + msg);
        const QSignalBlocker b(m_webAction);
        m_webAction->setChecked(false);
    });
    if (AppSettings::instance().webEnabled())
        m_webServer->start(AppSettings::instance().webPort());

    connect(m_workspace.cueList(), &CueList::cueAdded, this, [this](int index) {
        if (m_workspace.cueList()->cueAt(index)->type() == Cue::Type::Video)
            setupNewVideoCue(index);
    });

    connect(m_workspace.cueList(), &CueList::cueStateChanged, this, [this](int index, Cue::State state) {
        Cue *cue = m_workspace.cueList()->cueAt(index);
        if (!cue) return;
        if (cue->type() == Cue::Type::Text) {
            if (state == Cue::State::Playing)
                m_textOut->showCue(static_cast<TextCue*>(cue));
            else if (state == Cue::State::Idle)
                m_textOut->clearText();
        }
        // Show mode: inspector stacks waveforms of all playing audio cues
        if (m_showMode && cue->type() == Cue::Type::Audio) {
            auto *a = static_cast<AudioCue*>(cue);
            if (state == Cue::State::Playing)
                m_inspector->addShowAudioCue(a);
            else if (state == Cue::State::Idle)
                m_inspector->removeShowAudioCue(a);
        }
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

    // Undo coalescing: property changes in the inspector become undoable after 800ms of inactivity
    m_undoLastPushed = m_workspace.cueList()->toJson();
    m_undoPropTimer  = new QTimer(this);
    m_undoPropTimer->setSingleShot(true);
    m_undoPropTimer->setInterval(800);
    connect(m_undoPropTimer, &QTimer::timeout, this, &MainWindow::flushUndoPropChange);

    connect(m_workspace.cueList(), &CueList::cuePropertyChanged, this, [this](int) {
        if (!m_suppressUndoTracking)
            m_undoPropTimer->start();
    });
    // After undo/redo, resync the reference snapshot
    connect(m_undoStack, &QUndoStack::indexChanged, this, [this](int) {
        if (!m_suppressUndoTracking) {
            m_undoPropTimer->stop();
            m_undoLastPushed = m_workspace.cueList()->toJson();
        }
    });

    updateTitle();
}

void MainWindow::buildUi() {
    m_model      = new CueListModel(m_workspace.cueList(), this);
    m_cueView    = new CueListView(m_model, this);
    m_inspector  = new InspectorPanel(m_workspace.cueList(), this);
    m_infoBar    = new CueInfoBar(this);
    m_activeCues = new ActiveCuesPanel(m_workspace.cueList(), this);

    // Top: active cues panel + cue list (order set by applyPanelLayout)
    m_topSplit = new QSplitter(Qt::Horizontal);
    m_topSplit->addWidget(m_activeCues);
    m_topSplit->addWidget(m_cueView);
    m_topSplit->setStretchFactor(0, 0);
    m_topSplit->setStretchFactor(1, 1);
    m_topSplit->setHandleWidth(3);
    m_topSplit->setSizes({230, 800});

    // Inspector in a scroll area at bottom (for narrow windows)
    auto *inspScroll = new QScrollArea;
    inspScroll->setWidgetResizable(true);
    inspScroll->setFrameShape(QFrame::NoFrame);
    inspScroll->setWidget(m_inspector);
    inspScroll->setMinimumHeight(0);

    // Vertical split: content area (top) + inspector (bottom)
    m_mainSplit = new QSplitter(Qt::Vertical);
    m_mainSplit->addWidget(m_topSplit);
    m_mainSplit->addWidget(inspScroll);
    m_mainSplit->setStretchFactor(0, 1);
    m_mainSplit->setStretchFactor(1, 0);
    m_mainSplit->setHandleWidth(4);
    m_mainSplit->setCollapsible(1, true);
    m_mainSplit->setSizes({10000, 0});

    auto *central = new QWidget;
    auto *vlay    = new QVBoxLayout(central);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);
    vlay->addWidget(m_mainSplit, 1);
    vlay->addWidget(m_infoBar);
    setCentralWidget(central);

    m_statusLbl = new QLabel("Pronto");
    statusBar()->addWidget(m_statusLbl);

    m_webUrlLabel = new QLabel;
    m_webUrlLabel->setStyleSheet("color: #4a9eff; font-size: 9pt;");
    m_webUrlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_webUrlLabel->hide();
    statusBar()->addPermanentWidget(m_webUrlLabel);

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
#ifndef OQL_BASE
    connect(m_cueView, &CueListView::addEffectRequested,      this, &MainWindow::addEffectCue);
    connect(m_cueView, &CueListView::addResetEffectRequested, this, &MainWindow::addResetEffectCue);
    connect(m_cueView, &CueListView::addScriptRequested,      this, &MainWindow::addScriptCue);
#endif
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
        m_inspector->setCue(cue);
        // Auto-size inspector to fit content
        const QList<int> sz = m_mainSplit->sizes();
        if (sz.size() >= 2) {
            const int total  = sz[0] + sz[1];
            const int needed = qMax(420, m_inspector->sizeHint().height() + 20);
            const int inspH  = qBound(sz[1], needed, total * 2 / 3);
            if (inspH > sz[1])
                m_mainSplit->setSizes({ qMax(0, total - inspH), inspH });
        }
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

    // Add-cue shortcuts (key sequences set in applyShortcuts)
    auto makeSc = [&](const QString &key) {
        auto *sc = new QShortcut(QKeySequence(), this);
        m_addCueShortcuts[key] = sc;
        return sc;
    };
    connect(makeSc("audio"),       &QShortcut::activated, this, &MainWindow::addAudioCue);
    connect(makeSc("video"),       &QShortcut::activated, this, &MainWindow::addVideoCue);
    connect(makeSc("text"),        &QShortcut::activated, this, &MainWindow::addTextCue);
    connect(makeSc("mic"),         &QShortcut::activated, this, &MainWindow::addMicCue);
#ifndef OQL_BASE
    connect(makeSc("record"),      &QShortcut::activated, this, &MainWindow::addRecordCue);
#endif
    connect(makeSc("stop"),        &QShortcut::activated, this, &MainWindow::addStopCue);
    connect(makeSc("fade"),        &QShortcut::activated, this, &MainWindow::addFadeCue);
    connect(makeSc("pause"),       &QShortcut::activated, this, &MainWindow::addPauseCue);
    connect(makeSc("play"),        &QShortcut::activated, this, &MainWindow::addPlayCue);
#ifndef OQL_BASE
    connect(makeSc("effect"),      &QShortcut::activated, this, &MainWindow::addEffectCue);
    connect(makeSc("reseteffect"), &QShortcut::activated, this, &MainWindow::addResetEffectCue);
    connect(makeSc("script"),      &QShortcut::activated, this, &MainWindow::addScriptCue);
#endif
    connect(makeSc("group"),       &QShortcut::activated, this, &MainWindow::addGroupCue);
    connect(makeSc("label"),       &QShortcut::activated, this, &MainWindow::addLabelCue);

    applyPanelLayout();
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
    auto *file = menuBar()->addMenu(tr("&File"));
    menuAction(file, tr("&Nuovo"),       QKeySequence::New,    this, &MainWindow::newWorkspace);
    menuAction(file, tr("&Apri…"),       QKeySequence::Open,   this, &MainWindow::openWorkspace);
    m_recentMenu = file->addMenu(tr("Apri &recenti"));
    rebuildRecentMenu();
    file->addSeparator();
    menuAction(file, tr("&Salva"),       QKeySequence::Save,   this, &MainWindow::saveWorkspace);
    menuAction(file, tr("Salva &come…"), QKeySequence::SaveAs, this, &MainWindow::saveWorkspaceAs);
    file->addSeparator();
    menuAction(file, tr("&Esci"),        QKeySequence::Quit,   qApp, &QApplication::quit);

    // Modifica
    auto *edit = menuBar()->addMenu(tr("&Modifica"));

    auto *undoAct = m_undoStack->createUndoAction(this, tr("&Annulla"));
    undoAct->setShortcut(QKeySequence::Undo);
    edit->addAction(undoAct);

    auto *redoAct = m_undoStack->createRedoAction(this, tr("&Ripeti"));
    redoAct->setShortcut(QKeySequence("Ctrl+Shift+Z"));
    edit->addAction(redoAct);

    edit->addSeparator();

    menuAction(edit, tr("Aggiungi &Audio Cue"), QKeySequence("Ctrl+Shift+A"), this, &MainWindow::addAudioCue);
    menuAction(edit, tr("Aggiungi &Video Cue"), QKeySequence("Ctrl+Shift+V"), this, &MainWindow::addVideoCue);
    edit->addSeparator();
    menuAction(edit, tr("Aggiungi &Stop Cue"),          QKeySequence("Ctrl+Shift+S"), this, &MainWindow::addStopCue);
    menuAction(edit, tr("Aggiungi &Fade Cue"),          QKeySequence("Ctrl+Shift+F"), this, &MainWindow::addFadeCue);
    menuAction(edit, tr("Aggiungi &Pause Cue"),         QKeySequence("Ctrl+Shift+P"), this, &MainWindow::addPauseCue);
#ifndef OQL_BASE
    menuAction(edit, tr("Aggiungi cue &Velocizza"),     QKeySequence("Ctrl+Shift+U"), this, &MainWindow::addSpeedUpCue);
    menuAction(edit, tr("Aggiungi cue &Rallenta"),      QKeySequence("Ctrl+Shift+D"), this, &MainWindow::addSpeedDownCue);
#endif
    menuAction(edit, tr("Aggiungi cue &Play"),          QKeySequence("Ctrl+Shift+L"), this, &MainWindow::addPlayCue);
    menuAction(edit, tr("Aggiungi cue &Microfono"),     QKeySequence("Ctrl+Shift+M"), this, &MainWindow::addMicCue);
    edit->addSeparator();
    menuAction(edit, tr("Aggiungi &Gruppo"),            QKeySequence("Ctrl+Shift+G"), this, &MainWindow::addGroupCue);
    menuAction(edit, tr("Aggiungi &Etichetta"),         QKeySequence("Ctrl+Shift+E"), this, &MainWindow::addLabelCue);
    menuAction(edit, tr("Aggiungi &Testo"),             QKeySequence("Ctrl+Shift+T"), this, &MainWindow::addTextCue);
    edit->addSeparator();
#ifndef OQL_BASE
    menuAction(edit, tr("Aggiungi cue E&ffetto"),       QKeySequence("Ctrl+Shift+X"), this, &MainWindow::addEffectCue);
    menuAction(edit, tr("Aggiungi cue &Reset Effetti"), QKeySequence("Ctrl+Shift+R"), this, &MainWindow::addResetEffectCue);
    edit->addSeparator();
#endif
    menuAction(edit, tr("&Elimina cue"), QKeySequence::Delete, this, &MainWindow::deleteSelectedCue);

    // Finestra
    auto *win = menuBar()->addMenu(tr("&Finestra"));
    menuAction(win, tr("Mostra/nascondi output video"), QKeySequence("Ctrl+W"), this, &MainWindow::toggleVideoOutput);

    // Strumenti
    auto *tools = menuBar()->addMenu(tr("&Strumenti"));
    menuAction(tools, tr("&Impostazioni…"), QKeySequence("Ctrl+,"), this, &MainWindow::openSettings);

    // Aiuto
    auto *help = menuBar()->addMenu(tr("&Aiuto"));
    menuAction(help, tr("Informazioni su OQL…"), QKeySequence(), this, &MainWindow::showAbout);
}

void MainWindow::buildToolBar() {
    auto *tb = addToolBar("Principale");
    tb->setMovable(false);
    tb->setIconSize({32, 32});
    tb->setFixedHeight(90);
    tb->setStyleSheet(
        "QToolBar { spacing: 2px; padding: 3px; }"
        "QToolBar::separator { width: 1px; background: rgba(255,255,255,22); margin: 25px 5px; }"
        "QToolButton { min-width: 38px; min-height: 38px; border-radius: 5px; }"
        "QToolButton:hover   { background: rgba(255,255,255,18); }"
        "QToolButton:pressed { background: rgba(0,0,0,30); }");

    // ── GO button — large, no colored background ──────────────────────────────
    m_goBtn = new QToolButton;
    m_goBtn->setText("▶  GO");
    QFont goFont;
    goFont.setPointSize(18);
    goFont.setBold(true);
    m_goBtn->setFont(goFont);
    m_goBtn->setFixedSize(84, 84);
    m_goBtn->setStyleSheet(
        "QToolButton { background:#15803d; color:white; border-radius:10px; border:none;"
        " min-width:84px; max-width:84px; min-height:84px; max-height:84px; }"
        "QToolButton:pressed { background:#0f6030; }"
        "QToolButton:hover   { background:#16a34a; }");
    m_goBtn->setToolTip(tr("Vai"));
    connect(m_goBtn, &QToolButton::clicked, this, &MainWindow::go);
    tb->addWidget(m_goBtn);

    // m_goAction: keyboard shortcut only (window-level, not in toolbar visually)
    m_goAction = new QAction("GO", this);
    connect(m_goAction, &QAction::triggered, this, &MainWindow::go);
    addAction(m_goAction);

    tb->addSeparator();

    // ── Icon helper with rounded bg (for add-cue buttons) ─────────────────────
    auto makeTbIcon = [](const QColor &bg, auto drawFn) -> QIcon {
        QPixmap px(28, 28);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(0, 0, 28, 28, 6, 6);
        p.setBrush(Qt::white);
        drawFn(p);
        return QIcon(px);
    };

    // ── Flat icon helper (no bg) for transport buttons ────────────────────────
    auto makeTbIconFlat = [](auto drawFn) -> QIcon {
        QPixmap px(28, 28);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing);
        drawFn(p);
        return QIcon(px);
    };

    // ── Stop All + First Cue — flat, no colored bg ────────────────────────────
    m_stopAction = new QAction(this);
    m_stopAction->setIcon(makeTbIconFlat([](QPainter &p) {
        p.setBrush(Qt::white); p.setPen(Qt::NoPen);
        p.drawRoundedRect(5, 5, 18, 18, 2, 2);
    }));
    m_stopAction->setToolTip(tr("Ferma tutto"));
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopAll);
    tb->addAction(m_stopAction);
    addAction(m_stopAction);

    m_firstCueAction = new QAction(this);
    m_firstCueAction->setIcon(makeTbIconFlat([](QPainter &p) {
        p.setBrush(Qt::white); p.setPen(Qt::NoPen);
        p.drawRect(2, 4, 3, 20);
        QPolygon t1; t1 << QPoint(13, 4) << QPoint(13, 24) << QPoint(7, 14);
        QPolygon t2; t2 << QPoint(23, 4) << QPoint(23, 24) << QPoint(16, 14);
        p.drawPolygon(t1); p.drawPolygon(t2);
    }));
    m_firstCueAction->setToolTip(tr("Torna alla prima cue"));
    connect(m_firstCueAction, &QAction::triggered, this, &MainWindow::goToFirstCue);
    tb->addAction(m_firstCueAction);
    addAction(m_firstCueAction);

    tb->addSeparator();

    // ── Cue add buttons (tracked for Show Mode) ───────────────────────────────
    auto addCueBtn = [&](const QIcon &icon, const QString &tooltip, auto slot) {
        auto *btn = new QToolButton;
        btn->setIcon(icon);
        btn->setIconSize({32, 32});
        btn->setFixedSize(40, 40);
        btn->setToolTip(tooltip);
        connect(btn, &QToolButton::clicked, this, slot);
        tb->addWidget(btn);
        m_cueAddBtns.append(btn);
    };

    // ── Media: Audio (♪), Video, Text ────────────────────────────────────────
    auto audioIcon = makeTbIcon(QColor(0x2a, 0x6d, 0xcc), [](QPainter &p) {
        p.setBrush(Qt::white); p.setPen(Qt::NoPen);
        p.save(); p.translate(10.0, 21.0); p.rotate(-20.0);
        p.drawEllipse(QRectF(-5.0, -3.5, 10.0, 7.0)); p.restore();
        p.drawRect(15, 3, 2, 19);
        QPainterPath fp;
        fp.moveTo(17, 3); fp.quadTo(26, 7, 18, 14);
        fp.quadTo(22, 10, 17, 8); fp.closeSubpath();
        p.drawPath(fp);
    });
    auto videoIcon = makeTbIcon(QColor(0x2a, 0x88, 0x44), [](QPainter &p) {
        p.setBrush(Qt::NoBrush); p.setPen(QPen(Qt::white, 1.5));
        p.drawRoundedRect(2, 6, 24, 16, 2, 2);
        p.setPen(Qt::NoPen); p.setBrush(Qt::white);
        QPolygon t; t << QPoint(10,9) << QPoint(10,19) << QPoint(20,14); p.drawPolygon(t);
    });
    auto textIcon = makeTbIcon(QColor(0x0a, 0x72, 0x8a), [](QPainter &p) {
        p.drawRect(2, 4, 24, 4); p.drawRect(10, 4, 6, 20);
    });

    addCueBtn(audioIcon, tr("+ Audio Cue"), &MainWindow::addAudioCue);
    addCueBtn(videoIcon, tr("+ Video Cue"), &MainWindow::addVideoCue);
    addCueBtn(textIcon,  tr("+ Testo"),     &MainWindow::addTextCue);
    tb->addSeparator();

    // ── Controllo: Fade prima, poi Stop/Pause/Play ────────────────────────────
    auto fadeIcon = makeTbIcon(QColor(0xcc, 0x77, 0x22), [](QPainter &p) {
        QPolygon e; e << QPoint(3,25) << QPoint(25,3) << QPoint(25,25); p.drawPolygon(e);
    });
    auto stopCueIcon = makeTbIcon(QColor(0xcc, 0x33, 0x33), [](QPainter &p) {
        p.drawRect(6, 6, 16, 16);
    });
    auto pauseIcon = makeTbIcon(QColor(0x88, 0x55, 0xcc), [](QPainter &p) {
        p.drawRect(6, 6, 6, 16); p.drawRect(16, 6, 6, 16);
    });
    auto playCueIcon = makeTbIcon(QColor(0x22, 0xaa, 0x55), [](QPainter &p) {
        p.drawRect(4, 4, 4, 20);
        QPolygon t; t << QPoint(10,4) << QPoint(10,24) << QPoint(24,14); p.drawPolygon(t);
    });

    addCueBtn(fadeIcon,    tr("+ Fade Cue"),  &MainWindow::addFadeCue);
    addCueBtn(stopCueIcon, tr("+ Stop Cue"),  &MainWindow::addStopCue);
    addCueBtn(pauseIcon,   tr("+ Pause Cue"), &MainWindow::addPauseCue);
    addCueBtn(playCueIcon, tr("+ Play Cue"),  &MainWindow::addPlayCue);
    tb->addSeparator();

    // ── Velocità ──────────────────────────────────────────────────────────────
    auto speedUpIcon = makeTbIcon(QColor(0x11, 0x99, 0x99), [](QPainter &p) {
        QPolygon t1, t2;
        t1 << QPoint(2,7) << QPoint(2,21) << QPoint(12,14);
        t2 << QPoint(14,7) << QPoint(14,21) << QPoint(24,14);
        p.drawPolygon(t1); p.drawPolygon(t2);
    });
    auto speedDownIcon = makeTbIcon(QColor(0x11, 0x77, 0x77), [](QPainter &p) {
        QPolygon t1, t2;
        t1 << QPoint(26,7) << QPoint(26,21) << QPoint(16,14);
        t2 << QPoint(14,7) << QPoint(14,21) << QPoint(4,14);
        p.drawPolygon(t1); p.drawPolygon(t2);
    });

#ifndef OQL_BASE
    addCueBtn(speedUpIcon,   tr("+ Velocizza"), &MainWindow::addSpeedUpCue);
    addCueBtn(speedDownIcon, tr("+ Rallenta"),  &MainWindow::addSpeedDownCue);
#endif
    tb->addSeparator();

    // ── Ingresso ──────────────────────────────────────────────────────────────
    auto micIcon = makeTbIcon(QColor(0xcc, 0x22, 0x88), [](QPainter &p) {
        p.setBrush(Qt::white);
        p.drawRoundedRect(9, 2, 10, 14, 5, 5);
        p.setPen(QPen(Qt::white, 2.0)); p.setBrush(Qt::NoBrush);
        p.drawArc(4, 9, 20, 12, 0, -180 * 16);
        p.drawLine(14, 21, 14, 26);
        p.drawLine(8, 26, 20, 26);
    });
    auto recordIcon = makeTbIcon(QColor(0xaa, 0x22, 0x22), [](QPainter &p) {
        p.drawEllipse(5, 5, 18, 18);
    });

    addCueBtn(micIcon,    tr("+ Mic Cue"),    &MainWindow::addMicCue);
#ifndef OQL_BASE
    addCueBtn(recordIcon, tr("+ Record Cue"), &MainWindow::addRecordCue);
#endif
    tb->addSeparator();

    // ── Struttura ─────────────────────────────────────────────────────────────
    auto groupIcon = makeTbIcon(QColor(0x44, 0x48, 0x58), [](QPainter &p) {
        p.drawRect(1, 9, 11, 4); p.drawRect(1, 12, 26, 14);
    });
    auto labelIcon = makeTbIcon(QColor(0x60, 0x55, 0x10), [](QPainter &p) {
        p.setPen(QPen(Qt::white, 2)); p.setBrush(Qt::NoBrush);
        p.drawLine(3, 8, 25, 8);
        p.drawLine(3, 14, 25, 14);
        p.drawLine(3, 20, 17, 20);
    });

    addCueBtn(groupIcon, tr("+ Gruppo"),    &MainWindow::addGroupCue);
    addCueBtn(labelIcon, tr("+ Etichetta"), &MainWindow::addLabelCue);
    tb->addSeparator();

    // ── Effetti / Script ──────────────────────────────────────────────────────
    auto effectIcon = makeTbIcon(QColor(0x7b, 0x35, 0x9e), [](QPainter &p) {
        QPolygon bolt;
        bolt << QPoint(16,2) << QPoint(7,16) << QPoint(13,16)
             << QPoint(10,26) << QPoint(21,12) << QPoint(15,12);
        p.drawPolygon(bolt);
    });
    auto resetEffectIcon = makeTbIcon(QColor(0x35, 0x7b, 0x9e), [](QPainter &p) {
        p.setPen(QPen(Qt::white, 2.5)); p.setBrush(Qt::NoBrush);
        p.drawArc(3, 3, 22, 22, 50*16, 250*16);
        p.setPen(Qt::NoPen); p.setBrush(Qt::white);
        QPolygon arr; arr << QPoint(3,8) << QPoint(12,2) << QPoint(12,14);
        p.drawPolygon(arr);
    });
    auto scriptIcon = makeTbIcon(QColor(0x5a, 0x8a, 0x3a), [](QPainter &p) {
        p.setPen(QPen(Qt::white, 2.5)); p.setBrush(Qt::NoBrush);
        p.drawArc(5, 3, 7, 8, 90*16, 180*16);
        p.drawArc(5, 17, 7, 8, 180*16, 180*16);
        p.drawArc(16, 3, 7, 8, 270*16, 180*16);
        p.drawArc(16, 17, 7, 8, 0*16, 180*16);
        p.drawLine(12, 7, 12, 14); p.drawLine(16, 7, 16, 14);
    });

#ifndef OQL_BASE
    addCueBtn(effectIcon,      tr("+ Effetto"),      &MainWindow::addEffectCue);
    addCueBtn(resetEffectIcon, tr("+ Reset Effetti"), &MainWindow::addResetEffectCue);
    addCueBtn(scriptIcon,      tr("+ Script Cue"),   &MainWindow::addScriptCue);
#endif
    tb->addSeparator();

    m_webAction = tb->addAction("🌐 Remote");
    m_webAction->setCheckable(true);
    m_webAction->setToolTip(tr("Avvia / ferma Web Remote (controllabile anche da Impostazioni → Remote)"));
    connect(m_webAction, &QAction::toggled, this, [this](bool on) {
        if (on == m_webServer->isRunning()) return;
        if (on) m_webServer->start(AppSettings::instance().webPort());
        else    m_webServer->stop();
    });

    auto *videoWin = tb->addAction("📺 Video Out");
    connect(videoWin, &QAction::triggered, this, &MainWindow::toggleVideoOutput);
    auto *textWin = tb->addAction("📝 Text Out");
    connect(textWin, &QAction::triggered, this, [this]() {
        if (m_textOut->isVisible()) m_textOut->hide();
        else                        m_textOut->show();
    });

    // ── Expanding spacer → Show Mode isolated far right ───────────────────────
    auto *tbSpacer = new QWidget;
    tbSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(tbSpacer);

    auto showModeIcon = makeTbIcon(QColor(0x33, 0x55, 0x66), [](QPainter &p) {
        p.setPen(QPen(Qt::white, 2.0)); p.setBrush(Qt::NoBrush);
        QPainterPath eyePath;
        eyePath.moveTo(2, 14); eyePath.quadTo(14, 3, 26, 14);
        eyePath.quadTo(14, 25, 2, 14);
        p.drawPath(eyePath);
        p.setBrush(Qt::white); p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(14, 14), 4.5, 4.5);
    });
    auto *showBtn = new QToolButton;
    showBtn->setIcon(showModeIcon);
    showBtn->setIconSize({32, 32});
    showBtn->setFixedSize(40, 40);
    showBtn->setToolTip(tr("Modalità Show — visualizzazione senza modifiche"));
    showBtn->setCheckable(true);
    showBtn->setStyleSheet(
        "QToolButton { border-radius:5px; }"
        "QToolButton:checked { background:#1e5a3a; border:2px solid #3ccc77; }"
        "QToolButton:hover   { background:rgba(255,255,255,18); }");
    connect(showBtn, &QToolButton::toggled, this, [this](bool on) {
        m_showMode = on;
        m_inspector->setShowMode(on);
        for (auto *btn : m_cueAddBtns)
            btn->setEnabled(!on);
        if (on) {
            const QList<int> sz = m_mainSplit->sizes();
            if (sz.size() >= 2) {
                const int total = sz[0] + sz[1];
                const int inspH = total / 3;
                m_mainSplit->setSizes({ total - inspH, inspH });
            }
            m_mainSplit->setCollapsible(1, false);
        } else {
            m_mainSplit->setCollapsible(1, true);
        }
    });
    tb->addWidget(showBtn);

    m_showModeBtn = showBtn;
    m_showModeShortcut = new QShortcut(AppSettings::instance().keyShowMode(), this);
    connect(m_showModeShortcut, &QShortcut::activated, showBtn, &QToolButton::toggle);
}

// ── Transport ─────────────────────────────────────────────────────────────────

void MainWindow::go() {
    const auto sel = m_cueView->selectionModel()->selectedRows();

    // RecordCue in registrazione: secondo GO ferma la registrazione e sposta il
    // focus sulla cue successiva (il playhead è già avanzato dopo il primo GO).
    if (!sel.isEmpty()) {
        const int actual = m_model->actualRowForVisible(sel.first().row());
        Cue *selCue = actual >= 0 ? m_workspace.cueList()->cueAt(actual) : nullptr;
        if (selCue && selCue->type() == Cue::Type::Record
                   && selCue->state() == Cue::State::Playing) {
            selCue->stop();
            const int ph    = m_workspace.cueList()->playheadIndex();
            const int phVis = m_model->visibleRowForActual(ph);
            if (phVis >= 0) {
                m_programmaticSelect = true;
                m_cueView->selectRow(phVis);
                m_programmaticSelect = false;
            }
            return;
        }
    }

    int keepVisRow = -1;  // if >= 0, keep selection here after go (RecordCue starting)
    if (!sel.isEmpty()) {
        const int actual = m_model->actualRowForVisible(sel.first().row());
        Cue *selCue = actual >= 0 ? m_workspace.cueList()->cueAt(actual) : nullptr;
        if (!selCue || selCue->state() == Cue::State::Idle) {
            m_workspace.cueList()->setPlayhead(actual >= 0 ? actual : 0);
            if (selCue && selCue->type() == Cue::Type::Record)
                keepVisRow = sel.first().row();
        }
    }
    m_workspace.cueList()->go();

    // If we just started a RecordCue, keep selection on it so user can stop recording.
    if (keepVisRow >= 0) {
        Cue *recCue = m_model->cueForRow(keepVisRow);
        if (recCue && recCue->state() == Cue::State::Playing) {
            m_programmaticSelect = true;
            m_cueView->selectRow(keepVisRow);
            m_programmaticSelect = false;
            return;
        }
    }

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
    if (m_goBtn) m_goBtn->setToolTip(tr("Vai (%1)").arg(s.keyGo().toString()));
    m_stopAction->setShortcut(s.keyStopAll());
    m_stopAction->setToolTip(tr("Ferma tutto (%1)").arg(s.keyStopAll().toString()));
    m_firstCueAction->setShortcut(s.keyFirstCue());
    m_firstCueAction->setToolTip(tr("Torna alla prima cue (%1)").arg(s.keyFirstCue().toString()));
    for (auto it = m_addCueShortcuts.cbegin(); it != m_addCueShortcuts.cend(); ++it)
        it.value()->setKey(s.keyAddCue(it.key()));
    if (m_showModeShortcut)
        m_showModeShortcut->setKey(s.keyShowMode());
    if (m_showModeBtn)
        m_showModeBtn->setToolTip(tr("Modalità Show — visualizzazione senza modifiche (%1)").arg(s.keyShowMode().toString()));
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

#ifndef OQL_BASE
void MainWindow::addSpeedUpCue()   { addControlCueImpl(Cue::Type::Speed, 1.5); }
void MainWindow::addSpeedDownCue() { addControlCueImpl(Cue::Type::Speed, 0.5); }
void MainWindow::addEffectCue()      { addControlCueImpl(Cue::Type::Effect); }
void MainWindow::addResetEffectCue() { addControlCueImpl(Cue::Type::ResetEffect); }

void MainWindow::addScriptCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Script Cue", [&] {
        auto cue = std::make_unique<ScriptCue>();
        cue->setNumber(nextCueNumber());
        cue->setName("Script");
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}

void MainWindow::addRecordCue() {
    const auto sel = m_cueView->selectionModel()->selectedRows();
    const int  idx = sel.isEmpty() ? -1 : m_model->actualRowForVisible(sel.first().row()) + 1;
    doUndoable("Aggiungi Record Cue", [&] {
        auto cue = std::make_unique<RecordCue>();
        cue->setNumber(nextCueNumber());
        cue->setName("Registrazione");
        m_workspace.cueList()->addCue(std::move(cue), idx);
    });
    const int actual = idx < 0 ? m_workspace.cueList()->count() - 1 : idx;
    m_cueView->selectRow(m_model->visibleRowForActual(actual));
    m_cueView->setFocus();
}
#endif // OQL_BASE

void MainWindow::setupNewVideoCue(int index) {
    auto *cue = qobject_cast<VideoCue*>(m_workspace.cueList()->cueAt(index));
    if (cue)
        cue->setVideoSink(m_videoOut->videoWidget()->videoSink());
}

void MainWindow::showAbout() {
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::openSettings() {
    SettingsDialog dlg(&m_workspace, this);
    dlg.exec();
    AppSettings::applyLanguage();   // reinstall translator if language changed
    applyProjectSettings();
    applyShortcuts();
    applyWebServer();
    m_cueView->applyRowHeight();
    m_cueView->applyFont();
}

void MainWindow::applyWebServer() {
    const bool   shouldRun = AppSettings::instance().webEnabled();
    const quint16 port     = AppSettings::instance().webPort();
    if (shouldRun) {
        if (m_webServer->isRunning()) m_webServer->stop();
        m_webServer->start(port);
    } else {
        m_webServer->stop();
    }
}

void MainWindow::applyProjectSettings() {
    m_cueView->setColumnHidden(CueListModel::ColNumber, !m_workspace.showCueNumbers());
    applyPanelLayout();
}

void MainWindow::applyPanelLayout() {
    if (!m_topSplit) return;
    const int side = AppSettings::instance().activeCuePanelSide();
    // 0 = active cues on left (index 0), 1 = active cues on right (index 1)
    const int desired = (side == 1) ? 1 : 0;
    // m_activeCues is always one of the two widgets; find its current index
    for (int i = 0; i < m_topSplit->count(); ++i) {
        if (m_topSplit->widget(i) == m_activeCues) {
            if (i != desired)
                m_topSplit->insertWidget(desired, m_activeCues);
            break;
        }
    }
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
    if (m_showMode) return;
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
    // In show mode inspector is locked — only audio double-click updates it
    if (!m_showMode) {
        m_inspector->setCue(cue);
        // Auto-open inspector to fit content
        if (cue) {
            const QList<int> sz = m_mainSplit->sizes();
            if (sz.size() >= 2) {
                const int total  = sz[0] + sz[1];
                const int needed = qMax(420, m_inspector->sizeHint().height() + 20);
                if (sz[1] < needed)
                    m_mainSplit->setSizes({ qMax(0, total - needed), needed });
            }
        }
    }
    m_infoBar->setCue(cue);
}

// ── File I/O ──────────────────────────────────────────────────────────────────

void MainWindow::newWorkspace() {
    if (!confirmUnsaved()) return;
    m_workspace.reset();
    m_inspector->setCue(nullptr);
    m_undoPropTimer->stop();
    m_undoStack->clear();
    m_undoLastPushed = m_workspace.cueList()->toJson();
}

void MainWindow::openWorkspace() {
    if (!confirmUnsaved()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, "Apri workspace", {}, "OQL (*.oqlab);;JSON (*.json)");
    if (path.isEmpty()) return;
    if (!m_workspace.load(path)) {
        QMessageBox::warning(this, "Errore", "Impossibile aprire il file.");
        return;
    }
    assignVideoSinkToAll();
    m_inspector->setCue(nullptr);
    m_undoPropTimer->stop();
    m_undoStack->clear();
    m_undoLastPushed = m_workspace.cueList()->toJson();
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
        "OQL (*.oqlab);;JSON (*.json)",
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

void MainWindow::flushUndoPropChange() {
    if (m_suppressUndoTracking) return;
    m_undoPropTimer->stop();
    QJsonArray current = m_workspace.cueList()->toJson();
    if (current == m_undoLastPushed) return;
    m_suppressUndoTracking = true;
    m_undoStack->push(new SnapshotCommand(
        tr("Modifica proprietà"),
        m_workspace.cueList(),
        m_undoLastPushed,
        current,
        [this] { m_inspector->setCue(nullptr); m_infoBar->setCue(nullptr); },
        [this] { assignVideoSinkToAll(); }
    ));
    m_undoLastPushed = current;
    m_suppressUndoTracking = false;
}

void MainWindow::doUndoable(const QString &desc, std::function<void()> fn) {
    flushUndoPropChange();   // commit pending inspector changes before structural operation
    m_suppressUndoTracking = true;
    QJsonArray before = m_workspace.cueList()->toJson();
    fn();
    QJsonArray after = m_workspace.cueList()->toJson();
    if (before == after) {
        m_suppressUndoTracking = false;
        return;
    }
    m_undoStack->push(new SnapshotCommand(
        desc,
        m_workspace.cueList(),
        std::move(before),
        std::move(after),
        [this] { m_inspector->setCue(nullptr); m_infoBar->setCue(nullptr); },
        [this] { assignVideoSinkToAll(); }
    ));
    m_undoLastPushed = after;
    m_suppressUndoTracking = false;
}

void MainWindow::addToRecentFiles(const QString &path) {
    QSettings s("OQL", "OQL");
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
    QSettings s("OQL", "OQL");
    const QStringList recent = s.value("recentFiles").toStringList();
    for (const QString &path : recent) {
        const QString label = QFileInfo(path).fileName();
        m_recentMenu->addAction(label, this, [this, path]() { openRecentFile(path); });
    }
    if (recent.isEmpty())
        m_recentMenu->addAction("(nessun file recente)")->setEnabled(false);
    m_recentMenu->addSeparator();
    m_recentMenu->addAction("Svuota recenti", this, [this]() {
        QSettings("OQL", "OQL").setValue("recentFiles", QStringList{});
        rebuildRecentMenu();
    });
}

void MainWindow::openRecentFile(const QString &path) {
    if (!confirmUnsaved()) return;
    if (!m_workspace.load(path)) {
        QMessageBox::warning(this, "Errore", "Impossibile aprire il file:\n" + path);
        QSettings s("OQL", "OQL");
        QStringList recent = s.value("recentFiles").toStringList();
        recent.removeAll(path);
        s.setValue("recentFiles", recent);
        rebuildRecentMenu();
        return;
    }
    assignVideoSinkToAll();
    m_inspector->setCue(nullptr);
    m_undoPropTimer->stop();
    m_undoStack->clear();
    m_undoLastPushed = m_workspace.cueList()->toJson();
    addToRecentFiles(path);
}

void MainWindow::updateTitle() {
    QString title = "OQL";
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

void MainWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange) {
        menuBar()->clear();
        buildMenus();
    }
    QMainWindow::changeEvent(event);
}
