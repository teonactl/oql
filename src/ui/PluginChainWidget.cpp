#include "PluginChainWidget.h"
#include "engine/AudioPlugin.h"
#include "engine/PluginChain.h"
#include "engine/VstPlugin.h"
#include "engine/Lv2Plugin.h"
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
#include <lilv/lilv.h>
#include <memory>

// ── VST2 native editor dialog ─────────────────────────────────────────────────

class VstEditorDialog : public QDialog {
    Q_OBJECT
public:
    VstEditorDialog(VstPlugin *plugin, QWidget *parent = nullptr)
        : QDialog(parent, Qt::Window), m_plugin(plugin)
    {
        setWindowTitle(QString::fromStdString(plugin->name()));
        setAttribute(Qt::WA_DeleteOnClose);

        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);

        // Native container: VST2 embeds its X11 child window here
        m_container = new QWidget(this);
        m_container->setAttribute(Qt::WA_NativeWindow, true);
        m_container->setAttribute(Qt::WA_PaintOnScreen, true);
        m_container->setMinimumSize(100, 100);
        lay->addWidget(m_container);

        // Force creation of the native (X11) window before passing handle to plugin
        m_container->winId();

        // Query rect BEFORE open (some plugins report it then)
        applyEditorRect(plugin);

        plugin->openEditor(reinterpret_cast<void*>(m_container->winId()));

        // Re-query rect AFTER open (most plugins report it then)
        applyEditorRect(plugin);

        adjustSize();

        // Drive the plugin's UI refresh loop (~30 fps)
        m_idleTimer = new QTimer(this);
        m_idleTimer->setInterval(33);
        connect(m_idleTimer, &QTimer::timeout, this, [this]() {
            if (m_plugin) m_plugin->editorIdle();
        });
        m_idleTimer->start();
    }

    ~VstEditorDialog() override {
        m_idleTimer->stop();
        if (m_plugin) m_plugin->closeEditor();
    }

private:
    void applyEditorRect(VstPlugin *plugin) {
        ERect *rect = nullptr;
        if (!plugin->getEditorRect(&rect) || !rect) return;
        const int w = rect->right  - rect->left;
        const int h = rect->bottom - rect->top;
        if (w > 0 && h > 0) m_container->setFixedSize(w, h);
    }

    VstPlugin *m_plugin;
    QWidget   *m_container;
    QTimer    *m_idleTimer;
};

// ── Plugin browser dialog ─────────────────────────────────────────────────────

static std::unique_ptr<AudioPlugin> runBrowseDialog(QWidget *parent) {
    auto *dlg = new QDialog(parent);
    dlg->setWindowTitle("Aggiungi effetto");
    dlg->resize(480, 400);

    auto *lay  = new QVBoxLayout(dlg);
    auto *tabs = new QTabWidget;
    lay->addWidget(tabs);

    // ── VST2 tab ──────────────────────────────────────────────────────────────
    auto *vstTab  = new QWidget;
    auto *vstLay  = new QVBoxLayout(vstTab);
    auto *vstList = new QListWidget;
    vstLay->addWidget(new QLabel("Plugin VST2 in /usr/lib/vst/"));
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
    auto *lv2Tab  = new QWidget;
    auto *lv2Lay  = new QVBoxLayout(lv2Tab);
    auto *lv2List = new QListWidget;
    lv2Lay->addWidget(new QLabel("Plugin LV2 installati:"));
    lv2Lay->addWidget(lv2List, 1);
    tabs->addTab(lv2Tab, "LV2");

    // Enumerate via lilv
    LilvWorld       *w    = Lv2Plugin::world();
    const LilvPlugins *all = lilv_world_get_all_plugins(w);
    LILV_FOREACH(plugins, it, all) {
        const LilvPlugin *p = lilv_plugins_get(all, it);
        LilvNode *nameNode  = lilv_plugin_get_name(p);
        const LilvNode *uriNode = lilv_plugin_get_uri(p);
        const QString name = nameNode ? lilv_node_as_string(nameNode) : "?";
        const QString uri  = uriNode  ? lilv_node_as_uri(uriNode)     : "";
        lilv_node_free(nameNode);
        // Only audio effect plugins (skip instruments/MIDI)
        auto *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, uri);
        item->setData(Qt::UserRole + 1, "lv2");
        lv2List->addItem(item);
    }

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    if (dlg->exec() != QDialog::Accepted) { delete dlg; return nullptr; }

    // Pick the selected item from the active tab
    QListWidget *active = (tabs->currentIndex() == 0) ? vstList : lv2List;
    QListWidgetItem *sel = active->currentItem();
    delete dlg;
    if (!sel) return nullptr;

    const QString type = sel->data(Qt::UserRole + 1).toString();
    const QString key  = sel->data(Qt::UserRole).toString();

    std::unique_ptr<AudioPlugin> plug;
    if (type == "vst2") {
        plug = std::make_unique<VstPlugin>(key.toStdString());
    } else {
        plug = std::make_unique<Lv2Plugin>(key.toStdString());
    }
    if (!plug->isValid()) {
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
    m_addBtn    = new QPushButton("+");  m_addBtn->setFixedWidth(28);
    m_removeBtn = new QPushButton("−");  m_removeBtn->setFixedWidth(28);
    m_upBtn     = new QPushButton("▲");  m_upBtn->setFixedWidth(28);
    m_downBtn   = new QPushButton("▼");  m_downBtn->setFixedWidth(28);
    toolLay->addWidget(new QLabel("Effetti:"));
    toolLay->addStretch();
    toolLay->addWidget(m_upBtn);
    toolLay->addWidget(m_downBtn);
    toolLay->addWidget(m_removeBtn);
    toolLay->addWidget(m_addBtn);
    lay->addWidget(toolRow);

    m_list = new QListWidget;
    m_list->setMaximumHeight(90);
    lay->addWidget(m_list);

    // "Apri Editor" on its own row, below the list
    m_openEditorBtn = new QPushButton("Apri editor nativo");
    m_openEditorBtn->setEnabled(false);
    m_openEditorBtn->setVisible(false);
    lay->addWidget(m_openEditorBtn);

    // Parameter area
    m_paramScroll  = new QScrollArea;
    m_paramContent = new QWidget;
    m_paramScroll->setWidget(m_paramContent);
    m_paramScroll->setWidgetResizable(true);
    m_paramScroll->setMinimumHeight(100);
    new QVBoxLayout(m_paramContent); // empty initial layout
    lay->addWidget(m_paramScroll, 1);

    connect(m_addBtn,        &QPushButton::clicked, this, &PluginChainWidget::onAddPlugin);
    connect(m_removeBtn,     &QPushButton::clicked, this, &PluginChainWidget::onRemovePlugin);
    connect(m_upBtn,         &QPushButton::clicked, this, &PluginChainWidget::onMoveUp);
    connect(m_downBtn,       &QPushButton::clicked, this, &PluginChainWidget::onMoveDown);
    connect(m_openEditorBtn, &QPushButton::clicked, this, &PluginChainWidget::onOpenEditor);
    connect(m_list, &QListWidget::currentRowChanged,
            this,   &PluginChainWidget::onSelectionChanged);

    setChain(nullptr);
}

void PluginChainWidget::setChain(PluginChain *chain) {
    m_chain = chain;
    setEnabled(chain != nullptr);
    refresh();
}

void PluginChainWidget::refresh() {
    m_list->clear();
    clearParamArea();
    if (!m_chain) return;
    for (int i = 0; i < m_chain->count(); ++i) {
        auto *p = m_chain->plugin(i);
        auto *item = new QListWidgetItem(QString::fromStdString(p->name()));
        item->setCheckState(p->isActive() ? Qt::Checked : Qt::Unchecked);
        m_list->addItem(item);
    }
    if (m_chain->count() > 0)
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

    VstPlugin *vst = dynamic_cast<VstPlugin*>(m_chain->plugin(row));
    if (!vst || !vst->hasEditor()) return;

    // Bring existing editor to front instead of opening a second one
    if (m_editorDlg) {
        m_editorDlg->raise();
        m_editorDlg->activateWindow();
        return;
    }

    auto *dlg = new VstEditorDialog(vst, this);
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
            const int   sliderVal = int((p.value - p.minVal) / range * 1000.0f);

            auto *slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, 1000);
            slider->setValue(sliderVal);

            const QString lbl = QString::fromStdString(p.name)
                              + (p.unit.empty() ? "" : " [" + QString::fromStdString(p.unit) + "]");
            form->addRow(lbl, slider);

            const int idx = i; // capture by value
            connect(slider, &QSlider::valueChanged, this, [this, pluginIdx, idx](int v) {
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
    if (pluginIdx < m_list->count())
        m_list->item(pluginIdx)->setCheckState(active ? Qt::Checked : Qt::Unchecked);
    emit chainModified();
}

// VstEditorDialog has Q_OBJECT but is defined in this .cpp, not a header.
// AUTOMOC generates a .moc file that must be included here.
#include "PluginChainWidget.moc"
