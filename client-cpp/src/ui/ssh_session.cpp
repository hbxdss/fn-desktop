// SSH 会话：普通管道 + ssh.exe，远端 script 分配伪终端
#include "ui/ssh_session.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>
#include <QThread>

#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

void closeHandle(void *&handle)
{
    if (handle) {
        CloseHandle(static_cast<HANDLE>(handle));
        handle = nullptr;
    }
}

QString quote(const QString &value)
{
    return QLatin1Char('"') + value + QLatin1Char('"');
}

/** 远端命令：用 script 申请伪终端，再进入交互 bash；初始尺寸一并设置。 */
QString remoteCommand(quint16 cols, quint16 rows)
{
    // 注意：这里保持单引号，ssh 会把 host 之后的参数用空格拼成远端命令，
    // 单引号原样传给远端 shell，正好形成引号包裹。
    return QStringLiteral(
               "script -q -c 'stty rows %1 cols %2 2>/dev/null; "
               "export TERM=xterm-256color; exec bash -i' /dev/null")
        .arg(rows)
        .arg(cols);
}

/**
 * 丢弃 stty 尺寸命令的回显行（这是本程序注入的控制命令，不该出现在终端里）。
 * 命令回显可能被拆到两个数据块里，因此用 pending 暂存可能被截断的行首。
 */
QByteArray stripSttyEcho(const QByteArray &data, QByteArray &pending)
{
    QByteArray all = pending;
    pending.clear();
    all.append(data);

    QByteArray out;
    out.reserve(all.size());
    int start = 0;
    while (start < all.size()) {
        const int nl = all.indexOf('\n', start);
        if (nl < 0) {
            const QByteArray rest = all.mid(start);
            const QByteArray marker("stty rows ");
            // 半截命令（或"可能是半截命令"）暂存，等下一块拼完整后再判断
            if (marker.startsWith(rest) || rest.startsWith(marker)) {
                pending = rest;
            } else {
                out.append(rest);
            }
            break;
        }
        QByteArray line = all.mid(start, nl + 1 - start);
        QByteArray trimmed = line;
        while (!trimmed.isEmpty() && (trimmed.endsWith('\r') || trimmed.endsWith('\n'))) {
            trimmed.chop(1);
        }
        // 回显行可能带提示符前缀（LYC@test:~$ stty ...），所以只要求行内有该命令
        const bool isSizeEcho = trimmed.contains("stty rows ") && trimmed.contains(" cols ")
            && trimmed.endsWith("2>/dev/null");
        if (!isSizeEcho) {
            out.append(line);
        }
        start = nl + 1;
    }
    return out;
}

} // namespace

SshSession::SshSession(QObject *parent) : QObject(parent) {}

SshSession::~SshSession()
{
    stop();
}

bool SshSession::supported()
{
    return !clientPath().isEmpty();
}

QString SshSession::clientPath()
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("ssh"));
    if (!path.isEmpty()) {
        return path;
    }
    const QString root = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
    const QString fallback = root + QStringLiteral("/System32/OpenSSH/ssh.exe");
    if (QFileInfo::exists(fallback)) {
        return fallback;
    }
    return QString();
}

bool SshSession::start(const QString &host, quint16 port, const QString &user,
                       const QString &keyPath)
{
    if (m_running) {
        return false;
    }
    const QString cleanHost = host.trimmed();
    if (cleanHost.isEmpty()) {
        return false;
    }
    const QString program = clientPath();
    if (program.isEmpty()) {
        return false;
    }

    m_cols = m_cols ? m_cols : 120;
    m_rows = m_rows ? m_rows : 40;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite, &sa, 65536)) {
        return false;
    }
    if (!CreatePipe(&outputRead, &outputWrite, &sa, 65536)) {
        CloseHandle(inputRead);
        CloseHandle(inputWrite);
        return false;
    }

    const QString target = user.trimmed().isEmpty()
                               ? cleanHost
                               : user.trimmed() + QLatin1Char('@') + cleanHost;

    QString command = quote(program)
        + QStringLiteral(" -T -p %1 -o StrictHostKeyChecking=accept-new"
                         " -o ServerAliveInterval=30 -o ConnectTimeout=15")
              .arg(port);
    if (!keyPath.trimmed().isEmpty()) {
        command += QStringLiteral(" -i %1 -o IdentitiesOnly=yes").arg(quote(keyPath));
    }
    command += QLatin1Char(' ') + quote(target) + QLatin1Char(' ')
        + remoteCommand(m_cols, m_rows);

    std::vector<wchar_t> appPath(size_t(program.size()) + 1, L'\0');
    program.toWCharArray(appPath.data());
    std::vector<wchar_t> commandLine(size_t(command.size()) + 1, L'\0');
    command.toWCharArray(commandLine.data());

    STARTUPINFO si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inputRead;
    si.hStdOutput = outputWrite;
    si.hStdError = outputWrite;

    PROCESS_INFORMATION pi{};
    const BOOL created = CreateProcessW(appPath.data(), commandLine.data(),
                                        nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                        nullptr, nullptr, &si, &pi);
    if (!created) {
        CloseHandle(inputRead);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        CloseHandle(outputWrite);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);

    m_process = pi.hProcess;
    m_input = inputWrite;
    m_output = outputRead;
    m_running = true;
    const int generation = ++m_generation;

    HANDLE outputHandle = outputRead;
    m_reader = QThread::create([this, outputHandle, generation]() {
        QByteArray buffer;
        buffer.resize(8192);
        QByteArray pending;
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(outputHandle, buffer.data(), DWORD(buffer.size()), &read, nullptr)
                || read == 0) {
                break;
            }
            const QByteArray chunk =
                stripSttyEcho(QByteArray(buffer.constData(), int(read)), pending);
            if (chunk.isEmpty()) {
                continue;
            }
            QMetaObject::invokeMethod(
                this,
                [this, generation, chunk]() {
                    if (generation != m_generation) {
                        return;
                    }
                    emit output(chunk);
                },
                Qt::QueuedConnection);
        }
    });

    HANDLE processHandle = pi.hProcess;
    m_waiter = QThread::create([this, processHandle, generation]() {
        WaitForSingleObject(processHandle, INFINITE);
        DWORD code = 0;
        if (!GetExitCodeProcess(processHandle, &code) || code == STILL_ACTIVE) {
            code = 0;
        }
        const int exitCode = int(code);
        QMetaObject::invokeMethod(
            this,
            [this, generation, exitCode]() {
                if (generation != m_generation || !m_running) {
                    return;
                }
                m_running = false;
                emit finished(exitCode);
            },
            Qt::QueuedConnection);
    });

    m_reader->start();
    m_waiter->start();
    return true;
}

void SshSession::write(const QByteArray &data)
{
    if (!m_running || !m_input || data.isEmpty()) {
        return;
    }
    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(m_input), data.constData(), DWORD(data.size()),
              &written, nullptr);
}

void SshSession::resize(quint16 cols, quint16 rows)
{
    if (cols == 0 || rows == 0) {
        return;
    }
    if (cols == m_cols && rows == m_rows) {
        return;
    }
    m_cols = cols;
    m_rows = rows;
    if (m_running) {
        // 远端 pty 的行规程会把 \r 转成 \n；该命令本身会回显一次，属正常现象。
        write(QStringLiteral("stty rows %1 cols %2 2>/dev/null\r").arg(rows).arg(cols).toUtf8());
    }
}

void SshSession::stop()
{
    m_running = false;
    ++m_generation;

    if (m_input) {
        const QByteArray bye("exit\r");
        DWORD written = 0;
        WriteFile(static_cast<HANDLE>(m_input), bye.constData(), DWORD(bye.size()),
                  &written, nullptr);
    }

    if (m_process) {
        TerminateProcess(static_cast<HANDLE>(m_process), 0);
    }

    if (m_waiter) {
        if (!m_waiter->wait(2000)) {
            m_waiter->terminate();
            m_waiter->wait(500);
        }
        delete m_waiter;
        m_waiter = nullptr;
    }

    if (m_reader) {
        if (!m_reader->wait(2000)) {
            m_reader->terminate();
            m_reader->wait(500);
        }
        delete m_reader;
        m_reader = nullptr;
    }

    closeHandle(m_output);
    closeHandle(m_input);
    closeHandle(m_process);
}
