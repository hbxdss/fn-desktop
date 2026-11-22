#pragma once

#include <QMap>
#include <QString>
#include <QVector>

/** 一个「应用入口」：应用页里的一个卡片（官方应用 / Docker 应用 / 自定义应用）。 */
struct FnAppEntry
{
    QString name;
    QString url;
};

/** 一个保存的 SSH 会话（MobaXterm 风格的连接书签）。 */
struct SshProfile
{
    QString id;              // 稳定标识（UUID）
    QString name;            // 显示名，例如「NAS 主机」
    QString group;           // 分组名，可空
    QString host;
    int port = 22;
    QString user;
    QString keyPath;         // 私钥路径；空表示用密码认证
    QString passwordCipher;  // SecureVault 密文；空表示不保存密码
};

/** MCP 服务器权限位（用户逐项勾选）。 */
enum McpPermission : uint {
    McpPermNone = 0,
    McpPermFilesRead = 1u << 0,
    McpPermFilesWrite = 1u << 1,
    McpPermDockerRead = 1u << 2,
    McpPermDockerControl = 1u << 3,
    McpPermSystemRead = 1u << 4,
    McpPermSshExec = 1u << 5,
    McpPermAll = McpPermFilesRead | McpPermFilesWrite | McpPermDockerRead
                 | McpPermDockerControl | McpPermSystemRead | McpPermSshExec
};

/**
 * 客户端本地配置。
 * 登录密码与 MCP 令牌**只存密文**（SecureVault：TPM 2.0 优先，DPAPI 回退），
 * 内存态的本次登录密码不经过本类持久化。
 */
class AppConfig
{
public:
    QString host;                                  // NAS 地址（IP 或主机名）
    int apiPort = 59120;                           // 飞牛终端服务端（HTTPS / TLS1.3）
    int adminPort = 5666;                          // 飞牛网页管理后台（默认 5666；开启 HTTPS 后为 5667）
    QString adminPath = QStringLiteral("/");
    QString username;
    QString uiTheme = QStringLiteral("win11");     // win7 | win10 | win11 | glass
    QString colorMode = QStringLiteral("dark");    // light | dark
    QString accent = QStringLiteral("#2f81f7");
    QString lastPage = QStringLiteral("browser");
    QString downloadDir;
    QString sshUser;                               // SSH 登录用户名（不保存密码）
    int sshPort = 22;
    bool rememberUsername = true;

    // —— v2 新增 ——
    bool wizardCompleted = false;                  // 新手向导已完成/跳过
    bool showFnVideo = true;                       // 应用页显示「飞牛影视」
    bool showFnPhoto = true;
    bool showFnMusic = true;
    bool advancedMode = false;                     // 高级模式（Docker/SSH 分组 + 赛博朋克）
    int dashboardRefreshSec = 10;                  // 0 = 手动
    int dockerRefreshSec = 10;
    QString savedPasswordCipher;                   // SecureVault 密文；空=未保存
    bool webAutoLogin = false;                     // 网页后台填充后是否自动提交
    QMap<QString, QString> tlsPins;                // host -> 证书 SHA256 指纹（TOFU 首连固定）

    bool mcpEnabled = false;
    int mcpPort = 0;                               // 0 = 由系统分配端口
    QString mcpTokenCipher;                        // SecureVault 密文
    uint mcpPerms = McpPermNone;

    QVector<FnAppEntry> apps;                      // 用户自己新建的应用（空表示没有自定义应用）

    QVector<SshProfile> sshProfiles;               // SSH 会话书签
    int sshTermFontSize = 13;                      // 终端字号
    bool sshShowSftpSidebar = true;                // 是否显示 SFTP 侧栏

    static QString dataDir();
    static QString configPath();

    void load();
    void save() const;
    void normalize();

    QString adminUrl() const;      // http(s)://host:5666 或 5667
    QString apiUrl() const;        // https://host:59120
    QString displayAddress() const; // host:59120

    /** 管理后台协议：默认端口 5666 是 HTTP，开启 HTTPS 后的 5667 才是 HTTPS */
    QString adminScheme() const;
    QString effectiveDownloadDir() const;

    /** 用网页端口（adminPort）拼接某个路径前缀的完整地址，例如 appUrl("/v")。 */
    QString appUrl(const QString &path) const;

    /** 用户自己新建的应用。 */
    QVector<FnAppEntry> resolvedApps() const;
    /** 内置的飞牛官方应用入口：影视 / 相册 / 音乐（是否已安装由服务端探测）。 */
    QVector<FnAppEntry> builtinApps() const;
};
