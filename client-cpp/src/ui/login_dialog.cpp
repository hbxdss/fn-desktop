#include "ui/login_dialog.h"

#include "core/api_client.h"
#include "core/secure_vault.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QCheckBox>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

LoginDialog::LoginDialog(ApiClient *api, AppConfig *config, QWidget *parent)
    : QDialog(parent), m_api(api), m_config(config)
{
    setWindowTitle(QStringLiteral("登录 · 飞牛终端"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    // 透明窗口 + Backdrop 自绘圆角，否则无边框窗口的四角会是直角。
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedSize(470, 566);
    setObjectName(QStringLiteral("LoginRoot"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_backdrop = new Backdrop(this);
    m_backdrop->setTheme(Theme::idFromString(m_config->uiTheme),
                         m_config->colorMode == QLatin1String("light"));
    root->addWidget(m_backdrop);

    auto *outer = new QVBoxLayout(m_backdrop);
    outer->setContentsMargins(22, 16, 22, 22);
    outer->setSpacing(10);

    auto *topBar = new QHBoxLayout();
    topBar->setContentsMargins(2, 0, 0, 0);
    topBar->setSpacing(8);

    auto *logo = new QLabel(m_backdrop);
    logo->setFixedSize(20, 20);
    logo->setScaledContents(true);
    logo->setPixmap(QPixmap(QStringLiteral(":/app.png"))
                        .scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *appName = new QLabel(QStringLiteral("飞牛终端"), m_backdrop);
    appName->setObjectName(QStringLiteral("TitleBarSub"));

    auto *close = new QToolButton(m_backdrop);
    close->setObjectName(QStringLiteral("WinBtn"));
    close->setProperty("role", QStringLiteral("close"));
    close->setFixedSize(34, 26);
    close->setText(QStringLiteral("✕"));
    connect(close, &QToolButton::clicked, this, &QDialog::reject);

    topBar->addWidget(logo);
    topBar->addWidget(appName);
    topBar->addStretch(1);
    topBar->addWidget(close);
    outer->addLayout(topBar);
    outer->addStretch(1);

    auto *card = new QFrame(m_backdrop);
    card->setObjectName(QStringLiteral("LoginCard"));
    card->setFixedWidth(390);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(26, 24, 26, 24);
    cardLayout->setSpacing(9);

    m_title = new QLabel(QStringLiteral("登录飞牛终端"), card);
    m_title->setObjectName(QStringLiteral("LoginTitle"));
    m_sub = new QLabel(QStringLiteral("使用飞牛 NAS 账号登录桌面客户端"), card);
    m_sub->setObjectName(QStringLiteral("LoginSub"));
    m_sub->setWordWrap(true);

    cardLayout->addWidget(m_title);
    cardLayout->addWidget(m_sub);
    cardLayout->addSpacing(6);

    auto *hostLabel = makeLabel(QStringLiteral("服务器地址"), QStringLiteral("muted"), card);
    m_host = new QLineEdit(card);
    m_host->setPlaceholderText(QStringLiteral("例如 192.168.1.100 或 192.168.1.100:59120"));
    m_host->setText(m_config->host);

    auto *userLabel = makeLabel(QStringLiteral("用户名"), QStringLiteral("muted"), card);
    m_user = new QLineEdit(card);
    m_user->setPlaceholderText(QStringLiteral("飞牛账号"));
    m_user->setText(m_config->rememberUsername ? m_config->username : QString());

    auto *passwordLabel = makeLabel(QStringLiteral("密码"), QStringLiteral("muted"), card);
    m_password = new QLineEdit(card);
    m_password->setPlaceholderText(QStringLiteral("每次登录都需要输入"));
    m_password->setEchoMode(QLineEdit::Password);
    // 明文/密文切换
    auto *passwordRow = new QHBoxLayout();
    passwordRow->setContentsMargins(0, 0, 0, 0);
    passwordRow->setSpacing(8);
    auto *eyeButton = new QPushButton(QStringLiteral("显示"), card);
    eyeButton->setObjectName(QStringLiteral("GhostButton"));
    eyeButton->setFixedWidth(56);
    eyeButton->setCheckable(true);
    eyeButton->setAccessibleName(QStringLiteral("显示或隐藏密码"));
    eyeButton->setToolTip(QStringLiteral("显示 / 隐藏密码"));
    connect(eyeButton, &QPushButton::toggled, m_password, [this, eyeButton](bool on) {
        m_password->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
        eyeButton->setText(on ? QStringLiteral("隐藏") : QStringLiteral("显示"));
    });
    passwordRow->addWidget(m_password, 1);
    passwordRow->addWidget(eyeButton);

    // 初始化模式才显示：确认密码
    m_confirmRow = new QWidget(card);
    auto *confirmLay = new QVBoxLayout(m_confirmRow);
    confirmLay->setContentsMargins(0, 0, 0, 0);
    confirmLay->setSpacing(4);
    confirmLay->addWidget(makeLabel(QStringLiteral("确认密码"), QStringLiteral("muted"), m_confirmRow));
    m_password2 = new QLineEdit(m_confirmRow);
    m_password2->setEchoMode(QLineEdit::Password);
    m_password2->setPlaceholderText(QStringLiteral("再次输入密码"));
    confirmLay->addWidget(m_password2);
    m_confirmRow->setVisible(false);

    m_remember = new QCheckBox(QStringLiteral("记住用户名（不保存密码）"), card);
    m_remember->setChecked(m_config->rememberUsername);

    auto *pwdSaveRow = new QHBoxLayout();
    m_savePassword = new QCheckBox(QStringLiteral("保存密码用于管理后台自动填充"), card);
    const bool hasSavedPwd = !m_config->savedPasswordCipher.trimmed().isEmpty();
    m_savePassword->setChecked(hasSavedPwd);
    m_savePassword->setToolTip(QStringLiteral("密码将使用 %1 加密后写入本地配置。")
                                   .arg(SecureVault::providerName()));
    m_autoLogin = new QCheckBox(QStringLiteral("自动登录"), card);
    m_autoLogin->setChecked(m_config->webAutoLogin && hasSavedPwd);
    m_autoLogin->setEnabled(hasSavedPwd);
    pwdSaveRow->addWidget(m_savePassword);
    pwdSaveRow->addSpacing(8);
    pwdSaveRow->addWidget(m_autoLogin);
    pwdSaveRow->addStretch(1);

    // 选项区整体包一层，初始化模式下隐藏（首次使用没有可保存的账号）
    m_options = new QWidget(card);
    auto *optionsLay = new QVBoxLayout(m_options);
    optionsLay->setContentsMargins(0, 0, 0, 0);
    optionsLay->setSpacing(9);
    optionsLay->addWidget(m_remember);
    optionsLay->addLayout(pwdSaveRow);

    m_status = makeLabel(QString(), QStringLiteral("muted"), card);
    m_status->setWordWrap(true);
    m_status->setVisible(false);

    cardLayout->addWidget(hostLabel);
    cardLayout->addWidget(m_host);
    cardLayout->addWidget(userLabel);
    cardLayout->addWidget(m_user);
    cardLayout->addWidget(passwordLabel);
    cardLayout->addLayout(passwordRow);
    cardLayout->addWidget(m_confirmRow);
    cardLayout->addWidget(m_options);
    cardLayout->addWidget(m_status);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(10);
    m_test = makeButton(QStringLiteral("测试连接"), QStringLiteral("ghost"), card);
    m_submit = makeButton(QStringLiteral("登录"), QStringLiteral("primary"), card);
    m_submit->setMinimumHeight(36);
    m_test->setMinimumHeight(36);
    m_submit->setDefault(true);
    buttons->addWidget(m_test);
    buttons->addStretch(1);
    buttons->addWidget(m_submit);
    cardLayout->addLayout(buttons);

    auto *hint = new QLabel(QStringLiteral(
                                "密码用于登录校验；若勾选「保存密码」，将以系统加密方式（TPM 2.0 或 DPAPI）写入本地配置，仅用于管理后台自动填充。"),
                            card);
    hint->setObjectName(QStringLiteral("LoginHint"));
    hint->setWordWrap(true);
    cardLayout->addWidget(hint);

    auto *cardRow = new QHBoxLayout();
    cardRow->addStretch(1);
    cardRow->addWidget(card);
    cardRow->addStretch(1);
    outer->addLayout(cardRow);
    outer->addStretch(1);

    connect(m_submit, &QPushButton::clicked, this, &LoginDialog::submit);
    connect(m_test, &QPushButton::clicked, this, &LoginDialog::testConnection);
    connect(m_password, &QLineEdit::returnPressed, this, &LoginDialog::submit);
    connect(m_user, &QLineEdit::returnPressed, this, &LoginDialog::submit);
    connect(m_savePassword, &QCheckBox::toggled, m_autoLogin, &QCheckBox::setEnabled);
    connect(m_savePassword, &QCheckBox::toggled, m_autoLogin,
            [this](bool checked) { if (!checked) m_autoLogin->setChecked(false); });
    connect(m_password2, &QLineEdit::returnPressed, this, &LoginDialog::submit);

    if (m_config->host.isEmpty()) {
        m_host->setFocus();
    } else if (m_user->text().isEmpty()) {
        m_user->setFocus();
    } else {
        m_password->setFocus();
    }

    // 静默探测：服务端全新时直接切到「创建超级管理员」表单
    QTimer::singleShot(200, this, &LoginDialog::detectSetupState);
}

QString LoginDialog::resolvedHost() const
{
    AppConfig probe;
    // 支持 "host:port" 写法：自动拆出端口并覆盖管理端口
    QString text = m_host->text().trimmed();
    m_portOverride = -1;
    const int colon = text.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) {
        bool ok = false;
        const quint16 port = text.mid(colon + 1).toUShort(&ok);
        if (ok && port > 0 && !text.mid(colon + 1).contains(QLatin1Char(':'))) {
            m_portOverride = port;
            probe.host = text.left(colon);
            probe.adminPort = port;
            probe.normalize();
            return probe.host;
        }
    }
    probe.host = text;
    probe.adminPort = m_config->adminPort;
    probe.normalize();
    return probe.host;
}

void LoginDialog::setStatus(const QString &text, const QString &role)
{
    m_status->setText(text);
    m_status->setProperty("role", role);
    m_status->setVisible(!text.isEmpty());
    Theme::repolish(m_status);
}

void LoginDialog::detectSetupState()
{
    if (m_busy) {
        return;
    }
    const QString host = resolvedHost();
    if (host.isEmpty()) {
        return;
    }
    m_config->host = host;
    m_api->setBaseUrl(QUrl(m_config->apiUrl()));
    m_api->setupStatus([this](bool ok, const QJsonObject &data, const QString &) {
        if (!ok) {
            return; // 探测失败保持登录表单，不打扰用户
        }
        const bool initialized = data.value(QStringLiteral("initialized")).toBool(true);
        applyInitializingUi(!initialized);
    });
}

void LoginDialog::applyInitializingUi(bool initializing)
{
    if (m_initializing == initializing) {
        return;
    }
    m_initializing = initializing;
    m_confirmRow->setVisible(initializing);
    m_options->setVisible(!initializing);
    if (initializing) {
        m_title->setText(QStringLiteral("初始化飞牛终端"));
        m_sub->setText(QStringLiteral(
            "服务端尚未初始化。请创建超级管理员：用户名 3~32 位，密码至少 8 位且含字母和数字。"));
        m_submit->setText(QStringLiteral("创建并登录"));
        setStatus(QStringLiteral("首次使用需创建管理员账号"), QStringLiteral("muted"));
    } else {
        m_title->setText(QStringLiteral("登录飞牛终端"));
        m_sub->setText(QStringLiteral("使用飞牛 NAS 账号登录桌面客户端"));
        m_submit->setText(QStringLiteral("登录"));
        setStatus(QString(), QStringLiteral("muted"));
    }
}

bool LoginDialog::confirmCertificateChange()
{
    if (!m_api->hasCertificateChange()) {
        return false;
    }
    const QString host = m_api->certChangedHost();
    const auto answer = QMessageBox::question(
        this, QStringLiteral("服务端证书已变化"),
        QStringLiteral("服务器 %1 的 TLS 证书与首次连接时记录的不一致。\n\n"
                       "常见原因：NAS 重装了终端服务端，或自签证书已续期。\n"
                       "如果不是你本人操作，可能存在中间人风险，请选择「否」。\n\n"
                       "是否信任新证书并继续连接？")
            .arg(host),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        m_api->trustChangedCertificate();
        return true;
    }
    m_api->clearCertificateChange();
    return false;
}

void LoginDialog::submit()
{
    if (m_busy) {
        return;
    }
    const QString host = resolvedHost();
    if (host.isEmpty()) {
        setStatus(QStringLiteral("请先填写服务器地址，例如 192.168.1.100"), QStringLiteral("error"));
        m_host->setFocus();
        return;
    }
    if (m_user->text().trimmed().isEmpty()) {
        setStatus(QStringLiteral("请填写用户名"), QStringLiteral("error"));
        m_user->setFocus();
        return;
    }
    if (m_password->text().isEmpty()) {
        setStatus(QStringLiteral("请填写密码"), QStringLiteral("error"));
        m_password->setFocus();
        return;
    }
    if (m_initializing && m_password->text() != m_password2->text()) {
        setStatus(QStringLiteral("两次输入的密码不一致"), QStringLiteral("error"));
        m_password2->setFocus();
        return;
    }

    m_busy = true;
    m_submit->setEnabled(false);
    m_test->setEnabled(false);
    m_submit->setText(m_initializing ? QStringLiteral("正在创建…") : QStringLiteral("正在登录…"));
    // "host:port" 写法覆盖管理端口
    if (m_portOverride > 0) {
        m_config->adminPort = m_portOverride;
    }
    setStatus(QStringLiteral("正在连接 %1:%2 …").arg(host).arg(m_config->apiPort),
              QStringLiteral("muted"));

    m_config->host = host;
    m_api->setBaseUrl(QUrl(m_config->apiUrl()));

    const QString user = m_user->text().trimmed();
    const QString password = m_password->text();

    auto finish = [this, host, user, password]() {
        m_config->host = host;
        m_config->username = user;
        m_config->rememberUsername = m_remember->isChecked();
        m_config->webAutoLogin = m_autoLogin->isChecked() && !m_initializing;
        if (!m_config->rememberUsername) {
            m_config->username.clear();
        }

        SecureVault::setSessionPassword(password);
        if (!m_initializing && m_savePassword->isChecked() && password.size() >= 4) {
            m_config->savedPasswordCipher = SecureVault::protect(password);
        } else if (m_initializing) {
            // 初始化时创建的账号还没被记住，不写密码
            m_config->savedPasswordCipher.clear();
        } else if (m_savePassword->isChecked()) {
            // 密码太短（<4 字符）不落盘，避免生成无法自检的垃圾密文
            m_config->savedPasswordCipher.clear();
            m_config->webAutoLogin = false;
            setStatus(QStringLiteral("密码过短（少于 4 位），本次不保存"), QStringLiteral("error"));
        } else if (!m_config->savedPasswordCipher.trimmed().isEmpty()) {
            m_config->savedPasswordCipher.clear();
            m_config->webAutoLogin = false;
        }

        m_config->normalize();
        m_config->save();

        setStatus(m_initializing ? QStringLiteral("初始化成功，正在进入…")
                                 : QStringLiteral("登录成功，正在进入…"),
                  QStringLiteral("success"));
        QTimer::singleShot(120, this, &QDialog::accept);
    };

    auto onResult = [this, finish](bool ok, const QJsonObject &data, const QString &message) {
        m_busy = false;
        m_submit->setEnabled(true);
        m_test->setEnabled(true);
        m_submit->setText(m_initializing ? QStringLiteral("创建并登录") : QStringLiteral("登录"));

        if (!ok) {
            // 服务端证书指纹变化：确认后信任新证书并自动重试
            if (m_api->hasCertificateChange() && confirmCertificateChange()) {
                submit();
                return;
            }
            setStatus(message.isEmpty() ? QStringLiteral("操作失败") : message,
                      QStringLiteral("error"));
            m_password->selectAll();
            m_password->setFocus();
            return;
        }

        m_token = data.value(QStringLiteral("token")).toString();
        if (m_token.isEmpty()) {
            m_api->clearToken();
            setStatus(QStringLiteral("服务端未返回登录令牌，请检查服务端版本"), QStringLiteral("error"));
            return;
        }
        finish();
    };

    if (m_initializing) {
        m_api->setupInit(user, password, onResult);
    } else {
        m_api->login(user, password, onResult);
    }
}

void LoginDialog::testConnection()
{
    if (m_busy) {
        return;
    }
    const QString host = resolvedHost();
    if (host.isEmpty()) {
        setStatus(QStringLiteral("请先填写服务器地址"), QStringLiteral("error"));
        return;
    }
    m_busy = true;
    m_test->setEnabled(false);
    m_submit->setEnabled(false);
    m_test->setText(QStringLiteral("测试中…"));
    setStatus(QStringLiteral("正在测试 %1:%2 …").arg(host).arg(m_config->apiPort),
              QStringLiteral("muted"));

    const QUrl url(QStringLiteral("https://%1:%2").arg(host).arg(m_config->apiPort));
    m_api->setBaseUrl(url);
    m_api->ping([this](bool ok, const QJsonObject &data, const QString &message) {
        m_busy = false;
        m_test->setEnabled(true);
        m_submit->setEnabled(true);
        m_test->setText(QStringLiteral("测试连接"));
        if (ok) {
            if (!data.value(QStringLiteral("initialized")).toBool(true)) {
                applyInitializingUi(true);
            }
            const QString version = data.value(QStringLiteral("version")).toString();
            const QString hostname = data.value(QStringLiteral("hostname")).toString();
            QString text = QStringLiteral("连接成功");
            if (!hostname.isEmpty()) {
                text += QStringLiteral("，主机 %1").arg(hostname);
            }
            if (!version.isEmpty()) {
                text += QStringLiteral("，服务端 %1").arg(version);
            }
            setStatus(text, QStringLiteral("success"));
        } else {
            if (m_api->hasCertificateChange() && confirmCertificateChange()) {
                testConnection();
                return;
            }
            setStatus(message.isEmpty() ? QStringLiteral("连接失败") : message,
                      QStringLiteral("error"));
        }
    });
}

void LoginDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_backdrop
        && m_backdrop->rect().contains(m_backdrop->mapFrom(this, event->pos()))) {
        QWidget *win = window();
        if (win && win->windowHandle()) {
            win->windowHandle()->startSystemMove();
            event->accept();
            return;
        }
    }
    QDialog::mousePressEvent(event);
}

void LoginDialog::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
}
