#pragma once
#include <QWidget>

class PluginChain;
class AudioPlugin;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QFormLayout;
class QSlider;
class QCheckBox;
class QLabel;

class PluginChainWidget : public QWidget {
    Q_OBJECT
public:
    explicit PluginChainWidget(QWidget *parent = nullptr);

    void setChain(PluginChain *chain);   // null = hide all
    void refresh();                       // reload from chain

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

private:
    void buildParamArea(AudioPlugin *plugin, int pluginIdx);
    void clearParamArea();

    PluginChain *m_chain = nullptr;

    QListWidget   *m_list;
    QPushButton   *m_addBtn;
    QPushButton   *m_removeBtn;
    QPushButton   *m_upBtn;
    QPushButton   *m_downBtn;

    QScrollArea   *m_paramScroll;
    QWidget       *m_paramContent;
};
