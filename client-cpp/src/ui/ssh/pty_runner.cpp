#include "ui/ssh/pty_runner.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

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

/** 现有 OpenSSH 客户端（优先 PATH，其次 System32）。 */
QString findSsh(const char *name)
{
    QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
    if (!path.isEmpty()) {
        return path;
    }
    const QString root = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
    const QString fallback = root + QStringLiteral("/System32/OpenSSH/")
        + QString::fromLatin1(name) + QStringLiteral(".exe");
    return QFileInfo::exists(fallback) ? fallback : QString();
}

} // namespace

PtyRunner::PtyRunner(QObject *parent) : QObject(parent)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        if (m_running) {
            stop();
            emit finished(124, QString::fromLocal8Bit(m_buffer));
        }
    });
}

PtyRunner::~PtyRunner()
{
    stop();
}

bool PtyRunner::supported()
{
    return !sshExecutable().isEmpty();
}

QString PtyRunner::quote(const QString &arg)
{
    QString escaped = arg;
    escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

bool PtyRunner::run(const QString &commandLine, const QString &password, int timeoutMs)
{
    if (m_running || commandLine.trimmed().isEmpty()) {
        return false;
    }
    if (sshExecutable().isEmpty()) {
        return false;
    }

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

    STARTUPINFO si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inputRead;
    si.hStdOutput = outputWrite;
    si.hStdError = outputWrite;

    std::vector<wchar_t> cmdLine(size_t(commandLine.size()) + 1, L'\0');
    commandLine.toWCharArray(cmdLine.data());

    PROCESS_INFORMATION pi{};
    const BOOL created = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
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
    m_passwordSent = false;
    m_password = password;
    m_buffer.clear();
    const int generation = ++m_generation;

    HANDLE readHandle = outputRead;
    m_timeout->start(timeoutMs);

    m_reader = QThread::create([this, readHandle, generation]() {
        char buf[4096];
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(readHandle, buf, DWORD(sizeof(buf)), &read, nullptr) || read == 0) {
                break;
            }
            const QByteArray chunk(buf, int(read));
            QMetaObject::invokeMethod(
                this,
                [this, generation, chunk]() {
                    if (generation != m_generation) {
                        return;
                    }
                    m_buffer.append(chunk);
                    if (m_buffer.size() > 2 * 1024 * 1024) {
                        m_buffer.remove(0, m_buffer.size() - 2 * 1024 * 1024);
                    }
                    if (!m_passwordSent && !m_password.isEmpty()) {
                        const bool asked = m_buffer.contains("password")
                                           || m_buffer.contains("Password")
                                           || m_buffer.contains(QStringLiteral("密码").toUtf8());
                        if (asked) {
                            m_passwordSent = true;
                            writeRaw(m_password.toUtf8() + '\r');
                        }
                    }
                },
                Qt::QueuedConnection);
        }
    });
    m_reader->start();

    m_waiter = QThread::create([this, generation]() {
        HANDLE processHandle = static_cast<HANDLE>(m_process);
        WaitForSingleObject(processHandle, INFINITE);
        DWORD code = 0;
        if (!GetExitCodeProcess(processHandle, &code) || code == STILL_ACTIVE) {
            code = 0;
        }
        const int exitCode = int(code);
        QMetaObject::invokeMethod(
            this,
            [this, generation, exitCode]() {
                if (generation != m_generation) {
                    return;
                }
                m_running = false;
                if (m_timeout) {
                    m_timeout->stop();
                }
                emit finished(exitCode, QString::fromUtf8(m_buffer));
            },
            Qt::QueuedConnection);
    });
    m_waiter->start();
    return true;
}

void PtyRunner::writeRaw(const QByteArray &data)
{
    if (!m_running || !m_input || data.isEmpty()) {
        return;
    }
    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(m_input), data.constData(), DWORD(data.size()), &written,
              nullptr);
}

void PtyRunner::stop()
{
    m_running = false;
    ++m_generation;
    if (m_timeout) {
        m_timeout->stop();
    }
    if (m_process) {
        TerminateProcess(static_cast<HANDLE>(m_process), 0);
    }

    if (m_waiter) {
        if (!m_waiter->wait(1500)) {
            m_waiter->terminate();
            m_waiter->wait(300);
        }
        delete m_waiter;
        m_waiter = nullptr;
    }
    if (m_reader) {
        if (!m_reader->wait(1500)) {
            m_reader->terminate();
            m_reader->wait(300);
        }
        delete m_reader;
        m_reader = nullptr;
    }

    closeHandle(m_output);
    closeHandle(m_process);
    closeHandle(m_input);
}

QString PtyRunner::sshExecutable()
{
    return findSsh("ssh");
}

QString PtyRunner::scpExecutable()
{
    return findSsh("scp");
}
