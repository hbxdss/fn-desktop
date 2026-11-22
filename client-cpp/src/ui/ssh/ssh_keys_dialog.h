#pragma once

#include "ui/widgets.h"

class QLabel;
class QListWidget;

/**
 * SSH 密钥管理：生成 / 导入 / 查看公钥 / 删除。
 * 密钥统一放在 AppData/FNOS/FnDesktopPlus/keys 下，会话里可以直接选用。
 */
class SshKeysDialog : public DialogWindow
{
    Q_OBJECT

public:
    explicit SshKeysDialog(QWidget *parent = nullptr);

private:
    void buildUi();
    void reload();
    void generateKey();
    void importKey();
    void deleteSelected();
    void copyPublicKey();

    QString selectedKeyPath() const;

    QListWidget *m_list = nullptr;
    QLabel *m_hint = nullptr;
};
