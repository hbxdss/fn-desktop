#pragma once

#include "core/config.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QVector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

/** 设置：外观（主题1 ~ 主题4 / 深浅色 / 任意自定义强调色）、服务器地址、下载目录、账号偏好。 */
class SettingsPage : public BasePage
{
    Q_OBJECT

public:
    explicit SettingsPage(AppConfig *config, ApiClient *api, QWidget *parent = nullptr);

    void refresh() override;
    void reloadTheme() override;

signals:
    /** 外观变化：立即预览（由主窗口统一应用到全局样式）。 */
    void appearanceChanged(Theme::Id id, bool light, const QString &accent);
    /** 服务器地址变化：需要重新加载网页管理后台。 */
    void serverChanged();
    /** 高级模式开关变化：主窗口重建侧栏分组 + 赛博朋克皮肤。 */
    void advancedModeChanged(bool enabled);

private:
    void buildAppearanceCard();
    void buildServerCard();
    void buildAccountCard();
    void loadOfficialApiStatus();
    void loadFromConfig();
    void save();
    void pickTheme(Theme::Id id);
    void pickAccent(const QString &hex);
    void syncThemeCards();
    void syncAccentChips();
    void syncModeButtons();
    void pickAccentColor();
    void browseDownloadDir();

    AppConfig *m_config = nullptr;

    QVBoxLayout *m_contentLayout = nullptr;

    QWidget *m_themeRow = nullptr;
    QWidget *m_accentRow = nullptr;
    QPushButton *m_darkButton = nullptr;
    QPushButton *m_lightButton = nullptr;
    QLabel *m_accentValue = nullptr;

    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_adminPortBox = nullptr;
    QLabel *m_apiInfo = nullptr;
    QLabel *m_officialApiLabel = nullptr;
    QLineEdit *m_downloadEdit = nullptr;
    QCheckBox *m_rememberBox = nullptr;
    QCheckBox *m_advancedModeBox = nullptr;
    QLabel *m_hint = nullptr;

    Theme::Id m_theme = Theme::Id::Win11;
    bool m_light = false;
    QString m_accent;
    bool m_loading = false;
};
