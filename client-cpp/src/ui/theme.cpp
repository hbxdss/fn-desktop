#include "ui/theme.h"

#include "core/config.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QStyle>
#include <QWidget>

#include <cmath>

namespace {

QString css(const QColor &c)
{
    if (c.alpha() >= 255) {
        return c.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(QString::number(c.alphaF(), 'f', 3));
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    const double k = qBound(0.0, t, 1.0);
    const int r = int(std::lround(a.red() * (1.0 - k) + b.red() * k));
    const int g = int(std::lround(a.green() * (1.0 - k) + b.green() * k));
    const int b2 = int(std::lround(a.blue() * (1.0 - k) + b.blue() * k));
    const int al = int(std::lround(a.alpha() * (1.0 - k) + b.alpha() * k));
    return QColor(r, g, b2, al);
}

void fillWin7Light(Theme::Palette &p)
{
    p.light = true;
    p.radius = 6;
    p.radiusSm = 4;
    p.window = QColor("#dfeaf5");
    p.surface = QColor("#ffffff");
    p.surfaceAlt = QColor("#eff5fb");
    p.border = QColor("#a8c1da");
    p.text = QColor("#16283a");
    p.muted = QColor("#5a7288");
    p.accent = QColor("#3f8ed6");
    p.accentText = QColor("#ffffff");
    p.danger = QColor("#c2392c");
    p.success = QColor("#2c8a4b");
    p.warning = QColor("#b9791b");
    p.sidebar = QColor("#e9f3fc");
    p.sidebarText = QColor("#1d3550");
    p.titleBar = QColor("#e9f3fc");
    p.titleText = QColor("#12324e");
    p.hover = QColor("#d5e7f7");
    p.inputBg = QColor("#ffffff");
}

void fillWin7Dark(Theme::Palette &p)
{
    p.light = false;
    p.radius = 6;
    p.radiusSm = 4;
    p.window = QColor("#0d1620");
    p.surface = QColor("#16222e");
    p.surfaceAlt = QColor("#111c26");
    p.border = QColor("#2b3d4f");
    p.text = QColor("#dbe8f4");
    p.muted = QColor("#8ba3b8");
    p.accent = QColor("#4c9be0");
    p.accentText = QColor("#04121d");
    p.danger = QColor("#e05c4b");
    p.success = QColor("#43c07a");
    p.warning = QColor("#e0a641");
    p.sidebar = QColor("#101d29");
    p.sidebarText = QColor("#c3d6e6");
    p.titleBar = QColor("#13202c");
    p.titleText = QColor("#dceaf6");
    p.hover = QColor("#1e3345");
    p.inputBg = QColor("#0f1a24");
}

void fillWin10Light(Theme::Palette &p)
{
    p.light = true;
    p.radius = 3;
    p.radiusSm = 2;
    p.window = QColor("#f3f3f3");
    p.surface = QColor("#ffffff");
    p.surfaceAlt = QColor("#f7f7f7");
    p.border = QColor("#d6d6d6");
    p.text = QColor("#1b1b1b");
    p.muted = QColor("#6b6b6b");
    p.accent = QColor("#0078d4");
    p.accentText = QColor("#ffffff");
    p.danger = QColor("#c42b1c");
    p.success = QColor("#107c10");
    p.warning = QColor("#9d5d00");
    p.sidebar = QColor("#f3f3f3");
    p.sidebarText = QColor("#2b2b2b");
    p.titleBar = QColor("#f3f3f3");
    p.titleText = QColor("#1b1b1b");
    p.hover = QColor("#e6e6e6");
    p.inputBg = QColor("#ffffff");
}

void fillWin10Dark(Theme::Palette &p)
{
    p.light = false;
    p.radius = 3;
    p.radiusSm = 2;
    p.window = QColor("#1f1f1f");
    p.surface = QColor("#2b2b2b");
    p.surfaceAlt = QColor("#252525");
    p.border = QColor("#3a3a3a");
    p.text = QColor("#f0f0f0");
    p.muted = QColor("#a0a0a0");
    p.accent = QColor("#4cc2ff");
    p.accentText = QColor("#04121d");
    p.danger = QColor("#ff99a4");
    p.success = QColor("#6ccb5f");
    p.warning = QColor("#fce100");
    p.sidebar = QColor("#202020");
    p.sidebarText = QColor("#e4e4e4");
    p.titleBar = QColor("#1f1f1f");
    p.titleText = QColor("#f0f0f0");
    p.hover = QColor("#333333");
    p.inputBg = QColor("#1f1f1f");
}

void fillWin11Light(Theme::Palette &p)
{
    p.light = true;
    p.radius = 10;
    p.radiusSm = 7;
    p.window = QColor("#f3f3f3");
    p.surface = QColor("#ffffff");
    p.surfaceAlt = QColor("#f7f7f8");
    p.border = QColor("#e3e3e5");
    p.text = QColor("#1a1a1a");
    p.muted = QColor("#767676");
    p.accent = QColor("#005fb8");
    p.accentText = QColor("#ffffff");
    p.danger = QColor("#c42b1c");
    p.success = QColor("#0f7b0f");
    p.warning = QColor("#9d5d00");
    p.sidebar = QColor("#eeeeef");
    p.sidebarText = QColor("#262626");
    p.titleBar = QColor("#f3f3f3");
    p.titleText = QColor("#1a1a1a");
    p.hover = QColor("#e8e8ea");
    p.inputBg = QColor("#ffffff");
}

void fillWin11Dark(Theme::Palette &p)
{
    p.light = false;
    p.radius = 10;
    p.radiusSm = 7;
    p.window = QColor("#202020");
    p.surface = QColor("#2b2b2b");
    p.surfaceAlt = QColor("#2f2f2f");
    p.border = QColor("#3b3b3b");
    p.text = QColor("#f2f2f2");
    p.muted = QColor("#a3a3a3");
    p.accent = QColor("#4cc2ff");
    p.accentText = QColor("#04121d");
    p.danger = QColor("#ff99a4");
    p.success = QColor("#6ccb5f");
    p.warning = QColor("#fce100");
    p.sidebar = QColor("#262626");
    p.sidebarText = QColor("#eeeeee");
    p.titleBar = QColor("#202020");
    p.titleText = QColor("#f2f2f2");
    p.hover = QColor("#343434");
    p.inputBg = QColor("#1f1f1f");
}

void fillGlassLight(Theme::Palette &p)
{
    p.light = true;
    p.glass = true;
    p.radius = 18;
    p.radiusSm = 12;
    p.window = QColor(238, 244, 252, 210);
    p.surface = QColor(255, 255, 255, 168);
    p.surfaceAlt = QColor(255, 255, 255, 110);
    p.border = QColor(255, 255, 255, 200);
    p.text = QColor("#14202e");
    p.muted = QColor("#4d6274");
    p.accent = QColor("#2f7bf6");
    p.accentText = QColor("#ffffff");
    p.danger = QColor("#d13b2f");
    p.success = QColor("#178a4d");
    p.warning = QColor("#b06a10");
    p.sidebar = QColor(255, 255, 255, 96);
    p.sidebarText = QColor("#1a2a3a");
    p.titleBar = QColor(255, 255, 255, 90);
    p.titleText = QColor("#16273a");
    p.hover = QColor(255, 255, 255, 150);
    p.inputBg = QColor(255, 255, 255, 170);
}

void fillGlassDark(Theme::Palette &p)
{
    p.light = false;
    p.glass = true;
    p.radius = 18;
    p.radiusSm = 12;
    p.window = QColor(11, 17, 28, 225);
    p.surface = QColor(255, 255, 255, 20);
    p.surfaceAlt = QColor(255, 255, 255, 12);
    p.border = QColor(255, 255, 255, 36);
    p.text = QColor("#e9f2ff");
    p.muted = QColor("#93a8c0");
    p.accent = QColor("#6cb8ff");
    p.accentText = QColor("#04121d");
    p.danger = QColor("#ff8b80");
    p.success = QColor("#6ee7a8");
    p.warning = QColor("#ffd166");
    p.sidebar = QColor(255, 255, 255, 14);
    p.sidebarText = QColor("#d8e6f6");
    p.titleBar = QColor(255, 255, 255, 10);
    p.titleText = QColor("#eaf3ff");
    p.hover = QColor(255, 255, 255, 30);
    p.inputBg = QColor(255, 255, 255, 16);
}

bool isValidHex(const QString &hex)
{
    if (hex.size() != 7 || !hex.startsWith(QLatin1Char('#'))) {
        return false;
    }
    for (int i = 1; i < hex.size(); ++i) {
        if (!hex.at(i).isDigit() && (hex.at(i).toLower() < QLatin1Char('a') || hex.at(i).toLower() > QLatin1Char('f'))) {
            return false;
        }
    }
    return true;
}

void writeChevron(const QString &file, const QColor &color, bool up)
{
    QImage img(32, 32, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color);
    pen.setWidthF(3.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    if (up) {
        p.drawLine(QPointF(8, 20), QPointF(16, 12));
        p.drawLine(QPointF(16, 12), QPointF(24, 20));
    } else {
        p.drawLine(QPointF(8, 12), QPointF(16, 20));
        p.drawLine(QPointF(16, 20), QPointF(24, 12));
    }
    p.end();
    img.save(file, "PNG");
}

void writeCheck(const QString &file, const QColor &color)
{
    QImage img(32, 32, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color);
    pen.setWidthF(4.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(QPointF(7, 17), QPointF(13, 23));
    p.drawLine(QPointF(13, 23), QPointF(25, 9));
    p.end();
    img.save(file, "PNG");
}

} // namespace

namespace Theme {

Id idFromString(const QString &value)
{
    const QString v = value.trimmed().toLower();
    if (v == QLatin1String("win7") || v == QLatin1String("aero")) {
        return Id::Win7;
    }
    if (v == QLatin1String("win10")) {
        return Id::Win10;
    }
    if (v == QLatin1String("glass") || v == QLatin1String("liquid")) {
        return Id::Glass;
    }
    return Id::Win11;
}

QString toString(Id id)
{
    switch (id) {
    case Id::Win7:
        return QStringLiteral("win7");
    case Id::Win10:
        return QStringLiteral("win10");
    case Id::Glass:
        return QStringLiteral("glass");
    case Id::Win11:
        break;
    }
    return QStringLiteral("win11");
}

QString label(Id id)
{
    switch (id) {
    case Id::Win7:
        return QStringLiteral("Win7 经典");
    case Id::Win10:
        return QStringLiteral("Win10 简约");
    case Id::Glass:
        return QStringLiteral("玻璃拟态");
    case Id::Win11:
        break;
    }
    return QStringLiteral("Win11 现代");
}

QString description(Id id)
{
    switch (id) {
    case Id::Win7:
        return QStringLiteral("半透明毛玻璃、小圆角、经典蓝渐变");
    case Id::Win10:
        return QStringLiteral("扁平方正、直角卡片、清爽利落");
    case Id::Glass:
        return QStringLiteral("多层折射光斑、加厚圆角、弥散高光");
    case Id::Win11:
        break;
    }
    return QStringLiteral("云母材质、圆角卡片、居中柔和");
}

Palette palette(Id id, bool light, const QString &accentHex)
{
    Palette p;
    p.fontFamily = QStringLiteral("Microsoft YaHei UI");

    switch (id) {
    case Id::Win7:
        if (light) {
            fillWin7Light(p);
        } else {
            fillWin7Dark(p);
        }
        break;
    case Id::Win10:
        if (light) {
            fillWin10Light(p);
        } else {
            fillWin10Dark(p);
        }
        break;
    case Id::Glass:
        if (light) {
            fillGlassLight(p);
        } else {
            fillGlassDark(p);
        }
        break;
    case Id::Win11:
        if (light) {
            fillWin11Light(p);
        } else {
            fillWin11Dark(p);
        }
        break;
    }

    if (isValidHex(accentHex)) {
        const QColor accent(accentHex);
        if (accent.isValid()) {
            p.accent = accent;
            const double lum = (0.299 * accent.red() + 0.587 * accent.green() + 0.114 * accent.blue()) / 255.0;
            p.accentText = (lum > 0.62) ? QColor("#0b1622") : QColor("#ffffff");
        }
    }

    // Windows 7 的侧栏 / 标题栏使用 Aero 竖向渐变（QSS 支持 qlineargradient）
    if (id == Id::Win7) {
        if (light) {
            p.sidebar = QColor("#eaf4fd");
            p.titleBar = QColor("#eaf4fd");
        } else {
            p.sidebar = QColor("#101d29");
            p.titleBar = QColor("#13202c");
        }
    }
    return p;
}

QString styleSheet(Id id, bool light, const QString &accentHex)
{
    const Palette p = palette(id, light, accentHex);
    const QColor accentHover = mix(p.accent, p.light ? QColor("#000000") : QColor("#ffffff"), p.light ? 0.10 : 0.16);
    const QColor accentPressed = mix(p.accent, QColor("#000000"), 0.20);
    const QColor accentSoft = QColor(p.accent.red(), p.accent.green(), p.accent.blue(),
                                     p.glass ? 84 : (p.light ? 34 : 56));

    QString sidebarBg = css(p.sidebar);
    QString titleBg = css(p.titleBar);
    if (id == Id::Win7) {
        const QColor top = p.light ? QColor("#f6fbff") : QColor("#1b2c3c");
        const QColor bottom = p.light ? QColor("#d5e7f7") : QColor("#0c1620");
        const QString grad = QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:0.55 %2, stop:1 %3)")
                                 .arg(css(top), css(p.sidebar), css(bottom));
        sidebarBg = grad;
        titleBg = QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2)")
                      .arg(css(top), css(bottom));
    }

    const QColor chevronColor = p.muted;
    const QString chevron = assetUrl(QStringLiteral("chevron-down"), chevronColor);
    const QString check = assetUrl(QStringLiteral("check"), p.accentText);

    QString qss = QStringLiteral(R"QSS(
* {
    font-family: "%FONT%";
    outline: none;
}
QWidget {
    color: %TEXT%;
    font-size: 13px;
}
/* 主窗口背景由 Backdrop 自绘（含圆角裁剪），这里必须保持透明 */
QMainWindow {
    background: transparent;
}
QDialog, QWidget#RootSurface {
    background: %WINDOW%;
}
QToolTip {
    background: %SURFACE%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS_SM%px;
    padding: 6px 9px;
}

/* ---------- 无边框标题栏 ---------- */
#TitleBar {
    background: %TITLEBAR%;
    border: none;
}
#TitleBarTitle {
    color: %TITLE_TEXT%;
    font-size: 12px;
    font-weight: 600;
}
#TitleBarSub {
    color: %MUTED%;
    font-size: 11px;
}
#WinBtn {
    background: transparent;
    border: none;
    color: %TITLE_TEXT%;
    border-radius: %RADIUS_SM%px;
    padding: 0px;
}
#WinBtn:hover {
    background: %HOVER%;
}
#WinBtn:pressed {
    background: %SURFACE_ALT%;
}
#WinBtn[role="close"]:hover {
    background: #e81123;
    color: #ffffff;
}

/* ---------- 侧边栏 ---------- */
#Sidebar {
    background: %SIDEBAR%;
    border-right: 1px solid %BORDER%;
}
#BrandName {
    color: %TEXT%;
    font-size: 15px;
    font-weight: 700;
}
#BrandSub {
    color: %MUTED%;
    font-size: 11px;
}
#NavSection {
    color: %MUTED%;
    font-size: 11px;
    font-weight: 600;
    padding-left: 16px;
}
QListWidget#NavList {
    background: transparent;
    border: none;
    outline: none;
}
QListWidget#NavList::item {
    color: %SIDEBAR_TEXT%;
    padding: 9px 10px;
    margin: 2px 10px;
    border-radius: %RADIUS_SM%px;
    border: 1px solid transparent;
}
QListWidget#NavList::item:hover {
    background: %HOVER%;
}
QListWidget#NavList::item:selected {
    background: %ACCENT%;
    color: %ACCENT_TEXT%;
    font-weight: 600;
}
#SideFoot {
    color: %MUTED%;
    font-size: 11px;
}

/* ---------- 页面通用 ---------- */
#PageHeader {
    background: transparent;
}
#PageTitle {
    font-size: 20px;
    font-weight: 700;
    color: %TEXT%;
}
#PageSubtitle {
    color: %MUTED%;
    font-size: 12px;
}
QFrame[card="true"] {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%px;
}
QFrame[card="true"][flat="true"] {
    background: %SURFACE_ALT%;
}
QFrame[card="true"][option="true"]:hover {
    border-color: %ACCENT%;
}
QFrame[card="true"][option="true"][current="true"] {
    background: %ACCENT_SOFT%;
    border: 1px solid %ACCENT%;
}
QLabel[role="h2"] {
    font-size: 15px;
    font-weight: 600;
}
QLabel[role="h3"] {
    font-size: 13px;
    font-weight: 600;
}
QLabel[role="muted"] {
    color: %MUTED%;
}
QLabel[role="statValue"] {
    font-size: 26px;
    font-weight: 700;
}
QLabel[role="statLabel"] {
    color: %MUTED%;
    font-size: 12px;
}
QLabel[role="pill"] {
    background: %SURFACE_ALT%;
    border: 1px solid %BORDER%;
    border-radius: 10px;
    padding: 2px 10px;
    color: %MUTED%;
    font-size: 11px;
}
QLabel[role="pill"][tone="ok"] {
    color: %SUCCESS%;
    border-color: %SUCCESS%;
    background: transparent;
}
QLabel[role="pill"][tone="bad"] {
    color: %DANGER%;
    border-color: %DANGER%;
    background: transparent;
}
QLabel[role="error"] {
    color: %DANGER%;
}
QLabel[role="success"] {
    color: %SUCCESS%;
}
QLabel[role="warning"] {
    color: %WARNING%;
}
QLabel[role="mono"] {
    font-family: "Cascadia Mono", "Consolas", monospace;
    color: %MUTED%;
    font-size: 12px;
}
QFrame[separator="true"] {
    background: %BORDER%;
    max-height: 1px;
    border: none;
}

/* ---------- 按钮 ---------- */
QPushButton {
    background: %SURFACE%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS_SM%px;
    padding: 7px 14px;
}
QPushButton:hover {
    background: %HOVER%;
}
QPushButton:pressed {
    background: %SURFACE_ALT%;
}
QPushButton:disabled {
    color: %MUTED%;
    background: %SURFACE_ALT%;
    border-color: %BORDER%;
}
QPushButton[variant="primary"] {
    background: %ACCENT%;
    color: %ACCENT_TEXT%;
    border: 1px solid %ACCENT%;
    font-weight: 600;
}
QPushButton[variant="primary"]:hover {
    background: %ACCENT_HOVER%;
    border-color: %ACCENT_HOVER%;
}
QPushButton[variant="primary"]:pressed {
    background: %ACCENT_PRESSED%;
}
QPushButton[variant="primary"]:disabled {
    background: %ACCENT_SOFT%;
    color: %MUTED%;
    border-color: transparent;
}
QPushButton[variant="ghost"] {
    background: transparent;
    border: 1px solid transparent;
    color: %MUTED%;
}
QPushButton[variant="ghost"]:hover {
    background: %HOVER%;
    color: %TEXT%;
}
QPushButton[variant="danger"] {
    background: transparent;
    border: 1px solid %DANGER%;
    color: %DANGER%;
}
QPushButton[variant="danger"]:hover {
    background: %DANGER%;
    color: #ffffff;
}
QPushButton[variant="link"] {
    background: transparent;
    border: none;
    color: %ACCENT%;
    padding: 2px 4px;
}
QPushButton[variant="link"]:hover {
    color: %ACCENT_HOVER%;
    text-decoration: underline;
}
QToolButton {
    background: transparent;
    border: none;
    color: %MUTED%;
    border-radius: %RADIUS_SM%px;
    padding: 5px;
}
QToolButton:hover {
    background: %HOVER%;
    color: %TEXT%;
}

/* ---------- 输入控件 ---------- */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
    background: %INPUT_BG%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS_SM%px;
    padding: 7px 10px;
    selection-background-color: %ACCENT%;
    selection-color: %ACCENT_TEXT%;
}
QLineEdit:hover, QSpinBox:hover, QComboBox:hover, QPlainTextEdit:hover, QTextEdit:hover {
    border-color: %ACCENT_SOFT%;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus {
    border-color: %ACCENT%;
}
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled {
    color: %MUTED%;
    background: %SURFACE_ALT%;
}
QLineEdit[invalid="true"] {
    border-color: %DANGER%;
}
QComboBox::drop-down {
    border: none;
    width: 24px;
}
QComboBox::down-arrow {
    image: url("%CHEVRON%");
    width: 12px;
    height: 12px;
}
QComboBox QAbstractItemView {
    background: %SURFACE%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS_SM%px;
    selection-background-color: %ACCENT%;
    selection-color: %ACCENT_TEXT%;
    padding: 4px;
    outline: none;
}
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    background: transparent;
    border: none;
    width: 16px;
}
QCheckBox, QRadioButton {
    color: %TEXT%;
    spacing: 8px;
}
QCheckBox::indicator, QRadioButton::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid %BORDER%;
    background: %INPUT_BG%;
}
QCheckBox::indicator {
    border-radius: 4px;
}
QRadioButton::indicator {
    border-radius: 8px;
}
QCheckBox::indicator:hover, QRadioButton::indicator:hover {
    border-color: %ACCENT%;
}
QCheckBox::indicator:checked {
    background: %ACCENT%;
    border-color: %ACCENT%;
    image: url("%CHECK%");
}
QRadioButton::indicator:checked {
    background: %ACCENT%;
    border: 4px solid %INPUT_BG%;
}
QGroupBox {
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%px;
    margin-top: 14px;
    padding-top: 10px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 12px;
    padding: 0 6px;
    color: %MUTED%;
}

/* ---------- 设置页 ---------- */
QScrollArea#SettingsScroll {
    background: transparent;
    border: none;
}
QScrollArea#SettingsScroll > QWidget > QWidget,
#SettingsContent {
    background: transparent;
}
QFrame#Segmented {
    background: %SURFACE_ALT%;
    border: 1px solid %BORDER%;
    border-radius: 16px;
}
QPushButton[variant="segment"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 14px;
    padding: 5px 18px;
    color: %MUTED%;
}
QPushButton[variant="segment"]:hover {
    color: %TEXT%;
    background: %HOVER%;
}
QPushButton[variant="segment"][on="true"] {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    color: %TEXT%;
    font-weight: 600;
}
/* ---------- 表格 ---------- */
QTableWidget, QTableView {
    background: %SURFACE%;
    alternate-background-color: %SURFACE_ALT%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS_SM%px;
    gridline-color: %BORDER%;
    selection-background-color: %ACCENT_SOFT%;
    selection-color: %TEXT%;
}
QTableWidget::item, QTableView::item {
    padding: 6px 8px;
    border: none;
}
QTableWidget::item:selected, QTableView::item:selected {
    background: %ACCENT_SOFT%;
    color: %TEXT%;
}
QHeaderView {
    background: transparent;
}
QHeaderView::section {
    background: %SURFACE_ALT%;
    color: %MUTED%;
    border: none;
    border-bottom: 1px solid %BORDER%;
    border-right: 1px solid %BORDER%;
    padding: 8px;
    font-weight: 600;
}
QHeaderView::section:last {
    border-right: none;
}
QTableCornerButton::section {
    background: %SURFACE_ALT%;
    border: none;
}

/* ---------- 进度条 ---------- */
QProgressBar {
    background: %SURFACE_ALT%;
    border: 1px solid %BORDER%;
    border-radius: 6px;
    height: 14px;
    text-align: center;
    color: %TEXT%;
    font-size: 11px;
}
QProgressBar::chunk {
    background: %ACCENT%;
    border-radius: 5px;
}
#LoadBar {
    background: transparent;
    border: none;
    border-radius: 0px;
    height: 3px;
    max-height: 3px;
}
#LoadBar::chunk {
    background: %ACCENT%;
    border-radius: 0px;
}

/* ---------- 滚动条 ---------- */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: %BORDER%;
    border-radius: 4px;
    min-height: 28px;
}
QScrollBar::handle:vertical:hover {
    background: %MUTED%;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
    width: 0px;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: transparent;
}
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px;
}
QScrollBar::handle:horizontal {
    background: %BORDER%;
    border-radius: 4px;
    min-width: 28px;
}
QScrollBar::handle:horizontal:hover {
    background: %MUTED%;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    height: 0px;
    width: 0px;
}
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
    background: transparent;
}

/* ---------- 菜单 / 弹窗 ---------- */
QMenu {
    background: %SURFACE%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS_SM%px;
    padding: 6px;
}
QMenu::item {
    padding: 7px 22px 7px 14px;
    border-radius: %RADIUS_SM%px;
}
QMenu::item:selected {
    background: %ACCENT%;
    color: %ACCENT_TEXT%;
}
QMenu::item:disabled {
    color: %MUTED%;
}
QMenu::separator {
    height: 1px;
    background: %BORDER%;
    margin: 5px 8px;
}
QMenu::indicator {
    width: 14px;
    height: 14px;
    margin-left: 6px;
}
QMenu::indicator:checked {
    image: url("%CHECK%");
}
QMessageBox {
    background: %WINDOW%;
}
QMessageBox QLabel {
    color: %TEXT%;
}
QSplitter::handle {
    background: %BORDER%;
}
QTabWidget::pane {
    border: 1px solid %BORDER%;
    border-radius: %RADIUS_SM%px;
    background: %SURFACE%;
}
QTabBar::tab {
    background: transparent;
    color: %MUTED%;
    padding: 7px 14px;
    border-radius: %RADIUS_SM%px;
    margin-right: 4px;
}
QTabBar::tab:selected {
    background: %ACCENT_SOFT%;
    color: %TEXT%;
    font-weight: 600;
}

/* ---------- 应用页（应用卡片 + 常驻标签） ---------- */
#AppStrip, #AppStripBody, #WebHolder {
    background: transparent;
    border: none;
}
#AppStrip QScrollBar:horizontal {
    height: 6px;
    background: transparent;
    margin: 0;
    border: none;
}
#AppStrip QScrollBar::handle:horizontal {
    background: %BORDER%;
    border-radius: 3px;
    min-width: 40px;
}
#AppStrip QScrollBar::add-line:horizontal,
#AppStrip QScrollBar::sub-line:horizontal,
#AppStrip QScrollBar::add-page:horizontal,
#AppStrip QScrollBar::sub-page:horizontal {
    background: transparent;
    width: 0;
}
QPushButton#AppCard {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%px;
    text-align: left;
    padding: 0;
}
QPushButton#AppCard:hover {
    background: %HOVER%;
    border-color: %ACCENT_SOFT%;
}
QPushButton#AppCard:pressed {
    background: %ACCENT_SOFT%;
    border-color: %ACCENT%;
}
QPushButton#AppCard:focus {
    border-color: %ACCENT%;
}
QPushButton#AppCard[state="missing"] {
    background: %SURFACE_ALT%;
    border-style: dashed;
}
QPushButton#AppCard[dashed="true"] {
    background: transparent;
    border: 1px dashed %BORDER%;
}
QPushButton#AppCard[dashed="true"]:hover {
    background: %ACCENT_SOFT%;
    border-color: %ACCENT%;
}
QPushButton#AppChip {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 12px;
    padding: 4px 12px;
    color: %MUTED%;
    font-size: 12px;
}
QPushButton#AppChip:hover {
    background: %HOVER%;
    color: %TEXT%;
    border-color: %BORDER%;
}
QPushButton#AppChip:pressed {
    background: %ACCENT_SOFT%;
    color: %TEXT%;
    border-color: %ACCENT%;
}
QPushButton#AppChip:focus {
    border-color: %ACCENT%;
}
#BrowserToolBar {
    background: transparent;
    border: none;
}
#BrowserUrlBar {
    background: %INPUT_BG%;
    border: 1px solid %BORDER%;
    border-radius: 15px;
    padding: 6px 14px;
    color: %TEXT%;
}
#BrowserUrlBar:hover {
    border-color: %ACCENT_SOFT%;
}
#BrowserUrlBar:focus {
    border-color: %ACCENT%;
}
QTabWidget#BrowserTabs::pane {
    border: 1px solid %BORDER%;
    border-radius: %RADIUS_SM%px;
    background: %SURFACE%;
    top: -1px;
}
QTabWidget#BrowserTabs QTabBar::tab {
    background: %SURFACE_ALT%;
    color: %MUTED%;
    border: 1px solid %BORDER%;
    border-bottom: none;
    border-top-left-radius: %RADIUS_SM%px;
    border-top-right-radius: %RADIUS_SM%px;
    padding: 6px 10px;
    margin-right: 2px;
    min-width: 92px;
    max-width: 210px;
}
QTabWidget#BrowserTabs QTabBar::tab:selected {
    background: %SURFACE%;
    color: %TEXT%;
    font-weight: 600;
}
QTabWidget#BrowserTabs QTabBar::tab:hover:!selected {
    background: %HOVER%;
    color: %TEXT%;
}
QTabWidget#BrowserTabs QTabBar::close-button {
    background: transparent;
    border: none;
    border-radius: 3px;
}
QTabWidget#BrowserTabs QTabBar::close-button:hover {
    background: %HOVER%;
}
QTabWidget#BrowserTabs QTabBar QToolButton {
    background: %SURFACE_ALT%;
    border: none;
}

/* ---------- 登录 ---------- */
/* 窗口背景由 Backdrop 绘制（圆角裁剪），窗口本身保持透明 */
#LoginRoot {
    background: transparent;
}
#LoginCard {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%px;
}
#LoginTitle {
    font-size: 19px;
    font-weight: 700;
}
#LoginSub {
    color: %MUTED%;
    font-size: 12px;
}
#LoginError {
    color: %DANGER%;
    font-size: 12px;
}
#LoginHint {
    color: %MUTED%;
    font-size: 11px;
}

/* ---------- 内嵌网页管理后台 ---------- */
#WebHost {
    background: %SURFACE_ALT%;
    border: none;
}
#UrlBar {
    background: %INPUT_BG%;
    border: 1px solid %BORDER%;
    border-radius: 15px;
    padding: 6px 14px;
    color: %TEXT%;
}
#UrlBar:focus {
    border-color: %ACCENT%;
}
#WebNotice {
    color: %MUTED%;
    font-size: 12px;
}

/* ---------- 提示条 ---------- */
QFrame#Toast {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS%px;
}
QFrame#Toast[tone="error"] {
    border-color: %DANGER%;
}
QFrame#Toast[tone="success"] {
    border-color: %SUCCESS%;
}
QFrame#Toast[tone="warning"] {
    border-color: %WARNING%;
}
#ToastText {
    color: %TEXT%;
    font-size: 12px;
}

/* ---------- 空状态 ---------- */
#EmptyState {
    color: %MUTED%;
    font-size: 12px;
}
)QSS");

    // 主样式表已经接近 MSVC 单个字符串字面量的长度上限，SSH 终端的规则单独拼接。
    qss += QStringLiteral(R"QSS(
/* ---------- SSH 终端 ---------- */
#SshStatus {
    color: %MUTED%;
    font-size: 12px;
    font-weight: 600;
    padding: 3px 10px;
    border: 1px solid %BORDER%;
    border-radius: 10px;
}
#SshStatus[tone="busy"] {
    color: %WARNING%;
    border-color: %WARNING%;
}
#SshStatus[tone="ok"] {
    color: %SUCCESS%;
    border-color: %SUCCESS%;
}
#SshStatus[tone="error"] {
    color: %DANGER%;
    border-color: %DANGER%;
}
QPlainTextEdit#SshTerminal {
    background: #0b1016;
    border: 1px solid %BORDER%;
    border-radius: %RADIUS_SM%px;
    padding: 10px 12px;
    color: #cdd9e5;
    selection-background-color: %ACCENT%;
    selection-color: #ffffff;
}
QLineEdit#SshInput {
    background: %INPUT_BG%;
    border: 1px solid %BORDER%;
    border-radius: 15px;
    padding: 6px 14px;
    color: %TEXT%;
}
QLineEdit#SshInput:focus {
    border-color: %ACCENT%;
}
QLineEdit#SshInput:disabled {
    color: %MUTED%;
}
)QSS");

    qss.replace(QStringLiteral("%FONT%"), p.fontFamily);
    qss.replace(QStringLiteral("%TEXT%"), css(p.text));
    qss.replace(QStringLiteral("%TEXT_MUTED%"), css(p.muted));
    qss.replace(QStringLiteral("%MUTED%"), css(p.muted));
    qss.replace(QStringLiteral("%WINDOW%"), css(p.window));
    qss.replace(QStringLiteral("%SURFACE_ALT%"), css(p.surfaceAlt));
    qss.replace(QStringLiteral("%SURFACE%"), css(p.surface));
    qss.replace(QStringLiteral("%BORDER%"), css(p.border));
    qss.replace(QStringLiteral("%ACCENT_HOVER%"), css(accentHover));
    qss.replace(QStringLiteral("%ACCENT_PRESSED%"), css(accentPressed));
    qss.replace(QStringLiteral("%ACCENT_SOFT%"), css(accentSoft));
    qss.replace(QStringLiteral("%ACCENT_TEXT%"), css(p.accentText));
    qss.replace(QStringLiteral("%ACCENT%"), css(p.accent));
    qss.replace(QStringLiteral("%DANGER%"), css(p.danger));
    qss.replace(QStringLiteral("%SUCCESS%"), css(p.success));
    qss.replace(QStringLiteral("%WARNING%"), css(p.warning));
    qss.replace(QStringLiteral("%SIDEBAR_TEXT%"), css(p.sidebarText));
    qss.replace(QStringLiteral("%SIDEBAR%"), sidebarBg);
    qss.replace(QStringLiteral("%TITLE_TEXT%"), css(p.titleText));
    qss.replace(QStringLiteral("%TITLEBAR%"), titleBg);
    qss.replace(QStringLiteral("%HOVER%"), css(p.hover));
    qss.replace(QStringLiteral("%INPUT_BG%"), css(p.inputBg));
    qss.replace(QStringLiteral("%RADIUS_SM%"), QString::number(p.radiusSm));
    qss.replace(QStringLiteral("%RADIUS%"), QString::number(p.radius));
    qss.replace(QStringLiteral("%CHEVRON%"), chevron);
    qss.replace(QStringLiteral("%CHECK%"), check);

    return qss;
}

namespace {
Id g_currentId = Id::Win11;
bool g_currentLight = false;
QString g_currentAccent;

Palette &currentRef()
{
    static Palette pal = palette(Id::Win11, false, QString());
    return pal;
}
} // namespace

Palette current()
{
    return currentRef();
}

void apply(Id id, bool light, const QString &accentHex)
{
    g_currentId = id;
    g_currentLight = light;
    g_currentAccent = accentHex;
    currentRef() = palette(id, light, accentHex);

    auto *app = qobject_cast<QApplication *>(QApplication::instance());
    if (!app) {
        return;
    }
    app->setStyleSheet(styleSheet(id, light, accentHex));
    const QWidgetList widgets = app->allWidgets();
    for (QWidget *w : widgets) {
        repolish(w);
    }
}

void repolish(QWidget *widget)
{
    if (!widget) {
        return;
    }
    if (QStyle *style = widget->style()) {
        style->unpolish(widget);
        style->polish(widget);
    }
    widget->update();
}

QString assetUrl(const QString &name, const QColor &color)
{
    const QString dir = AppConfig::dataDir() + QStringLiteral("/theme");
    QDir().mkpath(dir);

    const QString safe = name + QLatin1Char('_') + color.name(QColor::HexArgb).mid(1);
    const QString file = dir + QLatin1Char('/') + safe + QStringLiteral(".png");
    if (!QFile::exists(file)) {
        if (name == QLatin1String("check")) {
            writeCheck(file, color);
        } else {
            writeChevron(file, color, name.contains(QLatin1String("up")));
        }
    }
    return QDir::toNativeSeparators(file).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

} // namespace Theme
