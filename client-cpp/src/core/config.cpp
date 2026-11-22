#include "core/config.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QUuid>

#include <utility>

QString AppConfig::dataDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/.fn-desktop-plus");
    }
    QDir().mkpath(base);
    return base;
}

QString AppConfig::configPath()
{
    return dataDir() + QStringLiteral("/config.json");
}

void AppConfig::normalize()
{
    host = host.trimmed();
    if (host.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)) {
        host.remove(0, 7);
    } else if (host.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        host.remove(0, 8);
    }
    while (host.endsWith(QLatin1Char('/'))) {
        host.chop(1);
    }
    // 支持用户在地址栏里直接输入 "192.168.1.2:5667"
    const int colon = host.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && host.count(QLatin1Char(':')) == 1) {
        bool ok = false;
        const int port = host.mid(colon + 1).toInt(&ok);
        if (ok && port > 0 && port <= 65535) {
            host = host.left(colon);
        }
    }

    if (apiPort <= 0 || apiPort > 65535) {
        apiPort = 59120;
    }
    if (adminPort <= 0 || adminPort > 65535) {
        adminPort = 5666;
    }
    if (adminPath.isEmpty() || !adminPath.startsWith(QLatin1Char('/'))) {
        adminPath = QStringLiteral("/");
    }
    if (uiTheme != QLatin1String("win7") && uiTheme != QLatin1String("win10")
        && uiTheme != QLatin1String("win11") && uiTheme != QLatin1String("glass")) {
        uiTheme = QStringLiteral("win11");
    }
    if (colorMode != QLatin1String("light") && colorMode != QLatin1String("dark")) {
        colorMode = QStringLiteral("dark");
    }
    if (!accent.startsWith(QLatin1Char('#')) || accent.size() != 7) {
        accent = QStringLiteral("#2f81f7");
    }
    sshUser = sshUser.trimmed();
    if (sshPort <= 0 || sshPort > 65535) {
        sshPort = 22;
    }
    if (dashboardRefreshSec < 0 || dashboardRefreshSec > 3600) {
        dashboardRefreshSec = 10;
    }
    if (dockerRefreshSec < 0 || dockerRefreshSec > 3600) {
        dockerRefreshSec = 10;
    }
    if (mcpPort < 0 || mcpPort > 65535) {
        mcpPort = 0;
    }
    mcpPerms &= McpPermAll;
}

void AppConfig::load()
{
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly)) {
        normalize();
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        normalize();
        return;
    }
    const QJsonObject obj = doc.object();

    host = obj.value(QStringLiteral("host")).toString(host);
    apiPort = obj.value(QStringLiteral("apiPort")).toInt(obj.value(QStringLiteral("port")).toInt(59120));
    adminPort = obj.value(QStringLiteral("adminPort")).toInt(5666);
    adminPath = obj.value(QStringLiteral("adminPath")).toString(QStringLiteral("/"));
    username = obj.value(QStringLiteral("username")).toString();
    uiTheme = obj.value(QStringLiteral("uiTheme")).toString();
    colorMode = obj.value(QStringLiteral("colorMode")).toString();
    accent = obj.value(QStringLiteral("accent")).toString(accent);
    lastPage = obj.value(QStringLiteral("lastPage")).toString(QStringLiteral("browser"));
    downloadDir = obj.value(QStringLiteral("downloadDir")).toString();
    sshUser = obj.value(QStringLiteral("sshUser")).toString();
    sshPort = obj.value(QStringLiteral("sshPort")).toInt(22);
    rememberUsername = obj.value(QStringLiteral("rememberUsername")).toBool(true);

    wizardCompleted = obj.value(QStringLiteral("wizardCompleted")).toBool(false);
    showFnVideo = obj.value(QStringLiteral("showFnVideo")).toBool(true);
    showFnPhoto = obj.value(QStringLiteral("showFnPhoto")).toBool(true);
    showFnMusic = obj.value(QStringLiteral("showFnMusic")).toBool(true);
    advancedMode = obj.value(QStringLiteral("advancedMode")).toBool(false);
    dashboardRefreshSec = obj.value(QStringLiteral("dashboardRefreshSec")).toInt(10);
    dockerRefreshSec = obj.value(QStringLiteral("dockerRefreshSec")).toInt(10);
    savedPasswordCipher = obj.value(QStringLiteral("savedPasswordCipher")).toString();
    webAutoLogin = obj.value(QStringLiteral("webAutoLogin")).toBool(false);
    tlsPins.clear();
    const QJsonObject pins = obj.value(QStringLiteral("tlsPins")).toObject();
    for (auto it = pins.begin(); it != pins.end(); ++it) {
        tlsPins.insert(it.key(), it.value().toString());
    }
    mcpEnabled = obj.value(QStringLiteral("mcpEnabled")).toBool(false);
    mcpPort = obj.value(QStringLiteral("mcpPort")).toInt(0);
    mcpTokenCipher = obj.value(QStringLiteral("mcpTokenCipher")).toString();
    mcpPerms = uint(obj.value(QStringLiteral("mcpPerms")).toDouble(0)) & McpPermAll;

    apps.clear();
    const QJsonArray appArray = obj.value(QStringLiteral("apps")).toArray();
    for (const QJsonValue &value : appArray) {
        const QJsonObject item = value.toObject();
        const QString name = item.value(QStringLiteral("name")).toString().trimmed();
        QString url = item.value(QStringLiteral("url")).toString().trimmed();
        if (name.isEmpty() || url.isEmpty()) {
            continue;
        }
        if (!url.contains(QStringLiteral("://"))) {
            url.prepend(QStringLiteral("https://"));
        }
        apps.append(FnAppEntry{name, url});
    }

    sshProfiles.clear();
    const QJsonArray profileArray = obj.value(QStringLiteral("sshProfiles")).toArray();
    for (const QJsonValue &value : profileArray) {
        const QJsonObject item = value.toObject();
        const QString profileHost = item.value(QStringLiteral("host")).toString().trimmed();
        if (profileHost.isEmpty()) {
            continue;
        }
        SshProfile profile;
        profile.id = item.value(QStringLiteral("id")).toString();
        if (profile.id.isEmpty()) {
            profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        profile.name = item.value(QStringLiteral("name")).toString();
        profile.group = item.value(QStringLiteral("group")).toString();
        profile.host = profileHost;
        profile.port = item.value(QStringLiteral("port")).toInt(22);
        profile.user = item.value(QStringLiteral("user")).toString();
        profile.keyPath = item.value(QStringLiteral("keyPath")).toString();
        profile.passwordCipher = item.value(QStringLiteral("passwordCipher")).toString();
        sshProfiles.append(profile);
    }
    sshTermFontSize = qBound(8, obj.value(QStringLiteral("sshTermFontSize")).toInt(13), 32);
    sshShowSftpSidebar = obj.value(QStringLiteral("sshShowSftpSidebar")).toBool(true);

    // 迁移旧配置：老版本内置的「飞牛网页管理后台（/）」以及文档 / 下载 / Docker /    // 文本编辑器 / 应用中心入口都不再出现在应用页，这里统一裁掉。
    bool appsChanged = false;
    {
        static const QStringList kRemovedPaths = {
            QStringLiteral("/"),
            QStringLiteral("/docs"),
            QStringLiteral("/apps/download"),
            QStringLiteral("/apps/docker"),
            QStringLiteral("/app/text-editor"),
            QStringLiteral("/app-center")
        };
        static const QHash<QString, QString> kRenamedPaths = {
            {QStringLiteral("/v"), QStringLiteral("飞牛影视")},
            {QStringLiteral("/p"), QStringLiteral("飞牛相册")},
            {QStringLiteral("/music"), QStringLiteral("飞牛音乐")}
        };
        QVector<FnAppEntry> filtered;
        filtered.reserve(apps.size());
        for (const FnAppEntry &entry : std::as_const(apps)) {
            const QUrl url(entry.url);
            const QString path = url.path();
            if (kRemovedPaths.contains(path)) {
                appsChanged = true;
                continue;
            }
            FnAppEntry migrated = entry;
            const auto it = kRenamedPaths.constFind(path);
            if (it != kRenamedPaths.constEnd()) {
                // 只重命名内置的默认入口（旧名称），用户自己填写的名称保持不动。
                if (migrated.name == QStringLiteral("飞牛桌面")
                    || migrated.name == QStringLiteral("影视")
                    || migrated.name == QStringLiteral("相册")
                    || migrated.name == QStringLiteral("音乐")) {
                    migrated.name = it.value();
                    appsChanged = true;
                }
            }
            filtered.append(migrated);
        }
        apps = filtered;
    }

    // 兼容旧版本配置（theme 字段只有 dark / light）
    const QString legacyTheme = obj.value(QStringLiteral("theme")).toString();
    if (uiTheme.isEmpty()) {
        uiTheme = QStringLiteral("win11");
        if (legacyTheme == QLatin1String("light")) {
            colorMode = QStringLiteral("light");
        }
    }
    if (colorMode.isEmpty()) {
        colorMode = (legacyTheme == QLatin1String("light")) ? QStringLiteral("light") : QStringLiteral("dark");
    }
    if (!rememberUsername) {
        username.clear();
    }
    normalize();
    if (appsChanged) {
        // 迁移结果立即落盘，避免每次启动都重复裁剪。
        save();
    }
}

void AppConfig::save() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("host"), host);
    obj.insert(QStringLiteral("apiPort"), apiPort);
    obj.insert(QStringLiteral("adminPort"), adminPort);
    obj.insert(QStringLiteral("adminPath"), adminPath);
    obj.insert(QStringLiteral("username"), rememberUsername ? username : QString());
    obj.insert(QStringLiteral("rememberUsername"), rememberUsername);
    obj.insert(QStringLiteral("uiTheme"), uiTheme);
    obj.insert(QStringLiteral("colorMode"), colorMode);
    obj.insert(QStringLiteral("accent"), accent);
    obj.insert(QStringLiteral("lastPage"), lastPage);
    obj.insert(QStringLiteral("downloadDir"), downloadDir);
    obj.insert(QStringLiteral("sshUser"), sshUser);
    obj.insert(QStringLiteral("sshPort"), sshPort);
    obj.insert(QStringLiteral("sshTermFontSize"), sshTermFontSize);
    obj.insert(QStringLiteral("sshShowSftpSidebar"), sshShowSftpSidebar);

    QJsonArray profileArray;
    for (const SshProfile &profile : sshProfiles) {
        QJsonObject item;
        item.insert(QStringLiteral("id"), profile.id);
        item.insert(QStringLiteral("name"), profile.name);
        item.insert(QStringLiteral("group"), profile.group);
        item.insert(QStringLiteral("host"), profile.host);
        item.insert(QStringLiteral("port"), profile.port);
        item.insert(QStringLiteral("user"), profile.user);
        item.insert(QStringLiteral("keyPath"), profile.keyPath);
        item.insert(QStringLiteral("passwordCipher"), profile.passwordCipher);
        profileArray.append(item);
    }
    obj.insert(QStringLiteral("sshProfiles"), profileArray);

    obj.insert(QStringLiteral("wizardCompleted"), wizardCompleted);
    obj.insert(QStringLiteral("showFnVideo"), showFnVideo);
    obj.insert(QStringLiteral("showFnPhoto"), showFnPhoto);
    obj.insert(QStringLiteral("showFnMusic"), showFnMusic);
    obj.insert(QStringLiteral("advancedMode"), advancedMode);
    obj.insert(QStringLiteral("dashboardRefreshSec"), dashboardRefreshSec);
    obj.insert(QStringLiteral("dockerRefreshSec"), dockerRefreshSec);
    obj.insert(QStringLiteral("savedPasswordCipher"), savedPasswordCipher);
    obj.insert(QStringLiteral("webAutoLogin"), webAutoLogin);
    QJsonObject pins;
    for (auto it = tlsPins.begin(); it != tlsPins.end(); ++it) {
        pins.insert(it.key(), it.value());
    }
    obj.insert(QStringLiteral("tlsPins"), pins);
    obj.insert(QStringLiteral("mcpEnabled"), mcpEnabled);
    obj.insert(QStringLiteral("mcpPort"), mcpPort);
    obj.insert(QStringLiteral("mcpTokenCipher"), mcpTokenCipher);
    obj.insert(QStringLiteral("mcpPerms"), double(mcpPerms));

    QJsonArray appArray;
    for (const FnAppEntry &entry : apps) {
        QJsonObject item;
        item.insert(QStringLiteral("name"), entry.name);
        item.insert(QStringLiteral("url"), entry.url);
        appArray.append(item);
    }
    obj.insert(QStringLiteral("apps"), appArray);

    QSaveFile file(configPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.commit();
}

QString AppConfig::adminScheme() const
{
    // 飞牛官方默认入口是 http://NAS:5666；5667 是开启 HTTPS 后的端口
    return adminPort == 5667 ? QStringLiteral("https") : QStringLiteral("http");
}

QString AppConfig::adminUrl() const
{
    const QString h = host.isEmpty() ? QStringLiteral("127.0.0.1") : host;
    return QStringLiteral("%1://%2:%3%4").arg(adminScheme(), h).arg(adminPort).arg(adminPath);
}

QString AppConfig::apiUrl() const
{
    const QString h = host.isEmpty() ? QStringLiteral("127.0.0.1") : host;
    return QStringLiteral("https://%1:%2").arg(h).arg(apiPort);
}

QString AppConfig::displayAddress() const
{
    const QString h = host.isEmpty() ? QStringLiteral("未设置地址") : host;
    return QStringLiteral("%1:%2").arg(h).arg(apiPort);
}

QString AppConfig::appUrl(const QString &path) const
{
    const QString h = host.isEmpty() ? QStringLiteral("127.0.0.1") : host;
    QString p = path.trimmed();
    if (p.isEmpty()) {
        p = QStringLiteral("/");
    }
    if (!p.startsWith(QLatin1Char('/'))) {
        p.prepend(QLatin1Char('/'));
    }
    return QStringLiteral("%1://%2:%3%4").arg(adminScheme(), h).arg(adminPort).arg(p);
}

QVector<FnAppEntry> AppConfig::builtinApps() const
{
    // 飞牛官方的影视 / 相册 / 音乐都跑在同一个 Web 端口上，靠路径前缀区分（NAS 上实测）。
    // 飞牛网页管理后台（/）不再作为应用入口，是否安装由服务端 /api/apps 探测。
    QVector<FnAppEntry> list;
    list.append(FnAppEntry{QStringLiteral("飞牛影视"), appUrl(QStringLiteral("/v"))});
    list.append(FnAppEntry{QStringLiteral("飞牛相册"), appUrl(QStringLiteral("/p"))});
    list.append(FnAppEntry{QStringLiteral("飞牛音乐"), appUrl(QStringLiteral("/music"))});
    return list;
}

QVector<FnAppEntry> AppConfig::resolvedApps() const
{
    // 只返回用户自己新建的应用；官方 / Docker 应用由服务端发现后动态合并。
    return apps;
}

QString AppConfig::effectiveDownloadDir() const
{
    if (!downloadDir.isEmpty()) {
        return downloadDir;
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty()) {
        base = QDir::homePath();
    }
    return base + QStringLiteral("/飞牛终端");
}
