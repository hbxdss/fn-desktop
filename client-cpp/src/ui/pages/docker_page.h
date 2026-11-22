#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include "ui/icons.h"
#include "ui/widgets.h"

class AppConfig;

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QTimer;

/** Docker 列表为空时的占位提示（图标 + 中文说明）。 */
class DockerEmptyState : public QWidget
{
    Q_OBJECT

public:
    DockerEmptyState(Icons::Kind kind, const QString &text, QWidget *parent = nullptr);

    void setText(const QString &text);
    void refreshTheme();

private:
    Icons::Kind m_kind;
    QLabel *m_icon = nullptr;
    QLabel *m_label = nullptr;
};

/** Docker 管理：容器 / 镜像 / 数据卷 / 网络。 */
class DockerPage : public BasePage
{
    Q_OBJECT

public:
    explicit DockerPage(ApiClient *api, AppConfig *config, QWidget *parent = nullptr);

    void refresh() override;
    void reloadTheme() override;

private:
    void buildUi();
    void buildStatusBar();
    QWidget *buildContainerTab();
    QWidget *buildImagesTab();
    QWidget *buildVolumesTab();
    QWidget *buildNetworksTab();
    QWidget *buildUnavailable();
    void applyIcons();

    void applyRefreshInterval(int sec);

    void refreshAll(bool manual);
    void loadOverview(bool manual);
    void loadTab(int index, bool manual);
    void loadContainers(bool manual);
    void loadImages(bool manual);
    void loadVolumes(bool manual);
    void loadNetworks(bool manual);
    void setDockerAvailable(bool available, const QString &detail);
    void updateBusy();

    void applyContainerFilter();
    void fillContainerTable(const QVector<QJsonObject> &rows);
    QMenu *buildContainerMenu(const QString &id, const QString &name, const QString &state);
    void showContainerMenu(const QPoint &pos);
    void runContainerAction(const QString &id, const QString &name, const QString &verb,
                            const QString &label);
    void openCreateContainer();
    void openContainerLogs(const QString &id, const QString &name);
    void renameContainer(const QString &id, const QString &currentName);
    void deleteContainer(const QString &id, const QString &name);
    void showContainerDetails(const QString &id, const QString &name);

    void pullImage();
    void deleteImage();
    void pruneImages();

    void openCreateVolume();
    void deleteVolume();
    void openCreateNetwork();
    void deleteNetwork();

    QStackedWidget *m_rootStack = nullptr;
    QWidget *m_normalPage = nullptr;
    QWidget *m_unavailablePage = nullptr;
    bool m_available = false;
    AppConfig *m_config = nullptr;

    // 顶部状态条
    QWidget *m_statusBar = nullptr;
    QPushButton *m_headerRefresh = nullptr;
    QLabel *m_versionLabel = nullptr;
    QLabel *m_containerStat = nullptr;
    QLabel *m_imageStat = nullptr;
    QLabel *m_sizeStat = nullptr;
    QLabel *m_unavailableDetail = nullptr;
    QCheckBox *m_autoRefresh = nullptr;
    QComboBox *m_intervalBox = nullptr;
    QPushButton *m_retryButton = nullptr;

    QTabWidget *m_tabs = nullptr;

    // 容器
    QLineEdit *m_containerSearch = nullptr;
    QComboBox *m_containerStateFilter = nullptr;
    QCheckBox *m_showStopped = nullptr;
    QStackedWidget *m_containerStack = nullptr;
    QTableWidget *m_containerTable = nullptr;
    DockerEmptyState *m_containerEmpty = nullptr;
    QPushButton *m_containerRefresh = nullptr;
    QPushButton *m_newContainerButton = nullptr;
    QVector<QJsonObject> m_containerRows;

    // 镜像
    QStackedWidget *m_imageStack = nullptr;
    QTableWidget *m_imageTable = nullptr;
    DockerEmptyState *m_imageEmpty = nullptr;
    QLineEdit *m_imagePullEdit = nullptr;
    QPushButton *m_imagePullButton = nullptr;
    QPushButton *m_imageDeleteButton = nullptr;
    QPushButton *m_imagePruneButton = nullptr;
    QPushButton *m_imageRefresh = nullptr;
    QStringList m_imageRefs;

    // 数据卷
    QStackedWidget *m_volumeStack = nullptr;
    QTableWidget *m_volumeTable = nullptr;
    DockerEmptyState *m_volumeEmpty = nullptr;
    QPushButton *m_volumeCreateButton = nullptr;
    QPushButton *m_volumeDeleteButton = nullptr;
    QPushButton *m_volumeRefresh = nullptr;
    QStringList m_volumeNames;

    // 网络
    QStackedWidget *m_networkStack = nullptr;
    QTableWidget *m_networkTable = nullptr;
    DockerEmptyState *m_networkEmpty = nullptr;
    QPushButton *m_networkCreateButton = nullptr;
    QPushButton *m_networkDeleteButton = nullptr;
    QPushButton *m_networkRefresh = nullptr;
    QStringList m_networkNames;

    QTimer *m_autoTimer = nullptr;
    int m_loading = 0;
    int m_actions = 0;
};
