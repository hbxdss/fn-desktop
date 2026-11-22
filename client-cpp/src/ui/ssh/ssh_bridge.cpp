#include "ui/ssh/ssh_bridge.h"

SshBridge::SshBridge(QObject *parent) : QObject(parent) {}

void SshBridge::ready()
{
    emit jsReady();
}

void SshBridge::sendData(const QString &sessionId, const QString &base64)
{
    const QByteArray data = QByteArray::fromBase64(base64.toLatin1());
    if (!data.isEmpty()) {
        emit dataFromJs(sessionId, data);
    }
}

void SshBridge::resize(const QString &sessionId, int cols, int rows)
{
    if (cols > 0 && rows > 0) {
        emit resizeRequested(sessionId, cols, rows);
    }
}

void SshBridge::titleChanged(const QString &sessionId, const QString &title)
{
    emit titleFromJs(sessionId, title);
}

void SshBridge::setClipboard(const QString &text)
{
    emit clipboardSetRequested(text);
}

void SshBridge::requestClipboard()
{
    emit clipboardRequested();
}

void SshBridge::openUrl(const QString &url)
{
    emit urlRequested(url);
}
