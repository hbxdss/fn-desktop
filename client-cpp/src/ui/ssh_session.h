#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QThread;

/**
 * 软件内置的 SSH 会话。
 *
 * 底层使用系统自带的 OpenSSH 客户端（ssh.exe），通过普通管道与 ssh.exe 交互，
 * 远端用 `script` 分配伪终端，因此远端 shell 具备完整交互能力（提示符、行编辑、
 * vim/top 等全屏程序）。
 *
 * write() 写入 stdin；output() 返回 stdout/stderr；finished() 表示会话结束。
 */
class SshSession : public QObject
{
    Q_OBJECT

public:
    explicit SshSession(QObject *parent = nullptr);
    ~SshSession() override;

    /** 系统 OpenSSH 客户端是否可用。 */
    static bool supported();
    /** 系统 OpenSSH 客户端路径；找不到时返回空字符串。 */
    static QString clientPath();

    bool start(const QString &host, quint16 port, const QString &user,
               const QString &keyPath = QString());
    void write(const QByteArray &data);
    /** 同步远端伪终端尺寸（通过 stty）。 */
    void resize(quint16 cols, quint16 rows);
    void stop();
    bool isRunning() const { return m_running; }

signals:
    void output(const QByteArray &data);
    void finished(int exitCode);

private:
    void *m_process = nullptr;    // ssh.exe 进程句柄
    void *m_input = nullptr;      // 管道写端（→ ssh.exe stdin）
    void *m_output = nullptr;     // 管道读端（← ssh.exe stdout/stderr）
    QThread *m_reader = nullptr;  // 阻塞读取输出
    QThread *m_waiter = nullptr;  // 等待 ssh.exe 退出
    int m_generation = 0;         // 防止上一次会话的回调误伤新会话
    quint16 m_cols = 120;
    quint16 m_rows = 40;
    bool m_running = false;
};
