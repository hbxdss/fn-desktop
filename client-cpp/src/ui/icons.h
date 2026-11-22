#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

/**
 * 纯代码绘制的线性图标集（不依赖任何外部图片资源），
 * 以便在任何主题 / 任意缩放比例下都保持清晰，并跟随主题颜色变化。
 */
namespace Icons {

enum class Kind {
    Dashboard,
    Folder,
    Docker,
    Users,
    Log,
    Globe,
    Gear,
    Refresh,
    Back,
    Forward,
    Home,
    Plus,
    Trash,
    Edit,
    Power,
    Play,
    Stop,
    Restart,
    Sun,
    Moon,
    Palette,
    Check,
    Close,
    Key,
    Download,
    Upload,
    ChevronDown,
    ChevronRight,
    Server,
    Shield,
    Bell,
    Search,
    Film,
    Photo,
    Music
};

QPixmap pixmap(Kind kind, const QColor &color, int size = 18);
QIcon icon(Kind kind, const QColor &color, int size = 18);

/** 图标：普通状态一个颜色，选中状态另一个颜色。 */
QIcon icon(Kind kind, const QColor &normal, const QColor &active, int size = 18);

} // namespace Icons
