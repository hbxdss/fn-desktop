#include "ui/pages/users_page.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include "core/api_client.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>

namespace {

/** 用户新建 / 编辑对话框：用户名、密码、角色、权限位。 */
class UserDialog : public DialogWindow
{
public:
    UserDialog(const QJsonObject &catalog, const QJsonObject &user, bool creating, QWidget *parent)
        : DialogWindow(parent), m_creating(creating)
    {
        setWindowTitle(creating ? QStringLiteral("新建用户") : QStringLiteral("编辑用户"));
        setModal(true);
        resize(580, 620);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(22, 20, 22, 18);
        root->setSpacing(14);

        auto *header = new QHBoxLayout();
        header->setContentsMargins(0, 0, 0, 0);
        header->addWidget(makeLabel(creating ? QStringLiteral("新建用户") : QStringLiteral("编辑用户"),
                                    QStringLiteral("h2"), this));
        header->addStretch(1);
        root->addLayout(header);

        auto *form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(14);
        form->setVerticalSpacing(10);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_username = new QLineEdit(this);
        m_username->setPlaceholderText(QStringLiteral("3~32 位字母、数字、下划线、点或短横线"));
        m_username->setText(user.value(QStringLiteral("username")).toString());
        form->addRow(QStringLiteral("用户名"), m_username);

        if (creating) {
            m_password = new QLineEdit(this);
            m_password->setEchoMode(QLineEdit::Password);
            m_password->setPlaceholderText(QStringLiteral("至少 8 位，需包含字母与数字"));
            form->addRow(QStringLiteral("初始密码"), m_password);
        }

        m_role = new QComboBox(this);
        const QJsonArray roles = catalog.value(QStringLiteral("roles")).toArray();
        for (const QJsonValue &value : roles) {
            const QJsonObject role = value.toObject();
            const QString key = role.value(QStringLiteral("key")).toString();
            if (key == QLatin1String("super")) {
                continue; // 超级管理员唯一，不可指定
            }
            m_role->addItem(role.value(QStringLiteral("label")).toString(), key);
        }
        if (m_role->count() == 0) {
            m_role->addItem(QStringLiteral("普通用户"), QStringLiteral("user"));
            m_role->addItem(QStringLiteral("管理员"), QStringLiteral("admin"));
        }
        const QString currentRole = user.value(QStringLiteral("role")).toString();
        const int roleIndex = m_role->findData(currentRole);
        if (roleIndex >= 0) {
            m_role->setCurrentIndex(roleIndex);
        }
        form->addRow(QStringLiteral("角色"), m_role);

        root->addLayout(form);

        auto *permTitle = makeLabel(QStringLiteral("权限"), QStringLiteral("h3"), this);
        root->addWidget(permTitle);

        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        auto *permHost = new QWidget(scroll);
        auto *permLayout = new QVBoxLayout(permHost);
        permLayout->setContentsMargins(2, 2, 8, 2);
        permLayout->setSpacing(10);

        const int perms = user.value(QStringLiteral("perms")).toInt();
        const QJsonArray groups = catalog.value(QStringLiteral("groups")).toArray();
        for (const QJsonValue &groupValue : groups) {
            const QJsonObject group = groupValue.toObject();
            auto *groupLabel = makeLabel(group.value(QStringLiteral("label")).toString(),
                                         QStringLiteral("muted"), permHost);
            permLayout->addWidget(groupLabel);

            auto *grid = new QGridLayout();
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setHorizontalSpacing(18);
            grid->setVerticalSpacing(6);

            const QJsonArray items = group.value(QStringLiteral("items")).toArray();
            int index = 0;
            for (const QJsonValue &itemValue : items) {
                const QJsonObject item = itemValue.toObject();
                const int bit = item.value(QStringLiteral("value")).toInt();
                auto *box = new QCheckBox(item.value(QStringLiteral("label")).toString(), permHost);
                box->setChecked((perms & bit) != 0);
                m_boxes.append(box);
                m_bits.append(bit);
                grid->addWidget(box, index / 3, index % 3);
                ++index;
            }
            grid->setColumnStretch(2, 1);
            permLayout->addLayout(grid);
        }
        permLayout->addStretch(1);
        scroll->setWidget(permHost);
        root->addWidget(scroll, 1);

        m_disabled = new QCheckBox(QStringLiteral("禁用该账号（禁止登录）"), this);
        m_disabled->setChecked(user.value(QStringLiteral("disabled")).toBool());
        m_disabled->setVisible(!creating && currentRole != QLatin1String("super"));
        root->addWidget(m_disabled);

        auto *footer = new QHBoxLayout();
        footer->setContentsMargins(0, 0, 0, 0);
        footer->setSpacing(8);
        footer->addStretch(1);

        auto *cancel = makeButton(QStringLiteral("取消"), QStringLiteral("ghost"), this);
        auto *save = makeButton(QStringLiteral("保存"), QStringLiteral("primary"), this);
        m_saveButton = save;
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        // 注意：保存按钮不直接 accept()。点击后先做本地校验并进入「正在保存」状态，
        // 由页面连接 saveButton 的 clicked 再真正发起请求；服务端成功后页面
        // 调 deleteLater() 关闭对话框，失败则调 markSaveFailure() 恢复按钮并内联报错。
        connect(save, &QPushButton::clicked, this, [this]() { maybeBeginSave(); });
        footer->addWidget(cancel);
        footer->addWidget(save);
        root->addLayout(footer);

        m_error = makeLabel(QString(), QStringLiteral("muted"), this);
        m_error->setStyleSheet(QStringLiteral("color:#e0533d;"));
        m_error->setVisible(false);
        root->addWidget(m_error);

        auto refreshInline = [this]() {
            // 输入时实时提示，但只在点保存后才阻断提交
            const QString message = validate();
            if (!message.isEmpty()) {
                m_error->setText(message);
                m_error->setVisible(true);
            } else {
                m_error->setVisible(false);
            }
        };
        connect(m_username, &QLineEdit::textChanged, this, refreshInline);
        if (m_password) {
            connect(m_password, &QLineEdit::textChanged, this, refreshInline);
        }

        // 切换角色时套用该角色的默认权限
        connect(m_role, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, catalog](int index) {
                    const QString role = m_role->itemData(index).toString();
                    const QJsonObject defaults = catalog.value(QStringLiteral("defaults")).toObject();
                    const int mask = defaults.value(role).toInt();
                    for (int i = 0; i < m_boxes.size(); ++i) {
                        m_boxes.at(i)->setChecked((mask & m_bits.at(i)) != 0);
                    }
                });

        if (creating) {
            m_password->setFocus();
        } else {
            m_username->setFocus();
        }
    }

    QJsonObject payload() const
    {
        int perms = 0;
        for (int i = 0; i < m_boxes.size(); ++i) {
            if (m_boxes.at(i)->isChecked()) {
                perms |= m_bits.at(i);
            }
        }

        QJsonObject body;
        body.insert(QStringLiteral("username"), m_username->text().trimmed());
        body.insert(QStringLiteral("role"), m_role->currentData().toString());
        body.insert(QStringLiteral("perms"), perms);
        if (m_creating) {
            body.insert(QStringLiteral("password"), m_password->text());
        } else {
            body.insert(QStringLiteral("disabled"), m_disabled->isChecked());
        }
        return body;
    }

    /** 服务端保存失败后由页面调用：恢复按钮并显示错误 */
    void markSaveFailure(const QString &message)
    {
        setSaving(false);
        m_error->setText(message);
        m_error->setVisible(true);
    }

    /** 供页面连接的保存按钮（页面在 clicked 里真正发起请求） */
    QPushButton *saveButton() const { return m_saveButton; }

    /** 本地校验是否已通过并进入保存状态 */
    bool isSaving() const
    {
        return m_saveButton && !m_saveButton->isEnabled();
    }

private:
    /** 点保存：本地校验通过则进入「正在保存」状态并返回 true */
    bool maybeBeginSave()
    {
        const QString message = validate();
        m_error->setVisible(!message.isEmpty());
        m_error->setText(message);
        if (message.isEmpty()) {
            setSaving(true);
            return true;
        }
        return false;
    }

    /** 校验通过返回空串，否则返回错误提示；错误在对话框里内联红字显示 */
    QString validate() const
    {
        const QString username = m_username->text().trimmed();
        if (username.isEmpty()) {
            return QStringLiteral("请填写用户名。");
        }
        if (username.size() < 3 || username.size() > 32) {
            return QStringLiteral("用户名长度需要 3~32 个字符。");
        }
        for (const QChar &ch : username) {
            const bool ok = ch.isLetterOrNumber() || ch == QLatin1Char('_')
                            || ch == QLatin1Char('.') || ch == QLatin1Char('-');
            if (!ok) {
                return QStringLiteral("用户名只能包含字母、数字、下划线、点或短横线。");
            }
        }
        if (m_creating) {
            const QString password = m_password->text();
            if (password.isEmpty()) {
                return QStringLiteral("请填写初始密码。");
            }
            if (password.size() < 8) {
                return QStringLiteral("密码至少需要 8 位。");
            }
            const bool hasLetter = std::any_of(password.cbegin(), password.cend(),
                                               [](const QChar &ch) { return ch.isLetter(); });
            const bool hasDigit = std::any_of(password.cbegin(), password.cend(),
                                              [](const QChar &ch) { return ch.isDigit(); });
            if (!hasLetter || !hasDigit) {
                return QStringLiteral("密码需要同时包含字母和数字。");
            }
        }
        return QString();
    }

    /** 点了保存且本地校验通过：禁用按钮防止重复提交 */
    void setSaving(bool saving)
    {
        m_saveButton->setEnabled(!saving);
        m_saveButton->setText(saving ? QStringLiteral("正在保存…") : QStringLiteral("保存"));
    }

    bool m_creating = false;
    QLineEdit *m_username = nullptr;
    QLineEdit *m_password = nullptr;
    QComboBox *m_role = nullptr;
    QCheckBox *m_disabled = nullptr;
    QLabel *m_error = nullptr;
    QPushButton *m_saveButton = nullptr;
    QVector<QCheckBox *> m_boxes;
    QVector<int> m_bits;
};

} // namespace

UsersPage::UsersPage(ApiClient *api, QWidget *parent) : BasePage(api, parent)
{
    setPageTitle(QStringLiteral("用户管理"));
    setPageSubtitle(QStringLiteral("管理后台账号、角色与权限位"));

    auto *refreshButton = makeButton(QStringLiteral("刷新"), QStringLiteral("ghost"), this);
    refreshButton->setIcon(Icons::icon(Icons::Kind::Refresh, Theme::current().text, 16));
    connect(refreshButton, &QPushButton::clicked, this, [this]() { loadUsers(); });
    addHeaderAction(refreshButton);

    auto *createButton = makeButton(QStringLiteral("新建用户"), QStringLiteral("primary"), this);
    createButton->setIcon(Icons::icon(Icons::Kind::Plus, Theme::current().accentText, 16));
    connect(createButton, &QPushButton::clicked, this, [this]() { openEditor(QJsonObject(), true); });
    addHeaderAction(createButton);

    buildUi();
    loadCatalog();
    loadUsers();
}

void UsersPage::buildUi()
{
    m_summary = makeLabel(QStringLiteral("正在读取用户列表…"), QStringLiteral("muted"), this);
    body()->addWidget(m_summary);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({QStringLiteral("用户名"), QStringLiteral("角色"),
                                        QStringLiteral("状态"), QStringLiteral("权限"),
                                        QStringLiteral("创建时间"), QStringLiteral("操作")});
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(40);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setSectionsClickable(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);

    body()->addWidget(m_table, 1);
}

void UsersPage::refresh()
{
    loadUsers();
}

void UsersPage::reloadTheme()
{
    loadUsers();
}

void UsersPage::loadCatalog()
{
    api()->get(QStringLiteral("/api/users/permissions"),
               [this](bool ok, const QJsonObject &data, const QString &) {
                   if (ok) {
                       m_catalog = data;
                       if (!m_users.isEmpty()) {
                           fillTable(m_users);
                       }
                   }
               });
}

int UsersPage::totalPermissions() const
{
    int total = 0;
    const QJsonArray groups = m_catalog.value(QStringLiteral("groups")).toArray();
    for (const QJsonValue &group : groups) {
        total += group.toObject().value(QStringLiteral("items")).toArray().size();
    }
    return total;
}

int UsersPage::grantedCount(int perms) const
{
    int count = 0;
    const QJsonArray groups = m_catalog.value(QStringLiteral("groups")).toArray();
    for (const QJsonValue &groupValue : groups) {
        const QJsonArray items = groupValue.toObject().value(QStringLiteral("items")).toArray();
        for (const QJsonValue &itemValue : items) {
            const int bit = itemValue.toObject().value(QStringLiteral("value")).toInt();
            if ((perms & bit) != 0) {
                ++count;
            }
        }
    }
    return count;
}

void UsersPage::loadUsers()
{
    api()->get(QStringLiteral("/api/users"),
               [this](bool ok, const QJsonObject &data, const QString &message) {
                   if (!ok) {
                       m_forbidden = true;
                       m_summary->setText(QStringLiteral("无法读取用户列表：%1").arg(message));
                       m_table->clearContents();
                       m_table->setRowCount(0);
                       if (!m_users.isEmpty()) {
                           m_users = QJsonArray();
                       }
                       return;
                   }
                   m_forbidden = false;
                   m_users = data.value(QStringLiteral("items")).toArray();
                   m_summary->setText(QStringLiteral("共 %1 个账号").arg(m_users.size()));
                   fillTable(m_users);
               });
}

void UsersPage::fillTable(const QJsonArray &items)
{
    const Theme::Palette pal = Theme::current();
    const int total = totalPermissions();

    m_table->setUpdatesEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(items.size());

    int actionWidth = 0;

    for (int row = 0; row < items.size(); ++row) {
        const QJsonObject user = items.at(row).toObject();
        const QString username = user.value(QStringLiteral("username")).toString();
        const QString role = user.value(QStringLiteral("role")).toString();
        const bool isSuper = user.value(QStringLiteral("isSuper")).toBool()
                             || role == QLatin1String("super");
        const bool disabled = user.value(QStringLiteral("disabled")).toBool();
        const int perms = user.value(QStringLiteral("perms")).toInt();

        auto *nameItem = new QTableWidgetItem(username);
        nameItem->setIcon(Icons::icon(isSuper ? Icons::Kind::Shield : Icons::Kind::Users,
                                      isSuper ? pal.accent : pal.muted, 16));
        m_table->setItem(row, 0, nameItem);

        QString roleLabel = user.value(QStringLiteral("roleLabel")).toString();
        if (roleLabel.isEmpty()) {
            roleLabel = role == QLatin1String("admin") ? QStringLiteral("管理员")
                       : role == QLatin1String("super") ? QStringLiteral("超级管理员")
                                                        : QStringLiteral("普通用户");
        }
        m_table->setItem(row, 1, new QTableWidgetItem(roleLabel));

        auto *stateItem = new QTableWidgetItem(disabled ? QStringLiteral("已禁用")
                                                        : QStringLiteral("正常"));
        stateItem->setForeground(disabled ? pal.danger : pal.success);
        m_table->setItem(row, 2, stateItem);

        auto *permItem = new QTableWidgetItem(
            isSuper ? QStringLiteral("全部权限")
                    : QStringLiteral("%1 / %2").arg(grantedCount(perms)).arg(total));
        m_table->setItem(row, 3, permItem);

        m_table->setItem(row, 4,
                         new QTableWidgetItem(Fmt::timestamp(user.value(QStringLiteral("createdAt")))));

        auto *actions = new QWidget(m_table);
        auto *actionLayout = new QHBoxLayout(actions);
        actionLayout->setContentsMargins(4, 3, 4, 3);
        actionLayout->setSpacing(4);

        auto *edit = makeButton(QStringLiteral("编辑"), QStringLiteral("link"), actions);
        edit->setMinimumHeight(24);
        connect(edit, &QPushButton::clicked, this, [this, user]() { openEditor(user, false); });
        actionLayout->addWidget(edit);

        auto *reset = makeButton(QStringLiteral("重置密码"), QStringLiteral("link"), actions);
        reset->setMinimumHeight(24);
        connect(reset, &QPushButton::clicked, this, [this, user]() { resetPassword(user); });
        actionLayout->addWidget(reset);

        if (!isSuper) {
            auto *remove = makeButton(QStringLiteral("删除"), QStringLiteral("link"), actions);
            remove->setMinimumHeight(24);
            connect(remove, &QPushButton::clicked, this, [this, user]() { removeUser(user); });
            actionLayout->addWidget(remove);
        }
        actionLayout->addStretch(1);

        m_table->setCellWidget(row, 5, actions);
        actionWidth = qMax(actionWidth, actions->sizeHint().width());
    }

    // 「操作」列放的是按钮控件，QHeaderView::ResizeToContents 只按表头文字估算，
    // 会把右侧按钮裁掉；这里按最宽的一组按钮固定列宽。
    if (actionWidth > 0) {
        m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
        m_table->setColumnWidth(5, actionWidth + 16);
    }

    m_table->setUpdatesEnabled(true);
}

void UsersPage::openEditor(const QJsonObject &user, bool creating)
{
    if (m_catalog.isEmpty()) {
        showToast(QStringLiteral("权限目录尚未加载完成，请稍后再试"), QStringLiteral("error"));
        return;
    }

    // 非模态 + 服务端成功后才关闭：保存失败时错误直接显示在对话框里，输入不丢。
    // 对话框内部点击保存 → 本地校验 + 进入「正在保存」状态（isSaving()）；
    // 这里再连接同一个按钮的 clicked 真正发起请求（连接顺序保证校验先执行）。
    QPointer<UserDialog> dialog = new UserDialog(m_catalog, user, creating, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const int userId = user.value(QStringLiteral("id")).toInt();

    connect(dialog->saveButton(), &QPushButton::clicked, this,
            [this, dialog, userId, creating]() {
                if (dialog.isNull() || !dialog->isSaving()) {
                    return; // 本地校验未通过，对话框内已显示红字
                }
                const QJsonObject body = dialog->payload();
                if (creating) {
                    api()->post(QStringLiteral("/api/users"), body,
                                [this, dialog](bool ok, const QJsonObject &, const QString &message) {
                                    if (!ok) {
                                        if (!dialog.isNull()) {
                                            dialog->markSaveFailure(message);
                                        }
                                        return;
                                    }
                                    // 成功：直接销毁对话框（deleteLater 不会再触发 finished）
                                    if (!dialog.isNull()) {
                                        dialog->deleteLater();
                                    }
                                    showToast(QStringLiteral("用户已创建"), QStringLiteral("success"));
                                    loadUsers();
                                });
                    return;
                }

                api()->put(QStringLiteral("/api/users/%1").arg(userId), body,
                           [this, dialog](bool ok, const QJsonObject &, const QString &message) {
                               if (!ok) {
                                   if (!dialog.isNull()) {
                                       dialog->markSaveFailure(message);
                                   }
                                   return;
                               }
                               if (!dialog.isNull()) {
                                   dialog->deleteLater();
                               }
                               showToast(QStringLiteral("用户已更新"), QStringLiteral("success"));
                               loadUsers();
                           });
            });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void UsersPage::resetPassword(const QJsonObject &user)
{
    bool accepted = false;
    const QString password =
        QInputDialog::getText(this, QStringLiteral("重置密码"),
                              QStringLiteral("为 %1 设置新密码：").arg(user.value(QStringLiteral("username")).toString()),
                              QLineEdit::Password, QString(), &accepted);
    if (!accepted || password.isEmpty()) {
        return;
    }

    const int id = user.value(QStringLiteral("id")).toInt();
    QJsonObject body;
    body.insert(QStringLiteral("password"), password);

    api()->post(QStringLiteral("/api/users/%1/reset-password").arg(id), body,
                [this](bool ok, const QJsonObject &, const QString &message) {
                    if (!ok) {
                        showToast(message, QStringLiteral("error"));
                        return;
                    }
                    showToast(QStringLiteral("密码已重置，该账号的其他登录已失效"),
                              QStringLiteral("success"));
                });
}

void UsersPage::removeUser(const QJsonObject &user)
{
    const QString username = user.value(QStringLiteral("username")).toString();
    const auto answer = QMessageBox::question(this, QStringLiteral("确认删除"),
                                              QStringLiteral("确定要删除用户「%1」吗？该操作不可撤销。").arg(username),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    const int id = user.value(QStringLiteral("id")).toInt();
    api()->del(QStringLiteral("/api/users/%1").arg(id),
               [this, username](bool ok, const QJsonObject &, const QString &message) {
                   if (!ok) {
                       showToast(message, QStringLiteral("error"));
                       return;
                   }
                   showToast(QStringLiteral("用户 %1 已删除").arg(username), QStringLiteral("success"));
                   loadUsers();
               });
}
