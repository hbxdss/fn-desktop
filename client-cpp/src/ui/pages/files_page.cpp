#include "ui/pages/files_page.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include "core/api_client.h"
#include "core/config.h"

#include <QAbstractItemView>
#include <QAction>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace {

QString formatMtime(const QJsonValue &value)
{
    if (value.isString()) {
        return Fmt::dateTime(value.toString());
    }
    const qint64 ms = qint64(value.toDouble());
    if (ms <= 0) {
        return QStringLiteral("—");
    }
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString typeText(const QJsonObject &item)
{
    if (item.value(QStringLiteral("isMount")).toBool()) {
        return item.value(QStringLiteral("readonly")).toBool() ? QStringLiteral("挂载（只读）")
                                                               : QStringLiteral("挂载目录");
    }
    if (item.value(QStringLiteral("isDir")).toBool()) {
        return QStringLiteral("文件夹");
    }
    const QString ext = item.value(QStringLiteral("ext")).toString();
    if (ext.isEmpty()) {
        return QStringLiteral("文件");
    }
    return QStringLiteral("%1 文件").arg(ext.toUpper());
}

/** 挂载点的可用性描述 */
QString mountStateText(const QJsonObject &item)
{
    if (item.contains(QStringLiteral("exists")) && !item.value(QStringLiteral("exists")).toBool(true)) {
        return QStringLiteral("目录不存在");
    }
    if (item.contains(QStringLiteral("readable")) && !item.value(QStringLiteral("readable")).toBool(true)) {
        return QStringLiteral("无读取权限");
    }
    if (item.value(QStringLiteral("readonly")).toBool()) {
        return QStringLiteral("只读挂载");
    }
    if (item.contains(QStringLiteral("writable"))) {
        return item.value(QStringLiteral("writable")).toBool() ? QStringLiteral("读写可用")
                                                               : QStringLiteral("无写入权限");
    }
    return QStringLiteral("可用");
}

/** 递归收集本地文件/文件夹，产出「本地文件 -> 远程目标目录」两组平行列表 */
void collectLocalEntries(const QString &local, const QString &remoteDir,
                         QStringList &files, QStringList &targets)
{
    const QFileInfo info(local);
    if (info.isFile()) {
        files.append(info.absoluteFilePath());
        targets.append(remoteDir);
        return;
    }
    if (!info.isDir()) {
        return;
    }
    QString base = remoteDir;
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    const QString rootDir = base + QLatin1Char('/') + info.fileName();
    const QDir rootPath(info.absoluteFilePath());
    QDirIterator it(info.absoluteFilePath(), QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString relDir = rootPath.relativeFilePath(QFileInfo(file).absolutePath());
        QString target = rootDir;
        if (relDir != QLatin1String(".") && !relDir.isEmpty()) {
            target += QLatin1Char('/') + relDir;
        }
        files.append(file);
        targets.append(target);
    }
}

/** Windows/Linux 通用非法文件名字符；返回空串表示合法，否则给出错误提示 */
QString fileNameError(const QString &name)
{
    if (name.trimmed().isEmpty()) {
        return QStringLiteral("名称不能为空");
    }
    const QString illegal = QStringLiteral("\\/:*?\"<>|");
    for (const QChar &ch : name) {
        if (illegal.contains(ch)) {
            return QStringLiteral("名称不能包含 \\ / : * ? \" < > | 这些字符");
        }
    }
    if (name == QLatin1String(".") || name == QLatin1String("..")) {
        return QStringLiteral("该名称是保留名，请换一个");
    }
    return QString();
}

/**
 * 带内联校验的名称输入对话框：非法字符 / 空名直接在输入框下用红字提示，
 * 不用等提交后才弹错误框。
 */
QString promptName(QWidget *parent, const QString &title, const QString &label,
                   const QString &text, bool *accepted)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setMinimumWidth(380);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 14);
    layout->setSpacing(8);

    layout->addWidget(makeLabel(label, QStringLiteral("muted"), &dialog));

    auto *edit = new QLineEdit(&dialog);
    edit->setText(text);
    edit->selectAll();
    layout->addWidget(edit);

    auto *error = makeLabel(QString(), QStringLiteral("muted"), &dialog);
    error->setStyleSheet(QStringLiteral("color:#e0533d;"));
    error->setVisible(false);
    layout->addWidget(error);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);

    auto *okButton = buttons->button(QDialogButtonBox::Ok);
    auto validate = [edit, error, okButton]() {
        const QString message = fileNameError(edit->text());
        error->setText(message);
        error->setVisible(!message.isEmpty());
        okButton->setEnabled(message.isEmpty());
    };
    QObject::connect(edit, &QLineEdit::textChanged, &dialog, validate);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    validate();

    *accepted = dialog.exec() == QDialog::Accepted;
    return *accepted ? edit->text().trimmed() : QString();
}

/** 判断目录名是否是全数字 UID（如 NAS 上的 /vol1/1000 用户目录） */
bool isUidName(const QString &name)
{
    if (name.isEmpty()) return false;
    for (const QChar &ch : name) {
        if (!ch.isDigit()) return false;
    }
    return true;
}

/** 飞牛应用文件夹权限指引（多处复用） */
QString permissionHint()
{
    return QStringLiteral(
        "如果提示没有权限，请在飞牛「设置 → 应用 → 飞牛终端 → 文件夹权限」中授予该目录的访问权限后再试。");
}

/**
 * 把 NAS 内部路径转换成用户能直接看懂的文字：
 *
 *   原始 API 路径                →   软件里显示给用户的
 *   ──────────────────────────────────────────────────
 *   /                            →   根目录
 *   /m/下载                      →   下载                （/m 整个去掉）
 *   /vol1/1000/影视              →   LYC 的文件 › 影视    （1000 换成用户名 + "的文件"）
 *   /vol1/public/共享文档         →   public › 共享文档    （不是 UID 段就保留原样）
 *
 * 规则：
 *   - /m/xxx     → 直接显示 xxx，/m 这个虚拟根对用户不可见
 *   - /vol1/后第一段如果全是数字（是 UID），换成当前登录用户名 + "的文件"
 *   - 其他路径各段用 " › " 连起来
 */
QString friendlyPath(const QString &path, const QString &username)
{
    if (path == QLatin1String("/") || path.isEmpty()) {
        return QStringLiteral("根目录");
    };
    // 统一规则：路径第一段若是纯数字（UID，如 1000），替换成 "用户名 的文件"
    auto friendlySegments = [&username](const QString &rest) -> QString {
        if (rest.isEmpty()) {
            return QStringLiteral("挂载");
        }
        const int firstSlash = rest.indexOf(QLatin1Char('/'));
        const QString first = firstSlash < 0 ? rest : rest.left(firstSlash);
        const QString tail = firstSlash < 0 ? QString() : rest.mid(firstSlash + 1);
        bool isUid = !first.isEmpty();
        for (int i = 0; i < first.size(); ++i) {
            if (!first[i].isDigit()) { isUid = false; break; }
        }
        QString head;
        if (isUid) {
            head = username.isEmpty() ? QStringLiteral("用户的文件")
                                      : QStringLiteral("%1 的文件").arg(username);
        } else {
            head = first;
        }
        if (tail.isEmpty()) {
            return head;
        }
        return head + QStringLiteral(" › ") + QString(tail).replace(QLatin1Char('/'), QLatin1String(" › "));
    };
    // 挂载点：/m/{mountName}/...   —— /m 这个虚拟根用户不用知道，直接显示内容
    if (path.startsWith(QStringLiteral("/m/"))) {
        return friendlySegments(path.mid(3));
    }
    // /vol1/...   —— 第一段可能是 UID（数字）或者是共享名（字母）
    if (path.startsWith(QStringLiteral("/vol1/"))) {
        return friendlySegments(path.mid(6));
    }
    // 兜底：同样做 UID 转换（有些后端返回不带 /m 或 /vol1 前缀的路径）
    return friendlySegments(path.startsWith(QLatin1Char('/')) ? path.mid(1) : path);
}

} // namespace

FilesPage::FilesPage(ApiClient *api, AppConfig *config, QWidget *parent)
    : BasePage(api, parent), m_config(config)
{
    setPageTitle(QStringLiteral("文件管理"));
    setPageSubtitle(QStringLiteral("浏览 NAS 共享目录；网盘挂载请在飞牛网页管理后台完成"));

    setAcceptDrops(true);
    buildUi();
    applyIcons();
    loadPath(QStringLiteral("/"));
}

void FilesPage::buildUi()
{
    auto *toolbar = new QFrame(this);
    toolbar->setProperty("card", true);
    toolbar->setProperty("flat", true);

    auto *outer = new QVBoxLayout(toolbar);
    outer->setContentsMargins(12, 10, 12, 10);
    outer->setSpacing(9);

    auto *pathRow = new QHBoxLayout();
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->setSpacing(8);

    m_upButton = makeButton(QStringLiteral("上级"), QStringLiteral("ghost"), toolbar);
    m_upButton->setToolTip(QStringLiteral("返回上一级目录"));
    m_refreshButton = makeButton(QStringLiteral("刷新"), QStringLiteral("ghost"), toolbar);
    m_refreshButton->setToolTip(QStringLiteral("重新读取当前目录"));

    m_pathEdit = new QLineEdit(toolbar);
    m_pathEdit->setText(QStringLiteral("根目录"));
    m_pathEdit->setPlaceholderText(QStringLiteral("显示友好名称；也可直接输入原始路径（如 /m/xxx）后回车"));
    m_pathEdit->setClearButtonEnabled(true);

    pathRow->addWidget(m_upButton);
    pathRow->addWidget(m_refreshButton);
    pathRow->addWidget(m_pathEdit, 1);
    m_friendlyPathLabel = makeLabel(QStringLiteral("根目录"), QStringLiteral("muted"), toolbar);
    m_friendlyPathLabel->setContentsMargins(0, 2, 0, 0);
    // 副标题（实际路径）砍掉：用户不想在界面上看到真实路径，只保留地址栏里的友好名
    m_friendlyPathLabel->hide();
    outer->addLayout(pathRow);
    outer->addWidget(m_friendlyPathLabel);

    auto *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(8);

    m_newDirButton = makeButton(QStringLiteral("新建文件夹"), QStringLiteral("ghost"), toolbar);
    m_uploadButton = makeButton(QStringLiteral("上传文件"), QStringLiteral("primary"), toolbar);
    m_downloadButton = makeButton(QStringLiteral("下载"), QStringLiteral("ghost"), toolbar);
    m_renameButton = makeButton(QStringLiteral("重命名"), QStringLiteral("ghost"), toolbar);
    m_compressButton = makeButton(QStringLiteral("压缩"), QStringLiteral("ghost"), toolbar);
    m_extractButton = makeButton(QStringLiteral("解压"), QStringLiteral("ghost"), toolbar);
    m_deleteButton = makeButton(QStringLiteral("删除"), QStringLiteral("danger"), toolbar);

    m_compressButton->setToolTip(QStringLiteral("将选中的文件 / 文件夹打包为 zip"));
    m_extractButton->setToolTip(QStringLiteral("解压选中的 zip 到所在目录"));

    actionRow->addWidget(m_newDirButton);
    actionRow->addWidget(m_uploadButton);
    actionRow->addWidget(m_downloadButton);
    actionRow->addWidget(m_renameButton);
    actionRow->addWidget(m_compressButton);
    actionRow->addWidget(m_extractButton);
    actionRow->addWidget(m_deleteButton);
    actionRow->addStretch(1);

    m_status = makeLabel(QStringLiteral("正在加载…"), QStringLiteral("muted"), toolbar);
    actionRow->addWidget(m_status);
    outer->addLayout(actionRow);

    body()->addWidget(toolbar);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("大小"),
                                        QStringLiteral("修改时间"), QStringLiteral("类型")});
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(34);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setSectionsClickable(true);
    m_table->horizontalHeader()->setSortIndicatorShown(true);
    m_table->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { openRow(row); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &FilesPage::updateButtons);
    // 点击表头按当前列排序（目录始终优先），重新用数据渲染而不是让表格自己排序，
    // 这样大小 / 时间列才能按数值比较而不是按文本。
    connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
            [this](int column, Qt::SortOrder order) {
                m_sortColumn = column;
                m_sortOrder = int(order);
                fillTable(m_items);
            });

    // 右键菜单：打开 / 下载 / 重命名 / 压缩 / 解压 / 删除
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this,
            &FilesPage::showContextMenu);

    body()->addWidget(m_table, 1);

    auto *uploadRow = new QHBoxLayout();
    uploadRow->setContentsMargins(0, 0, 0, 0);
    uploadRow->setSpacing(10);

    m_uploadLabel = makeLabel(QString(), QStringLiteral("muted"), this);
    m_uploadLabel->setVisible(false);
    m_uploadBar = new QProgressBar(this);
    m_uploadBar->setRange(0, 100);
    m_uploadBar->setValue(0);
    m_uploadBar->setTextVisible(true);
    m_uploadBar->setFixedHeight(14);
    m_uploadBar->setVisible(false);

    uploadRow->addWidget(m_uploadLabel);
    uploadRow->addWidget(m_uploadBar, 1);
    body()->addLayout(uploadRow);

    connect(m_upButton, &QPushButton::clicked, this, &FilesPage::goUp);
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() { loadPath(m_path); });
    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this]() {
        QString text = m_pathEdit->text().trimmed();
        // 如果内容和当前友好名一样（用户没改），就发原始路径
        const QString friendlyNow = friendlyPath(m_path, m_config ? m_config->username : QString());
        if (text == friendlyNow) {
            loadPath(m_path);
            return;
        }
        // 否则当原始路径用（高级用户直接敲 API 路径）
        if (text.isEmpty()) {
            text = QStringLiteral("/");
        }
        loadPath(text);
    });
    connect(m_newDirButton, &QPushButton::clicked, this, &FilesPage::makeDirectory);
    connect(m_uploadButton, &QPushButton::clicked, this, &FilesPage::chooseUpload);
    connect(m_downloadButton, &QPushButton::clicked, this, &FilesPage::downloadSelected);
    connect(m_renameButton, &QPushButton::clicked, this, &FilesPage::renameSelected);
    connect(m_compressButton, &QPushButton::clicked, this, &FilesPage::compressSelected);
    connect(m_extractButton, &QPushButton::clicked, this, &FilesPage::extractSelected);
    connect(m_deleteButton, &QPushButton::clicked, this, &FilesPage::deleteSelected);

    updateButtons();
}

void FilesPage::applyIcons()
{
    const Theme::Palette pal = Theme::current();

    m_upButton->setIcon(Icons::icon(Icons::Kind::Back, pal.text, 15));
    m_refreshButton->setIcon(Icons::icon(Icons::Kind::Refresh, pal.text, 15));
    m_newDirButton->setIcon(Icons::icon(Icons::Kind::Plus, pal.text, 15));
    m_uploadButton->setIcon(Icons::icon(Icons::Kind::Upload, pal.accentText, 15));
    m_downloadButton->setIcon(Icons::icon(Icons::Kind::Download, pal.text, 15));
    m_renameButton->setIcon(Icons::icon(Icons::Kind::Edit, pal.text, 15));
    m_deleteButton->setIcon(Icons::icon(Icons::Kind::Trash, pal.danger, 15));
}

void FilesPage::refresh()
{
    loadPath(m_path);
}

void FilesPage::reloadTheme()
{
    applyIcons();
    if (!m_items.isEmpty()) {
        fillTable(m_items);
    }
}

/** 默认下载目录：优先用设置页配置的下载目录，未配置时退回系统下载文件夹 */
QString FilesPage::defaultDownloadDir() const
{
    if (m_config) {
        const QString configured = m_config->effectiveDownloadDir();
        if (!configured.trimmed().isEmpty()) {
            return configured;
        }
    }
    const QString standard = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return standard.isEmpty() ? QDir::homePath() : standard;
}

/** 目录没有读取权限时的引导弹窗（指向飞牛网页后台） */
void FilesPage::showPermissionGuide()
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("无法访问此目录"));
    box.setText(QStringLiteral("目录不在当前账号的授权范围内。"));
    box.setInformativeText(QStringLiteral(
        "挂载网盘、授权文件夹权限需要在飞牛网页管理后台完成。"));
    QPushButton *guideButton = box.addButton(QStringLiteral("打开飞牛网页管理后台"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("知道了"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == guideButton) {
        emit requestAdminMountSettings();
        showToast(QStringLiteral("正在跳转管理后台…"), QStringLiteral("info"));
    }
}

void FilesPage::loadPath(const QString &path)
{
    QString target = path.trimmed();
    if (!target.startsWith(QLatin1Char('/'))) {
        target.prepend(QLatin1Char('/'));
    }

    const QString url = QStringLiteral("/api/files?path=%1")
                            .arg(QString::fromUtf8(QUrl::toPercentEncoding(target)));

    m_status->setText(QStringLiteral("正在加载…"));
    api()->get(url, [this, target](bool ok, const QJsonObject &data, const QString &message) {
        if (!ok) {
            m_status->setText(QStringLiteral("加载失败"));
            const QString code = data.value(QStringLiteral("code")).toString();
            const bool noPerm = code == QLatin1String("NO_READ_PERMISSION")
                                || message.contains(QStringLiteral("权限"));
            const bool notFound = code == QLatin1String("NOT_FOUND")
                                  || message.contains(QStringLiteral("目录不存在"));
            if (noPerm) {
                showPermissionGuide();
            } else if (notFound && target.startsWith(QLatin1String("/m/"))) {
                // 挂载点指向的目录在 NAS 上不存在：给出可操作的指引而不是干巴巴的报错
                const QString mountName = target.mid(3).section(QLatin1Char('/'), 0, 0);
                QMessageBox box(this);
                box.setWindowTitle(QStringLiteral("挂载目录不存在"));
                box.setIcon(QMessageBox::Warning);
                box.setText(QStringLiteral("挂载点「%1」指向的目录在 NAS 上不存在，可能已被移动、删除或盘符变化。")
                                .arg(mountName));
                box.setInformativeText(QStringLiteral(
                    "请到飞牛终端的「设置 → 挂载管理」里检查或重新选择挂载路径；"
                    "网盘挂载需要在飞牛网页管理后台完成。"));
                QPushButton *fixButton = box.addButton(QStringLiteral("打开挂载设置"), QMessageBox::AcceptRole);
                box.addButton(QStringLiteral("知道了"), QMessageBox::RejectRole);
                box.exec();
                if (box.clickedButton() == fixButton) {
                    emit requestAdminMountSettings();
                }
                loadPath(QStringLiteral("/"));
            } else {
                showToast(message, QStringLiteral("error"));
            }
            return;
        }
        const QString resolved = data.value(QStringLiteral("path")).toString(target);
        m_path = resolved.isEmpty() ? QStringLiteral("/") : resolved;
        const QString display = friendlyPath(m_path, m_config ? m_config->username : QString());
        // 界面上不再出现 /m/xxx 这类内部路径：输入框与副标题都显示友好名，
        // 原始路径只放在鼠标悬停提示里，方便排错。
        m_pathEdit->setText(display);
        m_friendlyPathLabel->setText(QStringLiteral("实际路径：%1").arg(m_path));
        m_friendlyPathLabel->setToolTip(m_path);
        fillTable(data.value(QStringLiteral("items")).toArray());
        m_status->setText(QStringLiteral("%1 个项目").arg(m_items.size()));
        updateButtons();
    });
}

void FilesPage::fillTable(const QJsonArray &items)
{
    m_items = items;

    QVector<QJsonObject> rows;
    rows.reserve(items.size());
    const bool isRoot = (m_path == QLatin1String("/"));
    const QString username = m_config ? m_config->username : QString();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString name = item.value(QStringLiteral("name")).toString();
        const bool isMount = item.value(QStringLiteral("isMount")).toBool();
        const bool isDir = isMount || item.value(QStringLiteral("isDir")).toBool();
        // 根目录：只保留挂载点（用户文件夹入口）和纯数字 UID 目录
        // 挂载点是飞牛 NAS 上通过网页后台挂载的 /m/<name>；UID 目录是老版本可能在 FILES_ROOT 下直接出现的数字目录
        // 过滤掉应用内部文件、非挂载的非 UID 目录、普通文件
        if (isRoot) {
            if (!isMount && !isUidName(name)) continue;
        }
        rows.append(item);
    }
    applySort(rows);

    const Theme::Palette pal = Theme::current();
    const QIcon folderIcon = Icons::icon(Icons::Kind::Folder, pal.accent, 16);
    const QIcon mountIcon = Icons::icon(Icons::Kind::Server, pal.accent, 16);

    m_table->setUpdatesEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(rows.size());

    for (int row = 0; row < rows.size(); ++row) {
        const QJsonObject item = rows.at(row);
        QString name = item.value(QStringLiteral("name")).toString();
        const bool isMount = item.value(QStringLiteral("isMount")).toBool();
        const bool isDir = isMount || item.value(QStringLiteral("isDir")).toBool();

        // 根目录里，挂载点名或 UID 目录名如果是纯数字，替换成 "用户名 的文件"
        if (isRoot && isDir && isUidName(name)) {
            name = username.isEmpty() ? QStringLiteral("用户的文件")
                                       : QStringLiteral("%1 的文件").arg(username);
        }

        auto *nameItem = new QTableWidgetItem(name);
        if (isMount) {
            nameItem->setIcon(mountIcon);
        } else if (isDir) {
            nameItem->setIcon(folderIcon);
        }
        nameItem->setData(Qt::UserRole, item.value(QStringLiteral("path")).toString());
        nameItem->setData(Qt::UserRole + 1, isDir);
        nameItem->setData(Qt::UserRole + 2, isMount);
        if (isMount) {
            const QString state = mountStateText(item);
            QString tip = QStringLiteral("%1\n状态：%2").arg(name, state);
            nameItem->setToolTip(tip);
        } else {
            nameItem->setToolTip(name);
        }
        m_table->setItem(row, 0, nameItem);

        auto *sizeItem = new QTableWidgetItem(
            isDir ? QStringLiteral("—")
                  : Fmt::bytes(qint64(item.value(QStringLiteral("size")).toDouble())));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 1, sizeItem);

        auto *timeItem = new QTableWidgetItem(formatMtime(item.value(QStringLiteral("mtime"))));
        m_table->setItem(row, 2, timeItem);

        auto *typeItem = new QTableWidgetItem(typeText(item));
        typeItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 3, typeItem);
    }

    m_table->setUpdatesEnabled(true);
}

/** 按表头排序：目录/挂载点始终排在文件前面，其余按当前列升 / 降序。 */
void FilesPage::applySort(QVector<QJsonObject> &rows) const
{
    const int column = m_sortColumn;
    const bool descending = m_sortOrder == int(Qt::DescendingOrder);

    const auto less = [column](const QJsonObject &a, const QJsonObject &b) -> int {
        const bool dirA = a.value(QStringLiteral("isMount")).toBool()
                          || a.value(QStringLiteral("isDir")).toBool();
        const bool dirB = b.value(QStringLiteral("isMount")).toBool()
                          || b.value(QStringLiteral("isDir")).toBool();
        if (dirA != dirB) {
            return dirA ? -1 : 1;
        }
        switch (column) {
        case 1: { // 大小（目录视为 0）
            const double sizeA = a.value(QStringLiteral("size")).toDouble();
            const double sizeB = b.value(QStringLiteral("size")).toDouble();
            return sizeA < sizeB ? -1 : (sizeA > sizeB ? 1 : 0);
        }
        case 2: { // 修改时间（可能是毫秒时间戳，也可能是字符串）
            qint64 tA = a.value(QStringLiteral("mtime")).isString()
                            ? QDateTime::fromString(a.value(QStringLiteral("mtime")).toString(),
                                                    Qt::ISODate)
                                  .toMSecsSinceEpoch()
                            : qint64(a.value(QStringLiteral("mtime")).toDouble());
            qint64 tB = b.value(QStringLiteral("mtime")).isString()
                            ? QDateTime::fromString(b.value(QStringLiteral("mtime")).toString(),
                                                    Qt::ISODate)
                                  .toMSecsSinceEpoch()
                            : qint64(b.value(QStringLiteral("mtime")).toDouble());
            return tA < tB ? -1 : (tA > tB ? 1 : 0);
        }
        case 3: // 类型
            return QString::localeAwareCompare(typeText(a), typeText(b));
        case 0:
        default:
            return QString::localeAwareCompare(a.value(QStringLiteral("name")).toString(),
                                               b.value(QStringLiteral("name")).toString());
        }
    };

    std::stable_sort(rows.begin(), rows.end(), [less, descending](const QJsonObject &a,
                                                                  const QJsonObject &b) {
        const int cmp = less(a, b);
        return descending ? cmp > 0 : cmp < 0;
    });
}

void FilesPage::showContextMenu(const QPoint &pos)
{
    const QModelIndexList selected = m_table->selectionModel()
                                         ? m_table->selectionModel()->selectedRows(0)
                                         : QModelIndexList();
    const int row = m_table->rowAt(pos.y());
    if (selected.isEmpty() && row < 0) {
        return; // 空白处不弹菜单
    }
    if (row >= 0 && !selected.contains(m_table->model()->index(row, 0))) {
        m_table->selectRow(row); // 右键未选中行时先选中
    }

    QMenu menu(this);
    makePopupRounded(&menu);
    QAction *openAction = menu.addAction(QStringLiteral("打开"));
    menu.addSeparator();
    QAction *downloadAction = menu.addAction(QStringLiteral("下载"));
    QAction *renameAction = menu.addAction(QStringLiteral("重命名"));
    menu.addSeparator();
    QAction *compressAction = menu.addAction(QStringLiteral("压缩"));
    QAction *extractAction = menu.addAction(QStringLiteral("解压"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(QStringLiteral("删除"));
    deleteAction->setProperty("danger", true);

    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    }
    if (chosen == openAction) {
        const QModelIndexList sel = m_table->selectionModel()->selectedRows(0);
        if (sel.size() == 1) {
            openRow(sel.first().row());
        }
        return;
    }
    if (chosen == downloadAction) {
        downloadSelected();
    } else if (chosen == renameAction) {
        renameSelected();
    } else if (chosen == compressAction) {
        compressSelected();
    } else if (chosen == extractAction) {
        extractSelected();
    } else if (chosen == deleteAction) {
        deleteSelected();
    }
}

void FilesPage::goUp()
{
    QString path = m_path;
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    // 挂载点顶层 /m/<名称> 的上级直接回到根，避免解析到不存在的 /m
    if (path.startsWith(QLatin1String("/m/"))) {
        const QString rest = path.mid(3);
        if (!rest.isEmpty() && !rest.contains(QLatin1Char('/'))) {
            loadPath(QStringLiteral("/"));
            return;
        }
    }
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    const QString parent = (slash <= 0) ? QStringLiteral("/") : path.left(slash);
    loadPath(parent);
}

int FilesPage::selectedRow() const
{
    const QModelIndexList selected = m_table->selectionModel()
                                         ? m_table->selectionModel()->selectedRows(0)
                                         : QModelIndexList();
    if (selected.isEmpty()) {
        return -1;
    }
    return selected.first().row();
}

QString FilesPage::rowPath(int row) const
{
    const QTableWidgetItem *item = m_table->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

bool FilesPage::rowIsDir(int row) const
{
    const QTableWidgetItem *item = m_table->item(row, 0);
    return item && item->data(Qt::UserRole + 1).toBool();
}

void FilesPage::openRow(int row)
{
    if (row < 0 || row >= m_table->rowCount()) {
        return;
    }
    if (rowIsDir(row)) {
        loadPath(rowPath(row));
        return;
    }
    m_table->selectRow(row);
    downloadSelected();
}

void FilesPage::downloadSelected()
{
    if (m_busy) {
        return;
    }
    const QModelIndexList selected = m_table->selectionModel()
                                         ? m_table->selectionModel()->selectedRows(0)
                                         : QModelIndexList();

    struct Entry {
        QString remote;
        QString name;
        bool isDir = false;
    };
    QVector<Entry> entries;
    for (const QModelIndex &index : selected) {
        const int row = index.row();
        const QString remote = rowPath(row);
        if (remote.isEmpty()) {
            continue;
        }
        entries.append({remote, QFileInfo(remote).fileName(), rowIsDir(row)});
    }
    if (entries.isEmpty()) {
        showToast(QStringLiteral("请选择要下载的文件或文件夹"), QStringLiteral("error"));
        return;
    }

    // 文件夹打包为 <名称>.zip，文件按原文件名保存
    const auto localNameFor = [](const Entry &entry) {
        return entry.isDir ? entry.name + QStringLiteral(".zip") : entry.name;
    };

    const QString defaultDir = defaultDownloadDir();

    if (entries.size() == 1) {
        const Entry entry = entries.first();
        const QString suggested = QDir(defaultDir).filePath(localNameFor(entry));
        const QString local = QFileDialog::getSaveFileName(
            this, entry.isDir ? QStringLiteral("保存文件夹（打包为 zip）") : QStringLiteral("保存文件"),
            suggested,
            entry.isDir ? QStringLiteral("Zip 压缩包 (*.zip)") : QString());
        if (local.isEmpty()) {
            return;
        }
        m_status->setText(entry.isDir ? QStringLiteral("正在打包下载文件夹…") : QStringLiteral("正在下载…"));
        const bool isDir = entry.isDir;
        const auto done = [this, local](bool ok, const QString &message) {
            if (ok) {
                m_status->setText(QStringLiteral("下载完成"));
                showToast(QStringLiteral("已保存到 %1").arg(QDir::toNativeSeparators(local)),
                          QStringLiteral("success"));
            } else {
                m_status->setText(QStringLiteral("下载失败"));
                showToast(message, QStringLiteral("error"));
            }
        };
        if (isDir) {
            api()->downloadFolder(entry.remote, local, done);
        } else {
            api()->download(entry.remote, local, done);
        }
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择保存目录"), defaultDir);
    if (dir.isEmpty()) {
        return;
    }

    auto pending = std::make_shared<int>(entries.size());
    auto failed = std::make_shared<int>(0);
    m_status->setText(QStringLiteral("正在下载 %1 个项目…").arg(entries.size()));

    for (const Entry &entry : entries) {
        const QString local = QDir(dir).filePath(localNameFor(entry));
        const auto done = [this, pending, failed, local, dir](bool ok, const QString &message) {
            if (!ok) {
                ++(*failed);
                showToast(QStringLiteral("%1：%2").arg(QFileInfo(local).fileName(), message),
                          QStringLiteral("error"));
            }
            if (--(*pending) == 0) {
                const int failures = *failed;
                m_status->setText(failures == 0 ? QStringLiteral("下载完成")
                                                : QStringLiteral("部分文件下载失败"));
                if (failures == 0) {
                    showToast(QStringLiteral("全部项目已下载到 %1").arg(QDir::toNativeSeparators(dir)),
                              QStringLiteral("success"));
                }
            }
        };
        if (entry.isDir) {
            api()->downloadFolder(entry.remote, local, done);
        } else {
            api()->download(entry.remote, local, done);
        }
    }
}

void FilesPage::compressSelected()
{
    if (m_busy) {
        return;
    }
    const QModelIndexList selected = m_table->selectionModel()
                                         ? m_table->selectionModel()->selectedRows(0)
                                         : QModelIndexList();
    QStringList paths;
    for (const QModelIndex &index : selected) {
        const QString path = rowPath(index.row());
        if (!path.isEmpty()) {
            paths.append(path);
        }
    }
    if (paths.isEmpty()) {
        showToast(QStringLiteral("请选择要压缩的文件或文件夹"), QStringLiteral("error"));
        return;
    }

    bool accepted = false;
    const QString input = promptName(this, QStringLiteral("压缩"), QStringLiteral("压缩包名称："),
                                     QFileInfo(paths.first()).fileName() + QStringLiteral(".zip"),
                                     &accepted);
    if (!accepted || input.isEmpty()) {
        return;
    }
    const QString name = input.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)
                             ? input
                             : input + QStringLiteral(".zip");

    QString dir = m_path;
    while (dir.endsWith(QLatin1Char('/'))) {
        dir.chop(1);
    }
    const QString target = dir + QLatin1Char('/') + name;

    QJsonObject body;
    body.insert(QStringLiteral("paths"), QJsonArray::fromStringList(paths));
    body.insert(QStringLiteral("target"), target);

    m_status->setText(QStringLiteral("正在压缩…"));
    api()->post(QStringLiteral("/api/files/compress"), body,
                [this, name](bool ok, const QJsonObject &, const QString &message) {
                    if (!ok) {
                        m_status->setText(QStringLiteral("压缩失败"));
                        showToast(message, QStringLiteral("error"));
                        return;
                    }
                    m_status->setText(QStringLiteral("压缩完成"));
                    showToast(QStringLiteral("已压缩为 %1").arg(name), QStringLiteral("success"));
                    loadPath(m_path);
                });
}

void FilesPage::extractSelected()
{
    if (m_busy) {
        return;
    }
    const QModelIndexList selected = m_table->selectionModel()
                                         ? m_table->selectionModel()->selectedRows(0)
                                         : QModelIndexList();
    if (selected.size() != 1) {
        showToast(QStringLiteral("请选择单个 zip 压缩包进行解压"), QStringLiteral("error"));
        return;
    }

    const QString path = rowPath(selected.first().row());
    if (path.isEmpty() || !path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        showToast(QStringLiteral("请选择单个 zip 压缩包进行解压"), QStringLiteral("error"));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("path"), path);

    m_status->setText(QStringLiteral("正在解压…"));
    api()->post(QStringLiteral("/api/files/extract"), body,
                [this](bool ok, const QJsonObject &data, const QString &message) {
                    if (!ok) {
                        m_status->setText(QStringLiteral("解压失败"));
                        showToast(message, QStringLiteral("error"));
                        return;
                    }
                    const int count = data.value(QStringLiteral("count")).toInt();
                    m_status->setText(QStringLiteral("解压完成"));
                    showToast(QStringLiteral("解压完成，共 %1 项").arg(count), QStringLiteral("success"));
                    loadPath(m_path);
                });
}

void FilesPage::renameSelected()
{
    if (m_busy) {
        return;
    }
    const QModelIndexList selected = m_table->selectionModel()
                                         ? m_table->selectionModel()->selectedRows(0)
                                         : QModelIndexList();
    if (selected.size() != 1) {
        showToast(QStringLiteral("请选择单个文件或文件夹进行重命名"), QStringLiteral("error"));
        return;
    }

    const int row = selected.first().row();
    const QString path = rowPath(row);
    if (path.isEmpty()) {
        return;
    }
    const QString current = QFileInfo(path).fileName();

    bool accepted = false;
    const QString name = promptName(this, QStringLiteral("重命名"), QStringLiteral("新的名称："),
                                    current, &accepted);
    if (!accepted || name.isEmpty() || name == current) {
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("path"), path);
    body.insert(QStringLiteral("name"), name);

    api()->post(QStringLiteral("/api/files/rename"), body,
                [this, name](bool ok, const QJsonObject &, const QString &message) {
                    if (!ok) {
                        showToast(message, QStringLiteral("error"));
                        return;
                    }
                    showToast(QStringLiteral("已重命名为 %1").arg(name), QStringLiteral("success"));
                    loadPath(m_path);
                });
}

void FilesPage::makeDirectory()
{
    if (m_busy) {
        return;
    }
    bool accepted = false;
    const QString name = promptName(this, QStringLiteral("新建文件夹"), QStringLiteral("文件夹名称："),
                                    QString(), &accepted);
    if (!accepted || name.isEmpty()) {
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("path"), m_path);
    body.insert(QStringLiteral("name"), name);

    api()->post(QStringLiteral("/api/files/mkdir"), body,
                [this, name](bool ok, const QJsonObject &, const QString &message) {
                    if (!ok) {
                        showToast(message, QStringLiteral("error"));
                        return;
                    }
                    showToast(QStringLiteral("已创建文件夹 %1").arg(name), QStringLiteral("success"));
                    loadPath(m_path);
                });
}

void FilesPage::deleteSelected()
{
    if (m_busy) {
        return;
    }
    const QModelIndexList selected = m_table->selectionModel()
                                         ? m_table->selectionModel()->selectedRows(0)
                                         : QModelIndexList();
    QStringList paths;
    QStringList names;
    for (const QModelIndex &index : selected) {
        const int row = index.row();
        const QString path = rowPath(row);
        if (path.isEmpty()) {
            continue;
        }
        paths.append(path);
        names.append(QFileInfo(path).fileName());
    }
    if (paths.isEmpty()) {
        showToast(QStringLiteral("请选择要删除的项目"), QStringLiteral("error"));
        return;
    }

    const QString detail = names.size() == 1
                               ? names.first()
                               : QStringLiteral("%1 等 %2 个项目").arg(names.first()).arg(names.size());
    const auto answer = QMessageBox::question(this, QStringLiteral("确认删除"),
                                              QStringLiteral("确定要删除 %1 吗？\n文件夹会连同内部内容一并删除，此操作不可撤销。")
                                                  .arg(detail),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    auto pending = std::make_shared<int>(paths.size());
    auto failed = std::make_shared<int>(0);
    m_status->setText(QStringLiteral("正在删除…"));

    for (const QString &path : paths) {
        QJsonObject body;
        body.insert(QStringLiteral("path"), path);
        api()->post(QStringLiteral("/api/files/delete"), body,
                    [this, pending, failed, path](bool ok, const QJsonObject &, const QString &message) {
                        if (!ok) {
                            ++(*failed);
                            showToast(QStringLiteral("删除 %1 失败：%2")
                                          .arg(QFileInfo(path).fileName(), message),
                                      QStringLiteral("error"));
                        }
                        if (--(*pending) == 0) {
                            if (*failed == 0) {
                                showToast(QStringLiteral("删除完成"), QStringLiteral("success"));
                            }
                            loadPath(m_path);
                        }
                    });
    }
}

void FilesPage::chooseUpload()
{
    if (m_busy) {
        return;
    }

    QMenu menu(this);
    makePopupRounded(&menu);
    QAction *filesAction = menu.addAction(QStringLiteral("上传文件…"));
    QAction *folderAction = menu.addAction(QStringLiteral("上传文件夹…"));
    QAction *chosen = menu.exec(m_uploadButton->mapToGlobal(
        QPoint(0, m_uploadButton->height())));
    if (!chosen) {
        return;
    }

    if (chosen == filesAction) {
        const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("选择要上传的文件"));
        startUpload(files);
        return;
    }
    if (chosen != folderAction) {
        return;
    }

    const QString root = QFileDialog::getExistingDirectory(this, QStringLiteral("选择要上传的文件夹"));
    if (root.isEmpty()) {
        return;
    }

    QStringList files;
    QStringList targets;
    collectLocalEntries(root, m_path, files, targets);
    if (files.isEmpty()) {
        showToast(QStringLiteral("该文件夹里没有可上传的文件"), QStringLiteral("error"));
        return;
    }
    startUploadTo(files, targets);
}

void FilesPage::startUpload(const QStringList &files)
{
    if (files.isEmpty()) {
        return;
    }
    QStringList targets;
    targets.reserve(files.size());
    for (int i = 0; i < files.size(); ++i) {
        targets.append(m_path);
    }
    startUploadTo(files, targets);
}

void FilesPage::startUploadTo(const QStringList &files, const QStringList &targetDirs)
{
    if (files.isEmpty()) {
        return;
    }

    // 汇总需要预先创建的目标目录，父级优先
    QStringList dirs;
    for (const QString &dir : targetDirs) {
        QString trimmed = dir;
        while (trimmed.endsWith(QLatin1Char('/')) && trimmed.size() > 1) {
            trimmed.chop(1);
        }
        if (trimmed.isEmpty() || trimmed == QLatin1String("/") || dirs.contains(trimmed)) {
            continue;
        }
        dirs.append(trimmed);
    }
    std::stable_sort(dirs.begin(), dirs.end(), [](const QString &a, const QString &b) {
        return a.count(QLatin1Char('/')) < b.count(QLatin1Char('/'));
    });

    m_busy = true;
    m_uploadBar->setValue(0);
    m_uploadBar->setVisible(true);
    m_uploadLabel->setVisible(true);
    m_uploadLabel->setText(QStringLiteral("正在准备上传…"));
    updateButtons();

    auto createNext = std::make_shared<std::function<void(int)>>();
    auto finalize = [this, files, targetDirs]() {
        m_queue = files;
        m_queueTargets = targetDirs;
        m_queueIndex = 0;
        uploadNext();
    };
    auto abortUpload = [this](const QString &message) {
        m_busy = false;
        m_uploadBar->setVisible(false);
        m_uploadLabel->setVisible(false);
        m_queue.clear();
        m_queueTargets.clear();
        m_queueIndex = 0;
        updateButtons();
        showToast(message, QStringLiteral("error"));
        loadPath(m_path);
    };

    *createNext = [this, dirs, createNext, finalize, abortUpload](int index) {
        if (index >= dirs.size()) {
            finalize();
            return;
        }
        const QString dir = dirs.at(index);
        QString parent = dir;
        while (parent.endsWith(QLatin1Char('/')) && parent.size() > 1) {
            parent.chop(1);
        }
        const int slash = parent.lastIndexOf(QLatin1Char('/'));
        const QString dirName = parent.mid(slash + 1);
        const QString parentPath = slash <= 0 ? QStringLiteral("/") : parent.left(slash);

        QJsonObject body;
        body.insert(QStringLiteral("path"), parentPath);
        body.insert(QStringLiteral("name"), dirName);
        api()->post(QStringLiteral("/api/files/mkdir"), body,
                    [createNext, abortUpload, index](bool ok, const QJsonObject &, const QString &message) {
                        // 目录已存在属于正常情况（同名文件夹），继续即可
                        if (!ok && !message.contains(QStringLiteral("已存在"))) {
                            abortUpload(QStringLiteral("创建目录失败：%1").arg(message));
                            return;
                        }
                        (*createNext)(index + 1);
                    });
    };
    (*createNext)(0);
}

void FilesPage::uploadNext()
{
    if (m_queueIndex >= m_queue.size()) {
        m_busy = false;
        m_uploadBar->setVisible(false);
        m_uploadLabel->setVisible(false);
        m_queue.clear();
        m_queueTargets.clear();
        m_queueIndex = 0;
        updateButtons();
        showToast(QStringLiteral("上传完成"), QStringLiteral("success"));
        loadPath(m_path);
        return;
    }

    const QString local = m_queue.at(m_queueIndex);
    const QString name = QFileInfo(local).fileName();
    const QString targetDir = m_queueTargets.value(m_queueIndex, m_path);

    m_uploadLabel->setText(QStringLiteral("正在上传 %1/%2：%3")
                               .arg(m_queueIndex + 1)
                               .arg(m_queue.size())
                               .arg(name));
    m_uploadBar->setValue(0);

    api()->uploadFile(
        local, targetDir, name,
        [this](qint64 sent, qint64 total) {
            if (total > 0) {
                m_uploadBar->setValue(int(qBound<qint64>(qint64(0), sent * 100 / total, qint64(100))));
            }
        },
        [this, name](bool ok, const QString &message) {
            if (!ok) {
                m_busy = false;
                m_uploadBar->setVisible(false);
                m_uploadLabel->setVisible(false);
                m_queue.clear();
                m_queueTargets.clear();
                m_queueIndex = 0;
                updateButtons();
                showToast(QStringLiteral("上传 %1 失败：%2").arg(name, message), QStringLiteral("error"));
                loadPath(m_path);
                return;
            }
            ++m_queueIndex;
            uploadNext();
        });
}

void FilesPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl &url : urls) {
            if (url.isLocalFile()) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    BasePage::dragEnterEvent(event);
}

void FilesPage::dropEvent(QDropEvent *event)
{
    if (m_busy) {
        showToast(QStringLiteral("正在上传中，请稍候再拖入文件"), QStringLiteral("error"));
        event->ignore();
        return;
    }
    QStringList files;
    QStringList targets;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            collectLocalEntries(url.toLocalFile(), m_path, files, targets);
        }
    }
    if (files.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    m_status->setText(QStringLiteral("准备上传 %1 个文件…").arg(files.size()));
    startUploadTo(files, targets);
}

void FilesPage::updateButtons()
{
    const QModelIndexList selected = m_table->selectionModel()
                                         ? m_table->selectionModel()->selectedRows(0)
                                         : QModelIndexList();
    const bool hasSelection = !selected.isEmpty();
    const bool single = selected.size() == 1;
    const bool busy = m_busy;
    const bool singleZip = single && !rowIsDir(selected.first().row())
                           && rowPath(selected.first().row())
                                  .endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive);

    m_upButton->setEnabled(!busy && m_path != QLatin1String("/"));
    m_refreshButton->setEnabled(!busy);
    m_newDirButton->setEnabled(!busy);
    m_uploadButton->setEnabled(!busy);
    m_downloadButton->setEnabled(!busy && hasSelection);
    m_renameButton->setEnabled(!busy && single);
    m_compressButton->setEnabled(!busy && hasSelection);
    m_extractButton->setEnabled(!busy && singleZip);
    m_deleteButton->setEnabled(!busy && hasSelection);
}
