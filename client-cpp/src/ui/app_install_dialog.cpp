#include "ui/app_install_dialog.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

AppInstallDialog::AppInstallDialog(const QString &appName, QWidget *parent)
    : DialogWindow(parent)
{
    setWindowTitle(QStringLiteral("安装「%1」").arg(appName));
    setModal(true);
    resize(460, 300);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 18);
    root->setSpacing(12);

    auto *title = makeLabel(QStringLiteral("「%1」还没有安装").arg(appName),
                            QStringLiteral("h2"), this);
    root->addWidget(title);

    auto *body = makeLabel(
        QStringLiteral("请在飞牛应用中心搜索并安装「%1」。\n"
                       "可以直接在软件内的飞牛网页管理后台里完成，也可以用系统浏览器打开。")
            .arg(appName),
        QStringLiteral("muted"), this);
    body->setWordWrap(true);
    root->addWidget(body);

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 5);
    m_bar->setValue(5);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(4);
    root->addWidget(m_bar);

    m_countLabel = makeLabel(QString(), QStringLiteral("muted"), this);
    root->addWidget(m_countLabel);

    root->addStretch(1);

    auto *row = new QHBoxLayout();
    auto *cancel = makeButton(QStringLiteral("取消"), QStringLiteral("ghost"), this);
    auto *browser = makeButton(QStringLiteral("用系统浏览器打开"), QStringLiteral("ghost"), this);
    m_inside = makeButton(QStringLiteral("在软件内打开应用中心"), QStringLiteral("primary"), this);
    m_inside->setIcon(Icons::icon(Icons::Kind::Globe, Theme::current().accentText, 15));
    row->addWidget(cancel);
    row->addStretch(1);
    row->addWidget(browser);
    row->addWidget(m_inside);
    root->addLayout(row);

    connect(cancel, &QPushButton::clicked, this, [this]() {
        m_decided = true;
        reject();
    });
    connect(browser, &QPushButton::clicked, this, [this]() {
        m_decided = true;
        emit openBrowser();
        accept();
    });
    connect(m_inside, &QPushButton::clicked, this, [this]() { chooseInside(); });

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &AppInstallDialog::tick);
    tick();
    m_timer->start();
}

void AppInstallDialog::tick()
{
    m_countLabel->setText(
        QStringLiteral("%1 秒后自动在软件内打开应用中心…").arg(qMax(0, m_left)));
    m_bar->setValue(qMax(0, m_left));
    if (m_left <= 0) {
        chooseInside();
        return;
    }
    --m_left;
}

void AppInstallDialog::chooseInside()
{
    if (m_decided) {
        return;
    }
    m_decided = true;
    m_timer->stop();
    emit openInside();
    accept();
}
