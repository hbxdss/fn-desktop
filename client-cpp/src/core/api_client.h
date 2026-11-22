#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QUrl>

#include <functional>

class QNetworkReply;
class AppConfig;

/**
 * 飞牛终端服务端（59120 / HTTPS / TLS1.3）的 REST 客户端。
 * 自签证书场景下仅信任当前配置的服务器主机，其他主机的证书错误一律拒绝。
 */
class ApiClient : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(bool ok, const QJsonObject &data, const QString &message)>;
    using FileCallback = std::function<void(bool ok, const QString &message)>;
    using ProgressCallback = std::function<void(qint64 sent, qint64 total)>;

    explicit ApiClient(QObject *parent = nullptr);

    void setBaseUrl(const QUrl &url);
    QUrl baseUrl() const { return m_base; }
    /** 注入全局配置，用于 TOFU 证书指纹的读取与持久化。 */
    void setConfig(AppConfig *config) { m_config = config; }

    void setToken(const QString &token) { m_token = token; }
    QString token() const { return m_token; }
    bool isLoggedIn() const { return !m_token.isEmpty(); }
    void clearToken() { m_token.clear(); }

    void get(const QString &path, Callback cb);
    void post(const QString &path, const QJsonObject &body, Callback cb);
    void put(const QString &path, const QJsonObject &body, Callback cb);
    void del(const QString &path, Callback cb);

    /** 长耗时请求（如拉取镜像、清理镜像），timeoutMs <= 0 表示仅受空闲超时限制。 */
    void getSlow(const QString &path, int timeoutMs, Callback cb);
    void postSlow(const QString &path, const QJsonObject &body, int timeoutMs, Callback cb);

    void login(const QString &username, const QString &password, Callback cb);
    void logout(Callback cb = Callback());
    void ping(Callback cb);

    /** 查询服务端是否已初始化（是否已存在超级管理员）。 */
    void setupStatus(Callback cb);
    /** 全新服务端首次初始化：创建超级管理员并直接返回登录令牌。 */
    void setupInit(const QString &username, const QString &password, Callback cb);

    /** 自签证书指纹与首次记录不一致（疑似重装/续期或中间人）。 */
    bool hasCertificateChange() const { return !m_certChangedHost.isEmpty(); }
    QString certChangedHost() const { return m_certChangedHost; }
    /** 用户确认信任新证书：写入新指纹并清除变化标记。 */
    void trustChangedCertificate();
    void clearCertificateChange();

    /** 下载 /api/files/download 到本地文件（覆盖写）。 */
    void download(const QString &remotePath, const QString &localFile, FileCallback cb);

    /** 打包下载整个文件夹（服务端生成 zip 后流式返回）到本地文件。 */
    void downloadFolder(const QString &remotePath, const QString &localFile, FileCallback cb);

    /**
     * 分块上传本地文件到服务器目录（与 web 后台一致的三步协议）：
     *   POST /api/files/upload?path=&name=&offset=（body 为原始分块）
     *   POST /api/files/upload/finish  {path,name,size}
     */
    void uploadFile(const QString &localFile,
                    const QString &remoteDir,
                    const QString &remoteName,
                    ProgressCallback progress,
                    FileCallback cb);

signals:
    void unauthorized();

private:
    QNetworkReply *send(const QByteArray &verb, const QString &path, const QJsonObject *body, int timeoutMs);
    QNetworkReply *sendRaw(const QString &path, const QByteArray &payload, int timeoutMs);
    void handleJson(QNetworkReply *reply, Callback cb);
    QNetworkRequest makeRequest(const QString &path) const;
    void attachCertificateHandler(QNetworkReply *reply);
    /** download / downloadFolder 的公共实现：把响应流写入本地文件。 */
    void downloadFrom(const QString &path, const QString &localFile, FileCallback cb);

    QNetworkAccessManager m_nam;
    QUrl m_base;
    QString m_token;
    AppConfig *m_config = nullptr;
    QString m_certChangedHost;  // 指纹变化但用户尚未确认的主机
    QString m_certChangedPin;   // 新证书的 SHA-256 指纹（hex）
};
