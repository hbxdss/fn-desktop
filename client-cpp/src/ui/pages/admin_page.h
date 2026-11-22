#pragma once

#include "ui/widgets.h"

#include "core/config.h"

class QLabel;
class QLineEdit;
class QProgressBar;
class QToolButton;
class QWebEnginePage;
class QWebEngineProfile;
class QWebEngineView;

/**
 * 飞牛网页管理后台：内嵌满血 Chromium 浏览器，默认打开 https://<NAS>:5667。
 * 使用「无痕配置」（off-the-record profile）——每次启动都必须重新登录，
 * 不会把密码、Cookie、会话写入磁盘。
 */
class AdminPage : public BasePage
{
    Q_OBJECT

public:
    explicit AdminPage(ApiClient *api, const AppConfig *config, QWidget *parent = nullptr);
    ~AdminPage() override;

    void refresh() override;
    void reloadTheme() override;

    void gotoHome();
    void navigateTo(const QString &input);
    void openAppCenter();
    void openMountSettings();
    void signOut();

    QString currentUrl() const;

private:
    void buildUi();
    void updateIcons();
    void setNotice(const QString &text);
    bool trustedHost(const QString &host) const;
    void injectAutoFill();

    const AppConfig *m_config = nullptr;
    QWebEngineProfile *m_profile = nullptr;
    QWebEnginePage *m_page = nullptr;
    QWebEngineView *m_view = nullptr;
    QLineEdit *m_urlBar = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_notice = nullptr;
    QToolButton *m_back = nullptr;
    QToolButton *m_forward = nullptr;
    QToolButton *m_reload = nullptr;
    QToolButton *m_home = nullptr;
    QToolButton *m_clear = nullptr;
    bool m_loadedOnce = false;
};
