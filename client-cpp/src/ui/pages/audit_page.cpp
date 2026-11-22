#include "ui/pages/audit_page.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include "core/api_client.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString actionLabel(const QString &action)
{
    static const QHash<QString, QString> kLabels = {
        {QStringLiteral("LOGIN"), QStringLiteral("登录")},
        {QStringLiteral("LOGIN_FAILED"), QStringLiteral("登录失败")},
        {QStringLiteral("LOGOUT"), QStringLiteral("退出登录")},
        {QStringLiteral("PASSWORD_CHANGE"), QStringLiteral("修改密码")},
        {QStringLiteral("SETUP_INIT"), QStringLiteral("初始化系统")},
        {QStringLiteral("USER_CREATE"), QStringLiteral("创建用户")},
        {QStringLiteral("USER_EDIT"), QStringLiteral("编辑用户")},
        {QStringLiteral("USER_DELETE"), QStringLiteral("删除用户")},
        {QStringLiteral("USER_RESET_PWD"), QStringLiteral("重置密码")},
        {QStringLiteral("FILE_MKDIR"), QStringLiteral("新建文件夹")},
        {QStringLiteral("FILE_UPLOAD"), QStringLiteral("上传文件")},
        {QStringLiteral("FILE_DOWNLOAD"), QStringLiteral("下载文件")},
        {QStringLiteral("FILE_DELETE"), QStringLiteral("删除文件")},
        {QStringLiteral("FILE_RENAME"), QStringLiteral("重命名文件")},
        {QStringLiteral("DOCKER_START"), QStringLiteral("启动容器")},
        {QStringLiteral("DOCKER_STOP"), QStringLiteral("停止容器")},
        {QStringLiteral("DOCKER_RESTART"), QStringLiteral("重启容器")},
        {QStringLiteral("DOCKER_CONTAINER_START"), QStringLiteral("启动容器")},
        {QStringLiteral("DOCKER_CONTAINER_STOP"), QStringLiteral("停止容器")},
        {QStringLiteral("DOCKER_CONTAINER_RESTART"), QStringLiteral("重启容器")},
        {QStringLiteral("DOCKER_CONTAINER_PAUSE"), QStringLiteral("暂停容器")},
        {QStringLiteral("DOCKER_CONTAINER_UNPAUSE"), QStringLiteral("恢复容器")},
        {QStringLiteral("DOCKER_CONTAINER_KILL"), QStringLiteral("强制结束容器")},
        {QStringLiteral("DOCKER_CONTAINER_CREATE"), QStringLiteral("创建容器")},
        {QStringLiteral("DOCKER_CONTAINER_DELETE"), QStringLiteral("删除容器")},
        {QStringLiteral("DOCKER_CONTAINER_RENAME"), QStringLiteral("重命名容器")},
        {QStringLiteral("DOCKER_CONTAINER_UPDATE"), QStringLiteral("更新容器")},
        {QStringLiteral("DOCKER_IMAGE_PULL"), QStringLiteral("拉取镜像")},
        {QStringLiteral("DOCKER_IMAGE_DELETE"), QStringLiteral("删除镜像")},
        {QStringLiteral("DOCKER_IMAGE_PRUNE"), QStringLiteral("清理镜像")},
        {QStringLiteral("DOCKER_VOLUME_CREATE"), QStringLiteral("创建数据卷")},
        {QStringLiteral("DOCKER_VOLUME_DELETE"), QStringLiteral("删除数据卷")},
        {QStringLiteral("DOCKER_NETWORK_CREATE"), QStringLiteral("创建网络")},
        {QStringLiteral("DOCKER_NETWORK_DELETE"), QStringLiteral("删除网络")},
        {QStringLiteral("FILE_TRASH"), QStringLiteral("移入回收站")},
        {QStringLiteral("TRASH_PURGE"), QStringLiteral("清空回收站")},
        {QStringLiteral("FILE_COMPRESS"), QStringLiteral("压缩文件")},
        {QStringLiteral("FILE_EXTRACT"), QStringLiteral("解压文件")},
        {QStringLiteral("STORAGE_MOUNT"), QStringLiteral("挂载目录")},
        {QStringLiteral("STORAGE_UNMOUNT"), QStringLiteral("卸载目录")},
        {QStringLiteral("SHARE_CREATE"), QStringLiteral("创建分享")},
        {QStringLiteral("SHARE_DELETE"), QStringLiteral("删除分享")},
        {QStringLiteral("SHARE_DOWNLOAD"), QStringLiteral("分享下载")},
        {QStringLiteral("SYSTEM_INIT"), QStringLiteral("初始化系统")}
    };
    const auto it = kLabels.constFind(action);
    if (it != kLabels.constEnd()) {
        return it.value();
    }
    return action.isEmpty() ? QStringLiteral("—") : action;
}

} // namespace

AuditPage::AuditPage(ApiClient *api, QWidget *parent) : BasePage(api, parent)
{
    setPageTitle(QStringLiteral("操作日志"));
    setPageSubtitle(QStringLiteral("记录后台账号的关键操作，便于审计追溯"));

    m_limitBox = new QComboBox(this);
    m_limitBox->addItem(QStringLiteral("最近 50 条"), 50);
    m_limitBox->addItem(QStringLiteral("最近 100 条"), 100);
    m_limitBox->addItem(QStringLiteral("最近 200 条"), 200);
    m_limitBox->addItem(QStringLiteral("最近 500 条"), 500);
    m_limitBox->addItem(QStringLiteral("全部"), 0);
    m_limitBox->setMinimumWidth(130);
    connect(m_limitBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { load(); });
    addHeaderAction(m_limitBox);

    auto *refreshButton = makeButton(QStringLiteral("刷新"), QStringLiteral("ghost"), this);
    refreshButton->setIcon(Icons::icon(Icons::Kind::Refresh, Theme::current().text, 16));
    connect(refreshButton, &QPushButton::clicked, this, [this]() { load(); });
    addHeaderAction(refreshButton);

    m_backupButton = makeButton(QStringLiteral("备份日志"), QStringLiteral("ghost"), this);
    m_backupButton->setIcon(Icons::icon(Icons::Kind::Download, Theme::current().text, 16));
    m_backupButton->setToolTip(QStringLiteral("把全部操作日志导出为 CSV / JSON 文件"));
    connect(m_backupButton, &QPushButton::clicked, this, &AuditPage::startBackup);
    addHeaderAction(m_backupButton);

    buildUi();
    load();
}

void AuditPage::buildUi()
{
    m_summary = makeLabel(QStringLiteral("正在读取日志…"), QStringLiteral("muted"), this);
    body()->addWidget(m_summary);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("操作者"),
                                        QStringLiteral("操作"), QStringLiteral("详情"),
                                        QStringLiteral("来源 IP")});
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(36);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setSectionsClickable(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);

    body()->addWidget(m_table, 1);
}

void AuditPage::refresh()
{
    load();
}

void AuditPage::reloadTheme()
{
    load();
}

void AuditPage::load()
{
    const int rawLimit = m_limitBox->currentData().toInt();
    const int limit = rawLimit <= 0 ? 500 : rawLimit;
    const QString url = QStringLiteral("/api/audit?limit=%1&offset=0").arg(limit);

    api()->get(url, [this, rawLimit](bool ok, const QJsonObject &data, const QString &message) {
        if (!ok) {
            m_summary->setText(QStringLiteral("读取日志失败：%1").arg(message));
            m_table->clearContents();
            m_table->setRowCount(0);
            showToast(message, QStringLiteral("error"));
            return;
        }
        const int total = int(data.value(QStringLiteral("total")).toDouble());
        const QJsonArray items = data.value(QStringLiteral("items")).toArray();
        QString summary = QStringLiteral("共 %1 条记录，当前显示 %2 条").arg(total).arg(items.size());
        if (rawLimit <= 0 && total > items.size()) {
            // 选了「全部」但服务端单次有上限，提示用备份导出完整日志
            summary += QStringLiteral("；单次最多加载 %1 条，完整日志请用「备份日志」导出")
                           .arg(items.size());
        }
        m_summary->setText(summary);
        fillTable(items);
    });
}

void AuditPage::fillTable(const QJsonArray &items)
{
    const Theme::Palette pal = Theme::current();

    m_table->setUpdatesEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(items.size());

    for (int row = 0; row < items.size(); ++row) {
        const QJsonObject item = items.at(row).toObject();

        m_table->setItem(row, 0,
                         new QTableWidgetItem(Fmt::timestamp(item.value(QStringLiteral("createdAt")))));

        QString username = item.value(QStringLiteral("username")).toString();
        if (username.isEmpty()) {
            username = QStringLiteral("—");
        }
        m_table->setItem(row, 1, new QTableWidgetItem(username));

        auto *actionItem = new QTableWidgetItem(actionLabel(item.value(QStringLiteral("action")).toString()));
        actionItem->setForeground(pal.accent);
        m_table->setItem(row, 2, actionItem);

        const QString detail = item.value(QStringLiteral("detail")).toString();
        auto *detailItem = new QTableWidgetItem(detail);
        detailItem->setToolTip(detail);
        m_table->setItem(row, 3, detailItem);

        m_table->setItem(row, 4, new QTableWidgetItem(item.value(QStringLiteral("ip")).toString()));
    }

    m_table->setUpdatesEnabled(true);
}

void AuditPage::startBackup()
{
    if (m_backingUp) {
        showToast(QStringLiteral("日志备份正在进行中…"), QStringLiteral("warning"));
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath();
    }
    const QString suggested = QDir(dir).filePath(QStringLiteral("fn-audit-%1.csv").arg(stamp));

    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("备份操作日志"), suggested,
        QStringLiteral("CSV 文件 (*.csv);;JSON 文件 (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    m_backupAsJson = path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive);
    if (!m_backupAsJson && !path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".csv");
    }

    m_backupPath = path;
    m_backupItems.clear();
    m_backupTotal = 0;
    m_backingUp = true;
    m_backupButton->setEnabled(false);
    m_backupButton->setText(QStringLiteral("正在备份…"));
    m_summary->setText(QStringLiteral("正在导出全部日志…"));
    fetchBackupPage();
}

void AuditPage::fetchBackupPage()
{
    // 服务端单次最多返回 500 条，这里按 offset 逐页拉取，直到凑满 total。
    const int offset = m_backupItems.size();
    const QString url = QStringLiteral("/api/audit?limit=500&offset=%1").arg(offset);

    api()->get(url, [this](bool ok, const QJsonObject &data, const QString &message) {
        if (!ok) {
            m_backingUp = false;
            m_backupButton->setEnabled(true);
            m_backupButton->setText(QStringLiteral("备份日志"));
            m_summary->setText(QStringLiteral("读取日志失败：%1").arg(message));
            showToast(QStringLiteral("导出失败：%1").arg(message), QStringLiteral("error"));
            return;
        }

        m_backupTotal = int(data.value(QStringLiteral("total")).toDouble());
        const QJsonArray items = data.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : items) {
            m_backupItems.append(value.toObject());
        }
        m_summary->setText(QStringLiteral("正在导出日志 %1 / %2 …")
                               .arg(m_backupItems.size())
                               .arg(m_backupTotal));

        if (!items.isEmpty() && m_backupItems.size() < m_backupTotal) {
            fetchBackupPage();
            return;
        }
        finishBackup();
    });
}

void AuditPage::finishBackup()
{
    QFile file(m_backupPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_backingUp = false;
        m_backupButton->setEnabled(true);
        m_backupButton->setText(QStringLiteral("备份日志"));
        m_summary->setText(QStringLiteral("无法写入文件：%1").arg(QDir::toNativeSeparators(m_backupPath)));
        showToast(QStringLiteral("导出失败：无法写入文件"), QStringLiteral("error"));
        return;
    }

    if (m_backupAsJson) {
        QJsonArray array;
        for (const QJsonObject &entry : m_backupItems) {
            array.append(entry);
        }
        file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    } else {
        QString text = QStringLiteral("时间,操作者,操作,详情,来源 IP\r\n");
        for (const QJsonObject &entry : m_backupItems) {
            const QStringList cells = {
                Fmt::timestamp(entry.value(QStringLiteral("createdAt"))),
                entry.value(QStringLiteral("username")).toString(),
                actionLabel(entry.value(QStringLiteral("action")).toString()),
                entry.value(QStringLiteral("detail")).toString(),
                entry.value(QStringLiteral("ip")).toString()
            };
            QStringList quoted;
            for (const QString &cell : cells) {
                QString value = cell;
                value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
                quoted << QStringLiteral("\"") + value + QStringLiteral("\"");
            }
            text += quoted.join(QStringLiteral(",")) + QStringLiteral("\r\n");
        }
        // UTF-8 BOM：避免 Excel 打开时中文变成乱码。
        file.write(QByteArrayLiteral("\xEF\xBB\xBF"));
        file.write(text.toUtf8());
    }
    file.close();

    const int count = m_backupItems.size();
    m_backingUp = false;
    m_backupButton->setEnabled(true);
    m_backupButton->setText(QStringLiteral("备份日志"));
    m_summary->setText(QStringLiteral("已导出 %1 条日志到 %2")
                           .arg(count)
                           .arg(QDir::toNativeSeparators(m_backupPath)));
    showToast(QStringLiteral("日志已备份（%1 条）").arg(count), QStringLiteral("success"));
}
