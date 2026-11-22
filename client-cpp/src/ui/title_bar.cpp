#include "ui/title_bar.h"

#include "ui/icons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QWindow>

namespace {

QPixmap glyph(const QColor &color, int size, const QString &kind)
{
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color);
    pen.setWidthF(1.2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);

    const qreal s = size;
    if (kind == QLatin1String("min")) {
        p.drawLine(QPointF(s * 0.28, s * 0.5), QPointF(s * 0.72, s * 0.5));
    } else if (kind == QLatin1String("max")) {
        p.drawRect(QRectF(s * 0.30, s * 0.30, s * 0.40, s * 0.40));
    } else if (kind == QLatin1String("restore")) {
        p.drawRect(QRectF(s * 0.26, s * 0.34, s * 0.36, s * 0.36));
        p.drawRect(QRectF(s * 0.38, s * 0.24, s * 0.36, s * 0.36));
    } else if (kind == QLatin1String("close")) {
        p.drawLine(QPointF(s * 0.32, s * 0.32), QPointF(s * 0.68, s * 0.68));
        p.drawLine(QPointF(s * 0.68, s * 0.32), QPointF(s * 0.32, s * 0.68));
    }
    p.end();
    return pm;
}

QColor titleColor(Theme::Id id, bool light)
{
    return Theme::palette(id, light).titleText;
}

} // namespace

TitleBar::TitleBar(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("TitleBar"));
    setFixedHeight(42);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 8, 0);
    layout->setSpacing(9);

    m_logo = new QLabel(this);
    m_logo->setFixedSize(20, 20);
    m_logo->setScaledContents(true);
    m_logo->setPixmap(QPixmap(QStringLiteral(":/app.png"))
                          .scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_logo->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("TitleBarTitle"));
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_subtitle = new QLabel(this);
    m_subtitle->setObjectName(QStringLiteral("TitleBarSub"));
    m_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_subtitle->setVisible(false);

    m_appearance = new QToolButton(this);
    m_appearance->setObjectName(QStringLiteral("WinBtn"));
    m_appearance->setToolTip(QStringLiteral("外观主题"));
    m_appearance->setCursor(Qt::PointingHandCursor);
    m_appearance->setFixedSize(30, 28);
    m_appearance->setPopupMode(QToolButton::InstantPopup);
    m_appearance->setArrowType(Qt::NoArrow);

    m_minimize = new QToolButton(this);
    m_minimize->setObjectName(QStringLiteral("WinBtn"));
    m_minimize->setToolTip(QStringLiteral("最小化"));
    m_minimize->setCursor(Qt::PointingHandCursor);
    m_minimize->setFixedSize(40, 28);

    m_maximize = new QToolButton(this);
    m_maximize->setObjectName(QStringLiteral("WinBtn"));
    m_maximize->setToolTip(QStringLiteral("最大化"));
    m_maximize->setCursor(Qt::PointingHandCursor);
    m_maximize->setFixedSize(40, 28);

    m_close = new QToolButton(this);
    m_close->setObjectName(QStringLiteral("WinBtn"));
    m_close->setProperty("role", QStringLiteral("close"));
    m_close->setToolTip(QStringLiteral("关闭"));
    m_close->setCursor(Qt::PointingHandCursor);
    m_close->setFixedSize(40, 28);

    layout->addWidget(m_logo);
    layout->addWidget(m_title);
    layout->addWidget(m_subtitle);
    layout->addStretch(1);
    layout->addWidget(m_appearance);
    layout->addSpacing(4);
    layout->addWidget(m_minimize);
    layout->addWidget(m_maximize);
    layout->addWidget(m_close);

    connect(m_minimize, &QToolButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_maximize, &QToolButton::clicked, this, &TitleBar::maximizeRequested);
    connect(m_close, &QToolButton::clicked, this, &TitleBar::closeRequested);

    reloadTheme(Theme::Id::Win11, false);
}

void TitleBar::setTitle(const QString &title, const QString &subtitle)
{
    m_title->setText(title);
    m_subtitle->setText(subtitle);
    m_subtitle->setVisible(!subtitle.isEmpty());
}

void TitleBar::setMaximized(bool maximized)
{
    m_maximized = maximized;
    updateWindowButtons();
}

void TitleBar::reloadTheme(Theme::Id id, bool light)
{
    m_themeId = id;
    m_light = light;
    const QColor color = titleColor(id, light);

    m_appearance->setIcon(QIcon());
    m_appearance->setText(QStringLiteral("外观"));
    m_appearance->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_appearance->setFixedSize(52, 26);
    QFont f = m_appearance->font();
    f.setPointSizeF(9.0);
    m_appearance->setFont(f);

    m_minimize->setIcon(QIcon(glyph(color, 16, QStringLiteral("min"))));
    m_close->setIcon(QIcon(glyph(color, 16, QStringLiteral("close"))));
    updateWindowButtons();
    Theme::repolish(m_appearance);
}

void TitleBar::setAppearanceMenu(QMenu *menu)
{
    m_appearance->setMenu(menu);
}

void TitleBar::updateWindowButtons()
{
    const QColor color = titleColor(m_themeId, m_light);
    const QPixmap icon = glyph(color, 16, m_maximized ? QStringLiteral("restore")
                                                      : QStringLiteral("max"));
    m_maximize->setIcon(QIcon(icon));
    m_maximize->setToolTip(m_maximized ? QStringLiteral("还原") : QStringLiteral("最大化"));
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QWidget *win = window();
        if (win && win->windowHandle()) {
            win->windowHandle()->startSystemMove();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit maximizeRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
