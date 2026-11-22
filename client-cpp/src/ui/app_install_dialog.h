#pragma once

#include "ui/widgets.h"

class QLabel;
class QPushButton;
class QTimer;
class QProgressBar;

/**
 * 未安装应用的引导对话框：
 * 两个打开方式（软件内应用中心 / 系统浏览器）+ 取消；
 * 5 秒倒计时结束后自动选择「软件内打开」。
 */
class AppInstallDialog : public DialogWindow
{
    Q_OBJECT

public:
    explicit AppInstallDialog(const QString &appName, QWidget *parent = nullptr);

signals:
    void openInside();
    void openBrowser();

private:
    void tick();
    void chooseInside();

    QLabel *m_countLabel = nullptr;
    QProgressBar *m_bar = nullptr;
    QPushButton *m_inside = nullptr;
    QTimer *m_timer = nullptr;
    int m_left = 5;
    bool m_decided = false;
};
