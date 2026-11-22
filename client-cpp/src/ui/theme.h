#pragma once

#include <QColor>
#include <QString>

class QWidget;

/**
 * 主题体系：四套内置主题（主题1 ~ 主题4），风格分别为经典毛玻璃、扁平直角、
 * 云母圆角、液态玻璃。每个主题都有完整的浅色 + 深色配色，整个客户端（标题栏、
 * 侧栏、卡片、表格、输入框、弹窗、内嵌网页外壳）统一使用同一套色板，
 * 不会再出现「一半白一半黑」。
 */
namespace Theme {

enum class Id {
    Win7,   // 主题1
    Win10,  // 主题2
    Win11,  // 主题3
    Glass   // 主题4
};

struct Palette {
    bool light = false;
    bool glass = false;
    int radius = 10;
    int radiusSm = 8;
    QColor window;
    QColor surface;
    QColor surfaceAlt;
    QColor border;
    QColor text;
    QColor muted;
    QColor accent;
    QColor accentText;
    QColor danger;
    QColor success;
    QColor warning;
    QColor sidebar;
    QColor sidebarText;
    QColor titleBar;
    QColor titleText;
    QColor hover;
    QColor inputBg;
    QString fontFamily;
};

Id idFromString(const QString &value);
QString toString(Id id);
QString label(Id id);
QString description(Id id);

Palette palette(Id id, bool light, const QString &accentHex = QString());
QString styleSheet(Id id, bool light, const QString &accentHex = QString());

/** 应用全局样式（会同时刷新所有控件的动态属性样式）。 */
void apply(Id id, bool light, const QString &accentHex = QString());

/** 最近一次 apply() 生效的色板（页面刷新图标颜色时使用）。 */
Palette current();

/** 让单个控件按当前样式表重新计算外观（动态属性变化后调用）。 */
void repolish(QWidget *widget);

/** 生成主题所需的辅助图片（下拉箭头 / 对勾），返回可用于 QSS 的 url() 内容。 */
QString assetUrl(const QString &name, const QColor &color);

} // namespace Theme
