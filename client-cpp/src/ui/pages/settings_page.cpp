#include "ui/pages/settings_page.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include "core/api_client.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

#include <functional>

namespace {

const char *const kAccentColors[] = {"#2f81f7", "#23c9a4", "#8b5cf6", "#f97316", "#ef4444", "#eab308"};

QString accentName(const QString &hex)
{
    static const QHash<QString, QString> kNames = {
        {QStringLiteral("#2f81f7"), QStringLiteral("飞牛蓝")},
        {QStringLiteral("#23c9a4"), QStringLiteral("薄荷青")},
        {QStringLiteral("#8b5cf6"), QStringLiteral("星云紫")},
        {QStringLiteral("#f97316"), QStringLiteral("落日橙")},
        {QStringLiteral("#ef4444"), QStringLiteral("珊瑚红")},
        {QStringLiteral("#eab308"), QStringLiteral("琥珀金")}
    };
    return kNames.value(hex, hex.toUpper());
}

/** 主题卡片里的小窗口预览：按该主题调色板画出标题栏 / 侧栏 / 内容块。 */
class ThemePreview : public QWidget
{
public:
    ThemePreview(Theme::Id id, bool light, const QString &accent, QWidget *parent = nullptr)
        : QWidget(parent), m_id(id), m_light(light), m_accent(accent)
    {
        setFixedHeight(58);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme::Palette pal = Theme::palette(m_id, m_light, m_accent);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (r.width() < 24 || r.height() < 24) {
            return;
        }
        const qreal radius = qBound<qreal>(4.0, pal.radius, 12.0);

        QPainterPath clip;
        clip.addRoundedRect(r, radius, radius);
        p.save();
        p.setClipPath(clip);
        p.fillRect(r, pal.window);

        const QRectF bar(r.left(), r.top(), r.width(), 12);
        p.fillRect(bar, pal.titleBar);

        QColor dot = pal.muted;
        dot.setAlpha(120);
        p.setPen(Qt::NoPen);
        p.setBrush(dot);
        for (int i = 0; i < 3; ++i) {
            p.drawEllipse(QPointF(r.right() - 9 - i * 6.0, r.top() + 6), 1.6, 1.6);
        }

        const QRectF side(r.left(), r.top() + 12, 26, r.height() - 12);
        p.fillRect(side, pal.sidebar);
        for (int i = 0; i < 3; ++i) {
            QColor c = (i == 0) ? pal.accent : pal.sidebarText;
            c.setAlpha(i == 0 ? 220 : 70);
            p.setBrush(c);
            p.drawRoundedRect(QRectF(side.left() + 5, side.top() + 6 + i * 9, 16, 3.2), 1.6, 1.6);
        }

        const QRectF content(r.left() + 34, r.top() + 20, qMax<qreal>(20, r.width() - 42), 15);
        p.setBrush(pal.surface);
        p.setPen(QPen(pal.border, 1));
        p.drawRoundedRect(content, radius * 0.5, radius * 0.5);
        p.setPen(Qt::NoPen);
        p.setBrush(pal.accent);
        p.drawRoundedRect(QRectF(content.left() + 6, content.top() + 3.5, 16, 5), 2.5, 2.5);
        QColor line = pal.muted;
        line.setAlpha(90);
        p.setBrush(line);
        p.drawRoundedRect(QRectF(content.left() + 6, content.top() + 11, content.width() - 12, 2.6),
                          1.3, 1.3);

        const QRectF strip(content.left(), content.bottom() + 5, content.width(), 11);
        p.setBrush(pal.surfaceAlt);
        p.setPen(QPen(pal.border, 1));
        p.drawRoundedRect(strip, radius * 0.4, radius * 0.4);
        p.restore();

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(pal.border, 1));
        p.drawRoundedRect(r, radius, radius);
    }

private:
    Theme::Id m_id;
    bool m_light = false;
    QString m_accent;
};

/** 主题选项卡（点击即预览）。 */
class ThemeTile : public QFrame
{
public:
    ThemeTile(Theme::Id id, bool light, const QString &accent, QWidget *parent = nullptr)
        : QFrame(parent), m_id(id)
    {
        setProperty("card", true);
        setProperty("option", true);
        setProperty("current", false);
        setCursor(Qt::PointingHandCursor);
        setMinimumWidth(122);
        setToolTip(Theme::description(id));

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 9);
        layout->setSpacing(8);
        layout->addWidget(new ThemePreview(id, light, accent, this), 1);

        auto *name = makeLabel(Theme::label(id), QStringLiteral("h3"), this);
        name->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        layout->addWidget(name);
    }

    std::function<void(Theme::Id)> onPick;
    Theme::Id id() const { return m_id; }

    void setCurrent(bool current)
    {
        if (property("current").toBool() == current) {
            return;
        }
        setProperty("current", current);
        Theme::repolish(this);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onPick) {
            onPick(m_id);
        }
        QFrame::mousePressEvent(event);
    }

private:
    Theme::Id m_id;
};

/** 强调色色块：选中时外圈描边 + 中间对勾。 */
class AccentSwatch : public QWidget
{
public:
    explicit AccentSwatch(const QString &hex, QWidget *parent = nullptr)
        : QWidget(parent), m_hex(hex)
    {
        setFixedSize(32, 32);
        setCursor(Qt::PointingHandCursor);
        setToolTip(accentName(hex));
    }

    const QString &hex() const { return m_hex; }

    void setSelected(bool selected)
    {
        if (m_selected == selected) {
            return;
        }
        m_selected = selected;
        update();
    }

    std::function<void(const QString &)> onPick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF outer = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        if (m_selected) {
            p.setPen(QPen(Theme::current().text, 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(outer);
        }
        const QRectF inner = outer.adjusted(5, 5, -5, -5);
        p.setPen(QPen(QColor(0, 0, 0, 46), 1));
        p.setBrush(QColor(m_hex));
        p.drawEllipse(inner);

        if (m_selected) {
            p.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const QPointF c = inner.center();
            const qreal s = inner.width() * 0.2;
            p.drawLine(c + QPointF(-s, 0), c + QPointF(-s * 0.2, s * 0.8));
            p.drawLine(c + QPointF(-s * 0.2, s * 0.8), c + QPointF(s, -s * 0.7));
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onPick) {
            onPick(m_hex);
        }
    }

private:
    QString m_hex;
    bool m_selected = false;
};

QPushButton *makeSegmentButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setProperty("variant", QStringLiteral("segment"));
    button->setProperty("on", false);
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

SettingsPage::SettingsPage(AppConfig *config, ApiClient *api, QWidget *parent)
    : BasePage(api, parent), m_config(config)
{
    setPageTitle(QStringLiteral("设置"));
    setPageSubtitle(QStringLiteral("外观、服务器与本地偏好；密码不会被保存"));

    m_theme = Theme::idFromString(m_config->uiTheme);
    m_light = m_config->colorMode == QLatin1String("light");
    m_accent = m_config->accent;

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("SettingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);

    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("SettingsContent"));
    content->setAttribute(Qt::WA_StyledBackground, false);
    m_contentLayout = new QVBoxLayout(content);
    m_contentLayout->setContentsMargins(0, 0, 6, 0);
    m_contentLayout->setSpacing(16);
    scroll->setWidget(content);

    buildAppearanceCard();
    buildServerCard();
    buildAccountCard();

    auto *saveRow = new QHBoxLayout();
    saveRow->setContentsMargins(0, 4, 0, 0);
    saveRow->setSpacing(10);

    auto *saveButton = makeButton(QStringLiteral("保存设置"), QStringLiteral("primary"), content);
    saveButton->setIcon(Icons::icon(Icons::Kind::Check, Theme::current().accentText, 16));
    connect(saveButton, &QPushButton::clicked, this, &SettingsPage::save);

    auto *resetButton = makeButton(QStringLiteral("恢复默认外观"), QStringLiteral("ghost"), content);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        m_config->uiTheme = QStringLiteral("win11");
        m_config->colorMode = QStringLiteral("dark");
        m_config->accent = QStringLiteral("#2f81f7");
        loadFromConfig();
        emit appearanceChanged(m_theme, m_light, m_accent);
    });

    m_hint = makeLabel(QStringLiteral("主题与强调色会立即预览，点击「保存设置」后写入本地配置。"),
                       QStringLiteral("muted"), content);

    saveRow->addWidget(saveButton);
    saveRow->addWidget(resetButton);
    saveRow->addStretch(1);
    saveRow->addWidget(m_hint);

    m_contentLayout->addLayout(saveRow);
    m_contentLayout->addStretch(1);

    body()->addWidget(scroll, 1);

    loadFromConfig();
}

void SettingsPage::buildAppearanceCard()
{
    auto *card = new Card(QStringLiteral("外观"), this);

    // 主题：4 个带预览的选项卡
    m_themeRow = new QWidget(card);
    auto *row = new QHBoxLayout(m_themeRow);
    row->setContentsMargins(0, 2, 0, 0);
    row->setSpacing(12);
    card->body()->addWidget(m_themeRow);

    auto *divider = makeSeparator(card);
    card->body()->addWidget(divider);

    // 颜色模式：分段控件
    auto *modeRow = new QHBoxLayout();
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->setSpacing(12);

    auto *modeLabel = makeLabel(QStringLiteral("颜色模式"), QStringLiteral("muted"), card);
    modeLabel->setFixedWidth(64);
    modeRow->addWidget(modeLabel);

    auto *segmented = new QFrame(card);
    segmented->setObjectName(QStringLiteral("Segmented"));
    auto *segmentedLayout = new QHBoxLayout(segmented);
    segmentedLayout->setContentsMargins(2, 2, 2, 2);
    segmentedLayout->setSpacing(2);

    m_darkButton = makeSegmentButton(QStringLiteral("深色"), segmented);
    m_lightButton = makeSegmentButton(QStringLiteral("浅色"), segmented);
    segmentedLayout->addWidget(m_darkButton);
    segmentedLayout->addWidget(m_lightButton);

    modeRow->addWidget(segmented);
    modeRow->addStretch(1);
    card->body()->addLayout(modeRow);

    // 强调色：色块 + 自定义
    auto *accentRow = new QHBoxLayout();
    accentRow->setContentsMargins(0, 0, 0, 0);
    accentRow->setSpacing(12);

    auto *accentLabel = makeLabel(QStringLiteral("强调色"), QStringLiteral("muted"), card);
    accentLabel->setFixedWidth(64);
    accentRow->addWidget(accentLabel);

    m_accentRow = new QWidget(card);
    auto *chips = new QHBoxLayout(m_accentRow);
    chips->setContentsMargins(0, 0, 0, 0);
    chips->setSpacing(6);
    accentRow->addWidget(m_accentRow);

    auto *customAccentButton = makeButton(QStringLiteral("自定义…"), QStringLiteral("ghost"), card);
    customAccentButton->setFixedWidth(84);
    connect(customAccentButton, &QPushButton::clicked, this, &SettingsPage::pickAccentColor);
    accentRow->addWidget(customAccentButton);

    m_accentValue = makeLabel(QString(), QStringLiteral("mono"), card);
    accentRow->addWidget(m_accentValue);
    accentRow->addStretch(1);

    card->body()->addLayout(accentRow);
    m_contentLayout->addWidget(card);

    connect(m_darkButton, &QPushButton::clicked, this, [this]() {
        if (m_loading || !m_light) {
            return;
        }
        m_light = false;
        syncModeButtons();
        syncThemeCards();
        emit appearanceChanged(m_theme, m_light, m_accent);
    });

    connect(m_lightButton, &QPushButton::clicked, this, [this]() {
        if (m_loading || m_light) {
            return;
        }
        m_light = true;
        syncModeButtons();
        syncThemeCards();
        emit appearanceChanged(m_theme, m_light, m_accent);
    });
}

void SettingsPage::buildServerCard()
{
    auto *card = new Card(QStringLiteral("服务器"), this);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);

    grid->addWidget(makeLabel(QStringLiteral("NAS 地址"), QStringLiteral("muted"), card), 0, 0);
    m_hostEdit = new QLineEdit(card);
    m_hostEdit->setPlaceholderText(QStringLiteral("例如 192.168.180.137"));
    grid->addWidget(m_hostEdit, 0, 1);

    grid->addWidget(makeLabel(QStringLiteral("管理后台端口"), QStringLiteral("muted"), card), 1, 0);
    m_adminPortBox = new QSpinBox(card);
    m_adminPortBox->setRange(1, 65535);
    m_adminPortBox->setValue(m_config->adminPort);
    grid->addWidget(m_adminPortBox, 1, 1);

    grid->addWidget(makeLabel(QStringLiteral("客户端接口"), QStringLiteral("muted"), card), 2, 0);
    m_apiInfo = makeLabel(QString(), QStringLiteral("mono"), card);
    grid->addWidget(m_apiInfo, 2, 1);

    grid->addWidget(makeLabel(QStringLiteral("下载目录"), QStringLiteral("muted"), card), 3, 0);
    auto *downloadRow = new QHBoxLayout();
    downloadRow->setContentsMargins(0, 0, 0, 0);
    downloadRow->setSpacing(8);
    m_downloadEdit = new QLineEdit(card);
    m_downloadEdit->setPlaceholderText(QStringLiteral("留空则使用系统下载目录"));
    downloadRow->addWidget(m_downloadEdit, 1);
    auto *browse = makeButton(QStringLiteral("浏览"), QStringLiteral("ghost"), card);
    browse->setFixedWidth(74);
    connect(browse, &QPushButton::clicked, this, &SettingsPage::browseDownloadDir);
    downloadRow->addWidget(browse);
    grid->addLayout(downloadRow, 3, 1);

    grid->setColumnStretch(1, 1);
    card->body()->addLayout(grid);

    auto *notice = makeLabel(
        QStringLiteral("网页管理后台地址：http://<NAS 地址>:5666/（开启 HTTPS 后为 5667），登录需要输入 NAS 账号密码。"),
        QStringLiteral("muted"), card);
    notice->setWordWrap(true);
    card->body()->addWidget(notice);

    // 飞牛官方 API：经服务端 WebSocket 协议桥接（无需额外配置即可读公开系统信息）
    auto *officialTitle = makeLabel(QStringLiteral("飞牛官方 API"), QStringLiteral("h3"), card);
    card->body()->addWidget(officialTitle);
    m_officialApiLabel = makeLabel(QStringLiteral("正在读取官方 API 状态…"), QStringLiteral("muted"), card);
    m_officialApiLabel->setWordWrap(true);
    card->body()->addWidget(m_officialApiLabel);

    m_contentLayout->addWidget(card);
    loadOfficialApiStatus();
}

void SettingsPage::loadOfficialApiStatus()
{
    if (!m_officialApiLabel) {
        return;
    }
    api()->get(QStringLiteral("/api/fnos/status"),
               [this](bool ok, const QJsonObject &data, const QString &message) {
                   if (!m_officialApiLabel) {
                       return;
                   }
                   if (!ok) {
                       m_officialApiLabel->setText(
                           QStringLiteral("官方 API 状态读取失败：%1").arg(message));
                       return;
                   }
                   if (!data.value(QStringLiteral("connected")).toBool()) {
                       m_officialApiLabel->setText(
                           QStringLiteral("未连接到飞牛官方接口：%1")
                               .arg(data.value(QStringLiteral("lastError")).toString()));
                       return;
                   }
                   QStringList parts;
                   parts << QStringLiteral("已连接 %1:%2")
                                .arg(data.value(QStringLiteral("host")).toString(),
                                     data.value(QStringLiteral("port")).toInt() == 0
                                         ? QStringLiteral("5666")
                                         : QString::number(data.value(QStringLiteral("port")).toInt()));
                   if (!data.value(QStringLiteral("hostName")).toString().isEmpty()) {
                       parts << QStringLiteral("主机名 %1")
                                    .arg(data.value(QStringLiteral("hostName")).toString());
                   }
                   if (!data.value(QStringLiteral("trimVersion")).toString().isEmpty()) {
                       parts << QStringLiteral("版本 %1")
                                    .arg(data.value(QStringLiteral("trimVersion")).toString());
                   }
                   const QJsonObject cpu = data.value(QStringLiteral("cpu")).toObject();
                   if (!cpu.isEmpty()) {
                       parts << QStringLiteral("%1（%2 核）")
                                    .arg(cpu.value(QStringLiteral("name")).toString())
                                    .arg(cpu.value(QStringLiteral("num")).toInt());
                   }
                   const QJsonObject mem = data.value(QStringLiteral("memory")).toObject();
                   if (mem.value(QStringLiteral("total")).toDouble() > 0) {
                       parts << QStringLiteral("内存 %1 GB")
                                    .arg(mem.value(QStringLiteral("total")).toDouble()
                                             / 1073741824.0,
                                         0, 'f', 1);
                   }
                   if (data.value(QStringLiteral("loggedIn")).toBool()) {
                       parts << QStringLiteral("已用官方账号登录");
                   } else if (!data.value(QStringLiteral("loginError")).toString().isEmpty()) {
                       parts << QStringLiteral("官方账号登录失败：%1")
                                    .arg(data.value(QStringLiteral("loginError")).toString());
                   }
                   m_officialApiLabel->setText(parts.join(QStringLiteral(" · ")));
               });
}

void SettingsPage::buildAccountCard()
{
    auto *card = new Card(QStringLiteral("账号"), this);

    m_rememberBox = new QCheckBox(QStringLiteral("记住用户名"), card);
    card->body()->addWidget(m_rememberBox);

    m_advancedModeBox = new QCheckBox(QStringLiteral("启用高级模式（显示 SSH 终端、容器管理）"), card);
    m_advancedModeBox->setToolTip(QStringLiteral("开启后侧栏会多出「高级」分组，并切换为赛博朋克霓虹风格。"));
    card->body()->addWidget(m_advancedModeBox);

    auto *notice = makeLabel(
        QStringLiteral("客户端可选择保存密码（使用 TPM 2.0 或 DPAPI 加密后写入本地），"
                       "并用于管理后台自动填充和 SSH 终端快捷填入。"
                       "登录令牌不会被持久化。"),
        QStringLiteral("muted"), card);
    notice->setWordWrap(true);
    card->body()->addWidget(notice);

    m_contentLayout->addWidget(card);
}

void SettingsPage::loadFromConfig()
{
    m_loading = true;

    m_hostEdit->setText(m_config->host);
    m_adminPortBox->setValue(m_config->adminPort > 0 ? m_config->adminPort : 5666);
    m_downloadEdit->setText(m_config->downloadDir);
    m_rememberBox->setChecked(m_config->rememberUsername);
    m_advancedModeBox->setChecked(m_config->advancedMode);

    m_theme = Theme::idFromString(m_config->uiTheme);
    m_light = m_config->colorMode == QLatin1String("light");
    m_accent = m_config->accent.isEmpty() ? QStringLiteral("#2f81f7") : m_config->accent;

    m_apiInfo->setText(QStringLiteral("https://%1:%2")
                           .arg(m_config->host.isEmpty() ? QStringLiteral("<NAS 地址>") : m_config->host)
                           .arg(m_config->apiPort));

    syncModeButtons();
    syncThemeCards();
    syncAccentChips();
    m_loading = false;
}

void SettingsPage::refresh()
{
    loadFromConfig();
}

void SettingsPage::reloadTheme()
{
    syncModeButtons();
    syncThemeCards();
    syncAccentChips();
}

void SettingsPage::syncModeButtons()
{
    if (!m_darkButton || !m_lightButton) {
        return;
    }
    m_darkButton->setProperty("on", !m_light);
    m_lightButton->setProperty("on", m_light);
    Theme::repolish(m_darkButton);
    Theme::repolish(m_lightButton);
}

void SettingsPage::syncThemeCards()
{
    if (!m_themeRow) {
        return;
    }
    auto *row = qobject_cast<QHBoxLayout *>(m_themeRow->layout());
    if (!row) {
        return;
    }

    while (QLayoutItem *item = row->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    const Theme::Id ids[] = {Theme::Id::Win7, Theme::Id::Win10, Theme::Id::Win11, Theme::Id::Glass};
    for (Theme::Id id : ids) {
        auto *tile = new ThemeTile(id, m_light, m_accent, m_themeRow);
        tile->onPick = [this](Theme::Id picked) { pickTheme(picked); };
        tile->setCurrent(id == m_theme);
        row->addWidget(tile, 1);
    }
}

void SettingsPage::syncAccentChips()
{
    if (!m_accentRow) {
        return;
    }
    auto *chips = qobject_cast<QHBoxLayout *>(m_accentRow->layout());
    if (!chips) {
        return;
    }

    while (QLayoutItem *item = chips->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    QStringList colors;
    for (const char *hex : kAccentColors) {
        colors << QString::fromLatin1(hex);
    }
    if (!m_accent.isEmpty() && !colors.contains(m_accent, Qt::CaseInsensitive)) {
        colors << m_accent;
    }

    for (const QString &hex : colors) {
        auto *swatch = new AccentSwatch(hex, m_accentRow);
        swatch->setSelected(hex.compare(m_accent, Qt::CaseInsensitive) == 0);
        swatch->onPick = [this](const QString &picked) { pickAccent(picked); };
        chips->addWidget(swatch);
    }
    chips->addStretch(1);

    if (m_accentValue) {
        // 自定义色时 accentName 就是 hex 本身，去重避免出现 "#aabbcc · #AABBCC"
        const QString name = accentName(m_accent);
        const QString hex = m_accent.toUpper();
        m_accentValue->setText(name.compare(hex, Qt::CaseInsensitive) == 0
                                   ? hex
                                   : QStringLiteral("%1 · %2").arg(name, hex));
    }
}

void SettingsPage::pickTheme(Theme::Id id)
{
    if (m_theme == id) {
        return;
    }
    m_theme = id;
    syncThemeCards();
    if (!m_loading) {
        emit appearanceChanged(m_theme, m_light, m_accent);
    }
}

void SettingsPage::pickAccent(const QString &hex)
{
    if (hex.compare(m_accent, Qt::CaseInsensitive) == 0) {
        return;
    }
    m_accent = hex;
    syncAccentChips();
    syncThemeCards();
    if (!m_loading) {
        emit appearanceChanged(m_theme, m_light, m_accent);
    }
}

void SettingsPage::pickAccentColor()
{
    const QColor current(m_accent);
    const QColor picked = QColorDialog::getColor(
        current.isValid() ? current : QColor(QStringLiteral("#2f81f7")), this,
        QStringLiteral("选择强调色"));
    if (!picked.isValid()) {
        return;
    }
    pickAccent(picked.name(QColor::HexRgb));
}

void SettingsPage::browseDownloadDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择下载目录"),
        m_downloadEdit->text().isEmpty() ? QDir::homePath() : m_downloadEdit->text());
    if (!dir.isEmpty()) {
        m_downloadEdit->setText(QDir::toNativeSeparators(dir));
    }
}

void SettingsPage::save()
{
    const QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty()) {
        showToast(QStringLiteral("请填写 NAS 地址"), QStringLiteral("error"));
        return;
    }

    const QString oldHost = m_config->host;
    const int oldPort = m_config->adminPort;

    m_config->host = host;
    m_config->adminPort = m_adminPortBox->value();
    m_config->downloadDir = m_downloadEdit->text().trimmed();
    m_config->rememberUsername = m_rememberBox->isChecked();
    const bool oldAdvanced = m_config->advancedMode;
    m_config->advancedMode = m_advancedModeBox->isChecked();
    m_config->uiTheme = Theme::toString(m_theme);
    m_config->colorMode = m_light ? QStringLiteral("light") : QStringLiteral("dark");
    m_config->accent = m_accent;
    m_config->normalize();
    m_config->save();

    m_apiInfo->setText(QStringLiteral("https://%1:%2").arg(m_config->host).arg(m_config->apiPort));

    emit appearanceChanged(m_theme, m_light, m_accent);
    if (oldAdvanced != m_config->advancedMode) {
        emit advancedModeChanged(m_config->advancedMode);
    }
    if (oldHost != m_config->host || oldPort != m_config->adminPort) {
        emit serverChanged();
    }
    showToast(QStringLiteral("设置已保存"), QStringLiteral("success"));
}
