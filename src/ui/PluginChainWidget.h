#pragma once
#include <QWidget>
#include <QPointer>

class PluginChain;
class AudioPlugin;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QLabel;
class QDialog;

class PluginChainWidget : public QWidget {
    Q_OBJECT
public:
    explicit PluginChainWidget(QWidget *parent = nullptr);

    void setChain(PluginChain *chain);   // null = hide all
    void refresh();                       // reload from chain
    void detachChain();                   // nullify ptr without rebuild (prevents dangling access)
    PluginChain *chainPtr() const { return m_chain; }

signals:
    void chainModified();

private slots:
    void onAddPlugin();
    void onRemovePlugin();
    void onMoveUp();
    void onMoveDown();
    void onSelectionChanged();
    void onParamChanged(int pluginIdx, int paramIdx, int sliderVal);
    void onPluginToggled(int pluginIdx, bool active);
    void onOpenEditor();

private:
    void buildParamArea(AudioPlugin *plugin, int pluginIdx);
    void clearParamArea();

    PluginChain *m_chain = nullptr;

    QStackedWidget *m_listStack        = nullptr;
    QListWidget    *m_list;
    QLabel         *m_listPlaceholder  = nullptr;
    QPushButton    *m_addBtn;
    QPushButton    *m_removeBtn;
    QPushButton    *m_upBtn;
    QPushButton    *m_downBtn;
    QPushButton    *m_openEditorBtn    = nullptr;

    QScrollArea   *m_paramScroll;
    QWidget       *m_paramContent;

    QPointer<QDialog> m_editorDlg;
};
