#pragma once

#include <QHash>
#include <QJsonObject>

#include "ui/widgets.h"

class QLabel;
class QProgressBar;
class QComboBox;
class QTimer;
class QVBoxLayout;
class AppConfig;

/** 仪表盘：CPU / 内存占用率 / 运行时长 / 容器 / 应用数量 + 系统信息 + 存储空间。 */
class DashboardPage : public BasePage
{
    Q_OBJECT

public:
    explicit DashboardPage(ApiClient *api, AppConfig *config = nullptr, QWidget *parent = nullptr);

    void refresh() override;
    void reloadTheme() override;

private:
    void buildUi();
    void load();
    void renderInfo(const QJsonObject &data);
    void renderStorage(const QJsonObject &data);
    void renderDocker(const QJsonObject &data);
    void renderApps(const QJsonObject &data);
    void clearStorageRows();
    void applyRefreshInterval(int seconds);

    AppConfig *m_config = nullptr;
    QTimer *m_timer = nullptr;
    QComboBox *m_intervalBox = nullptr;
    StatCard *m_cpuCard = nullptr;
    StatCard *m_memCard = nullptr;
    StatCard *m_uptimeCard = nullptr;
    StatCard *m_dockerCard = nullptr;
    StatCard *m_appsCard = nullptr;
    QProgressBar *m_cpuBar = nullptr;
    QProgressBar *m_memBar = nullptr;
    QLabel *m_cpuText = nullptr;
    QLabel *m_memText = nullptr;
    QVBoxLayout *m_storageBox = nullptr;
    QLabel *m_storageSummary = nullptr;
    QLabel *m_updated = nullptr;
    QHash<QString, QLabel *> m_infoValues;
    bool m_warned = false;
};
