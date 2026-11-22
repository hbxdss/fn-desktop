#pragma once

#include "ui/widgets.h"

#include <QDialog>

class AppConfig;
class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QLineEdit;
class QStackedWidget;

/**
 * 首次使用向导（圆角无边框）。
 * 完成或跳过都会写 wizardCompleted；内置应用显隐与高级模式选择立即落盘。
 */
class WizardDialog : public DialogWindow
{
    Q_OBJECT

public:
    explicit WizardDialog(AppConfig *config, QWidget *parent = nullptr);

    /** 文件访问页是否额外提示「需要先在 NAS 中授权文件夹」。 */
    void setStorageHint(bool needed);

signals:
    /** 完成或跳过时发出：应用显隐 / 高级模式可能已变化，需要主窗口刷新。 */
    void applied();

private:
    void buildPages();
    void go(int index);
    void finish(bool completed);

    AppConfig *m_config = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_stepLabel = nullptr;
    QPushButton *m_prev = nullptr;
    QPushButton *m_next = nullptr;
    QPushButton *m_skip = nullptr;
    QLabel *m_storageTip = nullptr;

    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_apiPortBox = nullptr;
    QSpinBox *m_adminPortBox = nullptr;
    QCheckBox *m_video = nullptr;
    QCheckBox *m_photo = nullptr;
    QCheckBox *m_music = nullptr;
    QCheckBox *m_advanced = nullptr;

    int m_index = 0;
};
