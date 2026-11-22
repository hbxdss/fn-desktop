#include "ui/wizard_dialog.h"

#include "core/config.h"
#include "ui/icons.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QWidget *pageShell(QWidget *content)
{
    auto *page = new QFrame();
    page->setProperty("wizardPage", true);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->addWidget(content);
    return page;
}

} // namespace

WizardDialog::WizardDialog(AppConfig *config, QWidget *parent)
    : DialogWindow(parent), m_config(config)
{
    setWindowTitle(QStringLiteral("新手向导 · 飞牛终端"));
    setModal(true);
    resize(720, 520);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 20);
    root->setSpacing(14);

    // 顶部：Logo 字 + 步骤
    auto *top = new QHBoxLayout();
    auto *title = makeLabel(QStringLiteral("欢迎使用飞牛终端"), QStringLiteral("h1"), this);
    m_stepLabel = makeLabel(QString(), QStringLiteral("muted"), this);
    top->addWidget(title);
    top->addStretch(1);
    top->addWidget(m_stepLabel);
    root->addLayout(top);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    buildPages();

    auto *row = new QHBoxLayout();
    m_skip = makeButton(QStringLiteral("跳过向导"), QStringLiteral("ghost"), this);
    m_prev = makeButton(QStringLiteral("上一步"), QStringLiteral("ghost"), this);
    m_next = makeButton(QStringLiteral("下一步"), QStringLiteral("primary"), this);
    row->addWidget(m_skip);
    row->addStretch(1);
    row->addWidget(m_prev);
    row->addWidget(m_next);
    root->addLayout(row);

    connect(m_skip, &QPushButton::clicked, this, [this]() { finish(false); });
    connect(m_prev, &QPushButton::clicked, this, [this]() { go(m_index - 1); });
    connect(m_next, &QPushButton::clicked, this, [this]() {
        if (m_index == m_stack->count() - 1) {
            finish(true);
        } else {
            go(m_index + 1);
        }
    });

    go(0);
}

void WizardDialog::setStorageHint(bool needed)
{
    if (m_storageTip) {
        m_storageTip->setVisible(needed);
    }
}

void WizardDialog::buildPages()
{
    // —— ① 欢迎 ——
    {
        auto *box = new QWidget();
        auto *lay = new QVBoxLayout(box);
        lay->setContentsMargins(0, 10, 0, 0);
        lay->setSpacing(12);
        auto *big = makeLabel(
            QStringLiteral("飞牛终端把你的飞牛 NAS 装进一个窗口：\n"
                          "应用、网页管理后台、文件、容器、仪表盘，一站直达。"),
            QStringLiteral("h3"), box);
        big->setWordWrap(true);
        auto *muted = makeLabel(
            QStringLiteral("接下来花一分钟完成基础设置。所有选项之后都可以在「设置」里修改。"),
            QStringLiteral("muted"), box);
        muted->setWordWrap(true);
        lay->addWidget(big);
        lay->addWidget(muted);
        lay->addStretch(1);
        m_stack->addWidget(pageShell(box));
    }

    // —— ② 服务器 ——
    {
        auto *box = new QWidget();
        auto *lay = new QVBoxLayout(box);
        lay->setSpacing(12);
        lay->addWidget(makeLabel(QStringLiteral("确认 NAS 连接信息"), QStringLiteral("h3"), box));
        lay->addWidget(makeLabel(
            QStringLiteral("这些信息需要与飞牛系统一致；不确定可以先保持现状，稍后到「设置」里修改。"),
            QStringLiteral("muted"), box));

        auto *form = new QFrame(box);
        auto *grid = new QVBoxLayout(form);
        grid->setSpacing(10);
        auto addRow = [&](const QString &label, QWidget *editor) {
            auto *row = new QHBoxLayout();
            auto *l = makeLabel(label, QString(), form);
            l->setMinimumWidth(130);
            row->addWidget(l);
            row->addWidget(editor, 1);
            grid->addLayout(row);
        };
        m_hostEdit = new QLineEdit(m_config->host, form);
        m_apiPortBox = new QSpinBox(form);
        m_apiPortBox->setRange(1, 65535);
        m_apiPortBox->setValue(m_config->apiPort);
        m_adminPortBox = new QSpinBox(form);
        m_adminPortBox->setRange(1, 65535);
        m_adminPortBox->setValue(m_config->adminPort);
        addRow(QStringLiteral("NAS 地址"), m_hostEdit);
        addRow(QStringLiteral("客户端接口端口"), m_apiPortBox);
        addRow(QStringLiteral("管理后台端口"), m_adminPortBox);
        lay->addWidget(form);
        lay->addStretch(1);
        m_stack->addWidget(pageShell(box));
    }

    // —— ③ 内置应用 ——
    {
        auto *box = new QWidget();
        auto *lay = new QVBoxLayout(box);
        lay->setSpacing(10);
        lay->addWidget(makeLabel(QStringLiteral("选择要在「应用」页显示的内置应用"),
                                 QStringLiteral("h3"), box));
        lay->addWidget(makeLabel(
            QStringLiteral("它们只是快捷入口，不会在 NAS 上安装任何东西；未安装的应用点击后会引导你去应用中心。"),
            QStringLiteral("muted"), box));
        m_video = new QCheckBox(QStringLiteral("飞牛影视"), box);
        m_photo = new QCheckBox(QStringLiteral("飞牛相册"), box);
        m_music = new QCheckBox(QStringLiteral("飞牛音乐"), box);
        // 配置默认值为 true（首次使用内置应用默认可见）；仅当用户此前显式关闭过才不勾
        m_video->setChecked(m_config->showFnVideo);
        m_photo->setChecked(m_config->showFnPhoto);
        m_music->setChecked(m_config->showFnMusic);
        for (auto *cb : {m_video, m_photo, m_music}) {
            cb->setCursor(Qt::PointingHandCursor);
            lay->addWidget(cb);
        }
        lay->addStretch(1);
        m_stack->addWidget(pageShell(box));
    }

    // —— ④ 文件访问 ——
    {
        auto *box = new QWidget();
        auto *lay = new QVBoxLayout(box);
        lay->setSpacing(12);
        lay->addWidget(makeLabel(QStringLiteral("文件与网盘挂载"), QStringLiteral("h3"), box));
        auto *body = makeLabel(
            QStringLiteral("文件管理会以「你的文件 / 共享文件夹」这样的友好方式呈现，不会暴露系统路径。\n\n"
                           "网盘挂载、文件夹授权由飞牛官方提供：需要时我们会直接带你到"
                           "「飞牛网页管理后台」的对应设置页完成，更安全也更稳定。"),
            QString(), box);
        body->setWordWrap(true);
        lay->addWidget(body);

        m_storageTip = makeLabel(
            QStringLiteral("检测到应用目录尚未授权：请稍后在网页管理后台的「设置 → 应用 → 飞牛终端 → 文件夹权限」"
                           "中勾选个人空间与共享文件夹。"),
            QStringLiteral("muted"), box);
        m_storageTip->setWordWrap(true);
        m_storageTip->setVisible(false);
        m_storageTip->setStyleSheet(QStringLiteral(
            "color:%1; background:rgba(249,115,22,0.12); border:1px solid rgba(249,115,22,0.4);"
            "border-radius:10px; padding:10px 12px;")
                                        .arg(Theme::current().warning.name()));
        lay->addWidget(m_storageTip);
        lay->addStretch(1);
        m_stack->addWidget(pageShell(box));
    }

    // —— ⑤ 高级模式 ——
    {
        auto *box = new QWidget();
        auto *lay = new QVBoxLayout(box);
        lay->setSpacing(12);
        lay->addWidget(makeLabel(QStringLiteral("高级模式"), QStringLiteral("h3"), box));
        auto *body = makeLabel(
            QStringLiteral("开启后，侧栏会出现「高级」分组，收纳容器管理与 SSH 终端，"
                           "并使用赛博朋克霓虹界面。普通使用无需开启，随时可在设置里切换。"),
            QStringLiteral("muted"), box);
        body->setWordWrap(true);
        lay->addWidget(body);
        m_advanced = new QCheckBox(QStringLiteral("开启高级模式"), box);
        m_advanced->setChecked(m_config->advancedMode);
        m_advanced->setCursor(Qt::PointingHandCursor);
        lay->addWidget(m_advanced);
        lay->addStretch(1);
        m_stack->addWidget(pageShell(box));
    }

    // —— ⑥ 完成 ——
    {
        auto *box = new QWidget();
        auto *lay = new QVBoxLayout(box);
        lay->setSpacing(12);
        lay->addWidget(makeLabel(QStringLiteral("一切就绪"), QStringLiteral("h3"), box));
        auto *body = makeLabel(
            QStringLiteral("点击「完成」进入飞牛终端。之后可以在「设置」里重新打开本向导、调整应用显示与高级模式。"),
            QString(), box);
        body->setWordWrap(true);
        lay->addWidget(body);
        lay->addStretch(1);
        m_stack->addWidget(pageShell(box));
    }
}

void WizardDialog::go(int index)
{
    m_index = qBound(0, index, m_stack->count() - 1);
    m_stack->setCurrentIndex(m_index);
    m_stepLabel->setText(QStringLiteral("%1 / %2").arg(m_index + 1).arg(m_stack->count()));
    m_prev->setVisible(m_index > 0);
    m_skip->setVisible(m_index < m_stack->count() - 1);
    m_next->setText(m_index == m_stack->count() - 1 ? QStringLiteral("完成")
                                                     : QStringLiteral("下一步"));
}

void WizardDialog::finish(bool completed)
{
    m_config->wizardCompleted = true;
    if (m_hostEdit) {
        m_config->host = m_hostEdit->text().trimmed();
        m_config->apiPort = m_apiPortBox->value();
        m_config->adminPort = m_adminPortBox->value();
    }
    m_config->showFnVideo = m_video->isChecked();
    m_config->showFnPhoto = m_photo->isChecked();
    m_config->showFnMusic = m_music->isChecked();
    m_config->advancedMode = m_advanced->isChecked();
    m_config->save();
    emit applied();
    accept();
}
