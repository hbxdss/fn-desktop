#pragma once

#include <QJsonArray>
#include <QJsonObject>

#include "ui/widgets.h"

class QLabel;
class QPushButton;
class QTableWidget;

/** 用户管理：账号增删改、角色与权限位分配、密码重置。 */
class UsersPage : public BasePage
{
    Q_OBJECT

public:
    explicit UsersPage(ApiClient *api, QWidget *parent = nullptr);

    void refresh() override;
    void reloadTheme() override;

private:
    void buildUi();
    void loadUsers();
    void loadCatalog();
    void fillTable(const QJsonArray &items);
    void openEditor(const QJsonObject &user, bool creating);
    void resetPassword(const QJsonObject &user);
    void removeUser(const QJsonObject &user);
    int grantedCount(int perms) const;
    int totalPermissions() const;

    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;

    QJsonObject m_catalog;
    QJsonArray m_users;
    bool m_forbidden = false;
};
