#include "ui/pages/dashboard_page.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include "core/api_client.h"
#include "core/config.h"

#include <QGridLayout>
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

namespace {

struct InfoField {
    const char *key;
    const char *label;
};

const InfoField kFields[] = {
    {"hostname", "主机名"},
    {"platform", "操作系统"},
    {"arch", "系统架构"},
    {"cpuModel", "处理器"},
    {"cpuCores", "核心数"},
    {"memTotal", "物理内存"},
    {"nodeVersion", "Node 版本"},
    {"serverVersion", "服务版本"},
    {"dataDir", "数据目录"},
    {"filesRoot", "文件根目录"}
};

QString valueOf(const QJsonObject &data, const QString &key)
{
    const QJsonValue value = data.value(key);
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 12);
    }
    if (value.isString()) {
        return value.toString();
    }
    return QString();
}

} // namespace

DashboardPage::DashboardPage(ApiClient *api, AppConfig *config, QWidget *parent)
    : BasePage(api, parent), m_config(config)
{
    setPageTitle(QStringLiteral("仪表盘"));
    setPageSubtitle(QStringLiteral("NAS 运行状态一览，每 5 秒自动刷新"));

    auto *refreshButton = makeButton(QStringLiteral("立即刷新"), QStringLiteral("ghost"), this);
    refreshButton->setIcon(Icons::icon(Icons::Kind::Refresh, Theme::current().text, 16));
    connect(refreshButton, &QPushButton::clicked, this, [this, refreshButton]() {
        refreshButton->setIcon(Icons::icon(Icons::Kind::Refresh, Theme::current().text, 16));
        load();
    });
    addHeaderAction(refreshButton);

    m_intervalBox = new QComboBox(this);
    m_intervalBox->setObjectName(QStringLiteral("RefreshInterval"));
    m_intervalBox->setToolTip(QStringLiteral("自动刷新间隔"));
    const QVector<QPair<QString, int>> intervals = {
        {QStringLiteral("每 5 秒"), 5},
        {QStringLiteral("每 10 秒"), 10},
        {QStringLiteral("每 30 秒"), 30},
        {QStringLiteral("每 1 分钟"), 60},
        {QStringLiteral("手动刷新"), 0},
    };
    for (const auto &item : intervals) {
        m_intervalBox->addItem(item.first, item.second);
    }
    connect(m_intervalBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        const int seconds = m_intervalBox->currentData().toInt();
        if (m_config) {
            m_config->dashboardRefreshSec = seconds;
            m_config->save();
        }
        applyRefreshInterval(seconds);
        load();
    });
    addHeaderAction(m_intervalBox);

    m_warned = false;

    buildUi();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &DashboardPage::load);
    const int initial = m_config ? m_config->dashboardRefreshSec : 5;
    const int initialIndex = m_intervalBox->findData(initial);
    if (initialIndex >= 0) {
        m_intervalBox->setCurrentIndex(initialIndex);
    }
    applyRefreshInterval(m_intervalBox->currentData().toInt());
}

void DashboardPage::applyRefreshInterval(int seconds)
{
    if (seconds <= 0) {
        m_timer->stop();
        setPageSubtitle(QStringLiteral("NAS 运行状态一览 · 自动刷新已暂停"));
        return;
    }
    m_timer->start(seconds * 1000);
    setPageSubtitle(QStringLiteral("NAS 运行状态一览，每 %1 秒自动刷新").arg(seconds));
}

void DashboardPage::buildUi()
{
    auto *stats = new QHBoxLayout();
    stats->setContentsMargins(0, 0, 0, 0);
    stats->setSpacing(14);

    m_cpuCard = new StatCard(QStringLiteral("CPU 使用率"), this);
    m_memCard = new StatCard(QStringLiteral("内存占用率"), this);
    m_uptimeCard = new StatCard(QStringLiteral("系统运行时长"), this);
    m_dockerCard = new StatCard(QStringLiteral("运行中的容器"), this);
    m_appsCard = new StatCard(QStringLiteral("应用数量"), this);

    m_cpuCard->setHint(QStringLiteral("等待数据…"));
    m_memCard->setHint(QStringLiteral("等待数据…"));
    m_uptimeCard->setHint(QStringLiteral("—"));
    m_dockerCard->setHint(QStringLiteral("—"));
    m_appsCard->setHint(QStringLiteral("等待检测…"));

    stats->addWidget(m_cpuCard, 1);
    stats->addWidget(m_memCard, 1);
    stats->addWidget(m_uptimeCard, 1);
    stats->addWidget(m_dockerCard, 1);
    stats->addWidget(m_appsCard, 1);
    body()->addLayout(stats);

    auto *columns = new QHBoxLayout();
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(14);

    // 左：系统信息
    auto *infoCard = new Card(QStringLiteral("系统信息"), this);
    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(10);

    int index = 0;
    for (const InfoField &field : kFields) {
        auto *name = makeLabel(QString::fromUtf8(field.label), QStringLiteral("muted"), infoCard);
        auto *value = makeLabel(QStringLiteral("—"), QString(), infoCard);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        const int row = index / 2;
        const int base = (index % 2) * 2;
        grid->addWidget(name, row, base);
        grid->addWidget(value, row, base + 1);
        m_infoValues.insert(QString::fromLatin1(field.key), value);
        ++index;
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    infoCard->body()->addLayout(grid);

    // 资源条
    auto *cpuRow = new QHBoxLayout();
    cpuRow->setContentsMargins(0, 0, 0, 0);
    cpuRow->setSpacing(10);
    cpuRow->addWidget(makeLabel(QStringLiteral("CPU"), QStringLiteral("muted"), infoCard));
    m_cpuBar = new QProgressBar(infoCard);
    m_cpuBar->setRange(0, 100);
    m_cpuBar->setTextVisible(false);
    m_cpuBar->setFixedHeight(8);
    cpuRow->addWidget(m_cpuBar, 1);
    m_cpuText = makeLabel(QStringLiteral("—"), QStringLiteral("mono"), infoCard);
    m_cpuText->setMinimumWidth(56);
    cpuRow->addWidget(m_cpuText);
    infoCard->body()->addLayout(cpuRow);

    auto *memRow = new QHBoxLayout();
    memRow->setContentsMargins(0, 0, 0, 0);
    memRow->setSpacing(10);
    memRow->addWidget(makeLabel(QStringLiteral("内存"), QStringLiteral("muted"), infoCard));
    m_memBar = new QProgressBar(infoCard);
    m_memBar->setRange(0, 100);
    m_memBar->setTextVisible(false);
    m_memBar->setFixedHeight(8);
    memRow->addWidget(m_memBar, 1);
    m_memText = makeLabel(QStringLiteral("—"), QStringLiteral("mono"), infoCard);
    m_memText->setMinimumWidth(56);
    memRow->addWidget(m_memText);
    infoCard->body()->addLayout(memRow);
    // 资源条紧跟在系统信息表格下方，多余空间留到卡片底部
    infoCard->body()->addStretch(1);

    // 右：存储
    auto *storageCard = new Card(QStringLiteral("存储空间"), this);
    m_storageSummary = makeLabel(QStringLiteral("正在读取存储信息…"), QStringLiteral("muted"), storageCard);
    storageCard->body()->addWidget(m_storageSummary);
    m_storageBox = new QVBoxLayout();
    m_storageBox->setContentsMargins(0, 0, 0, 0);
    m_storageBox->setSpacing(12);
    storageCard->body()->addLayout(m_storageBox);
    storageCard->body()->addStretch(1);

    columns->addWidget(infoCard, 3);
    columns->addWidget(storageCard, 2);
    body()->addLayout(columns, 1);

    m_updated = makeLabel(QString(), QStringLiteral("muted"), this);
    body()->addWidget(m_updated);
}

void DashboardPage::refresh()
{
    load();
}

void DashboardPage::reloadTheme()
{
    const Theme::Palette pal = Theme::current();
    if (!m_updated) {
        return;
    }
    Q_UNUSED(pal)
}

void DashboardPage::load()
{
    api()->get(QStringLiteral("/api/system/info"),
               [this](bool ok, const QJsonObject &data, const QString &message) {
                   if (!ok) {
                       if (!m_warned) {
                           m_warned = true;
                           showToast(message, QStringLiteral("error"));
                       }
                       return;
                   }
                   m_warned = false;
                   renderInfo(data);
                   m_updated->setText(QStringLiteral("最后更新 %1")
                                          .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
               });

    api()->get(QStringLiteral("/api/system/storage"),
               [this](bool ok, const QJsonObject &data, const QString &) {
                   if (ok) {
                       renderStorage(data);
                   }
               });

    api()->get(QStringLiteral("/api/docker/containers"),
               [this](bool ok, const QJsonObject &data, const QString &) {
                   if (ok) {
                       renderDocker(data);
                   }
               });

    api()->get(QStringLiteral("/api/apps"),
               [this](bool ok, const QJsonObject &data, const QString &) {
                   if (ok) {
                       renderApps(data);
                   }
               });
}

void DashboardPage::renderInfo(const QJsonObject &data)
{
    for (auto it = m_infoValues.constBegin(); it != m_infoValues.constEnd(); ++it) {
        QString text = valueOf(data, it.key());
        if (it.key() == QLatin1String("platform")) {
            const QString release = valueOf(data, QStringLiteral("release"));
            if (!release.isEmpty()) {
                text = text.isEmpty() ? release : QStringLiteral("%1 %2").arg(text, release);
            }
        } else if (it.key() == QLatin1String("memTotal")) {
            text = Fmt::bytes(qint64(data.value(QStringLiteral("memTotal")).toDouble()));
        } else if (it.key() == QLatin1String("cpuCores")) {
            const int cores = int(data.value(QStringLiteral("cpuCores")).toDouble());
            text = QStringLiteral("%1 核").arg(cores);
        }
        it.value()->setText(text.isEmpty() ? QStringLiteral("—") : text);
    }

    const double cpu = data.value(QStringLiteral("cpuUsage")).toDouble();
    const double mem = data.value(QStringLiteral("memUsage")).toDouble();
    m_cpuBar->setValue(int(qBound(0.0, cpu, 100.0)));
    m_memBar->setValue(int(qBound(0.0, mem, 100.0)));
    m_cpuText->setText(Fmt::percent(cpu));
    m_memText->setText(Fmt::percent(mem));

    m_cpuCard->setValue(Fmt::percent(cpu));
    m_cpuCard->setHint(QStringLiteral("%1 核 · %2")
                           .arg(int(data.value(QStringLiteral("cpuCores")).toDouble()))
                           .arg(valueOf(data, QStringLiteral("cpuModel"))));

    const qint64 memUsed = qint64(data.value(QStringLiteral("memUsed")).toDouble());
    const qint64 memTotal = qint64(data.value(QStringLiteral("memTotal")).toDouble());
    m_memCard->setValue(Fmt::percent(mem));
    m_memCard->setHint(QStringLiteral("%1 / %2").arg(Fmt::bytes(memUsed), Fmt::bytes(memTotal)));

    const qint64 uptime = qint64(data.value(QStringLiteral("uptime")).toDouble());
    m_uptimeCard->setValue(Fmt::duration(uptime));
    m_uptimeCard->setHint(QStringLiteral("服务已运行 %1")
                              .arg(Fmt::duration(qint64(data.value(QStringLiteral("processUptime")).toDouble()))));
}

void DashboardPage::renderDocker(const QJsonObject &data)
{
    const int running = int(data.value(QStringLiteral("running")).toDouble());
    const int total = int(data.value(QStringLiteral("total")).toDouble());
    m_dockerCard->setValue(QString::number(running));
    m_dockerCard->setHint(total > 0 ? QStringLiteral("共 %1 个容器").arg(total)
                                    : QStringLiteral("没有运行中的容器"));
}

void DashboardPage::clearStorageRows()
{
    while (QLayoutItem *item = m_storageBox->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void DashboardPage::renderApps(const QJsonObject &data)
{
    const QJsonArray official = data.value(QStringLiteral("official")).toArray();
    const QJsonArray docker = data.value(QStringLiteral("docker")).toArray();

    int installed = 0;
    for (const QJsonValue &value : official) {
        if (value.toObject().value(QStringLiteral("installed")).toBool()) {
            ++installed;
        }
    }
    const int custom = m_config ? int(m_config->resolvedApps().size()) : 0;
    const int total = installed + int(docker.size()) + custom;

    m_appsCard->setValue(QString::number(total));
    m_appsCard->setHint(QStringLiteral("官方 %1 · Docker %2 · 自定义 %3")
                            .arg(installed)
                            .arg(docker.size())
                            .arg(custom));
}

void DashboardPage::renderStorage(const QJsonObject &data)
{
    const QJsonArray items = data.value(QStringLiteral("items")).toArray();
    const qint64 total = qint64(data.value(QStringLiteral("total")).toDouble());
    const qint64 used = qint64(data.value(QStringLiteral("used")).toDouble());
    const double usage = data.value(QStringLiteral("usagePercent")).toDouble();

    clearStorageRows();

    if (items.isEmpty()) {
        m_storageSummary->setText(QStringLiteral("没有找到飞牛存储卷"));
        return;
    }

    m_storageSummary->setText(
        QStringLiteral("%1 个存储卷 · 已用 %2 / %3（%4）")
            .arg(items.size())
            .arg(Fmt::bytes(used), Fmt::bytes(total), Fmt::percent(usage)));

    QWidget *host = m_storageBox->parentWidget();

    for (const QJsonValue &value : items) {
        const QJsonObject volume = value.toObject();
        const QString name = volume.value(QStringLiteral("name")).toString();
        const qint64 volTotal = qint64(volume.value(QStringLiteral("total")).toDouble());
        const qint64 volUsed = qint64(volume.value(QStringLiteral("used")).toDouble());
        const double volUsage = volume.value(QStringLiteral("usagePercent")).toDouble();

        auto *row = new QWidget(host);
        auto *lay = new QVBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(6);

        auto *head = new QHBoxLayout();
        head->setContentsMargins(0, 0, 0, 0);
        head->setSpacing(8);
        head->addWidget(makeLabel(name.isEmpty() ? QStringLiteral("存储空间") : name,
                                  QString(), row));
        head->addStretch(1);
        head->addWidget(makeLabel(Fmt::percent(volUsage), QStringLiteral("mono"), row));
        lay->addLayout(head);

        auto *bar = new QProgressBar(row);
        bar->setRange(0, 100);
        bar->setValue(int(qBound(0.0, volUsage, 100.0)));
        bar->setTextVisible(false);
        bar->setFixedHeight(8);
        lay->addWidget(bar);

        lay->addWidget(makeLabel(QStringLiteral("已用 %1 / 共 %2")
                                     .arg(Fmt::bytes(volUsed), Fmt::bytes(volTotal)),
                                 QStringLiteral("muted"), row));

        m_storageBox->addWidget(row);
    }
}
