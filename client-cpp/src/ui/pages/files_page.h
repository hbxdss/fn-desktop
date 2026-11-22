#pragma once

#include <QJsonArray>
#include <QStringList>

#include "ui/widgets.h"

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class AppConfig;

/** 文件管理：浏览 / 上传 / 下载 / 新建目录 / 重命名 / 删除。 */
class FilesPage : public BasePage
{
    Q_OBJECT

public:
    explicit FilesPage(ApiClient *api, AppConfig *config = nullptr, QWidget *parent = nullptr);

    void refresh() override;
    void reloadTheme() override;

signals:
    /** 用户从权限引导里点了「打开飞牛网页管理后台」：切到后台页面去管理挂载。 */
    void requestAdminMountSettings();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildUi();
    void applyIcons();
    void loadPath(const QString &path);
    void fillTable(const QJsonArray &items);
    void goUp();
    void openRow(int row);
    void downloadSelected();
    void compressSelected();
    void extractSelected();
    void deleteSelected();
    void renameSelected();
    void makeDirectory();
    /** 表格右键菜单：打开 / 下载 / 重命名 / 压缩 / 解压 / 删除 */
    void showContextMenu(const QPoint &pos);
    /** 按当前表头排序状态重排行（目录始终优先） */
    void applySort(QVector<QJsonObject> &rows) const;
    void chooseUpload();
    void startUpload(const QStringList &files);
    /** 逐个上传到指定目录（可含子目录，会自动创建缺失目录） */
    void startUploadTo(const QStringList &files, const QStringList &targetDirs);
    void uploadNext();
    void updateButtons();
    /** 目录无读取权限时的引导弹窗（指向飞牛网页后台） */
    void showPermissionGuide();
    /** 默认下载目录：优先取设置页里的「下载目录」，否则用系统下载文件夹 */
    QString defaultDownloadDir() const;

    int selectedRow() const;
    QString rowPath(int row) const;
    bool rowIsDir(int row) const;

    QLineEdit *m_pathEdit = nullptr;
    QLabel *m_friendlyPathLabel = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_uploadLabel = nullptr;
    QProgressBar *m_uploadBar = nullptr;

    QPushButton *m_upButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_newDirButton = nullptr;
    QPushButton *m_uploadButton = nullptr;
    QPushButton *m_downloadButton = nullptr;
    QPushButton *m_renameButton = nullptr;
    QPushButton *m_compressButton = nullptr;
    QPushButton *m_extractButton = nullptr;
    QPushButton *m_deleteButton = nullptr;

    QJsonArray m_items;
    QString m_path = QStringLiteral("/");
    AppConfig *m_config = nullptr;
    QStringList m_queue;
    QStringList m_queueTargets;
    int m_queueIndex = 0;
    int m_sortColumn = 0;   // 0 名称 1 大小 2 修改时间 3 类型
    int m_sortOrder = 0;    // Qt::AscendingOrder / Qt::DescendingOrder
    bool m_busy = false;
};
