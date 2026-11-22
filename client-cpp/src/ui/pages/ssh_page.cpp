#include "ui/pages/ssh_page.h"

#include "ui/ssh/ssh_workspace.h"
#include "ui/theme.h"

#include <QDesktopServices>
#include <QMainWindow>
#include <QPointer>
#include <QUrl>
#include <QVBoxLayout>

SshPage::SshPage(AppConfig *config, ApiClient *api, QWidget *parent)
    : BasePage(api, parent), m_config(config)
{
    setPageTitle(QStringLiteral("SSH 终端"));
    setPageSubtitle(QStringLiteral("多会话终端 · 支持 SFTP 侧栏、密钥登录与一键提权"));

    m_workspace = new SshWorkspace(config, this);
    body()->addWidget(m_workspace, 1);

    connect(m_workspace, &SshWorkspace::newWindowRequested, this, &SshPage::openDetachedWindow);
    connect(m_workspace, &SshWorkspace::openUrlRequested, this, [](const QString &url) {
        if (!url.isEmpty()) {
            QDesktopServices::openUrl(QUrl(url));
        }
    });
}

SshPage::~SshPage() = default;

void SshPage::refresh()
{
    if (m_workspace) {
        m_workspace->reloadTheme();
    }
}

void SshPage::reloadTheme()
{
    if (m_workspace) {
        m_workspace->reloadTheme();
    }
}

void SshPage::openDetachedWindow()
{
    auto *window = new QMainWindow(this, Qt::Window);
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    window->setWindowTitle(QStringLiteral("SSH 终端 · 飞牛终端"));
    window->resize(1180, 760);

    auto *workspace = new SshWorkspace(m_config, window);
    window->setCentralWidget(workspace);
    window->show();
    window->raise();
    window->activateWindow();
}
