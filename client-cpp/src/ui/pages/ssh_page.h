#pragma once

#include <QVector>

#include "core/config.h"
#include "ui/widgets.h"

class SshWorkspace;

/**
 * SSH 终端页：承载 SshWorkspace（会话书签 + 多标签 + xterm 终端 + SFTP 侧栏）。
 * 历史版本里的自绘终端已整体替换为 xterm.js，密码直接在终端内输入。
 */
class SshPage : public BasePage
{
    Q_OBJECT

public:
    explicit SshPage(AppConfig *config, ApiClient *api, QWidget *parent = nullptr);
    ~SshPage() override;

    void refresh() override;
    void reloadTheme() override;

private:
    /** 把当前工作区复制到一个独立窗口里（MobaXterm 那种单独的大窗口）。 */
    void openDetachedWindow();

    AppConfig *m_config = nullptr;
    SshWorkspace *m_workspace = nullptr;
    QVector<QWidget *> m_detached;
};
