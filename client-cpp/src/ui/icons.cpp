#include "ui/icons.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include <cmath>

namespace Icons {

namespace {

constexpr double kPi = 3.14159265358979323846;

void draw(QPainter &p, Kind kind, const QColor &color)
{
    QPen pen(color, 1.85);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    auto fill = [&p, &color](const QPainterPath &path) {
        p.setPen(Qt::NoPen);
        p.fillPath(path, color);
        p.setPen(QPen(color, 1.85));
    };

    switch (kind) {
    case Kind::Dashboard: {
        p.drawRoundedRect(QRectF(3.2, 3.2, 7.4, 7.4), 1.6, 1.6);
        p.drawRoundedRect(QRectF(13.4, 3.2, 7.4, 7.4), 1.6, 1.6);
        p.drawRoundedRect(QRectF(3.2, 13.4, 7.4, 7.4), 1.6, 1.6);
        p.drawRoundedRect(QRectF(13.4, 13.4, 7.4, 7.4), 1.6, 1.6);
        break;
    }
    case Kind::Folder: {
        QPainterPath path;
        path.moveTo(3.0, 7.2);
        path.quadTo(3.0, 5.2, 5.0, 5.2);
        path.lineTo(9.4, 5.2);
        path.lineTo(11.4, 7.4);
        path.lineTo(19.2, 7.4);
        path.quadTo(21.0, 7.4, 21.0, 9.2);
        path.lineTo(21.0, 17.4);
        path.quadTo(21.0, 19.2, 19.2, 19.2);
        path.lineTo(4.8, 19.2);
        path.quadTo(3.0, 19.2, 3.0, 17.4);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Kind::Docker: {
        p.drawRoundedRect(QRectF(3.2, 11.0, 17.6, 8.0), 2.0, 2.0);
        p.drawRoundedRect(QRectF(6.4, 7.4, 3.4, 3.4), 0.8, 0.8);
        p.drawRoundedRect(QRectF(10.4, 7.4, 3.4, 3.4), 0.8, 0.8);
        p.drawRoundedRect(QRectF(14.4, 7.4, 3.4, 3.4), 0.8, 0.8);
        p.drawRoundedRect(QRectF(10.4, 3.6, 3.4, 3.4), 0.8, 0.8);
        break;
    }
    case Kind::Users: {
        p.drawEllipse(QPointF(9.2, 8.0), 3.3, 3.3);
        QPainterPath body;
        body.moveTo(3.4, 19.6);
        body.quadTo(3.4, 13.8, 9.2, 13.8);
        body.quadTo(15.0, 13.8, 15.0, 19.6);
        p.drawPath(body);
        p.drawEllipse(QPointF(17.2, 9.0), 2.6, 2.6);
        QPainterPath arm;
        arm.moveTo(17.2, 13.7);
        arm.quadTo(21.2, 14.2, 21.2, 18.4);
        p.drawPath(arm);
        break;
    }
    case Kind::Log: {
        p.drawEllipse(QPointF(5.6, 6.4), 1.2, 1.2);
        p.drawEllipse(QPointF(5.6, 12.0), 1.2, 1.2);
        p.drawEllipse(QPointF(5.6, 17.6), 1.2, 1.2);
        p.drawLine(QPointF(9.6, 6.4), QPointF(20.2, 6.4));
        p.drawLine(QPointF(9.6, 12.0), QPointF(20.2, 12.0));
        p.drawLine(QPointF(9.6, 17.6), QPointF(20.2, 17.6));
        break;
    }
    case Kind::Globe: {
        p.drawEllipse(QPointF(12, 12), 8.6, 8.6);
        p.drawEllipse(QPointF(12, 12), 3.8, 8.6);
        p.drawLine(QPointF(3.4, 12.0), QPointF(20.6, 12.0));
        p.drawArc(QRectF(3.4, 6.2, 17.2, 11.6), 0, 180 * 16);
        p.drawArc(QRectF(3.4, 6.2, 17.2, 11.6), 180 * 16, 180 * 16);
        break;
    }
    case Kind::Gear: {
        p.drawEllipse(QPointF(12, 12), 3.0, 3.0);
        for (int i = 0; i < 8; ++i) {
            const double angle = i * kPi / 4.0;
            const double x1 = 12 + std::cos(angle) * 5.6;
            const double y1 = 12 + std::sin(angle) * 5.6;
            const double x2 = 12 + std::cos(angle) * 8.4;
            const double y2 = 12 + std::sin(angle) * 8.4;
            p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
        }
        break;
    }
    case Kind::Refresh: {
        p.drawArc(QRectF(3.6, 3.6, 16.8, 16.8), 40 * 16, 280 * 16);
        QPainterPath arrow;
        arrow.moveTo(18.4, 2.6);
        arrow.lineTo(19.9, 6.6);
        arrow.lineTo(15.9, 7.4);
        p.drawPath(arrow);
        break;
    }
    case Kind::Back: {
        QPainterPath path;
        path.moveTo(14.6, 5.2);
        path.lineTo(8.0, 12.0);
        path.lineTo(14.6, 18.8);
        p.drawPath(path);
        break;
    }
    case Kind::Forward: {
        QPainterPath path;
        path.moveTo(9.4, 5.2);
        path.lineTo(16.0, 12.0);
        path.lineTo(9.4, 18.8);
        p.drawPath(path);
        break;
    }
    case Kind::Home: {
        QPainterPath path;
        path.moveTo(3.6, 10.6);
        path.lineTo(12.0, 3.6);
        path.lineTo(20.4, 10.6);
        p.drawPath(path);
        QPainterPath body;
        body.moveTo(5.8, 12.4);
        body.lineTo(5.8, 20.0);
        body.lineTo(18.2, 20.0);
        body.lineTo(18.2, 12.4);
        p.drawPath(body);
        break;
    }
    case Kind::Plus: {
        p.drawLine(QPointF(12, 4.6), QPointF(12, 19.4));
        p.drawLine(QPointF(4.6, 12), QPointF(19.4, 12));
        break;
    }
    case Kind::Trash: {
        p.drawLine(QPointF(4.4, 7.0), QPointF(19.6, 7.0));
        p.drawLine(QPointF(9.4, 7.0), QPointF(9.4, 4.4));
        p.drawLine(QPointF(9.4, 4.4), QPointF(14.6, 4.4));
        p.drawLine(QPointF(14.6, 4.4), QPointF(14.6, 7.0));
        QPainterPath bin;
        bin.moveTo(6.2, 7.0);
        bin.lineTo(7.2, 19.6);
        bin.lineTo(16.8, 19.6);
        bin.lineTo(17.8, 7.0);
        p.drawPath(bin);
        p.drawLine(QPointF(10.4, 10.4), QPointF(10.8, 16.4));
        p.drawLine(QPointF(13.6, 10.4), QPointF(13.2, 16.4));
        break;
    }
    case Kind::Edit: {
        QPainterPath path;
        path.moveTo(4.2, 19.6);
        path.lineTo(5.4, 15.0);
        path.lineTo(16.2, 4.2);
        path.lineTo(19.8, 7.8);
        path.lineTo(9.0, 18.6);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(QPointF(14.0, 6.4), QPointF(17.6, 10.0));
        break;
    }
    case Kind::Power: {
        p.drawArc(QRectF(4.4, 5.0, 15.2, 15.2), 120 * 16, 300 * 16);
        p.drawLine(QPointF(12, 3.2), QPointF(12, 11.0));
        break;
    }
    case Kind::Play: {
        QPainterPath path;
        path.moveTo(7.4, 4.6);
        path.lineTo(19.0, 12.0);
        path.lineTo(7.4, 19.4);
        path.closeSubpath();
        fill(path);
        break;
    }
    case Kind::Stop: {
        QPainterPath path;
        path.addRoundedRect(QRectF(6.2, 6.2, 11.6, 11.6), 1.8, 1.8);
        fill(path);
        break;
    }
    case Kind::Restart: {
        p.drawArc(QRectF(3.8, 3.8, 16.4, 16.4), 20 * 16, 300 * 16);
        QPainterPath arrow;
        arrow.moveTo(16.4, 3.0);
        arrow.lineTo(20.2, 5.4);
        arrow.lineTo(16.6, 8.0);
        p.drawPath(arrow);
        break;
    }
    case Kind::Sun: {
        p.drawEllipse(QPointF(12, 12), 4.2, 4.2);
        for (int i = 0; i < 8; ++i) {
            const double angle = i * kPi / 4.0;
            p.drawLine(QPointF(12 + std::cos(angle) * 6.8, 12 + std::sin(angle) * 6.8),
                       QPointF(12 + std::cos(angle) * 9.4, 12 + std::sin(angle) * 9.4));
        }
        break;
    }
    case Kind::Moon: {
        QPainterPath path;
        path.addEllipse(QRectF(3.8, 3.8, 16.4, 16.4));
        QPainterPath cut;
        cut.addEllipse(QRectF(9.0, 1.4, 15.0, 15.0));
        path = path.subtracted(cut);
        fill(path);
        break;
    }
    case Kind::Palette: {
        p.drawEllipse(QPointF(12, 12), 8.6, 8.6);
        p.setBrush(color);
        p.drawEllipse(QPointF(8.6, 9.4), 1.25, 1.25);
        p.drawEllipse(QPointF(13.0, 7.6), 1.25, 1.25);
        p.drawEllipse(QPointF(15.8, 11.6), 1.25, 1.25);
        break;
    }
    case Kind::Check: {
        QPainterPath path;
        path.moveTo(4.8, 12.6);
        path.lineTo(9.8, 17.6);
        path.lineTo(19.2, 6.6);
        p.drawPath(path);
        break;
    }
    case Kind::Close: {
        p.drawLine(QPointF(6.2, 6.2), QPointF(17.8, 17.8));
        p.drawLine(QPointF(17.8, 6.2), QPointF(6.2, 17.8));
        break;
    }
    case Kind::Key: {
        p.drawEllipse(QPointF(8.4, 8.4), 4.2, 4.2);
        p.drawLine(QPointF(11.4, 11.4), QPointF(19.6, 19.6));
        p.drawLine(QPointF(15.6, 15.6), QPointF(18.2, 13.0));
        p.drawLine(QPointF(17.8, 17.8), QPointF(20.2, 15.4));
        break;
    }
    case Kind::Download: {
        p.drawLine(QPointF(12, 3.8), QPointF(12, 15.2));
        QPainterPath arrow;
        arrow.moveTo(7.4, 10.8);
        arrow.lineTo(12.0, 15.4);
        arrow.lineTo(16.6, 10.8);
        p.drawPath(arrow);
        QPainterPath tray;
        tray.moveTo(4.6, 16.4);
        tray.lineTo(4.6, 19.6);
        tray.lineTo(19.4, 19.6);
        tray.lineTo(19.4, 16.4);
        p.drawPath(tray);
        break;
    }
    case Kind::Upload: {
        p.drawLine(QPointF(12, 15.4), QPointF(12, 4.0));
        QPainterPath arrow;
        arrow.moveTo(7.4, 8.4);
        arrow.lineTo(12.0, 3.8);
        arrow.lineTo(16.6, 8.4);
        p.drawPath(arrow);
        QPainterPath tray;
        tray.moveTo(4.6, 16.4);
        tray.lineTo(4.6, 19.6);
        tray.lineTo(19.4, 19.6);
        tray.lineTo(19.4, 16.4);
        p.drawPath(tray);
        break;
    }
    case Kind::ChevronDown: {
        QPainterPath path;
        path.moveTo(6.4, 9.4);
        path.lineTo(12.0, 15.0);
        path.lineTo(17.6, 9.4);
        p.drawPath(path);
        break;
    }
    case Kind::ChevronRight: {
        QPainterPath path;
        path.moveTo(9.4, 6.2);
        path.lineTo(15.2, 12.0);
        path.lineTo(9.4, 17.8);
        p.drawPath(path);
        break;
    }
    case Kind::Server: {
        p.drawRoundedRect(QRectF(3.4, 3.8, 17.2, 6.8), 1.6, 1.6);
        p.drawRoundedRect(QRectF(3.4, 13.4, 17.2, 6.8), 1.6, 1.6);
        p.setBrush(color);
        p.drawEllipse(QPointF(7.0, 7.2), 0.9, 0.9);
        p.drawEllipse(QPointF(7.0, 16.8), 0.9, 0.9);
        break;
    }
    case Kind::Shield: {
        QPainterPath path;
        path.moveTo(12.0, 3.2);
        path.lineTo(20.0, 6.0);
        path.lineTo(20.0, 12.4);
        path.quadTo(20.0, 18.4, 12.0, 20.8);
        path.quadTo(4.0, 18.4, 4.0, 12.4);
        path.lineTo(4.0, 6.0);
        path.closeSubpath();
        p.drawPath(path);
        QPainterPath tick;
        tick.moveTo(8.4, 11.8);
        tick.lineTo(11.2, 14.6);
        tick.lineTo(15.8, 9.6);
        p.drawPath(tick);
        break;
    }
    case Kind::Bell: {
        QPainterPath path;
        path.moveTo(6.0, 16.4);
        path.lineTo(6.0, 10.6);
        path.quadTo(6.0, 5.4, 12.0, 5.4);
        path.quadTo(18.0, 5.4, 18.0, 10.6);
        path.lineTo(18.0, 16.4);
        path.lineTo(20.0, 18.2);
        path.lineTo(4.0, 18.2);
        path.closeSubpath();
        p.drawPath(path);
        p.drawArc(QRectF(10.0, 18.0, 4.0, 3.4), 200 * 16, 140 * 16);
        break;
    }
    case Kind::Search: {
        p.drawEllipse(QPointF(10.6, 10.6), 6.2, 6.2);
        p.drawLine(QPointF(15.2, 15.2), QPointF(20.2, 20.2));
        break;
    }
    case Kind::Film: {
        // 胶片框 + 播放三角
        QRectF r(3.6, 5.6, 16.8, 12.8);
        p.drawRoundedRect(r, 2.2, 2.2);
        p.drawLine(QPointF(r.left() + 3.2, r.top()), QPointF(r.left() + 3.2, r.bottom()));
        p.drawLine(QPointF(r.right() - 3.2, r.top()), QPointF(r.right() - 3.2, r.bottom()));
        QPainterPath tri;
        tri.moveTo(10.8, 9.4);
        tri.lineTo(10.8, 14.6);
        tri.lineTo(15.0, 12.0);
        tri.closeSubpath();
        p.drawPath(tri);
        break;
    }
    case Kind::Photo: {
        // 相片框 + 太阳 + 山峰
        QRectF r(3.6, 5.6, 16.8, 13.0);
        p.drawRoundedRect(r, 2.0, 2.0);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(8.4, 9.8), 1.3, 1.3);
        p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath mtn;
        mtn.moveTo(5.2, 16.4);
        mtn.lineTo(9.4, 11.8);
        mtn.lineTo(12.4, 15.0);
        mtn.lineTo(14.6, 12.8);
        mtn.lineTo(18.8, 16.4);
        p.drawPath(mtn);
        break;
    }
    case Kind::Music: {
        // 两个八分音符 + 符杠
        p.drawLine(QPointF(9.0, 17.4), QPointF(9.0, 7.2));
        p.drawLine(QPointF(16.2, 15.4), QPointF(16.2, 5.4));
        p.drawLine(QPointF(9.0, 7.2), QPointF(16.2, 5.4));
        p.drawLine(QPointF(9.0, 9.6), QPointF(16.2, 7.8));
        QPainterPath note1;
        note1.addEllipse(QPointF(6.4, 17.4), 2.7, 2.0);
        QPainterPath note2;
        note2.addEllipse(QPointF(13.6, 15.4), 2.7, 2.0);
        p.fillPath(note1, color);
        p.fillPath(note2, color);
        break;
    }
    }
}

} // namespace

QPixmap pixmap(Kind kind, const QColor &color, int size)
{
    qreal dpr = 1.0;
    if (QGuiApplication::instance()) {
        dpr = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
    }
    if (dpr <= 0.0) {
        dpr = 1.0;
    }

    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 24.0, size / 24.0);
    draw(p, kind, color);
    p.end();
    return pm;
}

QIcon icon(Kind kind, const QColor &color, int size)
{
    return QIcon(pixmap(kind, color, size));
}

QIcon icon(Kind kind, const QColor &normal, const QColor &active, int size)
{
    QIcon result;
    result.addPixmap(pixmap(kind, normal, size), QIcon::Normal, QIcon::Off);
    result.addPixmap(pixmap(kind, active, size), QIcon::Normal, QIcon::On);
    result.addPixmap(pixmap(kind, active, size), QIcon::Selected, QIcon::Off);
    result.addPixmap(pixmap(kind, active, size), QIcon::Selected, QIcon::On);
    return result;
}

} // namespace Icons
