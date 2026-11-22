#include "ui/pages/admin_page.h"

#include "core/secure_vault.h"
#include "ui/icons.h"
#include "ui/pages/browser_page.h"
#include "ui/theme.h"

#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineCertificateError>
#include <QWebEngineCookieStore>
#include <QWebEngineHistory>
#include <QWebEngineNewWindowRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace {

QToolButton *makeToolButton(QWidget *parent, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(30, 30);
    button->setIconSize(QSize(17, 17));
    return button;
}

QString loadInjection()
{
    QFile file(QStringLiteral(":/inject.js"));
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

AdminPage::AdminPage(ApiClient *api, const AppConfig *config, QWidget *parent)
    : BasePage(api, parent), m_config(config)
{
    setPageTitle(QStringLiteral("飞牛网页管理后台"));
    buildUi();
    gotoHome();
}

AdminPage::~AdminPage() = default;

void AdminPage::buildUi()
{
    const Theme::Palette pal = Theme::palette(Theme::idFromString(m_config->uiTheme),
                                              m_config->colorMode == QLatin1String("light"));

    auto *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(6);

    m_back = makeToolButton(toolbar, QStringLiteral("后退"));
    m_forward = makeToolButton(toolbar, QStringLiteral("前进"));
    m_reload = makeToolButton(toolbar, QStringLiteral("刷新"));
    m_home = makeToolButton(toolbar, QStringLiteral("打开飞牛管理后台首页"));
    m_clear = makeToolButton(toolbar, QStringLiteral("退出飞牛网页管理后台登录（优先官方退出）"));

    m_urlBar = new QLineEdit(toolbar);
    m_urlBar->setObjectName(QStringLiteral("UrlBar"));
    m_urlBar->setClearButtonEnabled(true);
    m_urlBar->setPlaceholderText(QStringLiteral("https://NAS地址:5667/"));
    m_urlBar->setMinimumHeight(32);

    toolbarLayout->addWidget(m_back);
    toolbarLayout->addWidget(m_forward);
    toolbarLayout->addWidget(m_reload);
    toolbarLayout->addWidget(m_home);
    toolbarLayout->addSpacing(4);
    toolbarLayout->addWidget(m_urlBar, 1);
    toolbarLayout->addSpacing(4);
    toolbarLayout->addWidget(m_clear);

    m_notice = new QLabel(this);
    m_notice->setObjectName(QStringLiteral("WebNotice"));
    m_notice->setWordWrap(true);
    m_notice->setVisible(false);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("LoadBar"));
    m_progress->setTextVisible(false);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setFixedHeight(3);
    m_progress->setVisible(false);

    m_profile = new QWebEngineProfile(this); // 无痕：不落盘任何 Cookie / 密码 / 缓存
    m_profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);

    m_view = new QWebEngineView(this);
    m_page = new QWebEnginePage(m_profile, m_view);
    m_view->setPage(m_page);
    m_view->setContextMenuPolicy(Qt::DefaultContextMenu);
    m_page->setBackgroundColor(pal.window);

    QWebEngineSettings *settings = m_page->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);
    settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
    settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    settings->setAttribute(QWebEngineSettings::PdfViewerEnabled, true);
    settings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    settings->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, true);
    settings->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, true);

    body()->addWidget(toolbar);
    body()->addWidget(m_notice);
    body()->addWidget(m_progress);
    body()->addWidget(m_view, 1);

    connect(m_back, &QToolButton::clicked, m_view, &QWebEngineView::back);
    connect(m_forward, &QToolButton::clicked, m_view, &QWebEngineView::forward);
    connect(m_reload, &QToolButton::clicked, m_view, &QWebEngineView::reload);
    connect(m_home, &QToolButton::clicked, this, &AdminPage::gotoHome);

    connect(m_clear, &QToolButton::clicked, this, &AdminPage::signOut);

    connect(m_view, &QWebEngineView::urlChanged, this, [this](const QUrl &url) {
        if (!m_urlBar->hasFocus()) {
            m_urlBar->setText(url.toString());
        }
        m_back->setEnabled(m_view->history()->canGoBack());
        m_forward->setEnabled(m_view->history()->canGoForward());
    });

    connect(m_urlBar, &QLineEdit::returnPressed, this,
            [this]() { navigateTo(m_urlBar->text()); });

    connect(m_view, &QWebEngineView::loadStarted, this, [this]() {
        m_progress->setValue(0);
        m_progress->setVisible(true);
    });
    connect(m_view, &QWebEngineView::loadProgress, m_progress, &QProgressBar::setValue);
    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_progress->setVisible(false);
        m_progress->setValue(0);
        if (!ok) {
            // 复用与内置浏览器一致的中文错误页（含主题配色与重试按钮）
            m_view->setHtml(BrowserPage::buildErrorHtml(m_config, m_view->url()), m_view->url());
            setNotice(QStringLiteral("无法打开页面。请确认 NAS 地址正确、飞牛网页管理后台（%1 端口）"
                                     "已在 NAS 上启用，并且当前网络可以访问 NAS。")
                          .arg(m_config->adminPort));
            return;
        }
        setNotice(QString());
        m_loadedOnce = true;

        QJsonArray allowed;
        allowed.append(m_config->host);
        const QString prelude = QStringLiteral("window.__fnosAllowedHosts=%1;")
                                    .arg(QString::fromUtf8(
                                        QJsonDocument(allowed).toJson(QJsonDocument::Compact)));
        m_page->runJavaScript(prelude + QLatin1Char('\n') + loadInjection());
        injectAutoFill();
    });

    connect(m_page, &QWebEnginePage::certificateError, this,
            [this](QWebEngineCertificateError error) {
                // 仅放行用户自己填写的 NAS 主机（自签证书），其他主机一律拒绝
                const bool overridable = error.isOverridable();
                if (overridable && trustedHost(error.url().host())) {
                    error.acceptCertificate();
                } else {
                    error.rejectCertificate();
                }
            });

    connect(m_page, &QWebEnginePage::newWindowRequested, this,
            [this](QWebEngineNewWindowRequest &request) {
                const QUrl target = request.requestedUrl();
                if (target.isValid()) {
                    m_view->setUrl(target);
                }
            });

    m_back->setEnabled(false);
    m_forward->setEnabled(false);
    updateIcons();
}

void AdminPage::updateIcons()
{
    const Theme::Palette pal = Theme::palette(Theme::idFromString(m_config->uiTheme),
                                              m_config->colorMode == QLatin1String("light"));
    m_back->setIcon(Icons::icon(Icons::Kind::Back, pal.text, 17));
    m_forward->setIcon(Icons::icon(Icons::Kind::Forward, pal.text, 17));
    m_reload->setIcon(Icons::icon(Icons::Kind::Refresh, pal.text, 17));
    m_home->setIcon(Icons::icon(Icons::Kind::Home, pal.text, 17));
    m_clear->setIcon(Icons::icon(Icons::Kind::Power, pal.danger, 17));
}

void AdminPage::reloadTheme()
{
    const Theme::Palette pal = Theme::palette(Theme::idFromString(m_config->uiTheme),
                                              m_config->colorMode == QLatin1String("light"));
    if (m_page) {
        m_page->setBackgroundColor(pal.window);
    }
    updateIcons();
}

bool AdminPage::trustedHost(const QString &host) const
{
    const QString target = m_config->host.trimmed().toLower();
    const QString current = host.trimmed().toLower();
    if (target.isEmpty() || current.isEmpty()) {
        return false;
    }
    return current == target;
}

void AdminPage::setNotice(const QString &text)
{
    m_notice->setText(text);
    m_notice->setVisible(!text.isEmpty());
}

void AdminPage::gotoHome()
{
    setNotice(QString());
    m_view->setUrl(QUrl(m_config->adminUrl()));
    m_urlBar->setText(m_config->adminUrl());
}

void AdminPage::navigateTo(const QString &input)
{
    QString text = input.trimmed();
    if (text.isEmpty()) {
        return;
    }
    if (!text.contains(QLatin1String("://"))) {
        text.prepend(QStringLiteral("https://"));
    }
    const QUrl url(text);
    if (!url.isValid()) {
        setNotice(QStringLiteral("地址格式不正确：%1").arg(input.trimmed()));
        return;
    }
    m_view->setUrl(url);
}

QString AdminPage::currentUrl() const
{
    return m_view ? m_view->url().toString() : QString();
}

void AdminPage::refresh()
{
    if (!m_loadedOnce) {
        gotoHome();
    }
}

void AdminPage::injectAutoFill()
{
    // 解密保存的密码；优先会话内存密码，其次磁盘加密密码
    QString password = SecureVault::sessionPassword();
    if (password.isEmpty() && !m_config->savedPasswordCipher.trimmed().isEmpty()) {
        password = SecureVault::unprotect(m_config->savedPasswordCipher);
    }
    const QString username = m_config->username;
    if (password.isEmpty() && username.isEmpty()) {
        return; // 没有可用凭据
    }

    const QString js = QStringLiteral(
        "(function() {"
        "  var tries = 0;"
        "  var maxTries = 60;"
        "  var interval = setInterval(function() {"
        "    tries++;"
        "    if (tries > maxTries) { clearInterval(interval); return; }"
        "    var inputs = document.querySelectorAll('input');"
        "    var pwd = null, user = null;"
        "    for (var i = 0; i < inputs.length; i++) {"
        "      var t = (inputs[i].type || '').toLowerCase();"
        "      if (t === 'password' && inputs[i].offsetWidth > 0) { pwd = inputs[i]; }"
        "    }"
        "    if (pwd) {"
        "      for (var i = 0; i < inputs.length; i++) {"
        "        if (inputs[i] === pwd) break;"
        "        var t = (inputs[i].type || '').toLowerCase();"
        "        if (t === 'text' || t === 'email' || t === 'tel' || t === '') {"
        "          if (inputs[i].offsetWidth > 0) { user = inputs[i]; break; }"
        "        }"
        "      }"
        "    }"
        "    if (pwd) {"
        "      var changeEvt = new Event('input', {bubbles:true});"
        "      var blurEvt = new Event('blur', {bubbles:true});"
        "      if (user) { user.value = '%1'; user.dispatchEvent(changeEvt); user.dispatchEvent(blurEvt); }"
        "      pwd.value = '%2';"
        "      pwd.dispatchEvent(changeEvt); pwd.dispatchEvent(blurEvt);"
        "      var submitBtn = document.querySelector('button[type=\"submit\"]')"
        "                    || pwd.closest('form') && pwd.closest('form').querySelector('button');"
        "%3"
        "      clearInterval(interval);"
        "    }"
        "  }, 280);"
        "})();")
        .arg(username,
             password.toHtmlEscaped().replace(QStringLiteral("\\")
                                                  , QStringLiteral("\\\\")),
             m_config->webAutoLogin
                 ? QStringLiteral(
                       "      if (submitBtn) { setTimeout(function(){ submitBtn.click(); }, 350); }")
                 : QStringLiteral(""));
    m_page->runJavaScript(js);
}

void AdminPage::openAppCenter()
{
    const QString base = m_config->adminUrl();
    // 先跳转首页，再通过注入定位应用商店
    m_view->setUrl(QUrl(base));
    QTimer::singleShot(900, this, [this]() {
        const QString js = QStringLiteral(
            "(function() {"
            "  var tryClick = function(selectors) {"
            "    for (var s = 0; s < selectors.length; s++) {"
            "      var el = document.querySelector(selectors[s]);"
            "      if (el && el.offsetWidth > 0) { el.click(); return true; }"
            "    }"
            "    var links = document.querySelectorAll('a, button, [role=\"menuitem\"]');"
            "    var keywords = ['应用商店','应用中心','App Store','appstore'];"
            "    for (var i = 0; i < links.length; i++) {"
            "      var text = (links[i].innerText || links[i].textContent || '').trim();"
            "      for (var k = 0; k < keywords.length; k++) {"
            "        if (text.indexOf(keywords[k]) !== -1 && links[i].offsetWidth > 0) {"
            "          links[i].click(); return true;"
            "        }"
            "      }"
            "    }"
            "    return false;"
            "  };"
            "  tryClick(['a[href*=\"appstore\"]','a[href*=\"app-store\"]','a[href*=\"appStore\"]','[data-route*=\"app\"]']);"
            "})();");
        m_page->runJavaScript(js);
    });
}

void AdminPage::openMountSettings()
{
    const QString base = m_config->adminUrl();
    m_view->setUrl(QUrl(base));
    QTimer::singleShot(900, this, [this]() {
        const QString js = QStringLiteral(
            "(function() {"
            "  var links = document.querySelectorAll('a, button, [role=\"menuitem\"]');"
            "  var keywords = ['存储','挂载','共享','文件管理','Storage','Mount'];"
            "  for (var i = 0; i < links.length; i++) {"
            "    var text = (links[i].innerText || links[i].textContent || '').trim();"
            "    for (var k = 0; k < keywords.length; k++) {"
            "      if (text.indexOf(keywords[k]) !== -1 && links[i].offsetWidth > 0) {"
            "        links[i].click(); return;"
            "      }"
            "    }"
            "  }"
            "})();");
        m_page->runJavaScript(js);
    });
}

void AdminPage::signOut()
{
    // 优先尝试网页内置的官方退出
    const QString officialJs = QStringLiteral(
        "(function() {"
        "  var candidates = ['退出登录','退出','登出','Log out','Sign out','Logout'];"
        "  var all = document.querySelectorAll('a, button, [role=\"menuitem\"], li');"
        "  for (var i = 0; i < all.length; i++) {"
        "    var t = (all[i].innerText || all[i].textContent || '').trim();"
        "    for (var k = 0; k < candidates.length; k++) {"
        "      if (t.indexOf(candidates[k]) === 0 && all[i].offsetWidth > 0) {"
        "        all[i].click(); return 'official';"
        "      }"
        "    }"
        "  }"
        "  // 没找到官方按钮：走本地 fallback"
        "  return 'fallback';"
        "})();");
    m_page->runJavaScript(officialJs, [this](const QVariant &result) {
        const QString res = result.toString();
        // 只有明确返回 'official' 才算成功触发官方退出；其余（fallback / undefined /
        // 页面拦截 JS 时拿不到返回值）一律走强制清理，避免误判导致会话没清干净。
        if (res == QLatin1String("official")) {
            setNotice(QStringLiteral("已触发网页官方退出，正在返回登录页…"));
            QTimer::singleShot(700, this, [this]() {
                m_profile->cookieStore()->deleteAllCookies();
                gotoHome();
            });
        } else {
            m_profile->cookieStore()->deleteAllCookies();
            m_page->runJavaScript(QStringLiteral(
                "try{localStorage.clear();sessionStorage.clear();}catch(e){}"));
            setNotice(QStringLiteral("未找到官方退出按钮，已强制清除网页会话，正在返回登录页…"));
            QTimer::singleShot(260, this, [this]() { gotoHome(); });
        }
    });
}
