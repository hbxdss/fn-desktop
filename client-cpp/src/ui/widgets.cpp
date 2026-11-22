#include "ui/widgets.h"

#include <QDateTime>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

namespace {

QColor blend(const QColor &a, const QColor &b, double t)
{
    const double k = std::clamp(t, 0.0, 1.0);
    return QColor(int(a.red() * (1 - k) + b.red() * k),
                  int(a.green() * (1 - k) + b.green() * k),
                  int(a.blue() * (1 - k) + b.blue() * k),
                  int(a.alpha() * (1 - k) + b.alpha() * k));
}

void drawGlow(QPainter &p, const QRectF &rect, const QPointF &center, double radius,
              const QColor &color, int alpha)
{
    QColor c = color;
    c.setAlpha(alpha);
    QRadialGradient g(center, radius);
    g.setColorAt(0.0, c);
    QColor mid = color;
    mid.setAlpha(int(alpha * 0.45));
    g.setColorAt(0.55, mid);
    QColor edge = color;
    edge.setAlpha(0);
    g.setColorAt(1.0, edge);
    p.fillRect(rect, g);
}

} // namespace

namespace Fmt {

QString bytes(qint64 value)
{
    if (value < 0) {
        return QStringLiteral("—");
    }
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double v = double(value);
    int idx = 0;
    while (v >= 1024.0 && idx < 5) {
        v /= 1024.0;
        ++idx;
    }
    if (idx == 0) {
        return QStringLiteral("%1 B").arg(value);
    }
    const int digits = (v >= 100.0) ? 0 : (v >= 10.0 ? 1 : 2);
    return QStringLiteral("%1 %2").arg(v, 0, 'f', digits).arg(QString::fromLatin1(units[idx]));
}

QString duration(qint64 seconds)
{
    if (seconds <= 0) {
        return QStringLiteral("—");
    }
    qint64 rest = seconds;
    const qint64 days = rest / 86400;
    rest %= 86400;
    const qint64 hours = rest / 3600;
    rest %= 3600;
    const qint64 minutes = rest / 60;
    rest %= 60;

    QStringList parts;
    if (days > 0) {
        parts << QStringLiteral("%1 天").arg(days);
    }
    if (hours > 0) {
        parts << QStringLiteral("%1 小时").arg(hours);
    }
    if (days == 0 && minutes > 0) {
        parts << QStringLiteral("%1 分").arg(minutes);
    }
    if (parts.isEmpty()) {
        parts << QStringLiteral("%1 秒").arg(rest);
    }
    return parts.join(QLatin1Char(' '));
}

QString dateTime(const QString &iso)
{
    if (iso.isEmpty()) {
        return QStringLiteral("—");
    }
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(iso, Qt::ISODate);
    }
    if (!dt.isValid()) {
        // Docker 的 CreatedAt 形如 "2026-10-17 12:07:49 +0800 CST"，
        // ISODate 解析不了；这里只取前半段时间部分，避免把整串原样显示出来。
        static const QRegularExpression stamp(
            QStringLiteral("^(\\d{4}-\\d{2}-\\d{2})[ T](\\d{2}:\\d{2}:\\d{2})"));
        const QRegularExpressionMatch match = stamp.match(iso);
        if (match.hasMatch()) {
            dt = QDateTime::fromString(match.captured(1) + QLatin1Char(' ') + match.captured(2),
                                       QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        }
    }
    if (!dt.isValid()) {
        return iso;
    }
    return dt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString timestamp(const QJsonValue &value)
{
    if (value.isDouble()) {
        const qint64 ms = qint64(value.toDouble());
        if (ms <= 0) {
            return QStringLiteral("—");
        }
        return QDateTime::fromMSecsSinceEpoch(ms)
            .toLocalTime()
            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    if (value.isString()) {
        return dateTime(value.toString());
    }
    return QStringLiteral("—");
}

QString percent(double value)
{
    if (value < 0) {
        return QStringLiteral("—");
    }
    // 统一 1 位小数，避免「12.34%」和「87.5%」混排参差不齐
    return QStringLiteral("%1%").arg(value, 0, 'f', 1);
}

} // namespace Fmt

Backdrop::Backdrop(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("Backdrop"));
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
}

void Backdrop::setTheme(Theme::Id id, bool light)
{
    if (m_id == id && m_light == light) {
        return;
    }
    m_id = id;
    m_light = light;
    update();
}

void Backdrop::setRadius(int radius)
{
    if (m_radius == radius) {
        return;
    }
    m_radius = radius;
    update();
}

void Backdrop::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const Theme::Palette pal = Theme::palette(m_id, m_light);
    const QRectF r = rect();
    if (r.isEmpty()) {
        return;
    }

    // 无边框窗口不会被系统裁剪，这里自己裁出圆角，避免「四角变直角」。
    const qreal radius = m_radius >= 0 ? qreal(m_radius) : qreal(qMax(pal.radius, 8) + 2);
    const QRectF box = r.adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    if (radius > 0.5) {
        clip.addRoundedRect(box, radius, radius);
        p.setClipPath(clip);
    }

    QColor top;
    QColor bottom;
    if (m_light) {
        top = blend(pal.window, QColor("#ffffff"), 0.72);
        bottom = blend(pal.window, QColor("#000000"), 0.06);
    } else {
        top = blend(pal.window, QColor("#ffffff"), 0.07);
        bottom = blend(pal.window, QColor("#000000"), 0.32);
    }

    // 窗口本身是透明的（为了圆角），这里的底色必须完全不透明，
    // 否则桌面/壁纸会从窗口内部透出来（玻璃主题的 window 颜色带 alpha）。
    top.setAlpha(255);
    bottom.setAlpha(255);

    QLinearGradient base(r.topLeft(), r.bottomLeft());
    base.setColorAt(0.0, top);
    base.setColorAt(1.0, bottom);
    p.fillRect(r, base);

    const double w = r.width();
    const double h = r.height();

    if (pal.glass) {
        const int alphaStrong = m_light ? 132 : 104;
        const int alphaSoft = m_light ? 96 : 78;
        drawGlow(p, r, QPointF(w * 0.12, h * 0.06), std::max(w, h) * 0.62, pal.accent, alphaStrong);
        drawGlow(p, r, QPointF(w * 0.94, h * 0.20), std::max(w, h) * 0.52, QColor("#23c9a4"), alphaSoft);
        drawGlow(p, r, QPointF(w * 0.72, h * 0.98), std::max(w, h) * 0.58, QColor("#8b5cf6"), alphaSoft);
        drawGlow(p, r, QPointF(w * 0.42, h * 0.62), std::max(w, h) * 0.42, QColor("#ffffff"), m_light ? 120 : 34);

        // 顶部一层柔和高光，模拟玻璃表面的菲涅尔反射
        QLinearGradient sheen(r.topLeft(), QPointF(w * 0.35, h * 0.55));
        QColor shine = m_light ? QColor(255, 255, 255, 120) : QColor(255, 255, 255, 26);
        sheen.setColorAt(0.0, shine);
        QColor none = shine;
        none.setAlpha(0);
        sheen.setColorAt(0.55, none);
        sheen.setColorAt(1.0, none);
        p.fillRect(r, sheen);
    } else {
        drawGlow(p, r, QPointF(w * 0.86, h * 0.02), std::max(w, h) * 0.45, pal.accent,
                 m_light ? 46 : 40);
        if (m_id == Theme::Id::Win7) {
            drawGlow(p, r, QPointF(w * 0.02, h * 1.02), std::max(w, h) * 0.5, QColor("#ffffff"),
                     m_light ? 150 : 18);
        }
    }

    // 圆角描边，让窗口边缘清晰（最大值 / 全屏时半径为 0，退化为细边框）
    p.setClipping(false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(pal.border, 1));
    if (radius > 0.5) {
        p.drawRoundedRect(box, radius, radius);
    } else {
        p.drawRect(box);
    }
}

DialogWindow::DialogWindow(QWidget *parent) : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
}

int DialogWindow::cornerRadius() const
{
    const Theme::Palette pal = Theme::current();
    return qMax(pal.radius, 8) + 2;
}

void DialogWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const Theme::Palette pal = Theme::current();
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = cornerRadius();

    QPainterPath clip;
    clip.addRoundedRect(box, radius, radius);
    p.setClipPath(clip);
    p.fillRect(rect(), pal.window);

    p.setClipping(false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(pal.border, 1));
    p.drawRoundedRect(box, radius, radius);
}

void DialogWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (QWidget *win = window()) {
            if (win->windowHandle()) {
                win->windowHandle()->startSystemMove();
                event->accept();
                return;
            }
        }
    }
    QDialog::mousePressEvent(event);
}

Card::Card(QWidget *parent) : QFrame(parent)
{
    setProperty("card", true);
    m_root = new QVBoxLayout(this);
    m_root->setContentsMargins(16, 14, 16, 16);
    m_root->setSpacing(10);
    m_body = new QVBoxLayout();
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(10);
    m_root->addLayout(m_body, 1);
}

Card::Card(const QString &title, QWidget *parent) : Card(parent)
{
    setTitle(title);
}

void Card::setTitle(const QString &title)
{
    if (!m_title) {
        m_title = makeLabel(title, QStringLiteral("h2"), this);
        m_root->insertWidget(0, m_title);
        return;
    }
    m_title->setText(title);
}

StatCard::StatCard(const QString &label, QWidget *parent) : QFrame(parent)
{
    setProperty("card", true);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(4);
    lay->addWidget(makeLabel(label, QStringLiteral("statLabel"), this));
    m_value = makeLabel(QStringLiteral("—"), QStringLiteral("statValue"), this);
    lay->addWidget(m_value);
    m_hint = makeLabel(QString(), QStringLiteral("muted"), this);
    m_hint->setVisible(false);
    lay->addWidget(m_hint);
}

void StatCard::setValue(const QString &value)
{
    m_value->setText(value);
}

void StatCard::setHint(const QString &hint)
{
    m_hint->setText(hint);
    m_hint->setVisible(!hint.isEmpty());
}

void Toast::show(QWidget *anchor, const QString &text, const QString &tone)
{
    QWidget *host = anchor ? anchor->window() : nullptr;
    if (!host || text.isEmpty()) {
        return;
    }

    auto *frame = new QFrame(host);
    frame->setObjectName(QStringLiteral("Toast"));
    frame->setProperty("tone", tone);
    frame->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto *lay = new QHBoxLayout(frame);
    lay->setContentsMargins(16, 11, 16, 11);
    lay->setSpacing(0);
    auto *label = new QLabel(text, frame);
    label->setObjectName(QStringLiteral("ToastText"));
    label->setWordWrap(true);
    lay->addWidget(label);

    const int maxWidth = qBound(240, int(host->width() * 0.6), 460);
    frame->setMaximumWidth(maxWidth);
    frame->adjustSize();
    Theme::repolish(frame);

    const int w = qMin(maxWidth, qMax(frame->sizeHint().width(), label->sizeHint().width() + 34));
    frame->setFixedWidth(w);
    lay->activate();
    const int h = frame->sizeHint().height();
    frame->setGeometry((host->width() - w) / 2, host->height() - h - 30, w, h);
    frame->raise();
    frame->show();

    auto *effect = new QGraphicsOpacityEffect(frame);
    effect->setOpacity(0.0);
    frame->setGraphicsEffect(effect);

    auto *fadeIn = new QPropertyAnimation(effect, "opacity", frame);
    fadeIn->setDuration(150);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->start();

    QTimer::singleShot(2800, frame, [frame, effect]() {
        auto *fadeOut = new QPropertyAnimation(effect, "opacity", frame);
        fadeOut->setDuration(220);
        fadeOut->setStartValue(1.0);
        fadeOut->setEndValue(0.0);
        QObject::connect(fadeOut, &QPropertyAnimation::finished, frame, &QObject::deleteLater);
        fadeOut->start();
    });
}

QLabel *makeLabel(const QString &text, const QString &role, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    if (!role.isEmpty()) {
        label->setProperty("role", role);
    }
    return label;
}

QPushButton *makeButton(const QString &text, const QString &variant, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setProperty("variant", variant);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(32);
    return button;
}

QFrame *makeSeparator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setProperty("separator", true);
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(1);
    return line;
}

BasePage::BasePage(ApiClient *api, QWidget *parent) : QWidget(parent), m_api(api)
{
    setAttribute(Qt::WA_StyledBackground, false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 18, 26, 22);
    root->setSpacing(16);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("PageHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(10);

    auto *titles = new QVBoxLayout();
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(2);

    m_title = new QLabel(header);
    m_title->setObjectName(QStringLiteral("PageTitle"));
    m_subtitle = new QLabel(header);
    m_subtitle->setObjectName(QStringLiteral("PageSubtitle"));
    m_subtitle->setVisible(false);

    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    headerLayout->addLayout(titles);
    headerLayout->addStretch(1);

    m_actions = new QHBoxLayout();
    m_actions->setContentsMargins(0, 0, 0, 0);
    m_actions->setSpacing(8);
    headerLayout->addLayout(m_actions);

    root->addWidget(header);

    m_body = new QVBoxLayout();
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(16);
    root->addLayout(m_body, 1);
}

void BasePage::setPageTitle(const QString &title)
{
    m_title->setText(title);
}

void BasePage::setPageSubtitle(const QString &subtitle)
{
    m_subtitle->setText(subtitle);
    m_subtitle->setVisible(!subtitle.isEmpty());
}

void BasePage::addHeaderAction(QWidget *widget)
{
    if (!widget) {
        return;
    }
    m_actions->addWidget(widget);
}

void BasePage::showToast(const QString &text, const QString &tone)
{
    Toast::show(this, text, tone);
}

// ---------------------------------------------------------------------------
// 圆角提示气泡
// ---------------------------------------------------------------------------

namespace {

/**
 * 自绘的提示气泡。
 *
 * Windows 上 QToolTip 用的是原生提示窗口，窗口本身不透明，即使 QSS 里设了
 * border-radius，四个角也会被填成直角。这里改成半透明的无边框窗口，
 * 自己用 QPainter 画带圆角的卡片，四角就能真正透明。
 */
class RoundedToolTip : public QWidget
{
public:
    static RoundedToolTip *instance()
    {
        static RoundedToolTip *self = nullptr;
        if (!self) {
            self = new RoundedToolTip();
        }
        return self;
    }

    void showText(const QString &text, const QPoint &globalPos)
    {
        const Theme::Palette pal = Theme::current();
        m_label->setStyleSheet(QStringLiteral("background:transparent;color:%1;font-size:12px;")
                                   .arg(pal.text.name(QColor::HexRgb)));
        m_label->setText(text);
        m_label->adjustSize();

        const int pad = 10;
        const QSize hint = m_label->sizeHint();
        m_label->setGeometry(pad, pad, hint.width(), hint.height());
        resize(hint.width() + pad * 2, hint.height() + pad * 2);

        QPoint pos = globalPos + QPoint(14, 20);
        if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
            const QRect area = screen->availableGeometry();
            if (pos.x() + width() > area.right() - 4) {
                pos.setX(globalPos.x() - width() - 8);
            }
            pos.setX(qBound(area.left() + 4, pos.x(), area.right() - width() - 4));
            pos.setY(qBound(area.top() + 4, pos.y(), area.bottom() - height() - 4));
        }
        move(pos);
        show();
        raise();
        m_timer->start(6000);
    }

    void hideTip()
    {
        m_timer->stop();
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const Theme::Palette pal = Theme::current();
        const qreal radius = qMax(pal.radiusSm, 6) + 2.0;
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(pal.border, 1));
        painter.setBrush(pal.surface);
        painter.drawRoundedRect(box, radius, radius);
    }

private:
    RoundedToolTip()
    {
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setFocusPolicy(Qt::NoFocus);
        m_label = new QLabel(this);
        m_label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, [this]() { hide(); });
    }

    QLabel *m_label = nullptr;
    QTimer *m_timer = nullptr;
};

/** 全局事件过滤器：把控件自带的提示气泡换成自绘的圆角版本。 */
class ToolTipFilter : public QObject
{
public:
    explicit ToolTipFilter(QObject *parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        const QEvent::Type type = event->type();
        if (type == QEvent::ToolTip) {
            if (auto *widget = qobject_cast<QWidget *>(obj)) {
                auto *help = static_cast<QHelpEvent *>(event);
                const QString text = widget->toolTip();
                if (help && !text.isEmpty()) {
                    RoundedToolTip::instance()->showText(text, help->globalPos());
                }
            }
            return true; // 交给自绘气泡，不再让系统弹原生提示框
        }
        if (type == QEvent::Leave || type == QEvent::MouseButtonPress
            || type == QEvent::Wheel || type == QEvent::Hide || type == QEvent::FocusOut) {
            RoundedToolTip::instance()->hideTip();
        }
        return false;
    }
};

} // namespace

void makePopupRounded(QWidget *popup)
{
    if (!popup) {
        return;
    }
    popup->setWindowFlags(popup->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    popup->setAttribute(Qt::WA_TranslucentBackground, true);
}

void installRoundedToolTips()
{
    static ToolTipFilter *filter = nullptr;
    if (filter) {
        return;
    }
    filter = new ToolTipFilter(qApp);
    qApp->installEventFilter(filter);
}
