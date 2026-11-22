#pragma once

#include <QMainWindow>
#include <QVector>

#include "core/config.h"
#include "ui/theme.h"

class QAction;
class QActionGroup;
class QLabel;
class QListWidget;
class QMenu;
class QStackedWidget;
class QSystemTrayIcon;

class ApiClient;
class Backdrop;
class TitleBar;
class BasePage;
class AdminPage;
class BrowserPage;
class DashboardPage;
class FilesPage;
class DockerPage;
class UsersPage;
class AuditPage;
class SshPage;
class SettingsPage;

/**
 * 主窗口：无边框 + 自绘标题栏 + 侧边导航 + 内容堆栈。
 *
 * 导航顺序：应用（多标签内嵌浏览器，默认）→ 飞牛网页管理后台 → 仪表盘 →
 * 文件管理 → 容器管理 → 用户管理 → 操作日志 → SSH 终端 → 设置。外观（主题1 ~ 主题4，
 * 每套均含浅色与深色，强调色可自定义）由标题栏「外观」菜单与设置页共同驱动，
 * 全局只使用一套色板，不会出现明暗割裂。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ApiClient *api, AppConfig *config, QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    /** 用户主动退出登录（令牌失效或点击退出），调用方应重新弹出登录框。 */
    void logoutRequested();

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    QWidget *buildSidebar();
    void buildPages();
    void buildAppearanceMenu();
    void syncAppearanceMenu();
    void openAppearanceDialog();
    void applyAppearance(Theme::Id id, bool light, const QString &accent, bool persist);
    void applyAppearanceFromConfig();
    void reloadNavIcons();
    void selectPage(const QString &key);
    void updateFoot();
    void toggleMaximize();
    /** 最大化 / 全屏时窗口贴边，圆角需要归零，否则四个角会露出背景。 */
    void syncWindowRadius();
    void signOut();
    /** 首次使用引导：未完成向导时弹出多步向导 */
    void checkFirstRunSetup();
    /** 依据 config.advancedMode 刷新侧栏分组与当前页（Task 11 完整实现）。 */
    void applyAdvancedMode();
    /** 重新打开新手向导（设置页入口也可用）。 */
    void reopenWizard();
    QString subtitleText() const;

    ApiClient *m_api = nullptr;
    AppConfig *m_config = nullptr;

    Backdrop *m_backdrop = nullptr;
    TitleBar *m_titleBar = nullptr;
    QListWidget *m_nav = nullptr;
    QStackedWidget *m_stack = nullptr;

    BrowserPage *m_browserPage = nullptr;
    AdminPage *m_adminPage = nullptr;
    DashboardPage *m_dashboardPage = nullptr;
    FilesPage *m_filesPage = nullptr;
    DockerPage *m_dockerPage = nullptr;
    UsersPage *m_usersPage = nullptr;
    AuditPage *m_auditPage = nullptr;
    SshPage *m_sshPage = nullptr;
    SettingsPage *m_settingsPage = nullptr;
    QVector<BasePage *> m_pages;

    QLabel *m_footUser = nullptr;
    QLabel *m_footHost = nullptr;

    QMenu *m_appearanceMenu = nullptr;
    QActionGroup *m_themeGroup = nullptr;
    QVector<QAction *> m_themeActions;
    QVector<QAction *> m_accentActions;
    QAction *m_darkAction = nullptr;
    QAction *m_lightAction = nullptr;

    Theme::Id m_theme = Theme::Id::Win11;
    bool m_light = false;
    QString m_accent;
    bool m_setupChecked = false;
};
