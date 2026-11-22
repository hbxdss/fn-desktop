#pragma once

#include <QDialog>
#include <QFrame>
#include <QJsonValue>
#include <QString>
#include <QWidget>

#include "ui/theme.h"

class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;
class ApiClient;

/** 统一的数值 / 时间格式化，避免各页面各写一套。 */
namespace Fmt {
QString bytes(qint64 value);
QString duration(qint64 seconds);
QString dateTime(const QString &iso);
/** 兼容后端返回的毫秒时间戳（数字）与 ISO 字符串。 */
QString timestamp(const QJsonValue &value);
QString percent(double value);
} // namespace Fmt

/** 窗口背景：主题渐变 + 液态玻璃光斑（始终位于内容最底层）。 */
class Backdrop : public QWidget
{
    Q_OBJECT

public:
    explicit Backdrop(QWidget *parent = nullptr);

    void setTheme(Theme::Id id, bool light);
    /** 圆角半径；传入 -1 表示跟随当前主题。最大化 / 全屏时应传 0。 */
    void setRadius(int radius);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Theme::Id m_id = Theme::Id::Win11;
    bool m_light = false;
    int m_radius = -1;
};

/**
 * 无边框圆角弹窗基类。
 *
 * 无边框顶层窗口在 Windows 上不会被裁剪，QSS 的 border-radius 只影响绘制内容、
 * 窗口本身仍是直角。这里统一改为「透明窗口 + 自绘圆角背景」，四角才是真正的圆角。
 */
class DialogWindow : public QDialog
{
    Q_OBJECT

public:
    explicit DialogWindow(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    int cornerRadius() const;
};

/** 卡片容器，统一圆角 / 描边 / 背景。 */
class Card : public QFrame
{
    Q_OBJECT

public:
    explicit Card(QWidget *parent = nullptr);
    explicit Card(const QString &title, QWidget *parent = nullptr);

    QVBoxLayout *body() const { return m_body; }
    void setTitle(const QString &title);

private:
    QVBoxLayout *m_root = nullptr;
    QVBoxLayout *m_body = nullptr;
    QLabel *m_title = nullptr;
};

/** 指标卡：标题 + 大号数值 + 补充说明。 */
class StatCard : public QFrame
{
    Q_OBJECT

public:
    explicit StatCard(const QString &label, QWidget *parent = nullptr);

    void setValue(const QString &value);
    void setHint(const QString &hint);

private:
    QLabel *m_value = nullptr;
    QLabel *m_hint = nullptr;
};

/** 轻量提示条（右下角浮出，自动消失，不使用系统弹窗）。 */
class Toast
{
public:
    static void show(QWidget *anchor, const QString &text, const QString &tone = QStringLiteral("info"));
};

QLabel *makeLabel(const QString &text, const QString &role = QString(), QWidget *parent = nullptr);
QPushButton *makeButton(const QString &text, const QString &variant = QStringLiteral("ghost"), QWidget *parent = nullptr);
QFrame *makeSeparator(QWidget *parent = nullptr);

/**
 * 安装全局的圆角提示气泡。
 *
 * Windows 原生 QToolTip 窗口不透明，圆角在四角会变成直角；这里用应用级事件
 * 过滤器拦截 QEvent::ToolTip，改用自绘的半透明圆角窗口显示提示文字。
 */
void installRoundedToolTips();

/**
 * 给弹出窗口（QMenu 等）打开透明背景，QSS 的 border-radius 才能真正裁出圆角。
 * 必须在窗口第一次显示之前调用。
 */
void makePopupRounded(QWidget *popup);

/** 所有页面的基类：统一页头（标题 / 副标题 / 右侧操作区）与内容区。 */
class BasePage : public QWidget
{
    Q_OBJECT

public:
    explicit BasePage(ApiClient *api, QWidget *parent = nullptr);

    virtual void refresh() {}
    virtual void reloadTheme() {}

    void setPageTitle(const QString &title);
    void setPageSubtitle(const QString &subtitle);
    void addHeaderAction(QWidget *widget);
    void showToast(const QString &text, const QString &tone = QStringLiteral("info"));

protected:
    ApiClient *api() const { return m_api; }
    QVBoxLayout *body() const { return m_body; }

private:
    ApiClient *m_api = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    QHBoxLayout *m_actions = nullptr;
    QVBoxLayout *m_body = nullptr;
};
