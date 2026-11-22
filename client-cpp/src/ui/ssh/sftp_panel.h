#pragma once

#include <QString>
#include <QWidget>

#include <functional>

class QLabel;
class QListWidget;
class QPushButton;

class PtyRunner;

/**
 * SFTP 风格的文件侧栏。
 *
 * 因为没有引入 libssh2，这里通过系统 OpenSSH 实现：
 *   - 列表 / 新建 / 删除 / 重命名 → ssh.exe 执行远端 shell 命令
 *   - 上传 / 下载                 → scp.exe
 * 认证（密码 / 密钥）由 PtyRunner 用伪控制台完成，因此密码登录也能用。
 */
class SftpPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SftpPanel(QWidget *parent = nullptr);
    ~SftpPanel() override;

    /** 绑定目标主机；密码为空时依赖私钥或已保存密码。 */
    void setTarget(const QString &host, int port, const QString &user,
                   const QString &keyPath, const QString &password);
    void clearTarget();
    void reloadTheme();

    QString currentPath() const { return m_path; }

private:
    void buildUi();
    void refresh();
    void goUp();
    void enterSelected();
    void uploadFiles();
    void downloadSelected();
    void removeSelected();
    void renameSelected();
    void makeDirectory();
    void openSelected();

    void runSsh(const QString &remoteCommand, std::function<void(bool, const QString &)> done);
    void runScp(const QString &args, std::function<void(bool, const QString &)> done);
    QString sshCommandPrefix() const;
    QString scpCommandPrefix() const;
    QString joinRemote(const QString &name) const;
    QString remoteBase() const { return QStringLiteral("%1@%2").arg(m_user, m_host); }
    QString selectedName() const;
    bool selectedIsDir() const;
    void setStatus(const QString &text, bool error = false);

    QString m_host;
    QString m_user;
    QString m_keyPath;
    QString m_password;
    QString m_path = QStringLiteral("/");
    int m_port = 22;
    bool m_busy = false;

    QLabel *m_pathLabel = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_upButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_uploadButton = nullptr;
    QPushButton *m_downloadButton = nullptr;
    QPushButton *m_mkdirButton = nullptr;
    QPushButton *m_renameButton = nullptr;
    QPushButton *m_deleteButton = nullptr;

    PtyRunner *m_runner = nullptr;
    std::function<void(bool, const QString &)> m_callback;
};
