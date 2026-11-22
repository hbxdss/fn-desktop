#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "ui/widgets.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

/** 操作日志：查看后端审计记录，并支持把全部日志备份到本地（CSV / JSON）。 */
class AuditPage : public BasePage
{
    Q_OBJECT

public:
    explicit AuditPage(ApiClient *api, QWidget *parent = nullptr);

    void refresh() override;
    void reloadTheme() override;

private:
    void buildUi();
    void load();
    void fillTable(const QJsonArray &items);

    void startBackup();
    void fetchBackupPage();
    void finishBackup();

    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
    QComboBox *m_limitBox = nullptr;
    QPushButton *m_backupButton = nullptr;

    bool m_backingUp = false;
    bool m_backupAsJson = false;
    int m_backupTotal = 0;
    QString m_backupPath;
    QVector<QJsonObject> m_backupItems;
};
