#include "ui/ssh/sftp_panel.h"

#include "ui/icons.h"
#include "ui/ssh/pty_runner.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

/** 远端 shell 里的单引号转义：' → '\'' */
QString shQuote(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + out + QLatin1Char('\'');
}

} // namespace

SftpPanel::SftpPanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
    m_runner = new PtyRunner(this);
    connect(m_runner, &PtyRunner::finished, this, [this](int code, const QString &output) {
        auto callback = m_callback;
        m_callback = nullptr;
        m_busy = false;
        if (callback) {
            callback(code == 0, output);
        }
    });
    setStatus(QStringLiteral("未连接"), false);
}

SftpPanel::~SftpPanel() = default;

void SftpPanel::buildUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 8, 10);
    lay->setSpacing(8);

    auto *head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(6);
    head->addWidget(makeLabel(QStringLiteral("SFTP 文件"), QStringLiteral("h2"), this));
    head->addStretch(1);
    lay->addLayout(head);

    m_pathLabel = makeLabel(QStringLiteral("/"), QStringLiteral("muted"), this);
    m_pathLabel->setWordWrap(false);
    m_pathLabel->setToolTip(QStringLiteral("远端当前目录"));
    lay->addWidget(m_pathLabel);

    auto *tools = new QHBoxLayout();
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(4);
    m_upButton = makeButton(QStringLiteral("上级"), QStringLiteral("ghost"), this);
    m_refreshButton = makeButton(QStringLiteral("刷新"), QStringLiteral("ghost"), this);
    m_uploadButton = makeButton(QStringLiteral("上传"), QStringLiteral("primary"), this);
    m_mkdirButton = makeButton(QStringLiteral("新建"), QStringLiteral("ghost"), this);
    for (QPushButton *b : {m_upButton, m_refreshButton, m_uploadButton, m_mkdirButton}) {
        b->setMinimumHeight(26);
        tools->addWidget(b);
    }
    tools->addStretch(1);
    lay->addLayout(tools);

    auto *tools2 = new QHBoxLayout();
    tools2->setContentsMargins(0, 0, 0, 0);
    tools2->setSpacing(4);
    m_downloadButton = makeButton(QStringLiteral("下载"), QStringLiteral("ghost"), this);
    m_renameButton = makeButton(QStringLiteral("重命名"), QStringLiteral("ghost"), this);
    m_deleteButton = makeButton(QStringLiteral("删除"), QStringLiteral("ghost"), this);
    for (QPushButton *b : {m_downloadButton, m_renameButton, m_deleteButton}) {
        b->setMinimumHeight(26);
        tools2->addWidget(b);
    }
    tools2->addStretch(1);
    lay->addLayout(tools2);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("SftpList"));
    m_list->setAlternatingRowColors(false);
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    lay->addWidget(m_list, 1);

    m_status = makeLabel(QString(), QStringLiteral("muted"), this);
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    connect(m_refreshButton, &QPushButton::clicked, this, &SftpPanel::refresh);
    connect(m_upButton, &QPushButton::clicked, this, &SftpPanel::goUp);
    connect(m_uploadButton, &QPushButton::clicked, this, &SftpPanel::uploadFiles);
    connect(m_downloadButton, &QPushButton::clicked, this, &SftpPanel::downloadSelected);
    connect(m_mkdirButton, &QPushButton::clicked, this, &SftpPanel::makeDirectory);
    connect(m_renameButton, &QPushButton::clicked, this, &SftpPanel::renameSelected);
    connect(m_deleteButton, &QPushButton::clicked, this, &SftpPanel::removeSelected);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        openSelected();
    });
}

void SftpPanel::reloadTheme()
{
    const Theme::Palette p = Theme::current();
    if (m_list) {
        m_list->setStyleSheet(QStringLiteral(
                                  "QListWidget{background:transparent;border:1px solid %1;"
                                  "border-radius:8px;color:%2;outline:none;}"
                                  "QListWidget::item{padding:5px 6px;}"
                                  "QListWidget::item:selected{background:%3;color:%4;}")
                                  .arg(p.border.name(), p.text.name(), p.accent.name(),
                                       p.accentText.name()));
    }
}

void SftpPanel::setTarget(const QString &host, int port, const QString &user,
                          const QString &keyPath, const QString &password)
{
    m_host = host;
    m_port = port > 0 ? port : 22;
    m_user = user;
    m_keyPath = keyPath;
    m_password = password;
    m_path = QStringLiteral("/");
    m_list->clear();
    if (m_host.isEmpty()) {
        setStatus(QStringLiteral("未连接"), false);
        return;
    }
    setStatus(QStringLiteral("正在读取远端目录…"), false);
    refresh();
}

void SftpPanel::clearTarget()
{
    m_host.clear();
    m_path = QStringLiteral("/");
    m_list->clear();
    m_pathLabel->setText(QStringLiteral("/"));
    setStatus(QStringLiteral("未连接"), false);
}

QString SftpPanel::sshCommandPrefix() const
{
    QString line = PtyRunner::quote(PtyRunner::sshExecutable());
    line += QStringLiteral(" -T -p %1 -o StrictHostKeyChecking=accept-new -o ServerAliveInterval=15")
                .arg(m_port);
    if (!m_keyPath.trimmed().isEmpty()) {
        line += QStringLiteral(" -i ") + PtyRunner::quote(m_keyPath)
                + QStringLiteral(" -o IdentitiesOnly=yes");
    }
    line += QLatin1Char(' ') + PtyRunner::quote(remoteBase());
    return line;
}

QString SftpPanel::scpCommandPrefix() const
{
    QString line = PtyRunner::quote(PtyRunner::scpExecutable());
    line += QStringLiteral(" -P %1 -o StrictHostKeyChecking=accept-new -q").arg(m_port);
    if (!m_keyPath.trimmed().isEmpty()) {
        line += QStringLiteral(" -i ") + PtyRunner::quote(m_keyPath)
                + QStringLiteral(" -o IdentitiesOnly=yes");
    }
    return line;
}

QString SftpPanel::joinRemote(const QString &name) const
{
    if (m_path == QLatin1String("/")) {
        return QLatin1Char('/') + name;
    }
    return m_path + QLatin1Char('/') + name;
}

void SftpPanel::runSsh(const QString &remoteCommand,
                       std::function<void(bool, const QString &)> done)
{
    if (m_busy || m_host.isEmpty() || !PtyRunner::supported()) {
        return;
    }
    m_busy = true;
    m_callback = std::move(done);
    const QString command = sshCommandPrefix() + QLatin1Char(' ')
                            + PtyRunner::quote(remoteCommand);
    if (!m_runner->run(command, m_password, 25000)) {
        m_busy = false;
        m_callback = nullptr;
        setStatus(QStringLiteral("无法启动系统 ssh 客户端"), true);
    }
}

void SftpPanel::runScp(const QString &args, std::function<void(bool, const QString &)> done)
{
    if (m_busy || m_host.isEmpty() || !PtyRunner::supported()) {
        return;
    }
    m_busy = true;
    m_callback = std::move(done);
    const QString command = scpCommandPrefix() + QLatin1Char(' ') + args;
    if (!m_runner->run(command, m_password, 120000)) {
        m_busy = false;
        m_callback = nullptr;
        setStatus(QStringLiteral("无法启动系统 scp 客户端"), true);
    }
}

void SftpPanel::refresh()
{
    if (m_host.isEmpty()) {
        return;
    }
    // 注意：这里是远端 shell 里的 find -printf 格式串，必须用单 %（%% 会被 find 当成字面 %）
    const QString cmd = QStringLiteral(
                            "cd %1 && LC_ALL=C find . -mindepth 1 -maxdepth 1 "
                            "-printf '%y|%s|%f\\n' 2>/dev/null | LC_ALL=C sort")
                            .arg(shQuote(m_path));
    const QString path = m_path;
    QPointer<SftpPanel> guard(this);
    runSsh(cmd, [this, guard, path](bool ok, const QString &output) {
        if (!guard) {
            return;
        }
        if (!ok) {
            setStatus(QStringLiteral("读取目录失败（检查权限或网络）"), true);
            return;
        }
        m_path = path;
        m_pathLabel->setText(m_path);
        m_list->clear();
        const Theme::Palette pal = Theme::current();
        int count = 0;
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;?]*[A-Za-z]"));
        for (const QString &raw : lines) {
            QString line = raw;
            line.remove(ansi);
            line = line.trimmed();
            if (line.startsWith(QLatin1String("cd ")) || line.startsWith(QLatin1String("find "))) {
                continue;
            }
            const QStringList parts = line.split(QLatin1Char('|'));
            if (parts.size() < 3) {
                continue;
            }
            const QString type = parts.at(0).trimmed();
            if (type != QLatin1String("d") && type != QLatin1String("f")
                && type != QLatin1String("l")) {
                continue;
            }
            const qint64 size = parts.at(1).trimmed().toLongLong();
            const QString name = parts.mid(2).join(QLatin1Char('|'));
            if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) {
                continue;
            }
            const bool isDir = (type == QLatin1String("d"));
            auto *item = new QListWidgetItem(m_list);
            item->setText(name);
            item->setIcon(Icons::icon(isDir ? Icons::Kind::Folder : Icons::Kind::Download,
                                      isDir ? pal.accent : pal.muted, 14));
            item->setData(Qt::UserRole, name);
            item->setData(Qt::UserRole + 1, isDir);
            item->setToolTip(isDir ? name : QStringLiteral("%1 · %2").arg(name, Fmt::bytes(size)));
            ++count;
        }
        setStatus(count == 0 ? QStringLiteral("（空目录）")
                             : QStringLiteral("%1 项").arg(count),
                  false);
    });
}

void SftpPanel::goUp()
{
    if (m_host.isEmpty() || m_path == QLatin1String("/")) {
        return;
    }
    m_path = QFileInfo(m_path).path();
    if (m_path.isEmpty()) {
        m_path = QStringLiteral("/");
    }
    refresh();
}

QString SftpPanel::selectedName() const
{
    QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

bool SftpPanel::selectedIsDir() const
{
    QListWidgetItem *item = m_list->currentItem();
    return item && item->data(Qt::UserRole + 1).toBool();
}

void SftpPanel::openSelected()
{
    if (selectedName().isEmpty()) {
        return;
    }
    if (selectedIsDir()) {
        m_path = joinRemote(selectedName());
        refresh();
    } else {
        downloadSelected();
    }
}

void SftpPanel::enterSelected()
{
    openSelected();
}

void SftpPanel::makeDirectory()
{
    if (m_host.isEmpty()) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("新建文件夹"),
                                               QStringLiteral("文件夹名称："), QLineEdit::Normal,
                                               QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    const QString target = joinRemote(name);
    QPointer<SftpPanel> guard(this);
    runSsh(QStringLiteral("mkdir -p %1").arg(shQuote(target)),
           [this, guard](bool okResult, const QString &) {
               if (!guard) {
                   return;
               }
               setStatus(okResult ? QStringLiteral("文件夹已创建")
                                  : QStringLiteral("创建失败（可能没有权限）"),
                         !okResult);
               if (okResult) {
                   refresh();
               }
           });
}

void SftpPanel::renameSelected()
{
    const QString name = selectedName();
    if (name.isEmpty()) {
        return;
    }
    bool ok = false;
    const QString newName = QInputDialog::getText(this, QStringLiteral("重命名"),
                                                  QStringLiteral("新名称："), QLineEdit::Normal,
                                                  name, &ok)
                                .trimmed();
    if (!ok || newName.isEmpty() || newName == name) {
        return;
    }
    const QString from = joinRemote(name);
    const QString to = joinRemote(newName);
    QPointer<SftpPanel> guard(this);
    runSsh(QStringLiteral("mv %1 %2").arg(shQuote(from), shQuote(to)),
           [this, guard](bool okResult, const QString &) {
               if (!guard) {
                   return;
               }
               setStatus(okResult ? QStringLiteral("已重命名")
                                  : QStringLiteral("重命名失败"),
                         !okResult);
               if (okResult) {
                   refresh();
               }
           });
}

void SftpPanel::removeSelected()
{
    const QString name = selectedName();
    if (name.isEmpty()) {
        return;
    }
    const bool isDir = selectedIsDir();
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("删除"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("确定删除「%1」吗？此操作不可恢复。").arg(name));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) {
        return;
    }
    const QString target = joinRemote(name);
    const QString cmd = isDir ? QStringLiteral("rm -rf %1").arg(shQuote(target))
                              : QStringLiteral("rm -f %1").arg(shQuote(target));
    QPointer<SftpPanel> guard(this);
    runSsh(cmd, [this, guard](bool okResult, const QString &) {
        if (!guard) {
            return;
        }
        setStatus(okResult ? QStringLiteral("已删除") : QStringLiteral("删除失败"), !okResult);
        if (okResult) {
            refresh();
        }
    });
}

void SftpPanel::uploadFiles()
{
    if (m_host.isEmpty()) {
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("选择要上传的文件"));
    if (files.isEmpty()) {
        return;
    }
    QStringList args;
    for (const QString &file : files) {
        args << PtyRunner::quote(file);
    }
    const QString target = remoteBase() + QLatin1Char(':') + m_path;
    if (!m_path.endsWith(QLatin1Char('/'))) {
        args << PtyRunner::quote(target + QLatin1Char('/'));
    } else {
        args << PtyRunner::quote(target);
    }
    setStatus(QStringLiteral("正在上传 %1 个文件…").arg(files.size()), false);
    QPointer<SftpPanel> guard(this);
    runScp(args.join(QLatin1Char(' ')), [this, guard, files](bool okResult, const QString &) {
        if (!guard) {
            return;
        }
        setStatus(okResult ? QStringLiteral("上传完成：%1 个文件").arg(files.size())
                           : QStringLiteral("上传失败"),
                 !okResult);
        refresh();
    });
}

void SftpPanel::downloadSelected()
{
    const QString name = selectedName();
    if (name.isEmpty() || m_host.isEmpty()) {
        return;
    }
    const bool isDir = selectedIsDir();
    QString local;
    if (isDir) {
        local = QFileDialog::getExistingDirectory(this, QStringLiteral("选择保存到本地目录"));
        if (local.isEmpty()) {
            return;
        }
    } else {
        local = QFileDialog::getSaveFileName(this, QStringLiteral("保存文件"), name);
        if (local.isEmpty()) {
            return;
        }
    }
    QString args;
    if (isDir) {
        args = QStringLiteral(" -r ");
    }
    args += PtyRunner::quote(remoteBase() + QLatin1Char(':') + joinRemote(name));
    args += QLatin1Char(' ') + PtyRunner::quote(local);
    setStatus(QStringLiteral("正在下载 %1…").arg(name), false);
    QPointer<SftpPanel> guard(this);
    runScp(args, [this, guard, name](bool okResult, const QString &) {
        if (!guard) {
            return;
        }
        setStatus(okResult ? QStringLiteral("下载完成：%1").arg(name)
                           : QStringLiteral("下载失败"),
                 !okResult);
    });
}

void SftpPanel::setStatus(const QString &text, bool error)
{
    if (!m_status) {
        return;
    }
    m_status->setText(text);
    m_status->setProperty("tone", error ? QStringLiteral("danger") : QStringLiteral("muted"));
    Theme::repolish(m_status);
}
