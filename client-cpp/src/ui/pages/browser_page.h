#pragma once

#include "core/config.h"
#include "ui/icons.h"
#include "ui/widgets.h"

#include <QJsonArray>
#include <QUrl>
#include <QVector>
#include <QWebEnginePage>
#include <QWebEngineView>

class QHBoxLayout;
class QLineEdit;
class QProgressBar;
class QScrollArea;
class QTabWidget;
class QTimer;
class QToolButton;
class QWebEngineDownloadRequest;
class QWebEngineProfile;

/**
 * 内嵌网页：统一放行飞牛的自签证书。
 *
 * 注意：在 Qt 6.7.2 里 QWebEnginePage::certificateError 是**信号**而不是可重写的
 * 虚函数（没有 bool certificateError(const QWebEngineCertificateError &) 这个虚接口），
 * 因此在这个子类的构造函数里连接该信号，对可覆盖的证书错误调用 acceptCertificate()
 * 放行；未放行的错误会在对象析构时被自动拒绝。
 */
class FnWebPage : public QWebEnginePage
{
    Q_OBJECT

public:
    explicit FnWebPage(QWebEngineProfile *profile, QObject *parent = nullptr);
};

/** 单标签页视图：额外记录「首页」地址，便于空白标签回首页。 */
class FnWebView : public QWebEngineView
{
    Q_OBJECT

public:
    explicit FnWebView(QWidget *parent = nullptr);

    void setHomeUrl(const QUrl &url) { m_homeUrl = url; }
    QUrl homeUrl() const { return m_homeUrl; }

private:
    QUrl m_homeUrl;
};

/** 应用页里的一张应用卡片：官方应用 / Docker 应用 / 用户自定义应用。 */
struct AppCardEntry
{
    QString name;
    QString url;
    QString source;      // official | docker | custom
    QString hint;        // 卡片第二行说明
    QString appKey;      // video | photo | music（飞牛内置应用标识，其余为空）
    bool installed = true;
    int customIndex = -1; // 自定义应用在 AppConfig::apps 里的下标
};

/**
 * 「应用」页：应用卡片 + 多标签内嵌浏览器。
 *
 * - 卡片来源：服务端 /api/apps 自动发现的飞牛官方应用（影视 / 相册 / 音乐）与
 *   Docker 容器（按发布端口拼 http://NAS:端口），外加用户自己「新建」的应用。
 * - 官方应用未安装时卡片标注「未安装」，点击会提示去应用中心下载。
 * - 标签页常驻：切换到其他导航页不会销毁或重载，等同浏览器标签。
 * - 打开同一应用的地址会复用已有标签，不再重复开新标签。
 */
class BrowserPage : public BasePage
{
    Q_OBJECT

public:
    /**
     * 生成与应用主题一致的内嵌中文错误页（管理后台失败页复用）。
     */
    static QString buildErrorHtml(const AppConfig *config, const QUrl &url);
    explicit BrowserPage(ApiClient *api, AppConfig *config, QWidget *parent = nullptr);
    ~BrowserPage() override;

    void refresh() override;
    void reloadTheme() override;

    /** 服务器地址变化后重新探测并生成应用卡片。 */
    void reloadApps();

    /** 在当前标签打开一个地址（无协议前缀时自动补 https://）。 */
    void navigateTo(const QString &input);

signals:
    /** 用户在安装引导中选择「软件内打开应用中心」：主窗口切到后台页并定位应用中心。 */
    void openAppCenterRequested();

private:
    void buildUi();
    void rebuildAppCards();
    void requestDiscoveredApps();
    void launchApp(const AppCardEntry &entry);
    void showNotInstalled(const AppCardEntry &entry);
    void createCustomApp();
    void editCustomApp(int customIndex);
    void removeCustomApp(int customIndex);
    void showCardMenu(const AppCardEntry &entry, const QPoint &globalPos);
    void openInTab(const QUrl &url, const QString &title);
    void saveCustomApps(const QVector<FnAppEntry> &apps);

    FnWebView *createTab(const QUrl &url, const QString &title, bool activate);
    FnWebView *createWelcomeTab();
    FnWebView *currentView() const;
    FnWebView *viewAt(int index) const;
    int tabIndexOf(const FnWebView *view) const;
    FnWebView *findTabByUrl(const QString &url) const;
    void closeTab(int index);

    void updateIcons();
    void syncAddressBar();
    void updateNavState();
    void setLoadingUi(bool loading);
    void showErrorPage(FnWebView *view, const QUrl &url);
    QString welcomeHtml() const;

    void openExternal();
    void onDownloadRequested(QWebEngineDownloadRequest *item);

    QString homeUrlString() const;

    AppConfig *m_config = nullptr;
    QWebEngineProfile *m_profile = nullptr;

    QHBoxLayout *m_cardLayout = nullptr;
    QScrollArea *m_cardScroll = nullptr;

    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_urlBar = nullptr;
    QProgressBar *m_progress = nullptr;
    QTimer *m_progressTimer = nullptr;

    QToolButton *m_back = nullptr;
    QToolButton *m_forward = nullptr;
    QToolButton *m_reload = nullptr;
    QToolButton *m_external = nullptr;

    QVector<AppCardEntry> m_cards;  // 当前展示的应用卡片
    QJsonArray m_official;          // 服务端探测到的官方应用
    QJsonArray m_docker;            // 服务端探测到的 Docker 应用
    FnWebView *m_welcomeView = nullptr;

    bool m_userStopped = false;     // 用户主动停止加载，不要显示错误页
    bool m_loadedOnce = false;      // 是否已经完成首轮探测
};
