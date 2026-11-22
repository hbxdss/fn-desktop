#pragma once

#include <QMap>
#include <QString>
#include <QWidget>

#include "core/config.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTabBar;
class QToolButton;
class QWebChannel;
class QWebEngineView;

class SftpPanel;
class SshBridge;
class SshSession;

/**
 * SSH 工作区（MobaXterm 风格）。
 *
 * 布局：
 *   ┌──────────────────────────────────────────┬──────────────┐
 *   │  标签栏（多会话）                          │              │
 *   ├──────────────────────────────────────────┤  SFTP 侧栏    │
 *   │  xterm.js 终端（QWebEngine）              │              │
 *   └──────────────────────────────────────────┴──────────────┘
 *
 * 终端渲染交给 xterm.js（和 VS Code 同款内核），底层仍是系统 OpenSSH +
 * Windows 伪控制台，因此 vim / top / htop 这类全屏程序可以正常使用。
 * 密码直接在终端里输入，远端不会回显，不存在明文暴露。
 */
class SshWorkspace : public QWidget
{
    Q_OBJECT

public:
    explicit SshWorkspace(AppConfig *config, QWidget *parent = nullptr);
    ~SshWorkspace() override;

    void reloadTheme();
    void setSftpVisible(bool on);
    bool sftpVisible() const;
    bool hasSessions() const { return !m_sessions.isEmpty(); }

    /** 新建 / 编辑会话对话框（供外部菜单触发）。 */
    void createProfileDialog();
    /** 打开一个已保存的会话。 */
    void connectProfileById(const QString &profileId);
    /** 用当前配置里的默认主机快速连接。 */
    void quickConnectDefault();

signals:
    void activeTitleChanged(const QString &title);
    void openUrlRequested(const QString &url);
    /** 请求宿主把工作区放进独立窗口。 */
    void newWindowRequested();

private:
    struct LiveSession
    {
        QString title;
        QString host;
        int port = 22;
        QString user;
        QString keyPath;
        QString password;
        SshSession *session = nullptr;
        QString scanBuffer;   // 累积远端文本，用于检测 shell 提示符 / 密码提示
        bool promptSeen = false;
        bool sudoPending = false;    // 已请求 sudo 提权，等待密码提示后回填密码
        bool sudoSubmitted = false;  // 密码已提交，等待终端反馈成功或失败
        bool connectError = false;   // 已根据终端输出判定连接失败
        bool connected = false;
    };

    void buildUi();
    QWidget *buildTerminalArea();
    void connectProfile(const SshProfile &profile);
    void startSession(const QString &title, const QString &host, int port,
                      const QString &user, const QString &keyPath, const QString &password);
    void closeSession(const QString &id);
    void activateSession(const QString &id);
    void setStatus(const QString &text, const QString &tone);
    void updateTabsAndActions();

    QString activeId() const;
    LiveSession *activeSession() const;
    LiveSession *sessionById(const QString &id) const;

    void runJs(const QString &script);
    void jsCreate(const QString &id);
    void jsWrite(const QString &id, const QByteArray &data);
    void pushThemeToJs();
    QString themeJson() const;

    void onBridgeData(const QString &id, const QByteArray &data);
    void onBridgeResize(const QString &id, int cols, int rows);
    void onBridgeTitle(const QString &id, const QString &title);
    void onSessionOutput(const QString &id, const QByteArray &data);
    void onSessionFinished(const QString &id, int code);
    void scanOutput(const QString &id, const QByteArray &data);

    void editProfile(const QString &profileId, bool isNew);
    void deleteProfile(const QString &profileId);

    void quickLogin();
    void quickSudo();
    void toggleSftp();

    AppConfig *m_config = nullptr;

    QTabBar *m_tabs = nullptr;
    QWebEngineView *m_view = nullptr;
    QWebChannel *m_channel = nullptr;
    SshBridge *m_bridge = nullptr;
    SftpPanel *m_sftp = nullptr;
    QWidget *m_sftpWrap = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_sessionInfo = nullptr;
    QPushButton *m_sftpButton = nullptr;
    QPushButton *m_newWindowButton = nullptr;
    QPushButton *m_loginButton = nullptr;
    QPushButton *m_sudoButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPushButton *m_searchButton = nullptr;
    QWidget *m_searchBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;

    QMap<QString, LiveSession *> m_sessions;  // 会话 id -> 运行时状态
    QString m_activeId;
    bool m_webReady = false;
    int m_sessionSeed = 0;
};
