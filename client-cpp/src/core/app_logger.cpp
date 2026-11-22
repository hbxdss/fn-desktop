#include "core/app_logger.h"

#include "core/config.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QtGlobal>

namespace {

const qint64 kMaxBytes = 1024 * 1024;
const int kKeepFiles = 3;

QMutex &ioMutex()
{
    static QMutex m;
    return m;
}

QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral(" INFO");
    case QtWarningMsg:
        return QStringLiteral(" WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("?????");
}

void rotateIfNeeded(const QString &base)
{
    QFileInfo info(base);
    if (info.size() < kMaxBytes) {
        return;
    }
    // fn.log.2 删除，fn.log.1 → fn.log.2，fn.log → fn.log.1
    QFile::remove(base + QStringLiteral(".%1").arg(kKeepFiles - 1));
    for (int i = kKeepFiles - 2; i >= 1; --i) {
        const QString from = QStringLiteral("%1.%2").arg(base).arg(i);
        const QString to = QStringLiteral("%1.%2").arg(base).arg(i + 1);
        if (QFile::exists(from)) {
            QFile::remove(to);
            QFile::rename(from, to);
        }
    }
    QFile::rename(base, base + QStringLiteral(".1"));
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QString line = QStringLiteral("[%1] [%2] %3")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
                                  levelName(type), AppLogger::sanitize(message));

    {
        QMutexLocker locker(&ioMutex());
        const QString dir = AppLogger::logDir();
        QDir().mkpath(dir);
        const QString base = dir + QStringLiteral("/fn-desktop.log");
        rotateIfNeeded(base);
        QFile file(base);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            file.write(line.toUtf8());
            file.write("\n");
            file.close();
        }
    }

    // 调试期同时输出到调试器；fatal 走默认终止流程
    fprintf(type == QtCriticalMsg || type == QtFatalMsg ? stderr : stdout, "%s\n",
            line.toLocal8Bit().constData());
    if (type == QtFatalMsg) {
        abort();
    }
}

} // namespace

namespace AppLogger {

QString logDir()
{
    return AppConfig::dataDir() + QStringLiteral("/logs");
}

QStringList logFiles()
{
    QDir dir(logDir());
    QStringList files = dir.entryList({QStringLiteral("fn-desktop.log*")}, QDir::Files,
                                      QDir::Name);
    QStringList full;
    for (const QString &name : files) {
        full.prepend(dir.filePath(name)); // 旧→新，最后让调用方自行排序；这里保持简单
    }
    return full;
}

QString sanitize(const QString &text)
{
    static const QRegularExpression re(
        // JSON/表单风格："password":"xxx"、token=xxx、密码=xxx；以及 Bearer xxx
        QStringLiteral(R"(((?i)(password|passwd|pwd|token|secret|密码|令牌)("?\s*[:=]\s*"?))[^\s"&,]+)"));
    QString out = text;
    out.replace(re, QStringLiteral("\\1******"));
    static const QRegularExpression bearer(
        QStringLiteral(R"((?i)(authorization\s*:\s*bearer\s+)\S+)"));
    out.replace(bearer, QStringLiteral("\\1******"));
    return out;
}

void install()
{
    qInstallMessageHandler(messageHandler);
    qSetMessagePattern(QString()); // 由自定义处理器统一格式
}

} // namespace AppLogger
