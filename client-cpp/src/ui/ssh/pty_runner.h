#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QTimer;
class QThread;

/**
 * 执行一次性命令（ssh / scp）并收集输出。
 *
 * 使用普通匿名管道把 stdin/stdout 接到子进程，兼容性最好（不依赖 ConPTY）。
 * SFTP 侧栏的列目录、上传、下载都依赖这个执行器。
 *
 * 说明：ssh 在非终端模式下不会从 stdin 读取密码，因此密码自动应答只在
 * 远端主动从管道读取时才有意义；实际认证请使用 SSH 密钥。
 */
class PtyRunner : public QObject
{
    Q_OBJECT

public:
    explicit PtyRunner(QObject *parent = nullptr);
    ~PtyRunner() override;

    /** 当前系统是否有可用的 OpenSSH 客户端。 */
    static bool supported();
    /** 把参数包成命令行里的双引号形式。 */
    static QString quote(const QString &arg);
    /** 系统 ssh.exe / scp.exe 路径。 */
    static QString sshExecutable();
    static QString scpExecutable();

    /** 执行一条命令行；password 非空时检测到密码提示自动写入。 */
    bool run(const QString &commandLine, const QString &password, int timeoutMs = 25000);
    void stop();
    bool isRunning() const { return m_running; }

signals:
    void finished(int exitCode, const QString &output);

private:
    void writeRaw(const QByteArray &data);

    void *m_process = nullptr;
    void *m_input = nullptr;
    void *m_output = nullptr;
    int m_generation = 0;
    bool m_running = false;
    bool m_passwordSent = false;
    QString m_password;
    QByteArray m_buffer;
    QTimer *m_timeout = nullptr;
    QThread *m_reader = nullptr;
    QThread *m_waiter = nullptr;
};
