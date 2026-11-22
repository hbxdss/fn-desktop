#pragma once

#include <QDialog>

#include "core/config.h"

class ApiClient;
class Backdrop;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;

/**
 * 客户端登录（连接飞牛终端服务端 59120）。
 * 支持 host:port 写法；勾选「保存密码」时密码以系统加密（TPM 2.0 / DPAPI）落盘。
 */
class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(ApiClient *api, AppConfig *config, QWidget *parent = nullptr);

    QString token() const { return m_token; }

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void submit();
    void testConnection();
    void setStatus(const QString &text, const QString &role);
    QString resolvedHost() const;
    /** 静默探测服务端是否已初始化，未初始化时切换为「创建超级管理员」表单。 */
    void detectSetupState();
    void applyInitializingUi(bool initializing);
    /** 服务端证书指纹变化时询问用户是否信任并重试。成功信任返回 true。 */
    bool confirmCertificateChange();

    ApiClient *m_api = nullptr;
    AppConfig *m_config = nullptr;
    Backdrop *m_backdrop = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_sub = nullptr;
    QLineEdit *m_host = nullptr;
    QLineEdit *m_user = nullptr;
    QLineEdit *m_password = nullptr;
    QLineEdit *m_password2 = nullptr;
    QWidget *m_confirmRow = nullptr;
    QWidget *m_options = nullptr;
    QCheckBox *m_remember = nullptr;
    QCheckBox *m_savePassword = nullptr;
    QCheckBox *m_autoLogin = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_submit = nullptr;
    QPushButton *m_test = nullptr;
    QString m_token;
    bool m_initializing = false;  // 服务端全新，当前为「创建超级管理员」模式
    bool m_busy = false;
    mutable int m_portOverride = -1; // 最近一次解析出的 "host:port" 端口（-1 表示无）
};
