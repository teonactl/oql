#include "PluginChainWidget.h"
#include "engine/AudioPlugin.h"
#include "engine/PluginChain.h"
#include "engine/VstPlugin.h"
#include "engine/Lv2Plugin.h"
#include "engine/BuiltinPlugins.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QFormLayout>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>
#include <QStackedWidget>
#include <QLineEdit>
#include <QProxyStyle>
#include <QGuiApplication>
#ifdef Q_OS_LINUX
#include <suil/suil.h>
#include <X11/Xlib.h>
#endif
#include <cstdlib>
#include <memory>

// Makes sliders jump to the clicked position instead of moving by pageStep
class AbsoluteSliderStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint hint, const QStyleOption *opt,
                  const QWidget *widget, QStyleHintReturn *ret) const override {
        if (hint == QStyle::SH_Slider_AbsoluteSetButtons)
            return Qt::LeftButton | Qt::MiddleButton;
        return QProxyStyle::styleHint(hint, opt, widget, ret);
    }
};

static AbsoluteSliderStyle *s_sliderStyle = nullptr;

#ifdef Q_OS_LINUX
// VST2 native editor UIs use their own X11 connections and may trigger X11
// errors (e.g. BadWindow on XCreateColormap) before our window is fully synced.
// The default handler aborts the process; ours just logs and continues.
// s_vstGlxErrors counts GLX-specific errors (opcode 150) so we can detect when
// a plugin's OpenGL context creation has failed and abort before the plugin
// segfaults trying to render with null GL state.
static int s_vstGlxErrors = 0;

static int vstX11ErrorHandler(Display *dpy, XErrorEvent *ev) {
    char msg[256] = {};
    XGetErrorText(dpy, ev->error_code, msg, sizeof(msg));
    qWarning("VST2 editor X11 error (suppressed): %s  opcode=%d  resource=0x%lx",
             msg, int(ev->request_code), ulong(ev->resourceid));
    if (ev->request_code == 150)  // GLX major opcode (universally 150 on Linux)
        ++s_vstGlxErrors;
    return 0;
}
static void ensureVstX11Handler() {
    static bool installed = false;
    if (!installed) { XSetErrorHandler(vstX11ErrorHandler); installed = true; }
}
#endif // Q_OS_LINUX

// ── VST2 native editor dialog ─────────────────────────────────────────────────

class VstEditorDialog : public QDialog {
    Q_OBJECT
public:
    VstEditorDialog(VstPlugin *plugin, QWidget *parent = nullptr)
        : QDialog(parent, Qt::Window), m_plugin(plugin)
    {
#ifdef Q_OS_LINUX
        ensureVstX11Handler();
#endif
        setWindowTitle(QString::fromStdString(plugin->name()));
        setAttribute(Qt::WA_DeleteOnClose);
        setMinimumSize(100, 100);

        m_idleTimer = new QTimer(this);
        m_idleTimer->setInterval(33);
        connect(m_idleTimer, &QTimer::timeout, this, [this]() {
            if (!m_plugin || !m_plugin->hasEditor()) {
                m_idleTimer->stop();
                close();
                return;
            }
            m_plugin->editorIdle();
        });
    }

    ~VstEditorDialog() override {
        if (m_idleTimer) m_idleTimer->stop();
        if (m_plugin && m_editorOpened) m_plugin->closeEditor();
    }

protected:
    void showEvent(QShowEvent *ev) override {
        QDialog::showEvent(ev);
        if (!m_openAttempted) {
            m_openAttempted = true;
            // Defer to after Qt has processed the show event and flushed its
            // XCB buffer. The dialog's native X11 window is then mapped and
            // visible server-side, so we can safely pass its ID to the plugin.
            QTimer::singleShot(0, this, [this]() { tryOpenEditor(); });
        }
    }

private:
    void tryOpenEditor() {
#ifdef Q_OS_LINUX
        // Force Mesa software GL so NVIDIA's broken XWayland GLX is bypassed.
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
        if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
            XSync(x11->display(), False);
#endif

        const WId parentId = this->winId();

        // Resize dialog to match plugin's reported size
        {
            ERect *rect = nullptr;
            if (m_plugin->getEditorRect(&rect) && rect) {
                const int w = rect->right  - rect->left;
                const int h = rect->bottom - rect->top;
                if (w > 0 && h > 0) resize(w, h);
            }
        }

#ifdef Q_OS_LINUX
        s_vstGlxErrors = 0;
#endif
        if (!m_plugin->openEditor(reinterpret_cast<void*>(parentId))) {
            QMessageBox::warning(this, "Editor non disponibile",
                "Il plugin non supporta un editor grafico,\n"
                "oppure l'editor ha crashato durante l'apertura.");
            reject();
            return;
        }

#ifdef Q_OS_LINUX
        if (s_vstGlxErrors > 0) {
            m_plugin->closeEditor();
            QMessageBox::warning(this, "Editor non disponibile",
                "Questo plugin richiede OpenGL, che non è disponibile sul display corrente.\n\n"
                "Assicurati di avere Mesa installato:\n"
                "    sudo pacman -S mesa");
            reject();
            return;
        }
#endif

        // Re-apply rect: some plugins report correct size only after effEditOpen
        {
            ERect *rect = nullptr;
            if (m_plugin->getEditorRect(&rect) && rect) {
                const int w = rect->right  - rect->left;
                const int h = rect->bottom - rect->top;
                if (w > 0 && h > 0) resize(w, h);
            }
        }

        m_editorOpened = true;
        m_idleTimer->start();
    }

    VstPlugin *m_plugin        = nullptr;
    QTimer    *m_idleTimer     = nullptr;
    bool       m_editorOpened  = false;
    bool       m_openAttempted = false;
};

// ── LV2 native editor dialog (Linux/X11 only) ─────────────────────────────────
#ifdef Q_OS_LINUX
class Lv2EditorDialog : public QDialog {
    Q_OBJECT
public:
    Lv2EditorDialog(Lv2Plugin *plugin, QWidget *parent = nullptr)
        : QDialog(parent, Qt::Window), m_plugin(plugin)
    {
        ensureVstX11Handler();
        setWindowTitle(QString::fromStdString(plugin->name()));
        setAttribute(Qt::WA_DeleteOnClose);
        resize(600, 400);

        m_idleTimer = new QTimer(this);
        m_idleTimer->setInterval(33);
        connect(m_idleTimer, &QTimer::timeout, this, [this]() {
            if (!m_plugin) { close(); return; }
            m_plugin->editorIdle();
        });
    }

    ~Lv2EditorDialog() override {
        if (m_idleTimer) m_idleTimer->stop();
        if (m_plugin && m_editorOpened) m_plugin->closeEditor();
    }

protected:
    void showEvent(QShowEvent *ev) override {
        QDialog::showEvent(ev);
        if (!m_openAttempted) {
            m_openAttempted = true;
            QTimer::singleShot(0, this, [this]() { tryOpenEditor(); });
        }
    }

private:
    void tryOpenEditor() {
        auto *x11App = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        if (x11App) XSync(x11App->display(), False);

        const WId parentId = this->winId();
        if (!m_plugin->openEditor(reinterpret_cast<void*>(parentId))) {
            QMessageBox::warning(this, "Editor non disponibile",
                "Impossibile aprire l'editor LV2.\n"
                "Questo plugin potrebbe non avere un'interfaccia grafica compatibile.");
            reject();
            return;
        }

        // Query the X11 child window size created by the LV2 UI
        if (x11App) {
            Window root_ret, parent_ret;
            Window *children = nullptr;
            unsigned nchildren = 0;
            if (XQueryTree(x11App->display(), Window(parentId),
                           &root_ret, &parent_ret, &children, &nchildren)
                    && children && nchildren > 0) {
                XWindowAttributes attrs = {};
                if (XGetWindowAttributes(x11App->display(), children[0], &attrs)
                        && attrs.width > 10 && attrs.height > 10)
                    resize(attrs.width, attrs.height);
                XFree(children);
            }
        }

        m_editorOpened = true;
        m_idleTimer->start();
    }

    Lv2Plugin *m_plugin        = nullptr;
    QTimer    *m_idleTimer     = nullptr;
    bool       m_editorOpened  = false;
    bool       m_openAttempted = false;
};
#endif // Q_OS_LINUX

// ── Plugin browser dialog ─────────────────────────────────────────────────────

static std::unique_ptr<AudioPlugin> runBrowseDialog(QWidget *parent) {
    auto *dlg = new QDialog(parent);
    dlg->setWindowTitle("Aggiungi effetto");
    dlg->resize(480, 420);

    auto *lay  = new QVBoxLayout(dlg);
    auto *tabs = new QTabWidget;
    lay->addWidget(tabs);

    // ── Built-in tab (sempre disponibile, nessun plugin esterno) ─────────────
    auto *builtinTab = new QWidget;
    auto *builtinLay = new QVBoxLayout(builtinTab);
    auto *builtinList = new QListWidget;
    builtinLay->addWidget(new QLabel("Effetti integrati (disponibili su tutte le piattaforme):"));
    builtinLay->addWidget(builtinList, 1);
    tabs->addTab(builtinTab, "Built-in");

    struct BuiltinInfo { QString id; QString name; QString desc; };
    const QList<BuiltinInfo> builtins = {
        { GainPlugin::BUILTIN_ID,            "Gain",           "Regola il volume (+/- dB)" },
        { DelayPlugin::BUILTIN_ID,           "Delay",          "Eco con ritardo e feedback regolabili" },
        { ReverbPlugin::BUILTIN_ID,          "Reverb",         "Riverbero algoritmico (Freeverb)" },
        { EqPlugin::BUILTIN_ID,              "EQ 3-band",      "Equalizzatore: bass 200Hz, mid 1kHz, treble 6kHz" },
        { CompressorPlugin::BUILTIN_ID,      "Compressor",     "Compressore dinamico con soglia, ratio, attack/release" },
        { LimiterPlugin::BUILTIN_ID,         "Limiter",        "Limitatore di picco a soglia regolabile" },
        { ChorusPlugin::BUILTIN_ID,          "Chorus",         "Chorus stereofonioco con ritardo LFO-modulato" },
        { TremoloPlugin::BUILTIN_ID,         "Tremolo",        "Modulazione d'ampiezza con LFO" },
        { PhaserPlugin::BUILTIN_ID,          "Phaser",         "Phaser a 4 stadi all-pass con sweep LFO" },
        { StereoWidenerPlugin::BUILTIN_ID,   "Stereo Widener", "Amplia o restringe il campo stereo (mid-side)" },
    };
    for (const auto &bi : builtins) {
        auto *item = new QListWidgetItem(bi.name + " — " + bi.desc);
        item->setData(Qt::UserRole,     bi.id);
        item->setData(Qt::UserRole + 1, "builtin");
        builtinList->addItem(item);
    }

    // ── VST2 tab ──────────────────────────────────────────────────────────────
    auto *vstTab    = new QWidget;
    auto *vstLay    = new QVBoxLayout(vstTab);
    auto *vstSearch = new QLineEdit;
    vstSearch->setPlaceholderText("Cerca plugin...");
    vstSearch->setClearButtonEnabled(true);
    auto *vstList = new QListWidget;
    vstLay->addWidget(new QLabel("Plugin VST2 in /usr/lib/vst/"));
    vstLay->addWidget(vstSearch);
    vstLay->addWidget(vstList, 1);
    tabs->addTab(vstTab, "VST2");

    // Scan /usr/lib/vst/
    const QStringList vstDirs = { "/usr/lib/vst", "/usr/lib/lxvst",
                                   QDir::homePath() + "/.vst" };
    for (const QString &dir : vstDirs) {
        for (const QFileInfo &fi : QDir(dir).entryInfoList({"*.so"}, QDir::Files)) {
            auto *item = new QListWidgetItem(fi.fileName());
            item->setData(Qt::UserRole, fi.absoluteFilePath());
            item->setData(Qt::UserRole + 1, "vst2");
            vstList->addItem(item);
        }
    }

    // ── LV2 tab ───────────────────────────────────────────────────────────────
    auto *lv2Tab    = new QWidget;
    auto *lv2Lay    = new QVBoxLayout(lv2Tab);
    auto *lv2Search = new QLineEdit;
    lv2Search->setPlaceholderText("Cerca plugin...");
    lv2Search->setClearButtonEnabled(true);
    auto *lv2List = new QListWidget;
    lv2Lay->addWidget(new QLabel("Plugin LV2 installati:"));
    lv2Lay->addWidget(lv2Search);
    lv2Lay->addWidget(lv2List, 1);
    tabs->addTab(lv2Tab, "LV2");

    // Enumerate via lilv (crash-safe wrapper)
    for (const auto &info : Lv2Plugin::enumerate()) {
        const QString uri  = QString::fromStdString(info.uri);
        const QString name = QString::fromStdString(info.name);
        auto *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, uri);
        item->setData(Qt::UserRole + 1, "lv2");
        lv2List->addItem(item);
    }

    auto filterList = [](QLineEdit *search, QListWidget *list) {
        QObject::connect(search, &QLineEdit::textChanged, list, [list](const QString &text) {
            const QString lower = text.toLower();
            for (int i = 0; i < list->count(); ++i) {
                auto *item = list->item(i);
                item->setHidden(!lower.isEmpty() && !item->text().toLower().contains(lower));
            }
        });
    };
    filterList(vstSearch, vstList);
    filterList(lv2Search, lv2List);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    if (dlg->exec() != QDialog::Accepted) { delete dlg; return nullptr; }

    // Determina quale lista è attiva in base al tab selezionato
    QListWidget *active = nullptr;
    switch (tabs->currentIndex()) {
    case 0: active = builtinList; break;
    case 1: active = vstList;     break;
    case 2: active = lv2List;     break;
    }
    QListWidgetItem *sel = active ? active->currentItem() : nullptr;
    if (!sel) { delete dlg; return nullptr; }
    const QString type = sel->data(Qt::UserRole + 1).toString();
    const QString key  = sel->data(Qt::UserRole).toString();
    delete dlg;

    std::unique_ptr<AudioPlugin> plug;
    if (type == "builtin") {
        if      (key == GainPlugin::BUILTIN_ID)          plug = std::make_unique<GainPlugin>();
        else if (key == DelayPlugin::BUILTIN_ID)         plug = std::make_unique<DelayPlugin>();
        else if (key == ReverbPlugin::BUILTIN_ID)        plug = std::make_unique<ReverbPlugin>();
        else if (key == EqPlugin::BUILTIN_ID)            plug = std::make_unique<EqPlugin>();
        else if (key == CompressorPlugin::BUILTIN_ID)    plug = std::make_unique<CompressorPlugin>();
        else if (key == LimiterPlugin::BUILTIN_ID)       plug = std::make_unique<LimiterPlugin>();
        else if (key == ChorusPlugin::BUILTIN_ID)        plug = std::make_unique<ChorusPlugin>();
        else if (key == TremoloPlugin::BUILTIN_ID)       plug = std::make_unique<TremoloPlugin>();
        else if (key == PhaserPlugin::BUILTIN_ID)        plug = std::make_unique<PhaserPlugin>();
        else if (key == StereoWidenerPlugin::BUILTIN_ID) plug = std::make_unique<StereoWidenerPlugin>();
    } else if (type == "vst2") {
        plug = std::make_unique<VstPlugin>(key.toStdString());
    } else {
        plug = std::make_unique<Lv2Plugin>(key.toStdString());
    }
    if (!plug || !plug->isValid()) {
        QMessageBox::warning(parent, "Errore",
                             QString("Impossibile caricare il plugin:\n%1").arg(key));
        return nullptr;
    }
    return plug;
}

// ── PluginChainWidget ─────────────────────────────────────────────────────────

PluginChainWidget::PluginChainWidget(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    // Toolbar
    auto *toolRow = new QWidget;
    auto *toolLay = new QHBoxLayout(toolRow);
    toolLay->setContentsMargins(0, 0, 0, 0);
    toolLay->setSpacing(4);
    m_addBtn    = new QPushButton("+ Aggiungi effetto");
    m_removeBtn = new QPushButton("−");
    m_upBtn     = new QPushButton("▲");
    m_downBtn   = new QPushButton("▼");
    const QString iconBtnStyle =
        "QPushButton { border: 1px solid transparent; border-radius: 4px; padding: 2px 6px; }"
        "QPushButton:hover   { background: rgba(255,255,255,18); }"
        "QPushButton:pressed { background: rgba(0,0,0,30); }";
    m_removeBtn->setStyleSheet(iconBtnStyle);
    m_upBtn->setStyleSheet(iconBtnStyle);
    m_downBtn->setStyleSheet(iconBtnStyle);
    m_addBtn->setToolTip("Aggiungi un plugin Built-in, VST2 o LV2 alla catena");
    m_removeBtn->setToolTip("Rimuovi l'effetto selezionato");
    m_upBtn->setToolTip("Sposta su");
    m_downBtn->setToolTip("Sposta giù");
    toolLay->addWidget(m_addBtn);
    toolLay->addStretch();
    toolLay->addWidget(m_upBtn);
    toolLay->addWidget(m_downBtn);
    toolLay->addWidget(m_removeBtn);
    lay->addWidget(toolRow);

    // List + placeholder
    auto *listStack = new QStackedWidget;
    m_list = new QListWidget;
    listStack->addWidget(m_list);                          // index 0: normal list
    m_listPlaceholder = new QLabel(
        "Nessun effetto.\nPremi \"+ Aggiungi effetto\" per inserire\n"
        "un effetto Built-in, VST2 o LV2.");
    m_listPlaceholder->setAlignment(Qt::AlignCenter);
    m_listPlaceholder->setStyleSheet("color: #888; font-style: italic;");
    m_listPlaceholder->setWordWrap(true);
    listStack->addWidget(m_listPlaceholder);               // index 1: placeholder
    listStack->setCurrentIndex(1);
    m_listStack = listStack;
    lay->addWidget(listStack);

    // "Apri Editor" on its own row, below the list
    m_openEditorBtn = new QPushButton("Apri editor grafico del plugin");
    m_openEditorBtn->setEnabled(false);
    m_openEditorBtn->setVisible(false);
    lay->addWidget(m_openEditorBtn);

    // Parameter area with placeholder
    m_paramScroll  = new QScrollArea;
    m_paramContent = new QWidget;
    m_paramScroll->setWidget(m_paramContent);
    m_paramScroll->setWidgetResizable(true);
    m_paramScroll->setMinimumHeight(160);
    auto *initLay = new QVBoxLayout(m_paramContent);
    auto *paramPlaceholder = new QLabel("Seleziona un effetto\nper vederne i parametri.");
    paramPlaceholder->setAlignment(Qt::AlignCenter);
    paramPlaceholder->setStyleSheet("color: #888; font-style: italic;");
    initLay->addWidget(paramPlaceholder);
    lay->addWidget(m_paramScroll, 1);

    connect(m_addBtn,        &QPushButton::clicked, this, &PluginChainWidget::onAddPlugin);
    connect(m_removeBtn,     &QPushButton::clicked, this, &PluginChainWidget::onRemovePlugin);
    connect(m_upBtn,         &QPushButton::clicked, this, &PluginChainWidget::onMoveUp);
    connect(m_downBtn,       &QPushButton::clicked, this, &PluginChainWidget::onMoveDown);
    connect(m_openEditorBtn, &QPushButton::clicked, this, &PluginChainWidget::onOpenEditor);
    connect(m_list, &QListWidget::currentRowChanged,
            this,   &PluginChainWidget::onSelectionChanged);
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        const int row = m_list->row(item);
        if (row < 0 || !m_chain || row >= m_chain->count()) return;
        const bool active = (item->checkState() == Qt::Checked);
        m_chain->plugin(row)->setActive(active);
        emit chainModified();
    });

    setChain(nullptr);
}

void PluginChainWidget::setChain(PluginChain *chain) {
    if (m_chain == chain) return;  // same chain — don't rebuild, preserves selection + drag
    m_chain = chain;
    setEnabled(chain != nullptr);
    refresh();
}

void PluginChainWidget::refresh() {
    // Block itemChanged so the active-toggle handler doesn't fire while we rebuild the list
    m_list->blockSignals(true);
    m_list->clear();
    clearParamArea();
    if (!m_chain) {
        m_list->blockSignals(false);
        if (m_listStack) m_listStack->setCurrentIndex(1);
        return;
    }
    for (int i = 0; i < m_chain->count(); ++i) {
        auto *p = m_chain->plugin(i);
        auto *item = new QListWidgetItem(QString::fromStdString(p->name()));
        item->setCheckState(p->isActive() ? Qt::Checked : Qt::Unchecked);
        m_list->addItem(item);
    }
    m_list->blockSignals(false);

    const bool empty = (m_chain->count() == 0);
    if (m_listStack) m_listStack->setCurrentIndex(empty ? 1 : 0);
    if (!empty)
        m_list->setCurrentRow(0);
}

void PluginChainWidget::onAddPlugin() {
    if (!m_chain) return;
    auto plug = runBrowseDialog(this);
    if (!plug) return;

    // Prepare with default engine settings (will be reprepared on next go())
    plug->prepare(48000, 512);
    m_chain->addPlugin(std::move(plug));
    refresh();
    m_list->setCurrentRow(m_chain->count() - 1);
    emit chainModified();
}

void PluginChainWidget::onRemovePlugin() {
    if (!m_chain) return;
    const int row = m_list->currentRow();
    if (row < 0) return;
    m_chain->removePlugin(row);
    refresh();
    emit chainModified();
}

void PluginChainWidget::onMoveUp() {
    if (!m_chain) return;
    const int row = m_list->currentRow();
    if (row <= 0) return;
    m_chain->movePlugin(row, row - 1);
    refresh();
    m_list->setCurrentRow(row - 1);
    emit chainModified();
}

void PluginChainWidget::onMoveDown() {
    if (!m_chain) return;
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_chain->count() - 1) return;
    m_chain->movePlugin(row, row + 1);
    refresh();
    m_list->setCurrentRow(row + 1);
    emit chainModified();
}

void PluginChainWidget::onSelectionChanged() {
    clearParamArea();
    m_openEditorBtn->setEnabled(false);
    if (!m_chain) return;
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_chain->count()) return;
    AudioPlugin *plug = m_chain->plugin(row);
    const bool hasEd = plug->hasEditor();
    m_openEditorBtn->setEnabled(hasEd);
    m_openEditorBtn->setVisible(hasEd);
    buildParamArea(plug, row);
}

void PluginChainWidget::onOpenEditor() {
    if (!m_chain) return;
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_chain->count()) return;

    AudioPlugin *plug = m_chain->plugin(row);
    if (!plug || !plug->hasEditor()) return;

    // Bring existing editor to front instead of opening a second one
    if (m_editorDlg) {
        m_editorDlg->raise();
        m_editorDlg->activateWindow();
        return;
    }

    QDialog *dlg = nullptr;
    if (auto *vst = dynamic_cast<VstPlugin*>(plug))
        dlg = new VstEditorDialog(vst, this);
#ifdef Q_OS_LINUX
    else if (auto *lv2 = dynamic_cast<Lv2Plugin*>(plug))
        dlg = new Lv2EditorDialog(lv2, this);
#else
    Q_UNUSED(plug)
#endif

    if (!dlg) return;
    m_editorDlg = dlg;
    dlg->show();
}

void PluginChainWidget::buildParamArea(AudioPlugin *plugin, int pluginIdx) {
    auto *newContent = new QWidget;
    auto *form       = new QFormLayout(newContent);
    form->setContentsMargins(4, 4, 4, 4);
    form->setSpacing(4);

    // Active toggle
    auto *activeCk = new QCheckBox("Attivo");
    activeCk->setChecked(plugin->isActive());
    connect(activeCk, &QCheckBox::toggled, this, [this, pluginIdx](bool v) {
        onPluginToggled(pluginIdx, v);
    });
    form->addRow("", activeCk);

    const int nParams = plugin->numParams();
    if (nParams == 0) {
        form->addRow(new QLabel("(nessun parametro)"));
    } else {
        const int showMax = qMin(nParams, 40); // cap display for plugins with hundreds of params
        for (int i = 0; i < showMax; ++i) {
            PluginParam p = plugin->param(i);
            const float range = (p.maxVal > p.minVal) ? (p.maxVal - p.minVal) : 1.0f;
            const int   sliderVal = qBound(0, int((p.value - p.minVal) / range * 1000.0f), 1000);

            auto *slider   = new QSlider(Qt::Horizontal);
            if (!s_sliderStyle) s_sliderStyle = new AbsoluteSliderStyle;
            slider->setStyle(s_sliderStyle);
            auto *valLabel = new QLabel(QString::number(double(p.value), 'g', 4));
            valLabel->setFixedWidth(48);
            valLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            slider->setRange(0, 1000);
            slider->setValue(sliderVal);

            auto *row = new QWidget;
            auto *rowLay = new QHBoxLayout(row);
            rowLay->setContentsMargins(0, 0, 0, 0);
            rowLay->setSpacing(4);
            rowLay->addWidget(slider, 1);
            rowLay->addWidget(valLabel);

            const QString lbl = QString::fromStdString(p.name)
                              + (p.unit.empty() ? "" : " [" + QString::fromStdString(p.unit) + "]");
            form->addRow(lbl, row);

            const int   idx      = i;
            const float minVal   = p.minVal;
            const float paramRange = range;
            connect(slider, &QSlider::valueChanged, this,
                    [this, pluginIdx, idx, valLabel, minVal, paramRange](int v) {
                const float newVal = minVal + float(v) / 1000.0f * paramRange;
                valLabel->setText(QString::number(double(newVal), 'g', 4));
                onParamChanged(pluginIdx, idx, v);
            });
        }
        if (nParams > showMax)
            form->addRow(new QLabel(QString("… +%1 parametri").arg(nParams - showMax)));
    }

    m_paramScroll->takeWidget();
    if (m_paramContent) m_paramContent->deleteLater();
    m_paramContent = newContent;
    m_paramScroll->setWidget(newContent);
}

void PluginChainWidget::clearParamArea() {
    m_paramScroll->takeWidget();
    if (m_paramContent) { m_paramContent->deleteLater(); m_paramContent = nullptr; }
    m_paramContent = new QWidget;
    new QVBoxLayout(m_paramContent);
    m_paramScroll->setWidget(m_paramContent);
}

void PluginChainWidget::onParamChanged(int pluginIdx, int paramIdx, int sliderVal) {
    if (!m_chain || pluginIdx >= m_chain->count()) return;
    AudioPlugin *plugin = m_chain->plugin(pluginIdx);
    PluginParam  p      = plugin->param(paramIdx);
    const float range   = (p.maxVal > p.minVal) ? (p.maxVal - p.minVal) : 1.0f;
    const float newVal  = p.minVal + float(sliderVal) / 1000.0f * range;
    plugin->setParam(paramIdx, newVal);
    emit chainModified();
}

void PluginChainWidget::onPluginToggled(int pluginIdx, bool active) {
    if (!m_chain || pluginIdx >= m_chain->count()) return;
    m_chain->plugin(pluginIdx)->setActive(active);
    if (pluginIdx < m_list->count()) {
        // Block itemChanged so our lambda doesn't re-fire setActive
        QSignalBlocker blocker(m_list);
        m_list->item(pluginIdx)->setCheckState(active ? Qt::Checked : Qt::Unchecked);
    }
    emit chainModified();
}

// VstEditorDialog has Q_OBJECT but is defined in this .cpp, not a header.
// AUTOMOC generates a .moc file that must be included here.
#include "PluginChainWidget.moc"
