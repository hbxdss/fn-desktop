#include "ui/main_window.h"

#include "core/api_client.h"
#include "core/secure_vault.h"
#include "ui/appearance_dialog.h"
#include "ui/icons.h"
#include "ui/pages/admin_page.h"
#include "ui/pages/audit_page.h"
#include "ui/pages/browser_page.h"
#include "ui/pages/dashboard_page.h"
#include "ui/pages/docker_page.h"
#include "ui/pages/files_page.h"
#include "ui/pages/settings_page.h"
#include "ui/pages/ssh_page.h"
#include "ui/pages/users_page.h"
#include "ui/title_bar.h"
#include "ui/widgets.h"
#include "ui/wizard_dialog.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QPainterPath>
#include <QPainter>
#include <QBitmap>
#include <QRegion>
#include <QResizeEvent>
#include <qt_windows.h>

namespace {

struct NavEntry {
    const char *key;
    const char *title;
    Icons::Kind icon;
    bool advancedOnly = false;
};

const NavEntry kNavEntries[] = {
    {"browser", "应用", Icons::Kind::Home},
    {"admin", "飞牛网页管理后台", Icons::Kind::Globe},
    {"dashboard", "仪表盘", Icons::Kind::Dashboard},
    {"files", "文件管理", Icons::Kind::Folder},
    {"docker", "容器管理", Icons::Kind::Docker, true},
    {"users", "用户管理", Icons::Kind::Users},
    {"audit", "操作日志", Icons::Kind::Log},
    {"ssh", "SSH 终端", Icons::Kind::Server, true},
    {"settings", "设置", Icons::Kind::Gear},
};

const int kNavCount = int(sizeof(kNavEntries) / sizeof(kNavEntries[0]));

struct AccentEntry {
    const char *hex;
    const char *name;
};

const AccentEntry kAccents[] = {
    {"#2f81f7", "飞牛蓝"},
    {"#23c9a4", "薄荷青"},
    {"#8b5cf6", "星云紫"},
    {"#f97316", "落日橙"},
    {"#ef4444", "珊瑚红"},
    {"#eab308", "琥珀金"},
};

const int kAccentCount = int(sizeof(kAccents) / sizeof(kAccents[0]));

QIcon colorDot(const QString &hex, int size = 14)
{
    QPixmap pm(size * 2, size * 2);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    p.setBrush(QColor(hex));
    p.drawEllipse(QRectF(1, 1, size - 2, size - 2));
    p.end();
    return QIcon(pm);
}

} // namespace

MainWindow::MainWindow(ApiClient *api, AppConfig *config, QWidget *parent)
    : QMainWindow(parent), m_api(api), m_config(config)
{
    setWindowTitle(QStringLiteral("飞牛终端"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    // 无边框窗口自身不透明，圆角会被系统补成直角；改成透明窗口 + 自绘圆角背景。
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMinimumSize(1100, 720);
    resize(1440, 900);
    setWindowIcon(QIcon(QStringLiteral(":/app.png")));

    m_theme = Theme::idFromString(m_config->uiTheme);
    m_light = m_config->colorMode == QLatin1String("light");
    m_accent = m_config->accent;

    buildUi();
    buildAppearanceMenu();
    applyAppearanceFromConfig();

    connect(m_api, &ApiClient::unauthorized, this, &MainWindow::signOut);

    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        move(avail.center() - rect().center());
    }

    selectPage(m_config->lastPage);

    QTimer::singleShot(900, this, &MainWindow::checkFirstRunSetup);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("RootShell"));
    root->setAutoFillBackground(false);
    auto *grid = new QGridLayout(root);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);

    m_backdrop = new Backdrop(root);
    grid->addWidget(m_backdrop, 0, 0);

    auto *shell = new QWidget(root);
    shell->setObjectName(QStringLiteral("ContentShell"));
    shell->setAutoFillBackground(false);
    auto *shellLayout = new QVBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);

    m_titleBar = new TitleBar(shell);
    shellLayout->addWidget(m_titleBar);

    auto *row = new QWidget(shell);
    row->setObjectName(QStringLiteral("MainRow"));
    row->setAutoFillBackground(false);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);

    rowLayout->addWidget(buildSidebar());

    buildPages();
    rowLayout->addWidget(m_stack, 1);

    applyAdvancedMode();

    shellLayout->addWidget(row, 1);
    grid->addWidget(shell, 0, 0);

    setCentralWidget(root);

    connect(m_titleBar, &TitleBar::minimizeRequested, this, &MainWindow::showMinimized);
    connect(m_titleBar, &TitleBar::maximizeRequested, this, &MainWindow::toggleMaximize);
    connect(m_titleBar, &TitleBar::closeRequested, this, &QWidget::close);

    m_titleBar->setTitle(QStringLiteral("飞牛终端"), subtitleText());
}

QWidget *MainWindow::buildSidebar()
{
    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("Sidebar"));
    sidebar->setFixedWidth(212);

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(0, 16, 0, 12);
    layout->setSpacing(0);

    auto *brandBox = new QWidget(sidebar);
    auto *brandLayout = new QHBoxLayout(brandBox);
    brandLayout->setContentsMargins(18, 0, 16, 0);
    brandLayout->setSpacing(10);

    auto *logo = new QLabel(brandBox);
    logo->setFixedSize(28, 28);
    logo->setScaledContents(true);
    logo->setPixmap(QPixmap(QStringLiteral(":/app.png"))
                        .scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    auto *brandText = new QVBoxLayout();
    brandText->setContentsMargins(0, 0, 0, 0);
    brandText->setSpacing(1);
    auto *brandName = new QLabel(QStringLiteral("飞牛终端"), brandBox);
    brandName->setObjectName(QStringLiteral("BrandName"));
    auto *brandSub = new QLabel(QStringLiteral("FNOS Desktop Plus"), brandBox);
    brandSub->setObjectName(QStringLiteral("BrandSub"));
    brandText->addWidget(brandName);
    brandText->addWidget(brandSub);

    brandLayout->addWidget(logo);
    brandLayout->addLayout(brandText, 1);
    layout->addWidget(brandBox);
    layout->addSpacing(18);

    auto *section = new QLabel(QStringLiteral("工作区"), sidebar);
    section->setObjectName(QStringLiteral("NavSection"));
    layout->addWidget(section);
    layout->addSpacing(6);

    m_nav = new QListWidget(sidebar);
    m_nav->setObjectName(QStringLiteral("NavList"));
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setFocusPolicy(Qt::NoFocus);
    m_nav->setIconSize(QSize(18, 18));
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_nav->setSelectionMode(QAbstractItemView::SingleSelection);
    m_nav->setUniformItemSizes(true);

    for (int i = 0; i < kNavCount; ++i) {
        auto *item = new QListWidgetItem(QString::fromUtf8(kNavEntries[i].title), m_nav);
        item->setData(Qt::UserRole, QString::fromLatin1(kNavEntries[i].key));
        item->setData(Qt::UserRole + 1, kNavEntries[i].advancedOnly);
        item->setSizeHint(QSize(0, 40));
    }
    // 导航列表吃掉剩余高度，保证 8 个入口全部可见且不出滚动条
    layout->addWidget(m_nav, 1);

    auto *foot = new QWidget(sidebar);
    foot->setObjectName(QStringLiteral("SideFoot"));
    auto *footLayout = new QVBoxLayout(foot);
    footLayout->setContentsMargins(18, 0, 16, 0);
    footLayout->setSpacing(2);

    m_footUser = new QLabel(foot);
    m_footUser->setObjectName(QStringLiteral("SideFoot"));
    m_footHost = new QLabel(foot);
    m_footHost->setObjectName(QStringLiteral("SideFoot"));
    m_footHost->setWordWrap(true);

    auto *signOut = new QPushButton(QStringLiteral("退出登录"), foot);
    signOut->setProperty("variant", QStringLiteral("ghost"));
    signOut->setCursor(Qt::PointingHandCursor);
    signOut->setMinimumHeight(32);

    footLayout->addWidget(m_footUser);
    footLayout->addWidget(m_footHost);
    footLayout->addSpacing(8);
    footLayout->addWidget(signOut);
    layout->addWidget(foot);

    connect(signOut, &QPushButton::clicked, this, &MainWindow::signOut);
    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= m_pages.size()) {
            return;
        }
        m_stack->setCurrentIndex(row);
        BasePage *page = m_pages.at(row);
        if (page) {
            page->refresh();
        }
        m_config->lastPage = QString::fromLatin1(kNavEntries[row].key);
    });

    updateFoot();
    return sidebar;
}

void MainWindow::buildPages()
{
    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("PageStack"));

    m_browserPage = new BrowserPage(m_api, m_config, m_stack);
    m_adminPage = new AdminPage(m_api, m_config, m_stack);
    connect(m_browserPage, &BrowserPage::openAppCenterRequested, this, [this]() {
        selectPage(QStringLiteral("admin"));
        // Task 7 会把 gotoHome 换成 openAppCenter()（注入脚本定位应用商店）
        m_adminPage->gotoHome();
    });
    m_dashboardPage = new DashboardPage(m_api, m_config, m_stack);
    m_filesPage = new FilesPage(m_api, m_config, m_stack);
    connect(m_filesPage, &FilesPage::requestAdminMountSettings, this, [this]() {
        selectPage(QStringLiteral("admin"));
        m_adminPage->gotoHome();
    });
    m_dockerPage = new DockerPage(m_api, m_config, m_stack);
    m_usersPage = new UsersPage(m_api, m_stack);
    m_auditPage = new AuditPage(m_api, m_stack);
    m_sshPage = new SshPage(m_config, m_api, m_stack);
    m_settingsPage = new SettingsPage(m_config, m_api, m_stack);

    m_pages = {m_browserPage, m_adminPage, m_dashboardPage, m_filesPage, m_dockerPage,
               m_usersPage, m_auditPage, m_sshPage, m_settingsPage};

    for (BasePage *page : m_pages) {
        m_stack->addWidget(page);
    }

    connect(m_settingsPage, &SettingsPage::appearanceChanged, this,
            [this](Theme::Id id, bool light, const QString &accent) {
                applyAppearance(id, light, accent, true);
            });
    connect(m_settingsPage, &SettingsPage::serverChanged, this, [this]() {
        m_titleBar->setTitle(QStringLiteral("飞牛终端"), subtitleText());
        updateFoot();
        m_browserPage->reloadApps();
        if (m_adminPage) {
            m_adminPage->gotoHome();
        }
    });
    connect(m_settingsPage, &SettingsPage::advancedModeChanged, this,
            [this](bool) { applyAdvancedMode(); });
}

void MainWindow::buildAppearanceMenu()
{
    m_appearanceMenu = new QMenu(this);
    makePopupRounded(m_appearanceMenu);
    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);

    const Theme::Id ids[] = {Theme::Id::Win7, Theme::Id::Win10, Theme::Id::Win11, Theme::Id::Glass};
    for (Theme::Id id : ids) {
        auto *action = m_appearanceMenu->addAction(Theme::label(id));
        action->setCheckable(true);
        action->setActionGroup(m_themeGroup);
        m_themeActions.append(action);
        connect(action, &QAction::triggered, this, [this, id]() {
            applyAppearance(id, m_light, m_accent, true);
        });
    }

    m_appearanceMenu->addSeparator();

    m_darkAction = m_appearanceMenu->addAction(QStringLiteral("深色模式"));
    m_darkAction->setCheckable(true);
    m_lightAction = m_appearanceMenu->addAction(QStringLiteral("浅色模式"));
    m_lightAction->setCheckable(true);
    connect(m_darkAction, &QAction::triggered, this, [this]() {
        applyAppearance(m_theme, false, m_accent, true);
    });
    connect(m_lightAction, &QAction::triggered, this, [this]() {
        applyAppearance(m_theme, true, m_accent, true);
    });

    m_appearanceMenu->addSeparator();

    auto *accentMenu = m_appearanceMenu->addMenu(QStringLiteral("强调色"));
    for (int i = 0; i < kAccentCount; ++i) {
        const QString hex = QString::fromLatin1(kAccents[i].hex);
        auto *action = accentMenu->addAction(colorDot(hex), QString::fromUtf8(kAccents[i].name));
        action->setCheckable(true);
        action->setData(hex);
        m_accentActions.append(action);
        connect(action, &QAction::triggered, this, [this, hex]() {
            applyAppearance(m_theme, m_light, hex, true);
        });
    }

    m_appearanceMenu->addSeparator();

    auto *customAction = m_appearanceMenu->addAction(QStringLiteral("自定义外观…"));
    connect(customAction, &QAction::triggered, this, &MainWindow::openAppearanceDialog);

    m_titleBar->setAppearanceMenu(m_appearanceMenu);
    syncAppearanceMenu();
}

void MainWindow::openAppearanceDialog()
{
    const Theme::Id originalTheme = m_theme;
    const bool originalLight = m_light;
    const QString originalAccent = m_accent;

    AppearanceDialog dialog(m_theme, m_light, m_accent, this);
    connect(&dialog, &AppearanceDialog::previewChanged, this,
            [this](Theme::Id id, bool light, const QString &accent) {
                applyAppearance(id, light, accent, false);
            });

    if (dialog.exec() == QDialog::Accepted) {
        applyAppearance(dialog.themeId(), dialog.light(), dialog.accent(), true);
    } else {
        applyAppearance(originalTheme, originalLight, originalAccent, false);
    }
}

void MainWindow::syncAppearanceMenu()
{
    const Theme::Id ids[] = {Theme::Id::Win7, Theme::Id::Win10, Theme::Id::Win11, Theme::Id::Glass};
    for (int i = 0; i < m_themeActions.size() && i < 4; ++i) {
        m_themeActions.at(i)->setChecked(ids[i] == m_theme);
    }
    if (m_darkAction) {
        m_darkAction->setChecked(!m_light);
    }
    if (m_lightAction) {
        m_lightAction->setChecked(m_light);
    }
    for (QAction *action : m_accentActions) {
        action->setChecked(action->data().toString().compare(m_accent, Qt::CaseInsensitive) == 0);
    }
}

void MainWindow::applyAppearanceFromConfig()
{
    applyAppearance(Theme::idFromString(m_config->uiTheme),
                    m_config->colorMode == QLatin1String("light"), m_config->accent, false);
}

void MainWindow::applyAppearance(Theme::Id id, bool light, const QString &accent, bool persist)
{
    m_theme = id;
    m_light = light;
    m_accent = accent.isEmpty() ? QStringLiteral("#2f81f7") : accent;

    if (m_config) {
        m_config->uiTheme = Theme::toString(m_theme);
        m_config->colorMode = m_light ? QStringLiteral("light") : QStringLiteral("dark");
        m_config->accent = m_accent;
        if (persist) {
            m_config->save();
        }
    }

    Theme::apply(m_theme, m_light, m_accent);

    if (m_backdrop) {
        m_backdrop->setTheme(m_theme, m_light);
    }
    if (m_titleBar) {
        m_titleBar->reloadTheme(m_theme, m_light);
    }
    reloadNavIcons();
    for (BasePage *page : m_pages) {
        if (page) {
            page->reloadTheme();
        }
    }
    syncAppearanceMenu();
}

void MainWindow::reloadNavIcons()
{
    if (!m_nav) {
        return;
    }
    const Theme::Palette pal = Theme::current();
    const bool adv = m_config && m_config->advancedMode;
    for (int i = 0; i < m_nav->count(); ++i) {
        QListWidgetItem *item = m_nav->item(i);
        if (!item) continue;
        const bool advancedOnly = item->data(Qt::UserRole + 1).toBool();
        const bool neon = adv && advancedOnly;
        const QColor mainColor = neon ? QColor("#00f0ff") : pal.sidebarText;
        const QColor accentColor = neon ? QColor("#ff2bd6") : pal.accentText;
        item->setIcon(Icons::icon(kNavEntries[i].icon, mainColor, accentColor, 18));
    }
}

QString MainWindow::subtitleText() const
{
    if (!m_config || m_config->host.isEmpty()) {
        return QStringLiteral("FNOS Desktop Plus");
    }
    return QStringLiteral("FNOS Desktop Plus · 连接 %1").arg(m_config->displayAddress());
}

void MainWindow::updateFoot()
{
    if (m_footUser) {
        const QString user = m_config->username.isEmpty() ? QStringLiteral("已连接")
                                                          : m_config->username;
        m_footUser->setText(QStringLiteral("当前用户：%1").arg(user));
    }
    if (m_footHost) {
        m_footHost->setText(m_config->host.isEmpty()
                                ? QStringLiteral("尚未设置服务器")
                                : QStringLiteral("NAS：%1").arg(m_config->host));
    }
}

void MainWindow::selectPage(const QString &key)
{
    int row = 0;
    for (int i = 0; i < kNavCount; ++i) {
        if (key.compare(QString::fromLatin1(kNavEntries[i].key), Qt::CaseInsensitive) == 0) {
            row = i;
            break;
        }
    }
    m_nav->setCurrentRow(row);
    m_stack->setCurrentIndex(row);
}

void MainWindow::toggleMaximize()
{
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
    m_titleBar->setMaximized(isMaximized());
    syncWindowRadius();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        syncWindowRadius();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    syncWindowRadius();
}

void MainWindow::syncWindowRadius()
{
    if (!m_backdrop) {
        return;
    }
    const bool full = isMaximized() || isFullScreen();
    m_backdrop->setRadius(full ? 0 : -1);

    // 窗口本身是透明矩形，内容（侧栏 / 页面栈）不透明，会把 Backdrop 自绘的
    // 圆角盖成直角；这里再按同一半径对整个窗口做一次区域裁剪，保证四角一致。
    if (full) {
        clearMask();
        return;
    }
    const Theme::Palette pal = Theme::current();
    const int radius = qMax(pal.radius, 8) + 2;
    // 该 Qt 版本没有 QRegion(QPainterPath) 构造，用单色系图生成圆角区域。
    QBitmap bitmap(size());
    bitmap.fill(Qt::color0);
    {
        QPainter painter(&bitmap);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::color1);
        painter.drawRoundedRect(rect(), radius, radius);
    }
    setMask(QRegion(bitmap));
}

void MainWindow::checkFirstRunSetup()
{
    if (m_setupChecked) {
        return;
    }
    m_setupChecked = true;

    auto showWizard = [this](bool storageHint) {
        if (m_config->wizardCompleted) {
            return;
        }
        auto *wizard = new WizardDialog(m_config, this);
        wizard->setStorageHint(storageHint);
        connect(wizard, &WizardDialog::applied, this, [this]() {
            applyAdvancedMode();
            if (m_browserPage) {
                m_browserPage->refresh();
            }
        });
        QTimer::singleShot(300, wizard, [wizard]() { wizard->open(); });
    };

    m_api->get(QStringLiteral("/api/storage/mounts"),
               [showWizard](bool ok, const QJsonObject &data, const QString &) {
                   const bool hint = ok && data.value(QStringLiteral("needsSetup")).toBool(false);
                   showWizard(hint);
               });
}

void MainWindow::reopenWizard()
{
    auto *wizard = new WizardDialog(m_config, this);
    wizard->setStorageHint(false);
    connect(wizard, &WizardDialog::applied, this, [this]() {
        applyAdvancedMode();
        if (m_browserPage) {
            m_browserPage->refresh();
        }
    });
    wizard->open();
}

// Task 11 会替换为侧栏分组动态显隐 + 赛博朋克风格应用。
void MainWindow::applyAdvancedMode()
{
    if (!m_nav) {
        return;
    }
    const bool adv = m_config && m_config->advancedMode;
    m_nav->setProperty("advanced", adv);
    m_nav->style()->unpolish(m_nav);
    m_nav->style()->polish(m_nav);

    int currentAdvancedRow = -1;
    for (int i = 0; i < m_nav->count(); ++i) {
        QListWidgetItem *item = m_nav->item(i);
        if (!item) continue;
        const bool advancedOnly = item->data(Qt::UserRole + 1).toBool();
        item->setHidden(advancedOnly && !adv);
        if (advancedOnly && adv) {
            // 赛博朋克霓虹前景色
            item->setForeground(QBrush(QColor("#00f0ff")));
        } else if (!adv) {
            item->setForeground(QBrush()); // 重置
        }
        // 如果当前选中了一个正在被隐藏的高级页，切到浏览器
        if (!adv && advancedOnly && m_nav->currentRow() == i) {
            currentAdvancedRow = i;
        }
    }

    if (currentAdvancedRow >= 0) {
        // 找到第一个可见的工作区项
        for (int i = 0; i < m_nav->count(); ++i) {
            QListWidgetItem *item = m_nav->item(i);
            if (item && !item->isHidden()) {
                m_nav->setCurrentRow(i);
                break;
            }
        }
    }

    // 刷新图标（高级模式下使用赛博朋克霓虹色）
    reloadNavIcons();
}

void MainWindow::signOut()
{
    SecureVault::clearSessionPassword();
    if (m_config) {
        m_config->lastPage = QString::fromLatin1(kNavEntries[qMax(0, m_nav->currentRow())].key);
        m_config->save();
    }
    m_api->logout();
    emit logoutRequested();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_config && m_nav) {
        const int row = m_nav->currentRow();
        if (row >= 0 && row < kNavCount) {
            m_config->lastPage = QString::fromLatin1(kNavEntries[row].key);
        }
        m_config->save();
    }
    QMainWindow::closeEvent(event);
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (eventType == QByteArrayLiteral("windows_generic_MSG") && result) {
        auto *msg = static_cast<MSG *>(message);
        if (msg && msg->message == WM_NCHITTEST && !isMaximized() && !isFullScreen()) {
            RECT win {};
            if (GetWindowRect(reinterpret_cast<HWND>(winId()), &win)) {
                const int x = int(short(LOWORD(msg->lParam)));
                const int y = int(short(HIWORD(msg->lParam)));
                const int border = qMax(4, qRound(6 * devicePixelRatioF()));

                const bool left = x >= win.left && x < win.left + border;
                const bool right = x < win.right && x >= win.right - border;
                const bool top = y >= win.top && y < win.top + border;
                const bool bottom = y < win.bottom && y >= win.bottom - border;

                if (top && left) {
                    *result = HTTOPLEFT;
                    return true;
                }
                if (top && right) {
                    *result = HTTOPRIGHT;
                    return true;
                }
                if (bottom && left) {
                    *result = HTBOTTOMLEFT;
                    return true;
                }
                if (bottom && right) {
                    *result = HTBOTTOMRIGHT;
                    return true;
                }
                if (left) {
                    *result = HTLEFT;
                    return true;
                }
                if (right) {
                    *result = HTRIGHT;
                    return true;
                }
                if (top) {
                    *result = HTTOP;
                    return true;
                }
                if (bottom) {
                    *result = HTBOTTOM;
                    return true;
                }
            }
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
