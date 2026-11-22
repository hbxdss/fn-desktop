#pragma once

#include <QDialog>

#include "ui/theme.h"

class QComboBox;
class QLabel;
class QPushButton;

/**
 * 自定义外观对话框：主题（主题1 ~ 主题4）+ 颜色模式 + 强调色（预设 / 任意自定义色）。
 *
 * 任何改动都会发出 previewChanged()，由主窗口以「不落盘」的方式立即预览；
 * 点击「应用」后由主窗口持久化，点击「取消」则回滚到打开对话框前的状态。
 */
class AppearanceDialog : public QDialog
{
    Q_OBJECT

public:
    AppearanceDialog(Theme::Id id, bool light, const QString &accent, QWidget *parent = nullptr);

    Theme::Id themeId() const { return m_id; }
    bool light() const { return m_light; }
    QString accent() const { return m_accent; }

signals:
    /** 实时预览（尚未保存）。 */
    void previewChanged(Theme::Id id, bool light, const QString &accent);

private:
    void buildUi();
    void emitPreview();
    void pickColor();
    void updateAccentUi();
    void syncAccentBox();

    Theme::Id m_id = Theme::Id::Win11;
    bool m_light = false;
    QString m_accent;

    QComboBox *m_themeBox = nullptr;
    QComboBox *m_modeBox = nullptr;
    QComboBox *m_accentBox = nullptr;
    QPushButton *m_colorButton = nullptr;
    QLabel *m_swatch = nullptr;
    QLabel *m_hexLabel = nullptr;
    bool m_loading = false;
};
