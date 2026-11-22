#include "core/api_client.h"

#include "core/config.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QSaveFile>
#include <QSslConfiguration>
#include <QSslError>
#include <QTimer>

#include <memory>

namespace {

QString networkErrorMessage(QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401) {
        return QStringLiteral("登录状态已失效，请重新登录");
    }
    if (status == 403) {
        return QStringLiteral("没有权限执行该操作");
    }
    if (status >= 500) {
        return QStringLiteral("服务器内部错误（HTTP %1）").arg(status);
    }

    switch (reply->error()) {
    case QNetworkReply::HostNotFoundError:
        return QStringLiteral("找不到服务器，请检查 NAS 地址");
    case QNetworkReply::ConnectionRefusedError:
        return QStringLiteral("连接被拒绝，请确认服务端已启动");
    case QNetworkReply::TimeoutError:
        return QStringLiteral("请求超时，请检查网络连接");
    case QNetworkReply::SslHandshakeFailedError:
        return QStringLiteral("TLS 握手失败（服务端需启用 TLS 1.3）");
    case QNetworkReply::OperationCanceledError:
        return QStringLiteral("请求已取消或超时");
    case QNetworkReply::RemoteHostClosedError:
        return QStringLiteral("服务器提前关闭了连接");
    default:
        break;
    }
    if (status > 0) {
        return QStringLiteral("请求失败（HTTP %1）").arg(status);
    }
    return QStringLiteral("网络请求失败：%1").arg(reply->errorString());
}

QString messageFromPayload(const QByteArray &payload, const QString &fallback)
{
    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject obj = doc.object();
        // 服务端错误体形如 {ok:false,error:"…",code:"…"}，成功体用 message 字段，
        // 两个键都检查一次，避免把具体错误原因丢掉（例如目录无读取权限）。
        const QString message = obj.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            return message;
        }
        const QString error = obj.value(QStringLiteral("error")).toString();
        if (!error.isEmpty()) {
            return error;
        }
    }
    return fallback;
}

} // namespace

ApiClient::ApiClient(QObject *parent)
    : QObject(parent)
{
    m_nam.setTransferTimeout(20000);
}

void ApiClient::setBaseUrl(const QUrl &url)
{
    m_base = url;
    if (m_base.scheme().isEmpty()) {
        m_base.setScheme(QStringLiteral("https"));
    }
    while (m_base.path().endsWith(QLatin1Char('/'))) {
        QString path = m_base.path();
        path.chop(1);
        m_base.setPath(path);
    }
}

QNetworkRequest ApiClient::makeRequest(const QString &path) const
{
    QUrl url = m_base;
    if (path.startsWith(QLatin1Char('/'))) {
        // 分离路径与查询串：QUrl::setPath() 会把 '?' 转义成 %3F，导致查询参数全部失效。
        const int queryAt = path.indexOf(QLatin1Char('?'));
        if (queryAt >= 0) {
            url.setPath(path.left(queryAt));
            url.setQuery(path.mid(queryAt + 1), QUrl::StrictMode);
        } else {
            url.setPath(path);
        }
    } else {
        url = url.resolved(QUrl(path));
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("FnDesktopPlus/1.2 (Windows; Qt)"));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!m_token.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    }
    return request;
}

void ApiClient::attachCertificateHandler(QNetworkReply *reply)
{
    connect(reply, &QNetworkReply::sslErrors, reply, [this, reply](const QList<QSslError> &errors) {
        const QString host = reply->url().host().toLower();
        if (!m_config || m_base.host().isEmpty() || host.compare(m_base.host(), Qt::CaseInsensitive) != 0) {
            return; // 不是我们配置的 NAS，保留错误
        }
        // 自签证书：仅对配置的主机放行；首次连接（TOFU）记录证书指纹并长期固定，
        // 之后若指纹变化（疑似中间人）则拒绝连接，要求用户确认后才能更新。
        const QString fingerprint = QString::fromLatin1(
            QCryptographicHash::hash(reply->sslConfiguration().peerCertificate().toDer(),
                                     QCryptographicHash::Sha256).toHex());
        const QString known = m_config->tlsPins.value(host);
        if (known.isEmpty()) {
            m_config->tlsPins.insert(host, fingerprint);
            m_config->save();
            for (const QSslError &error : errors) {
                qWarning("[TLS] 首次连接，已记录 %s 的证书指纹：%s",
                         qPrintable(host), qPrintable(error.errorString()));
            }
            reply->ignoreSslErrors();
            return;
        }
        if (known == fingerprint) {
            // 证书仍是首次记录的那张（自签续期前不变），放行
            reply->ignoreSslErrors();
            return;
        }
        // 指纹变化：不让本次握手通过，但记录现场，交给界面询问用户是否信任新证书。
        m_certChangedHost = host;
        m_certChangedPin = fingerprint;
        qWarning("[TLS] %s 的证书指纹已变化（原 %s…，新 %s…），等待用户确认。",
                 qPrintable(host), qPrintable(known.left(16)), qPrintable(fingerprint.left(16)));
    });
}

void ApiClient::clearCertificateChange()
{
    m_certChangedHost.clear();
    m_certChangedPin.clear();
}

void ApiClient::trustChangedCertificate()
{
    if (m_certChangedHost.isEmpty() || !m_config) {
        clearCertificateChange();
        return;
    }
    m_config->tlsPins.insert(m_certChangedHost, m_certChangedPin);
    m_config->save();
    qInfo("[TLS] 用户已确认信任 %s 的新证书指纹 %s…",
          qPrintable(m_certChangedHost), qPrintable(m_certChangedPin.left(16)));
    clearCertificateChange();
}

QNetworkReply *ApiClient::send(const QByteArray &verb, const QString &path, const QJsonObject *body, int timeoutMs)
{
    QNetworkRequest request = makeRequest(path);
    QByteArray payload;
    if (body) {
        payload = QJsonDocument(*body).toJson(QJsonDocument::Compact);
        // JSON 请求体必须显式声明 Content-Type，否则部分严格的服务端/代理会按
        // text/plain 处理导致解析失败。
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    }

    QNetworkReply *reply = nullptr;
    if (verb == "GET") {
        reply = m_nam.get(request);
    } else if (verb == "POST") {
        reply = m_nam.post(request, payload);
    } else if (verb == "PUT") {
        reply = m_nam.put(request, payload);
    } else if (verb == "DELETE") {
        reply = m_nam.sendCustomRequest(request, "DELETE", payload);
    } else {
        reply = m_nam.get(request);
    }

    attachCertificateHandler(reply);

    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, reply, [reply]() {
            if (reply->isRunning()) {
                reply->abort();
            }
        });
    }
    return reply;
}

QNetworkReply *ApiClient::sendRaw(const QString &path, const QByteArray &payload, int timeoutMs)
{
    QNetworkRequest request = makeRequest(path);
    // 分块上传的是原始字节流，明确 Content-Type 避免被当作默认表单处理。
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    QNetworkReply *reply = m_nam.post(request, payload);
    attachCertificateHandler(reply);
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, reply, [reply]() {
            if (reply->isRunning()) {
                reply->abort();
            }
        });
    }
    return reply;
}

void ApiClient::handleJson(QNetworkReply *reply, Callback cb)
{
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        reply->deleteLater();
        const QByteArray payload = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        QJsonParseError parseError {};
        QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        const bool hasJson = (parseError.error == QJsonParseError::NoError) && doc.isObject();

        if (status == 401 && !m_token.isEmpty()) {
            clearToken();
            emit unauthorized();
        }

        if (reply->error() != QNetworkReply::NoError) {
            const QString message = hasJson ? messageFromPayload(payload, networkErrorMessage(reply))
                                            : networkErrorMessage(reply);
            if (cb) {
                cb(false, hasJson ? doc.object() : QJsonObject(), message);
            }
            return;
        }

        if (!hasJson) {
            if (cb) {
                cb(false, QJsonObject(), QStringLiteral("服务器返回了无法解析的数据"));
            }
            return;
        }

        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("ok")) && !obj.value(QStringLiteral("ok")).toBool(true)) {
            if (cb) {
                cb(false, obj, messageFromPayload(payload, QStringLiteral("操作失败")));
            }
            return;
        }
        if (cb) {
            cb(true, obj, obj.value(QStringLiteral("message")).toString());
        }
    });
}

void ApiClient::get(const QString &path, Callback cb)
{
    handleJson(send("GET", path, nullptr, 20000), std::move(cb));
}

void ApiClient::post(const QString &path, const QJsonObject &body, Callback cb)
{
    handleJson(send("POST", path, &body, 30000), std::move(cb));
}

void ApiClient::put(const QString &path, const QJsonObject &body, Callback cb)
{
    handleJson(send("PUT", path, &body, 30000), std::move(cb));
}

void ApiClient::del(const QString &path, Callback cb)
{
    handleJson(send("DELETE", path, nullptr, 30000), std::move(cb));
}

void ApiClient::getSlow(const QString &path, int timeoutMs, Callback cb)
{
    handleJson(send("GET", path, nullptr, timeoutMs), std::move(cb));
}

void ApiClient::postSlow(const QString &path, const QJsonObject &body, int timeoutMs, Callback cb)
{
    handleJson(send("POST", path, &body, timeoutMs), std::move(cb));
}

void ApiClient::login(const QString &username, const QString &password, Callback cb)
{
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("password"), password);
    QNetworkReply *reply = send("POST", QStringLiteral("/api/auth/login"), &body, 25000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        reply->deleteLater();
        const QByteArray payload = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError parseError {};
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        const bool hasJson = (parseError.error == QJsonParseError::NoError) && doc.isObject();
        const QJsonObject obj = hasJson ? doc.object() : QJsonObject();

        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            const QString message = hasJson ? messageFromPayload(payload, networkErrorMessage(reply))
                                            : networkErrorMessage(reply);
            if (cb) {
                cb(false, obj, message);
            }
            return;
        }

        const QString token = obj.value(QStringLiteral("token")).toString();
        if (token.isEmpty()) {
            if (cb) {
                cb(false, obj, QStringLiteral("服务器未返回登录令牌"));
            }
            return;
        }
        m_token = token;
        clearCertificateChange();
        if (cb) {
            cb(true, obj, QString());
        }
    });
}

void ApiClient::setupStatus(Callback cb)
{
    handleJson(send("GET", QStringLiteral("/api/setup/status"), nullptr, 15000), std::move(cb));
}

void ApiClient::setupInit(const QString &username, const QString &password, Callback cb)
{
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("password"), password);
    QNetworkReply *reply = send("POST", QStringLiteral("/api/setup/init"), &body, 25000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        reply->deleteLater();
        const QByteArray payload = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError parseError {};
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        const bool hasJson = (parseError.error == QJsonParseError::NoError) && doc.isObject();
        const QJsonObject obj = hasJson ? doc.object() : QJsonObject();

        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            const QString message = hasJson ? messageFromPayload(payload, networkErrorMessage(reply))
                                            : networkErrorMessage(reply);
            if (cb) {
                cb(false, obj, message);
            }
            return;
        }
        const QString token = obj.value(QStringLiteral("token")).toString();
        m_token = token;
        clearCertificateChange();
        if (cb) {
            cb(true, obj, QString());
        }
    });
}

void ApiClient::logout(Callback cb)
{
    if (m_token.isEmpty()) {
        if (cb) {
            cb(true, QJsonObject(), QString());
        }
        return;
    }
    post(QStringLiteral("/api/auth/logout"), QJsonObject(), [this, cb](bool ok, const QJsonObject &data, const QString &message) {
        clearToken();
        if (cb) {
            cb(ok, data, message);
        }
    });
}

void ApiClient::ping(Callback cb)
{
    get(QStringLiteral("/api/ping"), std::move(cb));
}

void ApiClient::download(const QString &remotePath, const QString &localFile, FileCallback cb)
{
    const QString path = QStringLiteral("/api/files/download?path=%1")
                             .arg(QString::fromUtf8(QUrl::toPercentEncoding(remotePath)));
    downloadFrom(path, localFile, std::move(cb));
}

void ApiClient::downloadFolder(const QString &remotePath, const QString &localFile, FileCallback cb)
{
    const QString path = QStringLiteral("/api/files/download-folder?path=%1")
                             .arg(QString::fromUtf8(QUrl::toPercentEncoding(remotePath)));
    downloadFrom(path, localFile, std::move(cb));
}

void ApiClient::downloadFrom(const QString &path, const QString &localFile, FileCallback cb)
{
    QNetworkReply *reply = send("GET", path, nullptr, 0);
    m_nam.setTransferTimeout(0); // 大文件下载不做整体超时，由下面的空闲定时器兜底

    auto *saveFile = new QSaveFile(localFile, this);
    if (!saveFile->open(QIODevice::WriteOnly)) {
        reply->abort();
        reply->deleteLater();
        saveFile->deleteLater();
        if (cb) {
            cb(false, QStringLiteral("无法写入本地文件：%1").arg(QFileInfo(localFile).absolutePath()));
        }
        return;
    }

    // 空闲超时：每收到数据就重置；连续 60 秒无数据视为连接卡死，主动中止，
    // 避免大文件下载悬挂在“进行中”永远不会回调。
    auto *idle = new QTimer(reply);
    idle->setInterval(60000);
    idle->start();
    connect(idle, &QTimer::timeout, reply, [reply, saveFile]() {
        if (reply->isRunning()) {
            saveFile->cancelWriting();
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::readyRead, reply, [reply, saveFile, idle]() {
        if (!reply || !saveFile) {
            return;
        }
        idle->start(); // 收到数据，重置空闲计时
        saveFile->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, saveFile, cb]() {
        reply->deleteLater();
        saveFile->write(reply->readAll());
        const bool ok = reply->error() == QNetworkReply::NoError;
        if (ok && saveFile->commit()) {
            if (cb) {
                cb(true, QString());
            }
        } else {
            saveFile->cancelWriting();
            if (cb) {
                cb(false, ok ? QStringLiteral("写入本地文件失败") : networkErrorMessage(reply));
            }
        }
        saveFile->deleteLater();
        m_nam.setTransferTimeout(20000);
    });
}

void ApiClient::uploadFile(const QString &localFile,
                           const QString &remoteDir,
                           const QString &remoteName,
                           ProgressCallback progress,
                           FileCallback cb)
{
    struct Job {
        QFile file;
        qint64 total = 0;
        qint64 sent = 0;
        std::function<void()> step;
    };

    auto job = std::make_shared<Job>();
    job->file.setFileName(localFile);
    if (!job->file.open(QIODevice::ReadOnly)) {
        if (cb) {
            cb(false, QStringLiteral("无法读取本地文件：%1").arg(localFile));
        }
        return;
    }
    job->total = job->file.size();

    constexpr qint64 kChunk = 512 * 1024;
    std::weak_ptr<Job> weak = job;
    const QString encDir = QString::fromUtf8(QUrl::toPercentEncoding(remoteDir));
    const QString encName = QString::fromUtf8(QUrl::toPercentEncoding(remoteName));

    job->step = [this, weak, encDir, encName, remoteDir, remoteName, progress, cb]() {
        auto current = weak.lock();
        if (!current) {
            return;
        }
        if (current->sent >= current->total) {
            QJsonObject body;
            body.insert(QStringLiteral("path"), remoteDir);
            body.insert(QStringLiteral("name"), remoteName);
            body.insert(QStringLiteral("size"), static_cast<double>(current->total));
            post(QStringLiteral("/api/files/upload/finish"), body,
                 [current, cb](bool ok, const QJsonObject &, const QString &message) {
                     if (cb) {
                         cb(ok, ok ? QString() : message);
                     }
                 });
            return;
        }

        const QByteArray chunk = current->file.read(kChunk);
        if (chunk.isEmpty()) {
            if (cb) {
                cb(false, QStringLiteral("读取本地文件失败，文件可能已被移动或删除"));
            }
            return;
        }

        // 注意：这里绝不能用 QString::arg() 拼装 URL。encDir 形如 "%2Ffnup"，
        // 其中的 "%2" 会被后续的 .arg(offset) 当成占位符替换掉，
        // 导致请求路径变成 "path=0Ffnup&…&offset=%3"，服务端直接返回 400。
        const QString path = QStringLiteral("/api/files/upload?path=") + encDir
                             + QStringLiteral("&name=") + encName
                             + QStringLiteral("&offset=") + QString::number(current->sent);
        QNetworkReply *reply = sendRaw(path, chunk, 120000);
        const qint64 chunkSize = chunk.size();
        // 分块回调必须持有 Job 的强引用：uploadFile() 返回后局部 job 即析构，
        // 若只捕获 weak_ptr，分块请求完成时会 lock 失败并静默 return，
        // 表现为进度卡在 0%、既不上报成功也不上报失败。
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, current, chunkSize, progress, cb]() {
                    reply->deleteLater();
                    if (!current) {
                        return;
                    }
                    if (reply->error() != QNetworkReply::NoError) {
                        const QByteArray payload = reply->readAll();
                        const QString message = messageFromPayload(payload, networkErrorMessage(reply));
                        if (cb) {
                            cb(false, message);
                        }
                        return;
                    }
                    current->sent += chunkSize;
                    if (progress) {
                        progress(current->sent, current->total);
                    }
                    current->step();
                });
    };

    job->step();
}
