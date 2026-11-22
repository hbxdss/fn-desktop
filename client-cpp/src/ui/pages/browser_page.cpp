#include "ui/pages/browser_page.h"

#include "core/api_client.h"
#include "ui/app_install_dialog.h"
#include "ui/icons.h"
#include "ui/theme.h"

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineCertificateError>
#include <QWebEngineDownloadRequest>
#include <QWebEngineHistory>
#include <QWebEngineNewWindowRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace {

/** 标签标题最长保留的字符数（含省略号）。 */
const int kTitleMaxChars = 18;

QString shortenTitle(const QString &text)
{
    const QString clean = text.simplified();
    if (clean.size() <= kTitleMaxChars) {
        return clean;
    }
    return clean.left(kTitleMaxChars - 1) + QStringLiteral("…");
}

QString addressText(const QUrl &url)
{
    if (url.isEmpty()) {
        return QString();
    }
    // about: / data: 是内部页面（起始页、错误页）的地址，
    // data: 会把整段 HTML 塞进地址栏，必须过滤掉。
    const QString scheme = url.scheme();
    if (scheme == QLatin1String("about") || scheme == QLatin1String("data")
        || scheme == QLatin1String("blob")) {
        return QString();
    }
    return url.toString();
}

/** 去掉末尾的斜杠，便于比较「是不是同一个应用」。 */
QString normalizedUrl(const QString &url)
{
    QString key = url.trimmed();
    while (key.endsWith(QLatin1Char('/'))) {
        key.chop(1);
    }
    return key;
}

QToolButton *makeToolButton(QWidget *parent, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(30, 30);
    button->setIconSize(QSize(17, 17));
    return button;
}

/** 与 admin_page 保持一致的一套网页能力开关。 */
void applyWebSettings(QWebEnginePage *page)
{
    QWebEngineSettings *settings = page->settings();
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
    settings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
}

QString cssColor(const QColor &color)
{
    if (color.alpha() >= 255) {
        return color.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}

/** 识别飞牛内置应用：返回 video / photo / music，识别不到返回空串。 */
QString fnAppKey(const QString &name)
{
    if (name.contains(QStringLiteral("影视"))) {
        return QStringLiteral("video");
    }
    if (name.contains(QStringLiteral("相册")) || name.contains(QStringLiteral("照片"))) {
        return QStringLiteral("photo");
    }
    if (name.contains(QStringLiteral("音乐"))) {
        return QStringLiteral("music");
    }
    return QString();
}

bool fnAppVisible(const AppConfig *config, const QString &key)
{
    if (key == QLatin1String("video")) {
        return config->showFnVideo;
    }
    if (key == QLatin1String("photo")) {
        return config->showFnPhoto;
    }
    if (key == QLatin1String("music")) {
        return config->showFnMusic;
    }
    return true;
}

Icons::Kind fnAppIcon(const QString &key, const QString &source)
{
    if (key == QLatin1String("video")) {
        return Icons::Kind::Film;
    }
    if (key == QLatin1String("photo")) {
        return Icons::Kind::Photo;
    }
    if (key == QLatin1String("music")) {
        return Icons::Kind::Music;
    }
    if (source == QLatin1String("docker")) {
        return Icons::Kind::Docker;
    }
    if (source == QLatin1String("add")) {
        return Icons::Kind::Plus;
    }
    return Icons::Kind::Globe;
}

/** 应用卡片：圆角按钮，左侧图标徽章 + 右侧两行文字（名称 + 说明）。 */
QPushButton *makeAppCard(QWidget *parent, Icons::Kind iconKind, const QString &name,
                         const QString &hint, const QString &state)
{
    auto *card = new QPushButton(parent);
    card->setObjectName(QStringLiteral("AppCard"));
    card->setProperty("state", state);
    card->setCursor(Qt::PointingHandCursor);
    card->setFixedSize(152, 68);
    card->setFocusPolicy(Qt::StrongFocus);

    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(12, 10, 12, 10);
    row->setSpacing(10);

    auto *badge = new QLabel(card);
    badge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    badge->setFixedSize(36, 36);
    badge->setAlignment(Qt::AlignCenter);
    const QColor badgeColor = state == QLatin1String("missing")
                                  ? Theme::current().muted
                                  : Theme::current().accent;
    badge->setPixmap(Icons::pixmap(iconKind, badgeColor, 20));

    auto *column = new QVBoxLayout();
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(3);

    auto *nameLabel = new QLabel(name, card);
    nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    nameLabel->setStyleSheet(
        QStringLiteral("font-size:13px;font-weight:600;background:transparent;"));
    auto *hintLabel = new QLabel(hint, card);
    hintLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    hintLabel->setProperty("role", QStringLiteral("muted"));
    hintLabel->setStyleSheet(QStringLiteral("font-size:11px;background:transparent;"));
    hintLabel->setToolTip(hint);

    column->addWidget(nameLabel);
    column->addWidget(hintLabel, 1);
    row->addWidget(badge, 0, Qt::AlignVCenter);
    row->addLayout(column, 1);
    return card;
}

} // namespace

// ---------------------------------------------------------------------------
// FnWebPage / FnWebView
// ---------------------------------------------------------------------------

FnWebPage::FnWebPage(QWebEngineProfile *profile, QObject *parent)
    : QWebEnginePage(profile, parent)
{
    // 飞牛网页端使用自签名证书：不显式放行就会整页白屏。
    connect(this, &QWebEnginePage::certificateError, this,
            [](const QWebEngineCertificateError &error) {
                if (error.isOverridable()) {
                    QWebEngineCertificateError mutableError = error;
                    mutableError.acceptCertificate();
                }
            });
}

FnWebView::FnWebView(QWidget *parent) : QWebEngineView(parent)
{
    setContextMenuPolicy(Qt::DefaultContextMenu);
}

// ---------------------------------------------------------------------------
// BrowserPage
// ---------------------------------------------------------------------------

BrowserPage::BrowserPage(ApiClient *api, AppConfig *config, QWidget *parent)
    : BasePage(api, parent), m_config(config)
{
    buildUi();
    rebuildAppCards();
    createWelcomeTab();
    updateIcons();
    m_loadedOnce = true;
    requestDiscoveredApps();
}

BrowserPage::~BrowserPage() = default;

void BrowserPage::buildUi()
{
    setPageTitle(QStringLiteral("应用"));
    setPageSubtitle(QStringLiteral("官方应用、Docker 应用与自定义应用，全部内嵌在常驻标签页中打开"));

    if (QLayout *root = layout()) {
        root->setContentsMargins(16, 12, 16, 16);
    }
    body()->setSpacing(10);

    // 持久化网页配置：Cookie / 缓存 / 本地存储都落在配置目录下的 webprofile，
    // 重启客户端后飞牛网页的登录状态仍然保留。
    const QString profileRoot = AppConfig::dataDir() + QStringLiteral("/webprofile");
    QDir().mkpath(profileRoot);
    QDir().mkpath(profileRoot + QStringLiteral("/cache"));

    m_profile = new QWebEngineProfile(QStringLiteral("fnos-browser"), this);
    m_profile->setPersistentStoragePath(profileRoot);
    m_profile->setCachePath(profileRoot + QStringLiteral("/cache"));
    m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    // ------------------------------ 应用卡片条 ------------------------------
    m_cardScroll = new QScrollArea(this);
    m_cardScroll->setObjectName(QStringLiteral("AppStrip"));
    m_cardScroll->setWidgetResizable(true);
    m_cardScroll->setFrameShape(QFrame::NoFrame);
    m_cardScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_cardScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_cardScroll->setFixedHeight(82);
    m_cardScroll->viewport()->setAutoFillBackground(false);

    auto *strip = new QWidget(m_cardScroll);
    strip->setObjectName(QStringLiteral("AppStripBody"));
    m_cardLayout = new QHBoxLayout(strip);
    m_cardLayout->setContentsMargins(0, 6, 0, 6);
    m_cardLayout->setSpacing(10);
    m_cardScroll->setWidget(strip);
    body()->addWidget(m_cardScroll);

    // ------------------------------ 工具栏 ------------------------------
    auto *toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("BrowserToolBar"));
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(6);

    m_back = makeToolButton(toolbar, QStringLiteral("后退"));
    m_forward = makeToolButton(toolbar, QStringLiteral("前进"));
    m_reload = makeToolButton(toolbar, QStringLiteral("刷新"));

    m_urlBar = new QLineEdit(toolbar);
    m_urlBar->setObjectName(QStringLiteral("BrowserUrlBar"));
    m_urlBar->setClearButtonEnabled(true);
    m_urlBar->setMinimumHeight(32);
    m_urlBar->setPlaceholderText(QStringLiteral("输入地址后回车，例如 https://192.168.1.100:5667/v"));

    m_external = makeToolButton(toolbar, QStringLiteral("在系统浏览器中打开"));

    toolbarLayout->addWidget(m_back);
    toolbarLayout->addWidget(m_forward);
    toolbarLayout->addWidget(m_reload);
    toolbarLayout->addSpacing(4);
    toolbarLayout->addWidget(m_urlBar, 1);
    toolbarLayout->addSpacing(4);
    toolbarLayout->addWidget(m_external);

    body()->addWidget(toolbar);

    // 加载进度条（贴着工具栏下方，加载完成后 600ms 自动隐藏）
    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("LoadBar"));
    m_progress->setTextVisible(false);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setFixedHeight(3);
    m_progress->setVisible(false);
    body()->addWidget(m_progress);

    m_progressTimer = new QTimer(this);
    m_progressTimer->setSingleShot(true);
    m_progressTimer->setInterval(600);
    connect(m_progressTimer, &QTimer::timeout, this, [this]() {
        m_progress->setVisible(false);
        m_progress->setValue(0);
    });

    // ------------------------------ 网页标签 ------------------------------
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("BrowserTabs"));
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setDrawBase(false);
    // 网页视图是原生子窗口，永远是矩形；给每个页面留内边距，让 pane 的圆角露出来
    m_tabs->tabBar()->setProperty("fnRounded", true);
    body()->addWidget(m_tabs, 1);

    connect(m_back, &QToolButton::clicked, this, [this]() {
        if (FnWebView *view = currentView()) {
            view->back();
        }
    });
    connect(m_forward, &QToolButton::clicked, this, [this]() {
        if (FnWebView *view = currentView()) {
            view->forward();
        }
    });
    connect(m_reload, &QToolButton::clicked, this, [this]() {
        FnWebView *view = currentView();
        if (!view) {
            return;
        }
        if (view->page() && view->page()->isLoading()) {
            m_userStopped = true;
            view->stop();
            setLoadingUi(false);
            m_progress->setVisible(false);
        } else {
            view->reload();
        }
    });
    connect(m_external, &QToolButton::clicked, this, &BrowserPage::openExternal);

    connect(m_urlBar, &QLineEdit::returnPressed, this, [this]() {
        navigateTo(m_urlBar->text());
        m_urlBar->clearFocus();
    });

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        syncAddressBar();
        updateNavState();
        FnWebView *view = currentView();
        const bool loading = view && view->page() && view->page()->isLoading();
        m_progress->setVisible(loading);
        if (!loading) {
            m_progress->setValue(0);
        }
    });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &BrowserPage::closeTab);

    connect(m_profile, &QWebEngineProfile::downloadRequested, this,
            &BrowserPage::onDownloadRequested);
}

void BrowserPage::rebuildAppCards()
{
    if (!m_cardLayout) {
        return;
    }

    while (QLayoutItem *item = m_cardLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    m_cards.clear();

    // 1) 飞牛官方应用（服务端探测；未探测到时退回内置的三个入口）
    if (m_official.isEmpty()) {
        const QVector<FnAppEntry> builtin = m_config->builtinApps();
        for (const FnAppEntry &entry : builtin) {
            const QString key = fnAppKey(entry.name);
            if (!fnAppVisible(m_config, key)) {
                continue;
            }
            AppCardEntry card;
            card.name = entry.name;
            card.url = entry.url;
            card.source = QStringLiteral("official");
            card.appKey = key;
            card.hint = QStringLiteral("飞牛官方应用");
            card.installed = true;
            m_cards.append(card);
        }
    } else {
        for (const QJsonValue &value : m_official) {
            const QJsonObject obj = value.toObject();
            const QString name = obj.value(QStringLiteral("name")).toString();
            const QString key = fnAppKey(name);
            if (!fnAppVisible(m_config, key)) {
                continue;
            }
            AppCardEntry card;
            card.name = name;
            card.url = m_config->appUrl(obj.value(QStringLiteral("path")).toString());
            card.source = QStringLiteral("official");
            card.appKey = key;
            card.installed = obj.value(QStringLiteral("installed")).toBool(true);
            const QString description = obj.value(QStringLiteral("description")).toString();
            card.hint = card.installed
                            ? (description.isEmpty() ? QStringLiteral("飞牛官方应用") : description)
                            : QStringLiteral("未安装 · 点击安装");
            m_cards.append(card);
        }
    }

    // 2) Docker 应用：按发布的宿主机端口拼 http://NAS:端口
    const QString nasHost = m_config->host.isEmpty() ? QStringLiteral("127.0.0.1") : m_config->host;
    for (const QJsonValue &value : m_docker) {
        const QJsonObject obj = value.toObject();
        const QJsonArray ports = obj.value(QStringLiteral("ports")).toArray();
        if (ports.isEmpty()) {
            continue;
        }
        const int port = ports.first().toInt();
        if (port <= 0) {
            continue;
        }
        AppCardEntry card;
        card.name = obj.value(QStringLiteral("name")).toString();
        card.url = QStringLiteral("http://%1:%2").arg(nasHost).arg(port);
        card.source = QStringLiteral("docker");
        card.hint = QStringLiteral("Docker · 端口 %1").arg(port);
        card.installed = true;
        m_cards.append(card);
    }

    // 3) 用户自己新建的应用
    const QVector<FnAppEntry> custom = m_config->resolvedApps();
    for (int i = 0; i < custom.size(); ++i) {
        AppCardEntry card;
        card.name = custom.at(i).name;
        card.url = custom.at(i).url;
        card.source = QStringLiteral("custom");
        card.hint = QStringLiteral("自定义应用");
        card.installed = true;
        card.customIndex = i;
        m_cards.append(card);
    }

    QWidget *strip = m_cardScroll->widget();
    for (const AppCardEntry &entry : m_cards) {
        QPushButton *card = makeAppCard(strip, fnAppIcon(entry.appKey, entry.source),
                                        entry.name, entry.hint,
                                        entry.installed ? QStringLiteral("ready")
                                                        : QStringLiteral("missing"));
        card->setToolTip(QStringLiteral("%1\n%2\n左键打开 · 右键更多操作")
                             .arg(entry.name, entry.url));
        card->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(card, &QPushButton::clicked, this, [this, entry]() { launchApp(entry); });
        connect(card, &QPushButton::customContextMenuRequested, this,
                [this, entry, card](const QPoint &pos) {
                    showCardMenu(entry, card->mapToGlobal(pos));
                });
        m_cardLayout->addWidget(card);
    }

    // 4) 「新建应用」卡片
    QPushButton *add = makeAppCard(strip, Icons::Kind::Plus, QStringLiteral("新建应用"),
                                   QStringLiteral("Docker / 网页应用"),
                                   QStringLiteral("add"));
    add->setProperty("dashed", true);
    add->setToolTip(QStringLiteral("把 Docker 里安装的应用（比如 8080 端口的应用）添加进来"));
    connect(add, &QPushButton::clicked, this, &BrowserPage::createCustomApp);
    m_cardLayout->addWidget(add);

    m_cardLayout->addStretch(1);
}

void BrowserPage::requestDiscoveredApps()
{
    api()->get(QStringLiteral("/api/apps"),
               [this](bool ok, const QJsonObject &data, const QString &) {
                   if (!ok) {
                       return;
                   }
                   m_official = data.value(QStringLiteral("official")).toArray();
                   m_docker = data.value(QStringLiteral("docker")).toArray();
                   rebuildAppCards();
               });
}

void BrowserPage::reloadApps()
{
    rebuildAppCards();
    requestDiscoveredApps();
}

void BrowserPage::launchApp(const AppCardEntry &entry)
{
    if (!entry.installed) {
        showNotInstalled(entry);
        return;
    }
    openInTab(QUrl(entry.url), entry.name);
}

void BrowserPage::showNotInstalled(const AppCardEntry &entry)
{
    AppInstallDialog dialog(entry.name, this);
    connect(&dialog, &AppInstallDialog::openInside, this,
            &BrowserPage::openAppCenterRequested);
    connect(&dialog, &AppInstallDialog::openBrowser, this, [this]() {
        QDesktopServices::openUrl(QUrl(m_config->adminUrl()));
    });
    dialog.exec();
}

void BrowserPage::showCardMenu(const AppCardEntry &entry, const QPoint &globalPos)
{
    QMenu menu(this);
    makePopupRounded(&menu);
    QAction *open = menu.addAction(QStringLiteral("打开"));
    QAction *external = menu.addAction(QStringLiteral("在系统浏览器中打开"));
    menu.addSeparator();
    QAction *edit = nullptr;
    QAction *remove = nullptr;
    if (entry.source == QLatin1String("custom")) {
        edit = menu.addAction(QStringLiteral("编辑这个应用"));
        remove = menu.addAction(QStringLiteral("删除这个应用"));
        menu.addSeparator();
    }
    QAction *add = menu.addAction(QStringLiteral("新建应用"));
    QAction *refresh = menu.addAction(QStringLiteral("重新检测应用"));

    QAction *picked = menu.exec(globalPos);
    if (!picked) {
        return;
    }
    if (picked == open) {
        launchApp(entry);
    } else if (picked == external) {
        QDesktopServices::openUrl(QUrl(entry.url));
    } else if (edit && picked == edit) {
        editCustomApp(entry.customIndex);
    } else if (remove && picked == remove) {
        removeCustomApp(entry.customIndex);
    } else if (picked == add) {
        createCustomApp();
    } else if (picked == refresh) {
        reloadApps();
        showToast(QStringLiteral("正在重新检测应用…"), QStringLiteral("info"));
    }
}

void BrowserPage::createCustomApp()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新建应用"));
    dialog.setMinimumWidth(420);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);

    auto *tip = new QLabel(
        QStringLiteral("把 Docker 里安装的应用加进来：填端口号即可（例如 8080），"
                       "也可以直接填完整地址。"),
        &dialog);
    tip->setProperty("role", QStringLiteral("muted"));
    tip->setWordWrap(true);
    layout->addWidget(tip);

    auto *form = new QFormLayout();
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    auto *nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText(QStringLiteral("例如 Immich、qBittorrent"));
    auto *urlEdit = new QLineEdit(&dialog);
    urlEdit->setPlaceholderText(QStringLiteral("例如 8080 或 http://192.168.1.10:8090"));
    form->addRow(QStringLiteral("应用名称"), nameEdit);
    form->addRow(QStringLiteral("端口 / 地址"), urlEdit);
    layout->addLayout(form);

    auto *error = new QLabel(&dialog);
    error->setProperty("role", QStringLiteral("muted"));
    error->setStyleSheet(QStringLiteral("color:#e0533d;"));
    error->setVisible(false);
    layout->addWidget(error);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("添加"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // 内联校验：名称 / 地址必填；纯数字按端口校验范围
    auto validate = [nameEdit, urlEdit, error, buttons]() -> bool {
        const QString name = nameEdit->text().trimmed();
        const QString raw = urlEdit->text().trimmed();
        QString message;
        if (name.isEmpty()) {
            message = QStringLiteral("请填写应用名称");
        } else if (raw.isEmpty()) {
            message = QStringLiteral("请填写端口 / 地址");
        } else {
            static const QRegularExpression digitsOnly(QStringLiteral("^\\d{1,5}$"));
            if (digitsOnly.match(raw).hasMatch()) {
                const int port = raw.toInt();
                if (port <= 0 || port > 65535) {
                    message = QStringLiteral("端口需要在 1~65535 之间");
                }
            }
        }
        error->setText(message);
        error->setVisible(!message.isEmpty());
        buttons->button(QDialogButtonBox::Ok)->setEnabled(message.isEmpty());
        return message.isEmpty();
    };
    connect(nameEdit, &QLineEdit::textChanged, &dialog, validate);
    connect(urlEdit, &QLineEdit::textChanged, &dialog, validate);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [validate, &dialog]() {
        if (validate()) {
            dialog.accept();
        }
    });
    validate();

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString name = nameEdit->text().trimmed();
    const QString raw = urlEdit->text().trimmed();

    QString url = raw;
    static const QRegularExpression digitsOnly(QStringLiteral("^\\d{1,5}$"));
    if (digitsOnly.match(raw).hasMatch()) {
        const int port = raw.toInt();
        if (port <= 0 || port > 65535) {
            showToast(QStringLiteral("端口不正确：%1").arg(raw), QStringLiteral("error"));
            return;
        }
        const QString host = m_config->host.isEmpty() ? QStringLiteral("127.0.0.1") : m_config->host;
        // 飞牛官方默认入口 5666 是 HTTP；开启 HTTPS 后的 5667 才是 HTTPS，其余按 Docker 默认的 HTTP 处理
        const QString scheme = (port == 5667) ? QStringLiteral("https")
                                                              : QStringLiteral("http");
        url = QStringLiteral("%1://%2:%3").arg(scheme, host).arg(port);
    } else if (!url.contains(QStringLiteral("://"))) {
        url.prepend(QStringLiteral("http://"));
    }

    QVector<FnAppEntry> list = m_config->resolvedApps();
    list.append(FnAppEntry{name, url});
    saveCustomApps(list);
}

void BrowserPage::editCustomApp(int customIndex)
{
    QVector<FnAppEntry> list = m_config->resolvedApps();
    if (customIndex < 0 || customIndex >= list.size()) {
        return;
    }
    const FnAppEntry old = list.at(customIndex);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("编辑应用"));
    dialog.setMinimumWidth(420);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);
    auto *form = new QFormLayout();
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    auto *nameEdit = new QLineEdit(old.name, &dialog);
    auto *urlEdit = new QLineEdit(old.url, &dialog);
    form->addRow(QStringLiteral("应用名称"), nameEdit);
    form->addRow(QStringLiteral("端口 / 地址"), urlEdit);
    layout->addLayout(form);

    auto *error = new QLabel(&dialog);
    error->setProperty("role", QStringLiteral("muted"));
    error->setStyleSheet(QStringLiteral("color:#e0533d;"));
    error->setVisible(false);
    layout->addWidget(error);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto validate = [nameEdit, urlEdit, error, buttons]() -> bool {
        const QString name = nameEdit->text().trimmed();
        const QString url = urlEdit->text().trimmed();
        QString message;
        if (name.isEmpty()) {
            message = QStringLiteral("请填写应用名称");
        } else if (url.isEmpty()) {
            message = QStringLiteral("请填写端口 / 地址");
        }
        error->setText(message);
        error->setVisible(!message.isEmpty());
        buttons->button(QDialogButtonBox::Ok)->setEnabled(message.isEmpty());
        return message.isEmpty();
    };
    connect(nameEdit, &QLineEdit::textChanged, &dialog, validate);
    connect(urlEdit, &QLineEdit::textChanged, &dialog, validate);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [validate, &dialog]() {
        if (validate()) {
            dialog.accept();
        }
    });
    validate();

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString name = nameEdit->text().trimmed();
    QString url = urlEdit->text().trimmed();
    if (!url.contains(QStringLiteral("://"))) {
        url.prepend(QStringLiteral("http://"));
    }
    list[customIndex] = FnAppEntry{name, url};
    saveCustomApps(list);
}

void BrowserPage::removeCustomApp(int customIndex)
{
    QVector<FnAppEntry> list = m_config->resolvedApps();
    if (customIndex < 0 || customIndex >= list.size()) {
        return;
    }
    const FnAppEntry entry = list.at(customIndex);
    const QMessageBox::StandardButton answer =
        QMessageBox::question(this, QStringLiteral("删除应用"),
                              QStringLiteral("确定要删除「%1」吗？").arg(entry.name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    list.removeAt(customIndex);
    saveCustomApps(list);
}

void BrowserPage::saveCustomApps(const QVector<FnAppEntry> &apps)
{
    m_config->apps = apps;
    m_config->save();
    rebuildAppCards();
    showToast(QStringLiteral("应用已更新"), QStringLiteral("success"));
}

QString BrowserPage::homeUrlString() const
{
    const QVector<FnAppEntry> apps = m_config->resolvedApps();
    if (!apps.isEmpty()) {
        return apps.first().url;
    }
    return m_config->adminUrl();
}

FnWebView *BrowserPage::createTab(const QUrl &url, const QString &title, bool activate)
{
    // 网页视图是原生子窗口，无法做圆角；外面套一层带内边距的容器，
    // 让 QTabWidget 面板的圆角能露出来。
    auto *holder = new QWidget(m_tabs);
    holder->setObjectName(QStringLiteral("WebHolder"));
    auto *holderLayout = new QVBoxLayout(holder);
    holderLayout->setContentsMargins(8, 6, 8, 8);
    holderLayout->setSpacing(0);

    auto *view = new FnWebView(holder);
    holderLayout->addWidget(view);
    view->setHomeUrl(QUrl(homeUrlString()));

    auto *page = new FnWebPage(m_profile, view);
    page->setBackgroundColor(Theme::current().window);
    applyWebSettings(page);
    view->setPage(page);

    // 事件绑定
    connect(view, &QWebEngineView::titleChanged, this, [this, view](const QString &text) {
        const int index = tabIndexOf(view);
        if (index < 0) {
            return;
        }
        QString label = text.trimmed();
        if (label.isEmpty()) {
            label = view->url().host();
        }
        if (label.isEmpty()) {
            label = QStringLiteral("新标签页");
        }
        m_tabs->setTabText(index, shortenTitle(label));
        m_tabs->setTabToolTip(index, label);
    });

    connect(view, &QWebEngineView::urlChanged, this, [this, view](const QUrl &changed) {
        const int index = tabIndexOf(view);
        if (index >= 0) {
            if (m_tabs->tabText(index).isEmpty()) {
                m_tabs->setTabText(index, shortenTitle(changed.host()));
            }
            m_tabs->setTabToolTip(index, changed.toString());
        }
        if (view == currentView()) {
            syncAddressBar();
            updateNavState();
        }
    });

    connect(view, &QWebEngineView::loadStarted, this, [this, view]() {
        if (view != currentView()) {
            return;
        }
        m_userStopped = false;
        m_progressTimer->stop();
        m_progress->setValue(0);
        m_progress->setVisible(true);
        setLoadingUi(true);
        updateNavState();
    });

    connect(view, &QWebEngineView::loadProgress, this, [this, view](int progress) {
        if (view != currentView()) {
            return;
        }
        if (!m_progress->isVisible()) {
            m_progress->setVisible(true);
        }
        m_progress->setValue(progress);
    });

    connect(view, &QWebEngineView::loadFinished, this, [this, view](bool ok) {
        if (view != currentView()) {
            return;
        }
        m_progressTimer->start(600);
        setLoadingUi(false);
        updateNavState();
        if (!ok && !m_userStopped) {
            const QUrl failed = view->url();
            if (failed.isValid() && (failed.scheme() == QLatin1String("https")
                                     || failed.scheme() == QLatin1String("http"))) {
                showErrorPage(view, failed);
            }
        }
    });

    if (page) {
        connect(page, &QWebEnginePage::newWindowRequested, this,
                [this](QWebEngineNewWindowRequest &request) {
                    // 网页里的 window.open / target=_blank 统一开成新标签
                    FnWebView *tab = createTab(QUrl(), QStringLiteral("新标签页"), true);
                    if (tab && tab->page()) {
                        request.openIn(tab->page());
                    }
                });
    }

    const int index = m_tabs->addTab(holder, shortenTitle(title));
    m_tabs->setTabToolTip(index, url.toString());
    if (activate) {
        m_tabs->setCurrentIndex(index);
    }
    if (url.isValid() && !url.isEmpty()) {
        view->setUrl(url);
    }
    return view;
}

FnWebView *BrowserPage::createWelcomeTab()
{
    FnWebView *view = createTab(QUrl(), QStringLiteral("起始页"), true);
    view->setProperty("appUrl", QString());
    view->setProperty("welcome", true);
    view->setHtml(welcomeHtml());
    m_welcomeView = view;
    return view;
}

FnWebView *BrowserPage::currentView() const
{
    if (!m_tabs) {
        return nullptr;
    }
    return viewAt(m_tabs->currentIndex());
}

FnWebView *BrowserPage::viewAt(int index) const
{
    if (!m_tabs || index < 0 || index >= m_tabs->count()) {
        return nullptr;
    }
    QWidget *page = m_tabs->widget(index);
    return page ? page->findChild<FnWebView *>() : nullptr;
}

int BrowserPage::tabIndexOf(const FnWebView *view) const
{
    if (!m_tabs || !view) {
        return -1;
    }
    QWidget *page = view->parentWidget();
    return page ? m_tabs->indexOf(page) : -1;
}

FnWebView *BrowserPage::findTabByUrl(const QString &url) const
{
    const QString key = normalizedUrl(url);
    if (key.isEmpty() || !m_tabs) {
        return nullptr;
    }
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (FnWebView *view = viewAt(i)) {
            if (normalizedUrl(view->property("appUrl").toString()) == key) {
                return view;
            }
        }
    }
    return nullptr;
}

void BrowserPage::closeTab(int index)
{
    FnWebView *view = viewAt(index);
    if (!view) {
        return;
    }
    if (view == m_welcomeView) {
        m_welcomeView = nullptr;
    }
    QWidget *page = m_tabs->widget(index);
    m_tabs->removeTab(index);
    if (page) {
        page->deleteLater();
    } else {
        view->deleteLater();
    }

    if (m_tabs->count() == 0) {
        createWelcomeTab();
    }
}

void BrowserPage::openInTab(const QUrl &url, const QString &title)
{
    if (!url.isValid() || url.host().isEmpty()) {
        showToast(QStringLiteral("地址格式不正确：%1").arg(url.toString()), QStringLiteral("error"));
        return;
    }

    if (FnWebView *existing = findTabByUrl(url.toString())) {
        m_tabs->setCurrentIndex(tabIndexOf(existing));
        return;
    }

    FnWebView *view = nullptr;
    // 首次打开应用时，直接用起始页那个标签，避免多出一个空白标签
    if (m_welcomeView && m_tabs->count() == 1 && viewAt(0) == m_welcomeView) {
        view = m_welcomeView;
        m_welcomeView = nullptr;
        view->setProperty("welcome", false);
    } else {
        view = createTab(QUrl(), title.isEmpty() ? url.host() : title, true);
    }

    view->setProperty("appUrl", normalizedUrl(url.toString()));
    view->setHomeUrl(url);

    const int index = tabIndexOf(view);
    if (index >= 0) {
        m_tabs->setTabText(index, shortenTitle(title.isEmpty() ? url.host() : title));
        m_tabs->setTabToolTip(index, url.toString());
    }
    view->setUrl(url);
    m_tabs->setCurrentIndex(tabIndexOf(view));
}

void BrowserPage::syncAddressBar()
{
    FnWebView *view = currentView();
    if (!view) {
        m_urlBar->clear();
        return;
    }
    if (m_urlBar->hasFocus()) {
        return; // 用户正在输入时不要覆盖
    }
    m_urlBar->setText(addressText(view->url()));
}

void BrowserPage::updateNavState()
{
    FnWebView *view = currentView();
    const bool hasView = view != nullptr;
    m_back->setEnabled(hasView && view->history() && view->history()->canGoBack());
    m_forward->setEnabled(hasView && view->history() && view->history()->canGoForward());
    m_external->setEnabled(hasView && !addressText(view->url()).isEmpty());
}

void BrowserPage::setLoadingUi(bool loading)
{
    const Theme::Palette pal = Theme::current();
    if (loading) {
        m_reload->setIcon(Icons::icon(Icons::Kind::Stop, pal.text, 17));
        m_reload->setToolTip(QStringLiteral("停止加载"));
    } else {
        m_reload->setIcon(Icons::icon(Icons::Kind::Refresh, pal.text, 17));
        m_reload->setToolTip(QStringLiteral("刷新"));
    }
}

void BrowserPage::navigateTo(const QString &input)
{
    QString text = input.trimmed();
    if (text.isEmpty()) {
        return;
    }
    if (!text.contains(QStringLiteral("://"))) {
        text.prepend(QStringLiteral("https://"));
    }
    const QUrl url(text);
    if (!url.isValid() || url.host().isEmpty()) {
        showToast(QStringLiteral("地址格式不正确：%1").arg(input.trimmed()),
                  QStringLiteral("error"));
        return;
    }

    FnWebView *view = currentView();
    if (!view) {
        openInTab(url, url.host());
        return;
    }
    // 用户在地址栏手动输入时沿用当前标签（浏览器行为）
    view->setUrl(url);
}

void BrowserPage::openExternal()
{
    FnWebView *view = currentView();
    if (!view) {
        return;
    }
    const QUrl url = view->url();
    if (addressText(url).isEmpty()) {
        showToast(QStringLiteral("当前标签还没有打开任何页面"), QStringLiteral("warning"));
        return;
    }
    if (!QDesktopServices::openUrl(url)) {
        showToast(QStringLiteral("无法调用系统浏览器"), QStringLiteral("error"));
    }
}

QString BrowserPage::welcomeHtml() const
{
    const Theme::Palette pal = Theme::palette(Theme::idFromString(m_config->uiTheme),
                                              m_config->colorMode == QLatin1String("light"),
                                              m_config->accent);
    QString html = QStringLiteral(R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>应用</title>
<style>
html, body { margin: 0; padding: 0; height: 100%; }
body {
    background: %BG%;
    color: %TEXT%;
    font-family: "Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI", sans-serif;
    display: flex;
    align-items: center;
    justify-content: center;
}
.wrap { max-width: 520px; padding: 24px; text-align: center; }
.logo {
    width: 56px; height: 56px; margin: 0 auto 18px;
    line-height: 56px; border-radius: 18px;
    font-size: 26px; font-weight: 700;
    color: %ACCENT_TEXT%; background: %ACCENT%;
}
h1 { font-size: 18px; font-weight: 700; margin: 0 0 10px; }
p { font-size: 13px; line-height: 1.8; color: %MUTED%; margin: 0 0 6px; }
kbd {
    padding: 1px 6px; border-radius: 5px;
    background: %SURFACE_ALT%; border: 1px solid %BORDER%;
    font-family: inherit; font-size: 12px;
}
</style>
</head>
<body>
    <div class="wrap">
        <div class="logo">应</div>
        <h1>从上方选择要打开的应用</h1>
        <p>点一下应用卡片，就会在这里的常驻标签页中打开，切换到其他页面也不会断开。</p>
        <p>Docker 里安装的应用可以点 <kbd>＋ 新建应用</kbd> 添加（填端口即可）。</p>
    </div>
</body>
</html>)HTML");

    html.replace(QStringLiteral("%BG%"), cssColor(pal.window));
    html.replace(QStringLiteral("%SURFACE_ALT%"), cssColor(pal.surfaceAlt));
    html.replace(QStringLiteral("%BORDER%"), cssColor(pal.border));
    html.replace(QStringLiteral("%TEXT%"), cssColor(pal.text));
    html.replace(QStringLiteral("%MUTED%"), cssColor(pal.muted));
    html.replace(QStringLiteral("%ACCENT_TEXT%"), cssColor(pal.accentText));
    html.replace(QStringLiteral("%ACCENT%"), cssColor(pal.accent));
    return html;
}

QString BrowserPage::buildErrorHtml(const AppConfig *config, const QUrl &url)
{
    const Theme::Palette pal = Theme::palette(Theme::idFromString(config->uiTheme),
                                              config->colorMode == QLatin1String("light"),
                                              config->accent);
    const QString target = url.toString();
    const QString escaped = target.toHtmlEscaped();

    // 嵌入 JS 字符串：转义反斜杠与单引号
    QString jsUrl = target;
    jsUrl.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    jsUrl.replace(QLatin1Char('\''), QStringLiteral("\\'"));

    QString html = QStringLiteral(R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>无法打开页面</title>
<style>
html, body { margin: 0; padding: 0; height: 100%; }
body {
    background: %BG%;
    color: %TEXT%;
    font-family: "Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI", sans-serif;
    display: flex;
    align-items: center;
    justify-content: center;
}
.card {
    max-width: 560px;
    margin: 24px;
    padding: 34px 38px 30px;
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%px;
    box-shadow: 0 12px 36px rgba(0, 0, 0, 0.18);
    text-align: center;
}
.badge {
    width: 44px;
    height: 44px;
    margin: 0 auto 16px;
    line-height: 44px;
    font-size: 22px;
    font-weight: 700;
    color: %ACCENT_TEXT%;
    background: %ACCENT%;
    border-radius: 22px;
}
h1 { font-size: 19px; font-weight: 700; margin: 0 0 12px; }
p { font-size: 13px; line-height: 1.75; color: %MUTED%; margin: 0 0 8px; word-break: break-all; }
.url {
    margin: 16px 0 22px;
    padding: 9px 12px;
    font-family: "Cascadia Mono", "Consolas", monospace;
    font-size: 12px;
    color: %TEXT%;
    background: %SURFACE_ALT%;
    border: 1px solid %BORDER%;
    border-radius: 6px;
    word-break: break-all;
}
button {
    font: inherit;
    font-size: 13px;
    padding: 9px 24px;
    color: %ACCENT_TEXT%;
    background: %ACCENT%;
    border: 1px solid %ACCENT%;
    border-radius: %RADIUS_SM%px;
    cursor: pointer;
}
button:hover { opacity: 0.88; }
</style>
</head>
<body>
    <div class="card">
        <div class="badge">!</div>
        <h1>无法打开页面</h1>
        <p>无法打开 %URL%，请确认飞牛服务已启动或地址正确。</p>
        <p>影视 / 相册 / 音乐等应用尚未安装时会返回 502，属正常现象；在飞牛「应用中心」安装后再试即可。</p>
        <div class="url">%URL%</div>
        <button onclick="fnRetry()">重试</button>
    </div>
    <script>
    function fnRetry() {
        try {
            location.href = '%JSURL%';
        } catch (e) {
            try { history.back(); } catch (e2) { /* 忽略 */ }
        }
    }
    </script>
</body>
</html>)HTML");

    html.replace(QStringLiteral("%BG%"), cssColor(pal.window));
    html.replace(QStringLiteral("%SURFACE_ALT%"), cssColor(pal.surfaceAlt));
    html.replace(QStringLiteral("%SURFACE%"), cssColor(pal.surface));
    html.replace(QStringLiteral("%BORDER%"), cssColor(pal.border));
    html.replace(QStringLiteral("%TEXT%"), cssColor(pal.text));
    html.replace(QStringLiteral("%MUTED%"), cssColor(pal.muted));
    html.replace(QStringLiteral("%ACCENT_TEXT%"), cssColor(pal.accentText));
    html.replace(QStringLiteral("%ACCENT%"), cssColor(pal.accent));
    html.replace(QStringLiteral("%RADIUS_SM%"), QString::number(pal.radiusSm));
    html.replace(QStringLiteral("%RADIUS%"), QString::number(pal.radius));
    html.replace(QStringLiteral("%URL%"), escaped);
    html.replace(QStringLiteral("%JSURL%"), jsUrl);
    return html;
}

void BrowserPage::showErrorPage(FnWebView *view, const QUrl &url)
{
    if (!view) {
        return;
    }
    view->setHtml(buildErrorHtml(m_config, url), url);
    const int index = tabIndexOf(view);
    if (index >= 0) {
        m_tabs->setTabText(index, shortenTitle(QStringLiteral("无法打开页面")));
        m_tabs->setTabToolTip(index, url.toString());
    }
}

void BrowserPage::onDownloadRequested(QWebEngineDownloadRequest *item)
{
    if (!item) {
        return;
    }
    const QString dir = m_config->effectiveDownloadDir();
    QDir().mkpath(dir);

    QString suggested = item->downloadFileName().trimmed();
    if (suggested.isEmpty()) {
        suggested = QStringLiteral("飞牛下载文件");
    }
    const QString target = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存下载文件"), QDir(dir).filePath(suggested),
        QStringLiteral("所有文件 (*.*)"));
    if (target.isEmpty()) {
        item->cancel();
        return;
    }

    const QFileInfo info(target);
    item->setDownloadDirectory(info.absolutePath());
    item->setDownloadFileName(info.fileName());
    item->accept();

    connect(item, &QWebEngineDownloadRequest::stateChanged, this,
            [this, item](QWebEngineDownloadRequest::DownloadState state) {
                switch (state) {
                case QWebEngineDownloadRequest::DownloadCompleted:
                    showToast(QStringLiteral("下载完成：%1").arg(item->downloadFileName()),
                              QStringLiteral("success"));
                    break;
                case QWebEngineDownloadRequest::DownloadInterrupted:
                    showToast(QStringLiteral("下载失败：%1").arg(item->interruptReasonString()),
                              QStringLiteral("error"));
                    break;
                case QWebEngineDownloadRequest::DownloadCancelled:
                    showToast(QStringLiteral("下载已取消"), QStringLiteral("warning"));
                    break;
                default:
                    break;
                }
            });

    showToast(QStringLiteral("开始下载：%1").arg(info.fileName()), QStringLiteral("info"));
}

void BrowserPage::updateIcons()
{
    const Theme::Palette pal = Theme::current();
    if (m_back) {
        m_back->setIcon(Icons::icon(Icons::Kind::Back, pal.text, 17));
    }
    if (m_forward) {
        m_forward->setIcon(Icons::icon(Icons::Kind::Forward, pal.text, 17));
    }
    if (m_reload) {
        FnWebView *view = currentView();
        const bool loading = view && view->page() && view->page()->isLoading();
        m_reload->setIcon(Icons::icon(loading ? Icons::Kind::Stop : Icons::Kind::Refresh,
                                      pal.text, 17));
        m_reload->setToolTip(loading ? QStringLiteral("停止加载") : QStringLiteral("刷新"));
    }
    if (m_external) {
        m_external->setIcon(Icons::icon(Icons::Kind::Globe, pal.text, 17));
    }
}

void BrowserPage::reloadTheme()
{
    updateIcons();
    rebuildAppCards();
    if (!m_tabs) {
        return;
    }
    const QColor background = Theme::current().window;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (FnWebView *view = viewAt(i)) {
            if (view->page()) {
                view->page()->setBackgroundColor(background);
            }
        }
    }
    if (m_welcomeView && m_welcomeView->property("welcome").toBool()) {
        m_welcomeView->setHtml(welcomeHtml());
    }
}

void BrowserPage::refresh()
{
    if (!m_loadedOnce) {
        return;
    }
    requestDiscoveredApps();
    FnWebView *view = currentView();
    if (view && view->url().isEmpty() && !view->property("welcome").toBool()) {
        view->setUrl(QUrl(homeUrlString()));
    }
}
