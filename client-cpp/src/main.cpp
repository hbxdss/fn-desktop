#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QPointer>
#include <QStyleFactory>
#include <QTranslator>

#include "core/api_client.h"
#include "core/app_logger.h"
#include "core/config.h"
#include "core/secure_vault.h"
#include "ui/login_dialog.h"
#include "ui/main_window.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
    // QWebEngine 需要与主窗口共享 OpenGL 上下文，必须在创建 QApplication 之前设置。
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    // 部分机器 GPU 合成 WebEngine 会渲染成黑块，禁用 GPU 加速走软件合成（工具类应用性能足够）。
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            QByteArrayLiteral("--disable-gpu --disable-gpu-compositing"));
#ifdef Q_OS_WIN
    // 控制台日志输出统一为 UTF-8，避免 qWarning 的中文在 GBK 控制台下乱码。
    SetConsoleOutputCP(CP_UTF8);
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("FnDesktopPlus"));
    app.setApplicationDisplayName(QStringLiteral("飞牛终端"));
    app.setOrganizationName(QStringLiteral("FNOS"));
    app.setApplicationVersion(QStringLiteral("1.2.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/app.png")));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    AppLogger::install();
    qInfo() << "应用启动 版本 1.2.0";

    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPointSizeF(9.5);
    app.setFont(font);

    // 加载 Qt 内置控件（QMessageBox / QInputDialog 等）的中文翻译，
    // 优先使用随程序分发的 translations 目录，其次回退到 Qt 安装目录。
    QTranslator qtTranslator;
    const QStringList translationDirs = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
        QLibraryInfo::path(QLibraryInfo::TranslationsPath)
    };
    for (const QString &dir : translationDirs) {
        if (qtTranslator.load(QStringLiteral("qtbase_zh_CN"), dir)) {
            app.installTranslator(&qtTranslator);
            break;
        }
    }

    AppConfig config;
    config.load();
    config.normalize();

    // 自检：已保存密码若解密失败或短得可疑（<4 字符），视为损坏数据，自动清除。
    // 避免 SSH「一键登录」/后台自动填充拿到垃圾值。下次登录重新勾选「保存密码」即可恢复。
    if (!config.savedPasswordCipher.trimmed().isEmpty()) {
        const QString probe = SecureVault::unprotect(config.savedPasswordCipher);
        if (probe.isEmpty() || probe.size() < 4) {
            qWarning() << "已保存密码数据损坏（解密结果"
                       << probe.size() << "字符），已自动清除";
            config.savedPasswordCipher.clear();
            config.webAutoLogin = false;
            config.save();
        }
    }

    Theme::apply(Theme::idFromString(config.uiTheme),
                 config.colorMode == QLatin1String("light"), config.accent);
    installRoundedToolTips();

    ApiClient api;
    api.setConfig(&config);

    for (;;) {
        LoginDialog login(&api, &config);
        if (login.exec() != QDialog::Accepted) {
            return 0;
        }

        bool relogin = false;
        auto *window = new MainWindow(&api, &config);
        window->setAttribute(Qt::WA_DeleteOnClose, true);
        QPointer<MainWindow> guard(window);
        QObject::connect(window, &MainWindow::logoutRequested, window, [&relogin, guard]() {
            relogin = true;
            if (guard) {
                guard->close();
            }
        });
        window->show();

        app.exec();

        if (!relogin) {
            return 0;
        }
        SecureVault::clearSessionPassword();
        api.clearToken();
    }
}
