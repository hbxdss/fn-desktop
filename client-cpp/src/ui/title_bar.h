#pragma once

#include <QWidget>

#include "ui/theme.h"

class QLabel;
class QMenu;
class QToolButton;

/**
 * 自绘标题栏：支持拖拽移动、双击最大化、最小化 / 最大化 / 关闭，
 * 以及「外观」主题入口。窗口本身为无边框模式。
 */
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    void setTitle(const QString &title, const QString &subtitle = QString());
    void setMaximized(bool maximized);
    void reloadTheme(Theme::Id id, bool light);
    void setAppearanceMenu(QMenu *menu);

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void updateWindowButtons();

    QLabel *m_logo = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    QToolButton *m_appearance = nullptr;
    QToolButton *m_minimize = nullptr;
    QToolButton *m_maximize = nullptr;
    QToolButton *m_close = nullptr;
    Theme::Id m_themeId = Theme::Id::Win11;
    bool m_light = false;
    bool m_maximized = false;
};
