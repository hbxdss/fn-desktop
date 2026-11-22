#include "ui/ssh/ssh_workspace.h"

#include "core/secure_vault.h"
#include "ui/ssh_session.h"
#include "ui/ssh/pty_runner.h"
#include "ui/ssh/sftp_panel.h"
#include "ui/ssh/ssh_bridge.h"
#include "ui/ssh/ssh_keys_dialog.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabBar>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <utility>

namespace {

/** 清除 CSI / OSC 控制序列，便于用纯字符串判断提示符和密码提示。 */
QString stripControl(const QString &s)
{
    static const QRegularExpression csi(QStringLiteral("\x1b\\[[0-9;?]*[A-Za-z]"));
    static const QRegularExpression osc(QStringLiteral("\x1b\\][^\x07]*\x07"));
    QString out = s;
    out.remove(csi);
    out.remove(osc);
    return out;
}

/** 最后一个 shell 提示符（user@host: / $ / # / %）所在位置；找不到返回 -1。 */
int findLastShellPrompt(const QString &text)
{
    int last = -1;
    int lineStart = 0;
    while (lineStart <= text.size()) {
        const int nl = text.indexOf(QLatin1Char('\n'), lineStart);
        const int lineEnd = (nl < 0) ? text.size() : nl;
        const QString line = text.mid(lineStart, lineEnd - lineStart).trimmed();
        const int at = line.indexOf(QLatin1Char('@'));
        if (at > 0) {
            for (int j = at + 1; j < line.size(); ++j) {
                const QChar c = line.at(j);
                if (c == QLatin1Char(':') || c == QLatin1Char('$') || c == QLatin1Char('#')
                    || c == QLatin1Char('%') || c == QLatin1Char('>')) {
                    last = lineStart;
                    break;
                }
                if (!(c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('-')
                      || c == QLatin1Char('_') || c == QLatin1Char('/'))) {
                    break;
                }
            }
        }
        if (nl < 0) {
            break;
        }
        lineStart = nl + 1;
    }
    return last;
}

/** 最后一个密码提示（password/passphrase/密码/口令 + 40 字符内冒号）位置；找不到返回 -1。 */
int findLastPasswordHint(const QString &text)
{
    static const QStringList keywords = {
        QStringLiteral("password"), QStringLiteral("Password"),
        QStringLiteral("passphrase"), QStringLiteral("Passphrase"),
        QStringLiteral("密码"), QStringLiteral("口令")
    };
    int last = -1;
    for (const QString &kw : keywords) {
        int from = 0;
        int idx = -1;
        while ((idx = text.indexOf(kw, from)) >= 0) {
            const int colon = text.indexOf(QLatin1Char(':'), idx);
            if (colon >= 0 && colon - idx <= 40) {
                last = qMax(last, idx);
            }
            from = idx + kw.size();
        }
    }
    return last;
}

/** 最近几行里是否有 root 提示符（root@host:...#）。 */
bool hasRootPrompt(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = lines.size() - 1; i >= 0 && i >= lines.size() - 6; --i) {
        const QString line = lines.at(i).trimmed();
        if (line.contains(QStringLiteral("root@")) && line.endsWith(QLatin1Char('#'))) {
            return true;
        }
    }
    return false;
}

/** 终端输出里是否包含连接类错误（以 ssh 自身的报错为准）。 */
bool hasConnectError(const QString &text)
{
    static const QStringList keys = {
        QStringLiteral("Permission denied"),
        QStringLiteral("Connection refused"),
        QStringLiteral("Connection timed out"),
        QStringLiteral("Operation timed out"),
        QStringLiteral("Could not resolve hostname"),
        QStringLiteral("Host key verification failed"),
        QStringLiteral("No route to host"),
    };
    for (const QString &key : keys) {
        if (text.contains(key)) {
            return true;
        }
    }
    return false;
}

/** 终端输出里是否包含 sudo 密码错误。 */
bool hasSudoFailure(const QString &text)
{
    return text.contains(QStringLiteral("Sorry, try again"))
        || text.contains(QStringLiteral("incorrect password"))
        || text.contains(QStringLiteral("认证失败"));
}

/** 转义成 JS 字符串字面量。 */
QString jsStr(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    out.replace(QLatin1Char('"'), QLatin1String("\\\""));
    out.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    out.replace(QLatin1Char('\r'), QLatin1String("\\r"));
    out.replace(QLatin1Char('\t'), QLatin1String("\\t"));
    return QLatin1Char('"') + out + QLatin1Char('"');
}

QString colorHex(const QColor &c)
{
    return c.name(QColor::HexRgb);
}

QString keysDir()
{
    return AppConfig::dataDir() + QStringLiteral("/keys");
}

} // namespace

SshWorkspace::SshWorkspace(AppConfig *config, QWidget *parent)
    : QWidget(parent), m_config(config)
{
    buildUi();
    setStatus(QStringLiteral("就绪"), QStringLiteral("idle"));
    updateTabsAndActions();
}

SshWorkspace::~SshWorkspace()
{
    for (LiveSession *live : std::as_const(m_sessions)) {
        if (live->session) {
            live->session->stop();
        }
        delete live;
    }
    m_sessions.clear();
}

/* ============================ UI ============================ */

void SshWorkspace::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(buildTerminalArea());

    // SFTP 侧栏（复用系统 OpenSSH，连接后自动跟随当前会话）
    m_sftp = new SftpPanel(this);
    m_sftpWrap = m_sftp;
    splitter->addWidget(m_sftpWrap);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({900, 320});
    root->addWidget(splitter);

    const bool showSftp = m_config ? m_config->sshShowSftpSidebar : true;
    m_sftpWrap->setVisible(showSftp);

    // WebChannel 必须在页面脚本执行前注册好
    m_channel = new QWebChannel(this);
    m_bridge = new SshBridge(this);
    m_channel->registerObject(QStringLiteral("bridge"), m_bridge);
    m_view->page()->setWebChannel(m_channel);

    connect(m_bridge, &SshBridge::dataFromJs, this, &SshWorkspace::onBridgeData);
    connect(m_bridge, &SshBridge::resizeRequested, this, &SshWorkspace::onBridgeResize);
    connect(m_bridge, &SshBridge::titleFromJs, this, &SshWorkspace::onBridgeTitle);
    connect(m_bridge, &SshBridge::clipboardSetRequested, this, [](const QString &text) {
        QApplication::clipboard()->setText(text);
    });
    connect(m_bridge, &SshBridge::clipboardRequested, this, [this]() {
        m_bridge->deliverClipboard(QApplication::clipboard()->text());
    });
    connect(m_bridge, &SshBridge::urlRequested, this, &SshWorkspace::openUrlRequested);

    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok) {
            setStatus(QStringLiteral("终端页面加载失败"), QStringLiteral("error"));
            return;
        }
        m_webReady = true;
        runJs(QStringLiteral("sshInit(%1,%2)")
                  .arg(jsStr(themeJson()))
                  .arg(QString::number(m_config ? m_config->sshTermFontSize : 13)));
        pushThemeToJs();
        if (!m_activeId.isEmpty()) {
            activateSession(m_activeId);
        }
    });

    m_view->setUrl(QUrl(QStringLiteral("qrc:/ssh/terminal.html")));
}

QWidget *SshWorkspace::buildTerminalArea()
{
    auto *panel = new QWidget(this);
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // —— 顶部：标签 + 操作按钮 ——
    auto *top = new QWidget(panel);
    top->setObjectName(QStringLiteral("SshToolbar"));
    auto *topLay = new QHBoxLayout(top);
    topLay->setContentsMargins(10, 8, 10, 8);
    topLay->setSpacing(8);

    m_tabs = new QTabBar(top);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setUsesScrollButtons(true);
    topLay->addWidget(m_tabs, 1);

    auto *connectButton = makeButton(QStringLiteral("连接 NAS"), QStringLiteral("primary"), top);
    auto *newSessionButton = makeButton(QStringLiteral("新建会话"), QStringLiteral("ghost"), top);
    auto *keysButton = makeButton(QStringLiteral("密钥"), QStringLiteral("ghost"), top);
    m_loginButton = makeButton(QStringLiteral("一键登录"), QStringLiteral("ghost"), top);
    m_sudoButton = makeButton(QStringLiteral("一键提权"), QStringLiteral("ghost"), top);
    m_searchButton = makeButton(QStringLiteral("搜索"), QStringLiteral("ghost"), top);
    m_clearButton = makeButton(QStringLiteral("清屏"), QStringLiteral("ghost"), top);
    m_sftpButton = makeButton(QStringLiteral("SFTP"), QStringLiteral("ghost"), top);
    m_newWindowButton = makeButton(QStringLiteral("新窗口"), QStringLiteral("ghost"), top);
    for (QPushButton *b : {connectButton, newSessionButton, keysButton, m_loginButton,
                           m_sudoButton, m_searchButton, m_clearButton, m_sftpButton,
                           m_newWindowButton}) {
        b->setMinimumHeight(28);
        topLay->addWidget(b);
    }
    lay->addWidget(top);

    // —— 搜索栏（默认隐藏） ——
    m_searchBar = new QWidget(panel);
    auto *searchLay = new QHBoxLayout(m_searchBar);
    searchLay->setContentsMargins(10, 4, 10, 4);
    searchLay->setSpacing(6);
    m_searchEdit = new QLineEdit(m_searchBar);
    m_searchEdit->setPlaceholderText(QStringLiteral("在终端里搜索…"));
    m_searchEdit->setMinimumHeight(30);
    searchLay->addWidget(m_searchEdit, 1);
    auto *prevButton = makeButton(QStringLiteral("上一个"), QStringLiteral("ghost"), m_searchBar);
    auto *nextButton = makeButton(QStringLiteral("下一个"), QStringLiteral("ghost"), m_searchBar);
    auto *closeSearch = makeButton(QStringLiteral("关闭"), QStringLiteral("ghost"), m_searchBar);
    searchLay->addWidget(prevButton);
    searchLay->addWidget(nextButton);
    searchLay->addWidget(closeSearch);
    m_searchBar->hide();
    lay->addWidget(m_searchBar);

    // —— 终端 ——
    m_view = new QWebEngineView(panel);
    m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_view->page()->setBackgroundColor(QColor(QStringLiteral("#0d1117")));
    m_view->setContextMenuPolicy(Qt::NoContextMenu);
    lay->addWidget(m_view, 1);

    // —— 底部状态栏 ——
    auto *bottom = new QWidget(panel);
    bottom->setObjectName(QStringLiteral("SshStatusBar"));
    auto *bottomLay = new QHBoxLayout(bottom);
    bottomLay->setContentsMargins(12, 4, 12, 6);
    bottomLay->setSpacing(10);
    m_status = makeLabel(QStringLiteral("就绪"), QStringLiteral("muted"), bottom);
    m_sessionInfo = makeLabel(QString(), QStringLiteral("muted"), bottom);
    bottomLay->addWidget(m_status);
    bottomLay->addStretch(1);
    bottomLay->addWidget(m_sessionInfo);
    lay->addWidget(bottom);

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0) {
            return;
        }
        const QString id = m_tabs->tabData(index).toString();
        if (!id.isEmpty() && id != m_activeId) {
            activateSession(id);
        }
    });
    connect(m_tabs, &QTabBar::tabCloseRequested, this, [this](int index) {
        const QString id = m_tabs->tabData(index).toString();
        if (!id.isEmpty()) {
            closeSession(id);
        }
    });
    connect(connectButton, &QPushButton::clicked, this, &SshWorkspace::quickConnectDefault);
    connect(newSessionButton, &QPushButton::clicked, this, [this]() {
        editProfile(QString(), true);
    });
    connect(keysButton, &QPushButton::clicked, this, [this]() {
        SshKeysDialog dialog(this);
        dialog.exec();
    });
    connect(m_loginButton, &QPushButton::clicked, this, &SshWorkspace::quickLogin);
    connect(m_sudoButton, &QPushButton::clicked, this, &SshWorkspace::quickSudo);
    connect(m_clearButton, &QPushButton::clicked, this, [this]() {
        if (!m_activeId.isEmpty()) {
            runJs(QStringLiteral("sshClear(%1)").arg(jsStr(m_activeId)));
        }
    });
    connect(m_sftpButton, &QPushButton::clicked, this, &SshWorkspace::toggleSftp);
    connect(m_newWindowButton, &QPushButton::clicked, this, [this]() {
        emit newWindowRequested();
    });
    connect(m_searchButton, &QPushButton::clicked, this, [this]() {
        const bool show = m_searchBar->isHidden();
        m_searchBar->setVisible(show);
        if (show) {
            m_searchEdit->setFocus();
            m_searchEdit->selectAll();
        } else {
            runJs(QStringLiteral("sshSearch(%1,%2,true)").arg(jsStr(m_activeId), jsStr(QString())));
            if (!m_activeId.isEmpty()) {
                runJs(QStringLiteral("sshFocus(%1)").arg(jsStr(m_activeId)));
            }
        }
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!m_activeId.isEmpty()) {
            runJs(QStringLiteral("sshSearch(%1,%2,true)")
                      .arg(jsStr(m_activeId), jsStr(text)));
        }
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        if (!m_activeId.isEmpty()) {
            runJs(QStringLiteral("sshSearch(%1,%2,true)")
                      .arg(jsStr(m_activeId), jsStr(m_searchEdit->text())));
        }
    });
    connect(prevButton, &QPushButton::clicked, this, [this]() {
        if (!m_activeId.isEmpty()) {
            runJs(QStringLiteral("sshSearch(%1,%2,false)")
                      .arg(jsStr(m_activeId), jsStr(m_searchEdit->text())));
        }
    });
    connect(nextButton, &QPushButton::clicked, this, [this]() {
        if (!m_activeId.isEmpty()) {
            runJs(QStringLiteral("sshSearch(%1,%2,true)")
                      .arg(jsStr(m_activeId), jsStr(m_searchEdit->text())));
        }
    });
    connect(closeSearch, &QPushButton::clicked, m_searchBar, &QWidget::hide);
    return panel;
}

void SshWorkspace::createProfileDialog()
{
    editProfile(QString(), true);
}

void SshWorkspace::connectProfileById(const QString &profileId)
{
    for (const SshProfile &profile : std::as_const(m_config->sshProfiles)) {
        if (profile.id == profileId) {
            connectProfile(profile);
            return;
        }
    }
}

void SshWorkspace::quickConnectDefault()
{
    if (!m_config) {
        return;
    }
    if (!m_config->sshProfiles.isEmpty()) {
        connectProfile(m_config->sshProfiles.first());
        return;
    }
    startSession(m_config->host, m_config->host, m_config->sshPort, m_config->sshUser,
                 QString(), QString());
}

void SshWorkspace::connectProfile(const SshProfile &profile)
{
    QString password;
    if (!profile.passwordCipher.trimmed().isEmpty()) {
        password = SecureVault::unprotect(profile.passwordCipher);
    }
    const QString title = profile.name.trimmed().isEmpty()
                              ? QStringLiteral("%1@%2").arg(profile.user, profile.host)
                              : profile.name.trimmed();
    startSession(title, profile.host, profile.port, profile.user, profile.keyPath, password);
}

void SshWorkspace::editProfile(const QString &profileId, bool isNew)
{
    if (!m_config) {
        return;
    }
    SshProfile profile;
    if (!isNew) {
        bool found = false;
        for (const SshProfile &p : std::as_const(m_config->sshProfiles)) {
            if (p.id == profileId) {
                profile = p;
                found = true;
                break;
            }
        }
        if (!found) {
            return;
        }
    } else {
        profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        profile.host = m_config->host;
        profile.port = m_config->sshPort;
        profile.user = m_config->sshUser.isEmpty() ? m_config->username : m_config->sshUser;
        profile.name = QStringLiteral("新会话");
    }

    DialogWindow dialog(this);
    dialog.setWindowTitle(isNew ? QStringLiteral("新建 SSH 会话") : QStringLiteral("编辑 SSH 会话"));
    dialog.setMinimumWidth(480);
    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(22, 20, 22, 18);
    outer->setSpacing(12);
    outer->addWidget(makeLabel(isNew ? QStringLiteral("新建 SSH 会话")
                                     : QStringLiteral("编辑 SSH 会话"),
                               QStringLiteral("h2"), &dialog));

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

    auto *nameEdit = new QLineEdit(profile.name, &dialog);
    nameEdit->setPlaceholderText(QStringLiteral("例如：NAS 主机"));
    auto *groupEdit = new QLineEdit(profile.group, &dialog);
    groupEdit->setPlaceholderText(QStringLiteral("可选，例如：家里 / 公司"));
    auto *hostEdit = new QLineEdit(profile.host, &dialog);
    hostEdit->setPlaceholderText(QStringLiteral("192.168.1.100"));
    auto *portBox = new QSpinBox(&dialog);
    portBox->setRange(1, 65535);
    portBox->setValue(profile.port > 0 ? profile.port : 22);
    auto *userEdit = new QLineEdit(profile.user, &dialog);
    userEdit->setPlaceholderText(QStringLiteral("登录用户名"));

    auto *keyRow = new QWidget(&dialog);
    auto *keyLay = new QHBoxLayout(keyRow);
    keyLay->setContentsMargins(0, 0, 0, 0);
    keyLay->setSpacing(6);
    auto *keyBox = new QComboBox(keyRow);
    keyBox->addItem(QStringLiteral("不使用密钥（密码登录）"), QString());
    QDir kd(keysDir());
    if (kd.exists()) {
        const QFileInfoList files = kd.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files) {
            if (fi.fileName().endsWith(QLatin1String(".pub"))) {
                continue;
            }
            keyBox->addItem(fi.fileName(), fi.absoluteFilePath());
        }
    }
    if (!profile.keyPath.isEmpty()) {
        int idx = keyBox->findData(profile.keyPath);
        if (idx < 0) {
            keyBox->addItem(QDir(profile.keyPath).dirName(), profile.keyPath);
            idx = keyBox->count() - 1;
        }
        keyBox->setCurrentIndex(idx);
    }
    auto *browseKey = makeButton(QStringLiteral("浏览…"), QStringLiteral("ghost"), keyRow);
    keyLay->addWidget(keyBox, 1);
    keyLay->addWidget(browseKey);

    auto *passwordEdit = new QLineEdit(&dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setPlaceholderText(QStringLiteral("可选；不填则连接后在终端里输入"));
    if (!profile.passwordCipher.trimmed().isEmpty()) {
        passwordEdit->setText(SecureVault::unprotect(profile.passwordCipher));
    }
    auto *savePassword = new QCheckBox(QStringLiteral("保存密码（系统加密存储）"), &dialog);
    savePassword->setChecked(!profile.passwordCipher.trimmed().isEmpty());

    form->addRow(QStringLiteral("名称"), nameEdit);
    form->addRow(QStringLiteral("分组"), groupEdit);
    form->addRow(QStringLiteral("主机"), hostEdit);
    form->addRow(QStringLiteral("端口"), portBox);
    form->addRow(QStringLiteral("用户名"), userEdit);
    form->addRow(QStringLiteral("私钥"), keyRow);
    form->addRow(QStringLiteral("密码"), passwordEdit);
    form->addRow(QString(), savePassword);
    outer->addLayout(form);

    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(0, 4, 0, 0);
    footer->setSpacing(8);
    footer->addStretch(1);
    auto *cancel = makeButton(QStringLiteral("取消"), QStringLiteral("ghost"), &dialog);
    auto *ok = makeButton(QStringLiteral("保存"),
                          isNew ? QStringLiteral("primary") : QStringLiteral("primary"), &dialog);
    footer->addWidget(cancel);
    footer->addWidget(ok);
    outer->addLayout(footer);

    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(browseKey, &QPushButton::clicked, &dialog, [&]() {
        const QString file = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("选择私钥文件"), QDir::homePath());
        if (!file.isEmpty()) {
            int idx = keyBox->findData(file);
            if (idx < 0) {
                keyBox->addItem(QFileInfo(file).fileName(), file);
                idx = keyBox->count() - 1;
            }
            keyBox->setCurrentIndex(idx);
        }
    });

    QPointer<SshWorkspace> guard(this);
    connect(ok, &QPushButton::clicked, &dialog, [&]() {
        const QString host = hostEdit->text().trimmed();
        if (host.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("缺少主机"),
                                 QStringLiteral("请填写 NAS 地址或主机名。"));
            hostEdit->setFocus();
            return;
        }
        SshProfile result = profile;
        result.name = nameEdit->text().trimmed();
        result.group = groupEdit->text().trimmed();
        result.host = host;
        result.port = portBox->value();
        result.user = userEdit->text().trimmed();
        result.keyPath = keyBox->currentData().toString();
        const QString pwd = passwordEdit->text();
        result.passwordCipher = (savePassword->isChecked() && !pwd.isEmpty())
                                    ? SecureVault::protect(pwd)
                                    : QString();
        if (!guard) {
            return;
        }
        bool updated = false;
        for (SshProfile &p : m_config->sshProfiles) {
            if (p.id == result.id) {
                p = result;
                updated = true;
                break;
            }
        }
        if (!updated) {
            m_config->sshProfiles.append(result);
        }
        m_config->save();
        dialog.accept();
    });
    dialog.exec();
}

void SshWorkspace::deleteProfile(const QString &profileId)
{
    if (!m_config) {
        return;
    }
    auto *box = new QMessageBox(this);
    box->setWindowTitle(QStringLiteral("删除会话"));
    box->setIcon(QMessageBox::Question);
    box->setText(QStringLiteral("确定删除这个会话书签吗？（不会影响远端数据）"));
    box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box->setDefaultButton(QMessageBox::No);
    if (box->exec() != QMessageBox::Yes) {
        box->deleteLater();
        return;
    }
    box->deleteLater();
    for (int i = 0; i < m_config->sshProfiles.size(); ++i) {
        if (m_config->sshProfiles.at(i).id == profileId) {
            m_config->sshProfiles.removeAt(i);
            break;
        }
    }
    m_config->save();
}

/* ============================ 会话生命周期 ============================ */

void SshWorkspace::startSession(const QString &title, const QString &host, int port,
                                const QString &user, const QString &keyPath,
                                const QString &password)
{
    if (!m_webReady) {
        setStatus(QStringLiteral("终端还没准备好，请稍后再试"), QStringLiteral("error"));
        return;
    }
    const QString id = QStringLiteral("s%1").arg(++m_sessionSeed);
    auto *live = new LiveSession;
    live->title = title.trimmed().isEmpty() ? host : title.trimmed();
    live->host = host;
    live->port = port;
    live->user = user;
    live->keyPath = keyPath;
    live->password = password;
    live->session = new SshSession(this);
    m_sessions.insert(id, live);

    jsCreate(id);

    const int index = m_tabs->addTab(live->title);
    m_tabs->setTabData(index, id);
    m_tabs->setTabToolTip(index, QStringLiteral("%1@%2:%3").arg(user, host).arg(port));

    connect(live->session, &SshSession::output, this,
            [this, id](const QByteArray &data) { onSessionOutput(id, data); });
    connect(live->session, &SshSession::finished, this,
            [this, id](int code) { onSessionFinished(id, code); });

    m_tabs->setCurrentIndex(index);
    activateSession(id);

    if (!live->session->start(host, quint16(port), user, keyPath)) {
        jsWrite(id, QStringLiteral("无法启动内置 SSH 会话：请确认系统已安装 OpenSSH 客户端。\r\n").toUtf8());
        setStatus(QStringLiteral("连接失败"), QStringLiteral("error"));
        return;
    }
    live->connected = true;
    setStatus(QStringLiteral("正在连接 %1…").arg(host), QStringLiteral("busy"));
    updateTabsAndActions();

    if (m_sftp) {
        QString pwd = live->password;
        if (pwd.isEmpty() && m_config && !m_config->savedPasswordCipher.trimmed().isEmpty()) {
            pwd = SecureVault::unprotect(m_config->savedPasswordCipher);
        }
        m_sftp->setTarget(host, port, user, keyPath, pwd);
    }
}

void SshWorkspace::closeSession(const QString &id)
{
    LiveSession *live = m_sessions.value(id, nullptr);
    if (!live) {
        return;
    }
    if (live->session) {
        live->session->stop();
        live->session->deleteLater();
    }
    runJs(QStringLiteral("sshClose(%1)").arg(jsStr(id)));
    for (int i = m_tabs->count() - 1; i >= 0; --i) {
        if (m_tabs->tabData(i).toString() == id) {
            m_tabs->removeTab(i);
            break;
        }
    }
    m_sessions.remove(id);
    delete live;
    if (m_activeId == id) {
        m_activeId.clear();
        if (m_tabs->count() > 0) {
            const int last = m_tabs->currentIndex() >= 0 ? m_tabs->currentIndex() : 0;
            const QString next = m_tabs->tabData(last).toString();
            if (!next.isEmpty()) {
                activateSession(next);
            }
        }
    }
    updateTabsAndActions();
}

void SshWorkspace::activateSession(const QString &id)
{
    LiveSession *live = sessionById(id);
    if (!live) {
        return;
    }
    m_activeId = id;
    runJs(QStringLiteral("sshActivate(%1)").arg(jsStr(id)));
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->tabData(i).toString() == id) {
            if (m_tabs->currentIndex() != i) {
                m_tabs->setCurrentIndex(i);
            }
            break;
        }
    }
    updateTabsAndActions();
    emit activeTitleChanged(live->title);
}

void SshWorkspace::onSessionOutput(const QString &id, const QByteArray &data)
{
    jsWrite(id, data);
    scanOutput(id, data);
}

void SshWorkspace::onSessionFinished(const QString &id, int code)
{
    LiveSession *live = sessionById(id);
    if (!live) {
        return;
    }
    live->connected = false;
    jsWrite(id, QStringLiteral("\r\n[会话已结束，退出码 %1]\r\n").arg(code).toUtf8());
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->tabData(i).toString() == id) {
            m_tabs->setTabText(i, live->title + QStringLiteral(" (已断开)"));
            break;
        }
    }
    if (!live->promptSeen) {
        live->connectError = true;
        setStatus(QStringLiteral("连接失败（退出码 %1），请查看终端输出").arg(code),
                  QStringLiteral("error"));
    } else {
        setStatus(QStringLiteral("会话已结束"), QStringLiteral("idle"));
    }
    updateTabsAndActions();
}

void SshWorkspace::scanOutput(const QString &id, const QByteArray &data)
{
    LiveSession *live = sessionById(id);
    if (!live) {
        return;
    }
    live->scanBuffer += QString::fromUtf8(data);
    if (live->scanBuffer.size() > 20000) {
        live->scanBuffer.remove(0, live->scanBuffer.size() - 20000);
    }
    const QString clean = stripControl(live->scanBuffer);
    const QString tail = clean.right(800);
    const int lastPwd = findLastPasswordHint(tail);
    const int lastShell = findLastShellPrompt(tail);

    bool prompt = false;
    if (lastShell >= 0 && lastShell > lastPwd) {
        prompt = true;
    } else if (lastPwd < 0 && lastShell < 0) {
        const QString trimmed = clean.trimmed();
        prompt = trimmed.endsWith(QLatin1Char('#')) || trimmed.endsWith(QLatin1Char('$'))
                 || trimmed.endsWith(QLatin1Char('%'));
    }
    // 连接失败以 ssh 自身的报错为准
    if (!live->promptSeen && !live->connectError && hasConnectError(tail)) {
        live->connectError = true;
        setStatus(QStringLiteral("连接失败，请查看终端中的错误信息"), QStringLiteral("error"));
        updateTabsAndActions();
    }

    // 只有检测到 sudo 的密码提示时才回填密码，密码不会出现在普通命令行里。
    if (live->sudoPending && lastPwd >= 0 && lastPwd > lastShell) {
        live->sudoPending = false;
        QString pwd = live->password;
        if (pwd.isEmpty() && m_config && !m_config->savedPasswordCipher.trimmed().isEmpty()) {
            pwd = SecureVault::unprotect(m_config->savedPasswordCipher);
        }
        if (!pwd.isEmpty() && live->session && live->session->isRunning()) {
            live->session->write(pwd.toUtf8() + '\r');
            live->sudoSubmitted = true;
            setStatus(QStringLiteral("正在验证 sudo 密码…"), QStringLiteral("busy"));
        } else {
            setStatus(QStringLiteral("没有可用密码，请手动输入"), QStringLiteral("warning"));
        }
    }

    // 提权结果同样以终端反馈为准：失败提示或 root 提示符
    if (live->sudoSubmitted) {
        if (hasSudoFailure(tail)) {
            live->sudoSubmitted = false;
            setStatus(QStringLiteral("sudo 密码错误，请手动输入"), QStringLiteral("error"));
        } else if (hasRootPrompt(tail)) {
            live->sudoSubmitted = false;
            setStatus(QStringLiteral("已提权为 root"), QStringLiteral("ok"));
            updateTabsAndActions();
        }
    }

    if (prompt && !live->promptSeen) {
        live->promptSeen = true;
        if (!live->connectError) {
            setStatus(QStringLiteral("已连接 · 可以直接输入命令"), QStringLiteral("ok"));
        }
        updateTabsAndActions();
    }
}

/* ============================ 桥接 ============================ */

void SshWorkspace::onBridgeData(const QString &id, const QByteArray &data)
{
    LiveSession *live = sessionById(id);
    if (live && live->session) {
        live->session->write(data);
    }
}

void SshWorkspace::onBridgeResize(const QString &id, int cols, int rows)
{
    LiveSession *live = sessionById(id);
    if (live && live->session) {
        live->session->resize(quint16(cols), quint16(rows));
    }
}

void SshWorkspace::onBridgeTitle(const QString &id, const QString &title)
{
    LiveSession *live = sessionById(id);
    if (!live) {
        return;
    }
    const QString t = title.trimmed();
    // ssh.exe 会把自身路径塞进窗口标题（C:\Windows\System32\OpenSSH\ssh.exe），
    // 这种标题没有信息量，忽略它，标签继续用会话名。
    if (t.isEmpty() || t.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)
        || t.contains(QLatin1String("OpenSSH"), Qt::CaseInsensitive)) {
        return;
    }
    live->title = t.left(40);
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->tabData(i).toString() == id) {
            m_tabs->setTabText(i, live->title);
            break;
        }
    }
}

void SshWorkspace::runJs(const QString &script)
{
    if (!m_view) {
        return;
    }
    m_view->page()->runJavaScript(script);
}

void SshWorkspace::jsCreate(const QString &id)
{
    runJs(QStringLiteral("sshCreate(%1,%2,%3)")
              .arg(jsStr(id), jsStr(themeJson()))
              .arg(QString::number(m_config ? m_config->sshTermFontSize : 13)));
}

void SshWorkspace::jsWrite(const QString &id, const QByteArray &data)
{
    if (!m_webReady || data.isEmpty()) {
        return;
    }
    const QString b64 = QString::fromLatin1(data.toBase64());
    runJs(QStringLiteral("sshWriteB64(%1,%2)").arg(jsStr(id), jsStr(b64)));
}

QString SshWorkspace::themeJson() const
{
    const Theme::Palette p = Theme::current();
    QJsonObject o;
    o.insert(QStringLiteral("background"),
             p.light ? QStringLiteral("#ffffff") : QStringLiteral("#0d1117"));
    o.insert(QStringLiteral("foreground"),
             p.light ? QStringLiteral("#1f2328") : QStringLiteral("#c9d1d9"));
    o.insert(QStringLiteral("cursor"), colorHex(p.accent));
    o.insert(QStringLiteral("selection"),
             p.light ? QStringLiteral("rgba(47,129,247,0.22)")
                     : QStringLiteral("rgba(88,166,255,0.30)"));
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void SshWorkspace::pushThemeToJs()
{
    if (!m_webReady) {
        return;
    }
    runJs(QStringLiteral("sshSetTheme(%1)").arg(jsStr(themeJson())));
    if (m_config) {
        runJs(QStringLiteral("sshSetFontSize(%1)").arg(QString::number(m_config->sshTermFontSize)));
    }
}

/* ============================ 操作 ============================ */

QString SshWorkspace::activeId() const
{
    return m_activeId;
}

SshWorkspace::LiveSession *SshWorkspace::activeSession() const
{
    return m_sessions.value(m_activeId, nullptr);
}

SshWorkspace::LiveSession *SshWorkspace::sessionById(const QString &id) const
{
    return m_sessions.value(id, nullptr);
}

void SshWorkspace::setStatus(const QString &text, const QString &tone)
{
    if (!m_status) {
        return;
    }
    m_status->setText(text);
    m_status->setProperty("tone", tone);
    Theme::repolish(m_status);
}

void SshWorkspace::updateTabsAndActions()
{
    LiveSession *live = activeSession();
    const bool on = live && live->session && live->session->isRunning();
    const bool hasPwd = (live && !live->password.isEmpty())
                        || (m_config && !m_config->savedPasswordCipher.trimmed().isEmpty());
    if (m_loginButton) {
        m_loginButton->setEnabled(on && hasPwd && !(live && live->promptSeen));
        m_loginButton->setToolTip(live && live->promptSeen
                                      ? QStringLiteral("已登录，无需再发送密码")
                                      : QStringLiteral("把保存的密码发送到当前会话（远端等待密码时使用）"));
    }
    if (m_sudoButton) {
        m_sudoButton->setEnabled(on);
    }
    if (m_clearButton) {
        m_clearButton->setEnabled(!m_activeId.isEmpty());
    }
    if (m_searchButton) {
        m_searchButton->setEnabled(!m_activeId.isEmpty());
    }
    if (m_sessionInfo) {
        if (live) {
            m_sessionInfo->setText(QStringLiteral("%1@%2:%3")
                                       .arg(live->user, live->host)
                                       .arg(live->port));
        } else {
            m_sessionInfo->clear();
        }
    }
}

void SshWorkspace::quickLogin()
{
    LiveSession *live = activeSession();
    if (!live || !live->session || !live->session->isRunning()) {
        setStatus(QStringLiteral("当前没有已连接的会话"), QStringLiteral("warning"));
        return;
    }
    // 内置会话使用 SSH 密钥认证，登录阶段不需要交互式密码。
    setStatus(QStringLiteral("会话使用密钥认证，无需输入密码"), QStringLiteral("ok"));
}

void SshWorkspace::quickSudo()
{
    LiveSession *live = activeSession();
    if (!live || !live->session || !live->session->isRunning()) {
        setStatus(QStringLiteral("当前没有已连接的会话"), QStringLiteral("warning"));
        return;
    }
    // 不在这里直接发送密码：密码只会在真正出现 sudo 密码提示时回填，
    // 避免 sudo 免密（cookie 有效）时把密码当成命令明文写进终端。
    live->scanBuffer.clear();
    live->sudoPending = true;
    live->session->write(QByteArrayLiteral("sudo -i\r"));
    setStatus(QStringLiteral("已请求 sudo 提权，等待密码提示…"), QStringLiteral("busy"));

    const QString id = m_activeId;
    QTimer::singleShot(30000, this, [this, id]() {
        LiveSession *l = sessionById(id);
        if (l && l->sudoPending) {
            l->sudoPending = false;
            setStatus(QStringLiteral("未检测到 sudo 密码提示，请手动输入"), QStringLiteral("idle"));
        }
    });
}

void SshWorkspace::toggleSftp()
{
    if (!m_sftpWrap) {
        return;
    }
    const bool show = m_sftpWrap->isHidden();
    m_sftpWrap->setVisible(show);
    if (m_config) {
        m_config->sshShowSftpSidebar = show;
        m_config->save();
    }
    if (show && m_sftp) {
        LiveSession *live = activeSession();
        if (live) {
            QString pwd = live->password;
            if (pwd.isEmpty() && m_config && !m_config->savedPasswordCipher.trimmed().isEmpty()) {
                pwd = SecureVault::unprotect(m_config->savedPasswordCipher);
            }
            m_sftp->setTarget(live->host, live->port, live->user, live->keyPath, pwd);
        }
    }
}

bool SshWorkspace::sftpVisible() const
{
    return m_sftpWrap && m_sftpWrap->isVisible();
}

void SshWorkspace::setSftpVisible(bool on)
{
    if (m_sftpWrap) {
        m_sftpWrap->setVisible(on);
    }
}

void SshWorkspace::reloadTheme()
{
    pushThemeToJs();
    if (m_sftp) {
        m_sftp->reloadTheme();
    }
    setStatus(m_status ? m_status->text() : QStringLiteral("就绪"), QStringLiteral("idle"));
}
