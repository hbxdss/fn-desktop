#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

/**
 * xterm.js 终端页面与 C++ 之间的桥。
 *
 * JS → C++：通过 QWebChannel 调用这里的 slot（键盘输入、尺寸变化、标题变化、
 * 复制粘贴、点击链接）。
 * C++ → JS：直接 QWebEnginePage::runJavaScript 调 terminal.html 里的 window.ssh* 函数。
 *
 * 终端数据统一用 base64 传递：既避开二进制/编码转换问题，也不会把控制字符
 * 带进 JS 字符串字面量里。
 */
class SshBridge : public QObject
{
    Q_OBJECT

public:
    explicit SshBridge(QObject *parent = nullptr);

public slots:
    /** 页面初始化完成、QWebChannel 已连上。 */
    void ready();
    /** 键盘输入（base64 编码的原始字节）。 */
    void sendData(const QString &sessionId, const QString &base64);
    /** xterm 尺寸变化（cols/rows），需要同步给 ConPTY。 */
    void resize(const QString &sessionId, int cols, int rows);
    /** 远端标题变化（OSC 0/2），用于标签名。 */
    void titleChanged(const QString &sessionId, const QString &title);
    /** Ctrl+Shift+C 复制：JS 把选中文本交给 C++ 写系统剪贴板。 */
    void setClipboard(const QString &text);
    /** Ctrl+Shift+V 粘贴：请求 C++ 读取剪贴板，结果通过 clipboardReady 信号回传。 */
    void requestClipboard();
    /** 点击终端里的链接：交给主程序用内置浏览器打开。 */
    void openUrl(const QString &url);

signals:
    void jsReady();
    void dataFromJs(const QString &sessionId, const QByteArray &data);
    void resizeRequested(const QString &sessionId, int cols, int rows);
    void titleFromJs(const QString &sessionId, const QString &title);
    void clipboardSetRequested(const QString &text);
    /** 请求宿主读取系统剪贴板，宿主随后调用 deliverClipboard()。 */
    void clipboardRequested();
    /** 把剪贴板内容送回 xterm（粘贴到活动标签）。 */
    void clipboardReady(const QString &text);
    void urlRequested(const QString &url);

public:
    /** 宿主读取剪贴板后通过该函数把内容回传给 xterm。 */
    void deliverClipboard(const QString &text) { emit clipboardReady(text); }
};
