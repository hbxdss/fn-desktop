#include "ui/ssh/ssh_keys_dialog.h"

#include "core/config.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

QString keysDirectory()
{
    const QString dir = AppConfig::dataDir() + QStringLiteral("/keys");
    QDir().mkpath(dir);
    return dir;
}

QString keygenExecutable()
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("ssh-keygen"));
    if (path.isEmpty()) {
        const QString root = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
        path = root + QStringLiteral("/System32/OpenSSH/ssh-keygen.exe");
    }
    return path;
}

} // namespace

SshKeysDialog::SshKeysDialog(QWidget *parent) : DialogWindow(parent)
{
    setWindowTitle(QStringLiteral("SSH 密钥管理"));
    setMinimumSize(560, 440);
    buildUi();
    reload();
}

void SshKeysDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(22, 20, 22, 18);
    outer->setSpacing(12);

    outer->addWidget(makeLabel(QStringLiteral("SSH 密钥管理"), QStringLiteral("h2"), this));
    m_hint = makeLabel(QStringLiteral(
                           "生成的密钥保存在本机配置目录；把对应的 .pub 公钥内容粘贴到 NAS 的 "
                           "~/.ssh/authorized_keys 后即可免密码登录。"),
                       QStringLiteral("muted"), this);
    m_hint->setWordWrap(true);
    outer->addWidget(m_hint);

    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    m_list->setStyleSheet(QStringLiteral(
                              "QListWidget{background:transparent;border:1px solid %1;"
                              "border-radius:8px;outline:none;}"
                              "QListWidget::item{padding:6px;}")
                              .arg(Theme::current().border.name()));
    outer->addWidget(m_list, 1);

    auto *tools = new QHBoxLayout();
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(8);
    auto *generate = makeButton(QStringLiteral("生成新密钥"), QStringLiteral("primary"), this);
    auto *import = makeButton(QStringLiteral("导入私钥"), QStringLiteral("ghost"), this);
    auto *copyPub = makeButton(QStringLiteral("复制公钥"), QStringLiteral("ghost"), this);
    auto *remove = makeButton(QStringLiteral("删除"), QStringLiteral("ghost"), this);
    tools->addWidget(generate);
    tools->addWidget(import);
    tools->addWidget(copyPub);
    tools->addWidget(remove);
    tools->addStretch(1);
    auto *close = makeButton(QStringLiteral("关闭"), QStringLiteral("primary"), this);
    tools->addWidget(close);
    outer->addLayout(tools);

    connect(generate, &QPushButton::clicked, this, &SshKeysDialog::generateKey);
    connect(import, &QPushButton::clicked, this, &SshKeysDialog::importKey);
    connect(copyPub, &QPushButton::clicked, this, &SshKeysDialog::copyPublicKey);
    connect(remove, &QPushButton::clicked, this, &SshKeysDialog::deleteSelected);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
}

void SshKeysDialog::reload()
{
    m_list->clear();
    QDir dir(keysDirectory());
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name);
    int count = 0;
    for (const QFileInfo &info : files) {
        if (info.fileName().endsWith(QLatin1String(".pub"))) {
            continue;
        }
        auto *item = new QListWidgetItem(m_list);
        item->setText(info.fileName());
        item->setData(Qt::UserRole, info.absoluteFilePath());
        item->setToolTip(info.absoluteFilePath());
        ++count;
    }
    if (count == 0) {
        m_hint->setText(QStringLiteral(
            "还没有密钥。点「生成新密钥」创建 ed25519 密钥，然后把 .pub 内容粘贴到 NAS 的 "
            "~/.ssh/authorized_keys。"));
    }
}

QString SshKeysDialog::selectedKeyPath() const
{
    QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void SshKeysDialog::generateKey()
{
    const QString name = QInputDialog::getText(this, QStringLiteral("生成新密钥"),
                                               QStringLiteral("密钥文件名（例如 nas_ed25519）："),
                                               QLineEdit::Normal,
                                               QStringLiteral("nas_ed25519"))
                             .trimmed();
    if (name.isEmpty()) {
        return;
    }
    const QString path = keysDirectory() + QLatin1Char('/') + name;
    if (QFile::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("文件已存在"),
                             QStringLiteral("同名密钥已存在，请换一个名称。"));
        return;
    }
    const QString keygen = keygenExecutable();
    if (keygen.isEmpty() || !QFile::exists(keygen)) {
        QMessageBox::warning(this, QStringLiteral("缺少 ssh-keygen"),
                             QStringLiteral("系统未安装 OpenSSH 客户端，无法生成密钥。"));
        return;
    }
    QProcess process(this);
    process.start(keygen, {QStringLiteral("-t"), QStringLiteral("ed25519"),
                           QStringLiteral("-f"), path, QStringLiteral("-N"), QString(),
                           QStringLiteral("-C"), QStringLiteral("fn-desktop-plus")});
    if (!process.waitForFinished(20000) || process.exitCode() != 0) {
        QMessageBox::warning(this, QStringLiteral("生成失败"),
                             QString::fromLocal8Bit(process.readAllStandardError()));
        return;
    }
    reload();
    m_hint->setText(QStringLiteral("密钥已生成：%1（公钥 %1.pub）").arg(name));
}

void SshKeysDialog::importKey()
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择私钥文件"));
    if (file.isEmpty()) {
        return;
    }
    const QString target = keysDirectory() + QLatin1Char('/') + QFileInfo(file).fileName();
    if (QFile::exists(target)) {
        QMessageBox::warning(this, QStringLiteral("文件已存在"),
                             QStringLiteral("keys 目录里已有同名文件。"));
        return;
    }
    if (!QFile::copy(file, target)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("无法复制到配置目录，请检查权限。"));
        return;
    }
    // 如果同目录有同名 .pub，一并复制，便于查看公钥
    if (QFile::exists(file + QStringLiteral(".pub"))) {
        QFile::copy(file + QStringLiteral(".pub"), target + QStringLiteral(".pub"));
    }
    reload();
    m_hint->setText(QStringLiteral("已导入：%1").arg(QFileInfo(target).fileName()));
}

void SshKeysDialog::copyPublicKey()
{
    const QString path = selectedKeyPath();
    if (path.isEmpty()) {
        return;
    }
    QFile pub(path + QStringLiteral(".pub"));
    if (!pub.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_hint->setText(QStringLiteral("没有找到对应的 .pub 公钥文件。"));
        return;
    }
    const QString text = QString::fromUtf8(pub.readAll()).trimmed();
    if (text.isEmpty()) {
        m_hint->setText(QStringLiteral("公钥文件为空。"));
        return;
    }
    QApplication::clipboard()->setText(text);
    m_hint->setText(QStringLiteral("公钥已复制到剪贴板，粘贴到 NAS 的 ~/.ssh/authorized_keys 即可。"));
}

void SshKeysDialog::deleteSelected()
{
    const QString path = selectedKeyPath();
    if (path.isEmpty()) {
        return;
    }
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("删除密钥"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("确定删除「%1」吗？").arg(QFileInfo(path).fileName()));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) {
        return;
    }
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".pub"));
    reload();
    m_hint->setText(QStringLiteral("已删除密钥。"));
}
