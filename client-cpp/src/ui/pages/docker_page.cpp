#include "ui/pages/docker_page.h"

#include "ui/icons.h"
#include "ui/theme.h"

#include "core/api_client.h"
#include "core/config.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>

namespace {

/** 容器 / 数据卷 / 网络名称的合法格式（与服务端校验保持一致）。 */
const QRegularExpression kNamePattern(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,127}$"));

/** 镜像引用格式：仓库地址、标签、摘要，不允许空白字符。 */
const QRegularExpression kImagePattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._/:@-]*$"));

/** 环境变量键名格式，与服务端 ENV_KEY_PATTERN 保持一致。 */
const QRegularExpression kEnvKeyPattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));

/** 容器状态的中文名称。 */
QString stateLabel(const QString &state)
{
    if (state == QLatin1String("running")) {
        return QStringLiteral("运行中");
    }
    if (state == QLatin1String("exited") || state == QLatin1String("stopped")) {
        return QStringLiteral("已停止");
    }
    if (state == QLatin1String("paused")) {
        return QStringLiteral("已暂停");
    }
    if (state == QLatin1String("restarting")) {
        return QStringLiteral("重启中");
    }
    if (state == QLatin1String("created")) {
        return QStringLiteral("已创建");
    }
    if (state == QLatin1String("dead")) {
        return QStringLiteral("异常");
    }
    return state.isEmpty() ? QStringLiteral("未知") : state;
}

/** 容器状态对应的主题色。 */
QColor stateColor(const QString &state)
{
    const Theme::Palette pal = Theme::current();
    if (state == QLatin1String("running")) {
        return pal.success;
    }
    if (state == QLatin1String("restarting") || state == QLatin1String("paused")) {
        return pal.warning;
    }
    if (state == QLatin1String("dead")) {
        return pal.danger;
    }
    return pal.muted;
}

/** 是否处于「运行」语义（运行中 / 重启中）。 */
bool isActiveState(const QString &state)
{
    return state == QLatin1String("running") || state == QLatin1String("restarting");
}

/** 状态筛选下拉的匹配逻辑。 */
bool matchesStateFilter(const QString &state, const QString &filter)
{
    if (filter.isEmpty() || filter == QLatin1String("all")) {
        return true;
    }
    if (filter == QLatin1String("stopped")) {
        return state != QLatin1String("running") && state != QLatin1String("restarting")
               && state != QLatin1String("paused");
    }
    return state == filter;
}

/** 读取字符串字段，空值统一显示为「—」。 */
QString textOrDash(const QJsonObject &object, const QString &key)
{
    const QString value = object.value(key).toString().trimmed();
    return value.isEmpty() ? QStringLiteral("—") : value;
}

/** 把字符串数组用分隔符拼接，可用于命令、环境变量、网络列表。 */
QString joinStrings(const QJsonArray &array, const QString &separator)
{
    QStringList parts;
    for (const QJsonValue &value : array) {
        const QString text = value.isObject() ? value.toObject().value(QStringLiteral("line")).toString()
                                              : value.toString();
        if (!text.isEmpty()) {
            parts.append(text);
        }
    }
    return parts.join(separator);
}

/** 等宽字体（日志、环境变量使用）。 */
QFont monospaceFont()
{
    QFont font(QStringLiteral("Consolas"));
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(9);
    return font;
}

/** 统一下拉框样式属性的辅助函数：无（保留默认主题）。 */

/** 自绘确认对话框：标题 + 说明 + 可选勾选项，不使用系统原生弹窗。 */
class DockerConfirmDialog : public DialogWindow
{
public:
    DockerConfirmDialog(const QString &title,
                        const QString &message,
                        const QStringList &extras,
                        const QString &confirmText,
                        QWidget *parent)
        : DialogWindow(parent)
    {
        setWindowTitle(title);
        setModal(true);
        setMinimumWidth(480);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(22, 20, 22, 18);
        root->setSpacing(12);

        root->addWidget(makeLabel(title, QStringLiteral("h2"), this));

        auto *text = makeLabel(message, QStringLiteral("muted"), this);
        text->setWordWrap(true);
        root->addWidget(text);

        for (const QString &item : extras) {
            auto *box = new QCheckBox(item, this);
            m_boxes.append(box);
            root->addWidget(box);
        }

        auto *footer = new QHBoxLayout();
        footer->setContentsMargins(0, 4, 0, 0);
        footer->setSpacing(8);
        footer->addStretch(1);

        auto *cancel = makeButton(QStringLiteral("取消"), QStringLiteral("ghost"), this);
        auto *confirm = makeButton(confirmText, QStringLiteral("danger"), this);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(confirm, &QPushButton::clicked, this, &QDialog::accept);
        footer->addWidget(cancel);
        footer->addWidget(confirm);
        root->addLayout(footer);
    }

    bool isChecked(int index) const
    {
        return index >= 0 && index < m_boxes.size() && m_boxes.at(index)->isChecked();
    }

private:
    QVector<QCheckBox *> m_boxes;
};

/** 单输入框对话框（重命名容器 / 新建数据卷 / 新建网络），带名称校验与红字提示。 */
class DockerInputDialog : public DialogWindow
{
public:
    DockerInputDialog(const QString &title,
                      const QString &label,
                      const QString &initial,
                      const QStringList &drivers,
                      const QString &defaultDriver,
                      QWidget *parent)
        : DialogWindow(parent)
    {
        setWindowTitle(title);
        setModal(true);
        setMinimumWidth(460);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(22, 20, 22, 18);
        root->setSpacing(12);

        root->addWidget(makeLabel(title, QStringLiteral("h2"), this));

        auto *form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(10);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_name = new QLineEdit(this);
        m_name->setPlaceholderText(QStringLiteral("以字母或数字开头，可包含下划线、点和短横线"));
        m_name->setText(initial);
        m_name->selectAll();
        form->addRow(label, m_name);

        if (!drivers.isEmpty()) {
            m_driver = new QComboBox(this);
            for (const QString &item : drivers) {
                m_driver->addItem(item, item);
            }
            const int index = m_driver->findData(defaultDriver);
            if (index >= 0) {
                m_driver->setCurrentIndex(index);
            }
            form->addRow(QStringLiteral("驱动"), m_driver);
        }

        root->addLayout(form);

        m_error = makeLabel(QString(), QStringLiteral("error"), this);
        m_error->setWordWrap(true);
        m_error->setVisible(false);
        root->addWidget(m_error);

        auto *footer = new QHBoxLayout();
        footer->setContentsMargins(0, 2, 0, 0);
        footer->setSpacing(8);
        footer->addStretch(1);

        auto *cancel = makeButton(QStringLiteral("取消"), QStringLiteral("ghost"), this);
        auto *confirm = makeButton(QStringLiteral("确定"), QStringLiteral("primary"), this);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(confirm, &QPushButton::clicked, this, [this]() {
            const QString text = m_name->text().trimmed();
            if (text.isEmpty()) {
                m_error->setText(QStringLiteral("请填写名称"));
                m_error->setVisible(true);
                return;
            }
            if (!kNamePattern.match(text).hasMatch()) {
                m_error->setText(QStringLiteral("名称只能由字母、数字、下划线、点和短横线组成，"
                                                "且必须以字母或数字开头（1~128 个字符）"));
                m_error->setVisible(true);
                return;
            }
            m_error->setVisible(false);
            accept();
        });
        footer->addWidget(cancel);
        footer->addWidget(confirm);
        root->addLayout(footer);

        connect(m_name, &QLineEdit::returnPressed, confirm, &QPushButton::click);
    }

    QString value() const { return m_name->text().trimmed(); }
    QString driver() const { return m_driver ? m_driver->currentData().toString() : QString(); }

private:
    QLineEdit *m_name = nullptr;
    QComboBox *m_driver = nullptr;
    QLabel *m_error = nullptr;
};

} // namespace

DockerEmptyState::DockerEmptyState(Icons::Kind kind, const QString &text, QWidget *parent)
    : QWidget(parent), m_kind(kind)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 24, 0, 24);
    layout->setSpacing(10);

    m_icon = new QLabel(this);
    m_icon->setObjectName(QStringLiteral("EmptyState"));
    m_icon->setAlignment(Qt::AlignCenter);

    m_label = new QLabel(text, this);
    m_label->setObjectName(QStringLiteral("EmptyState"));
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setWordWrap(true);

    layout->addStretch(1);
    layout->addWidget(m_icon, 0, Qt::AlignHCenter);
    layout->addWidget(m_label, 0, Qt::AlignHCenter);
    layout->addStretch(1);

    refreshTheme();
}

void DockerEmptyState::setText(const QString &text)
{
    m_label->setText(text);
}

void DockerEmptyState::refreshTheme()
{
    m_icon->setPixmap(Icons::pixmap(m_kind, Theme::current().muted, 40));
}

namespace {

/** 新建容器对话框：镜像、名称、命令、重启策略、网络、端口映射、卷映射、环境变量。 */
class DockerCreateDialog : public DialogWindow
{
public:
    DockerCreateDialog(ApiClient *api,
                       const QStringList &images,
                       const QStringList &networks,
                       QWidget *parent)
        : DialogWindow(parent), m_api(api)
    {
        setWindowTitle(QStringLiteral("新建容器"));
        setModal(true);
        resize(720, 780);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(22, 20, 22, 18);
        root->setSpacing(12);

        root->addWidget(makeLabel(QStringLiteral("新建容器"), QStringLiteral("h2"), this));

        auto *form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(9);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_image = new QComboBox(this);
        m_image->setEditable(true);
        m_image->setInsertPolicy(QComboBox::NoInsert);
        for (const QString &item : images) {
            m_image->addItem(item, item);
        }
        if (m_image->count() > 0) {
            m_image->setCurrentIndex(0);
        }
        m_image->lineEdit()->setPlaceholderText(QStringLiteral("如 nginx:latest（必填）"));
        form->addRow(QStringLiteral("镜像"), m_image);

        m_name = new QLineEdit(this);
        m_name->setPlaceholderText(QStringLiteral("留空则自动生成随机名称"));
        form->addRow(QStringLiteral("容器名称"), m_name);

        m_command = new QLineEdit(this);
        m_command->setPlaceholderText(QStringLiteral("覆盖镜像默认命令，可留空"));
        form->addRow(QStringLiteral("启动命令"), m_command);

        m_restart = new QComboBox(this);
        m_restart->addItem(QStringLiteral("不自动重启（no）"), QStringLiteral("no"));
        m_restart->addItem(QStringLiteral("总是重启（always）"), QStringLiteral("always"));
        m_restart->addItem(QStringLiteral("除非手动停止（unless-stopped）"), QStringLiteral("unless-stopped"));
        m_restart->addItem(QStringLiteral("异常退出时重启（on-failure）"), QStringLiteral("on-failure"));
        form->addRow(QStringLiteral("重启策略"), m_restart);

        m_network = new QComboBox(this);
        m_network->addItem(QStringLiteral("默认网络（bridge）"), QString());
        for (const QString &item : networks) {
            m_network->addItem(item, item);
        }
        form->addRow(QStringLiteral("网络"), m_network);

        root->addLayout(form);

        // ---------------- 端口映射 ----------------
        root->addWidget(makeLabel(QStringLiteral("端口映射"), QStringLiteral("h3"), this));

        m_ports = new QTableWidget(this);
        m_ports->setColumnCount(3);
        m_ports->setHorizontalHeaderLabels({QStringLiteral("宿主端口"), QStringLiteral("容器端口"),
                                            QStringLiteral("协议")});
        m_ports->verticalHeader()->setVisible(false);
        m_ports->verticalHeader()->setDefaultSectionSize(30);
        m_ports->horizontalHeader()->setHighlightSections(false);
        m_ports->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_ports->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_ports->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_ports->setShowGrid(false);
        m_ports->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_ports->setSelectionMode(QAbstractItemView::SingleSelection);
        m_ports->setMinimumHeight(96);
        m_ports->setMaximumHeight(140);
        root->addWidget(m_ports);

        auto *portButtons = new QHBoxLayout();
        portButtons->setContentsMargins(0, 0, 0, 0);
        portButtons->setSpacing(8);
        auto *addPort = makeButton(QStringLiteral("添加映射"), QStringLiteral("ghost"), this);
        auto *removePort = makeButton(QStringLiteral("删除选中"), QStringLiteral("ghost"), this);
        addPort->setMinimumHeight(28);
        removePort->setMinimumHeight(28);
        connect(addPort, &QPushButton::clicked, this, [this]() { addPortRow(QString(), QString(), QStringLiteral("tcp")); });
        connect(removePort, &QPushButton::clicked, this, [this]() {
            const int row = m_ports->currentRow();
            if (row >= 0) {
                m_ports->removeRow(row);
            }
        });
        portButtons->addWidget(addPort);
        portButtons->addWidget(removePort);
        portButtons->addStretch(1);
        root->addLayout(portButtons);

        auto *portHint = makeLabel(QStringLiteral("宿主端口留空表示由 Docker 随机分配，容器端口必填（1~65535）。"),
                                   QStringLiteral("muted"), this);
        portHint->setWordWrap(true);
        root->addWidget(portHint);

        // ---------------- 卷映射 ----------------
        root->addWidget(makeLabel(QStringLiteral("卷映射"), QStringLiteral("h3"), this));

        m_volumes = new QTableWidget(this);
        m_volumes->setColumnCount(3);
        m_volumes->setHorizontalHeaderLabels({QStringLiteral("宿主路径"), QStringLiteral("容器路径"),
                                              QStringLiteral("只读")});
        m_volumes->verticalHeader()->setVisible(false);
        m_volumes->verticalHeader()->setDefaultSectionSize(30);
        m_volumes->horizontalHeader()->setHighlightSections(false);
        m_volumes->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_volumes->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_volumes->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_volumes->setShowGrid(false);
        m_volumes->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_volumes->setSelectionMode(QAbstractItemView::SingleSelection);
        m_volumes->setMinimumHeight(96);
        m_volumes->setMaximumHeight(140);
        root->addWidget(m_volumes);

        auto *volumeButtons = new QHBoxLayout();
        volumeButtons->setContentsMargins(0, 0, 0, 0);
        volumeButtons->setSpacing(8);
        auto *addVolume = makeButton(QStringLiteral("添加映射"), QStringLiteral("ghost"), this);
        auto *removeVolume = makeButton(QStringLiteral("删除选中"), QStringLiteral("ghost"), this);
        addVolume->setMinimumHeight(28);
        removeVolume->setMinimumHeight(28);
        connect(addVolume, &QPushButton::clicked, this, [this]() { addVolumeRow(QString(), QString(), false); });
        connect(removeVolume, &QPushButton::clicked, this, [this]() {
            const int row = m_volumes->currentRow();
            if (row >= 0) {
                m_volumes->removeRow(row);
            }
        });
        volumeButtons->addWidget(addVolume);
        volumeButtons->addWidget(removeVolume);
        volumeButtons->addStretch(1);
        root->addLayout(volumeButtons);

        auto *volumeHint = makeLabel(QStringLiteral("宿主路径与容器路径都必须以 / 开头，且不能包含 “..”。"),
                                     QStringLiteral("muted"), this);
        volumeHint->setWordWrap(true);
        root->addWidget(volumeHint);

        // ---------------- 环境变量 ----------------
        root->addWidget(makeLabel(QStringLiteral("环境变量"), QStringLiteral("h3"), this));

        m_env = new QPlainTextEdit(this);
        m_env->setPlaceholderText(QStringLiteral("每行一个 KEY=VALUE，例如：\nTZ=Asia/Shanghai\nMODE=production"));
        m_env->setFont(monospaceFont());
        m_env->setMinimumHeight(80);
        m_env->setMaximumHeight(110);
        root->addWidget(m_env);

        m_error = makeLabel(QString(), QStringLiteral("error"), this);
        m_error->setWordWrap(true);
        m_error->setVisible(false);
        root->addWidget(m_error);

        auto *footer = new QHBoxLayout();
        footer->setContentsMargins(0, 2, 0, 0);
        footer->setSpacing(8);
        footer->addStretch(1);

        m_cancelButton = makeButton(QStringLiteral("取消"), QStringLiteral("ghost"), this);
        m_submitButton = makeButton(QStringLiteral("创建"), QStringLiteral("primary"), this);
        connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_submitButton, &QPushButton::clicked, this, [this]() { submit(); });
        footer->addWidget(m_cancelButton);
        footer->addWidget(m_submitButton);
        root->addLayout(footer);
    }

    /** 创建成功后服务端返回的容器名称。 */
    QString createdName() const { return m_createdName; }

private:
    void addPortRow(const QString &host, const QString &container, const QString &protocol)
    {
        const int row = m_ports->rowCount();
        m_ports->insertRow(row);
        m_ports->setItem(row, 0, new QTableWidgetItem(host));
        m_ports->setItem(row, 1, new QTableWidgetItem(container));

        auto *combo = new QComboBox(m_ports);
        combo->addItem(QStringLiteral("tcp"), QStringLiteral("tcp"));
        combo->addItem(QStringLiteral("udp"), QStringLiteral("udp"));
        combo->addItem(QStringLiteral("sctp"), QStringLiteral("sctp"));
        const int index = combo->findData(protocol);
        if (index >= 0) {
            combo->setCurrentIndex(index);
        }
        m_ports->setCellWidget(row, 2, combo);
    }

    void addVolumeRow(const QString &host, const QString &container, bool ro)
    {
        const int row = m_volumes->rowCount();
        m_volumes->insertRow(row);
        m_volumes->setItem(row, 0, new QTableWidgetItem(host));
        m_volumes->setItem(row, 1, new QTableWidgetItem(container));

        auto *roItem = new QTableWidgetItem();
        roItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        roItem->setCheckState(ro ? Qt::Checked : Qt::Unchecked);
        roItem->setTextAlignment(Qt::AlignCenter);
        m_volumes->setItem(row, 2, roItem);
    }

    void setBusy(bool busy)
    {
        m_submitButton->setEnabled(!busy);
        m_cancelButton->setEnabled(!busy);
        m_submitButton->setText(busy ? QStringLiteral("创建中…") : QStringLiteral("创建"));
    }

    /** 校验表单，返回空字符串表示通过。 */
    QString validate() const
    {
        const QString image = m_image->currentText().trimmed();
        if (image.isEmpty()) {
            return QStringLiteral("请填写要使用的镜像名称，例如 nginx:latest");
        }
        if (!kImagePattern.match(image).hasMatch()) {
            return QStringLiteral("镜像名称格式不正确，请填写如 nginx:latest 的镜像引用");
        }

        const QString name = m_name->text().trimmed();
        if (!name.isEmpty() && !kNamePattern.match(name).hasMatch()) {
            return QStringLiteral("容器名称只能由字母、数字、下划线、点和短横线组成，"
                                  "且必须以字母或数字开头（1~128 个字符）");
        }

        for (int row = 0; row < m_ports->rowCount(); ++row) {
            const QString hostText = m_ports->item(row, 0) ? m_ports->item(row, 0)->text().trimmed() : QString();
            const QString containerText = m_ports->item(row, 1) ? m_ports->item(row, 1)->text().trimmed() : QString();
            if (hostText.isEmpty() && containerText.isEmpty()) {
                continue;
            }
            bool ok = false;
            const int containerPort = containerText.toInt(&ok);
            if (!ok || containerPort < 1 || containerPort > 65535) {
                return QStringLiteral("第 %1 行端口映射的容器端口必须是 1~65535 的整数").arg(row + 1);
            }
            if (!hostText.isEmpty()) {
                const int hostPort = hostText.toInt(&ok);
                if (!ok || hostPort < 1 || hostPort > 65535) {
                    return QStringLiteral("第 %1 行端口映射的宿主端口必须是 1~65535 的整数").arg(row + 1);
                }
            }
        }

        for (int row = 0; row < m_volumes->rowCount(); ++row) {
            const QString hostText = m_volumes->item(row, 0) ? m_volumes->item(row, 0)->text().trimmed() : QString();
            const QString containerText = m_volumes->item(row, 1) ? m_volumes->item(row, 1)->text().trimmed() : QString();
            if (hostText.isEmpty() && containerText.isEmpty()) {
                continue;
            }
            if (hostText.isEmpty() || containerText.isEmpty()) {
                return QStringLiteral("第 %1 行卷映射的宿主路径与容器路径都必须填写").arg(row + 1);
            }
            if (!hostText.startsWith(QLatin1Char('/')) || hostText.contains(QLatin1String(".."))) {
                return QStringLiteral("第 %1 行卷映射的宿主路径必须以 / 开头，且不能包含 “..”").arg(row + 1);
            }
            if (!containerText.startsWith(QLatin1Char('/')) || containerText.contains(QLatin1String(".."))) {
                return QStringLiteral("第 %1 行卷映射的容器路径必须以 / 开头，且不能包含 “..”").arg(row + 1);
            }
        }

        const QStringList envLines = m_env->toPlainText().split(QLatin1Char('\n'));
        for (int index = 0; index < envLines.size(); ++index) {
            const QString line = envLines.at(index).trimmed();
            if (line.isEmpty()) {
                continue;
            }
            const int equalAt = line.indexOf(QLatin1Char('='));
            if (equalAt <= 0) {
                return QStringLiteral("环境变量第 %1 行格式不正确，应为 KEY=VALUE").arg(index + 1);
            }
            const QString key = line.left(equalAt).trimmed();
            if (!kEnvKeyPattern.match(key).hasMatch()) {
                return QStringLiteral("环境变量第 %1 行的键名不合法（只能包含字母、数字和下划线，且不能以数字开头）")
                    .arg(index + 1);
            }
        }

        return QString();
    }

    /** 组装创建容器的请求体。 */
    QJsonObject buildPayload() const
    {
        QJsonObject payload;
        payload.insert(QStringLiteral("image"), m_image->currentText().trimmed());

        const QString name = m_name->text().trimmed();
        if (!name.isEmpty()) {
            payload.insert(QStringLiteral("name"), name);
        }

        const QString command = m_command->text().trimmed();
        if (!command.isEmpty()) {
            payload.insert(QStringLiteral("cmd"), command);
        }

        payload.insert(QStringLiteral("restartPolicy"), m_restart->currentData().toString());

        const QString network = m_network->currentData().toString();
        if (!network.isEmpty()) {
            payload.insert(QStringLiteral("network"), network);
        }

        QJsonArray ports;
        for (int row = 0; row < m_ports->rowCount(); ++row) {
            const QString hostText = m_ports->item(row, 0) ? m_ports->item(row, 0)->text().trimmed() : QString();
            const QString containerText = m_ports->item(row, 1) ? m_ports->item(row, 1)->text().trimmed() : QString();
            if (containerText.isEmpty()) {
                continue;
            }
            auto *combo = qobject_cast<QComboBox *>(m_ports->cellWidget(row, 2));
            QJsonObject port;
            if (!hostText.isEmpty()) {
                port.insert(QStringLiteral("host"), hostText.toInt());
            }
            port.insert(QStringLiteral("container"), containerText.toInt());
            port.insert(QStringLiteral("protocol"), combo ? combo->currentData().toString() : QStringLiteral("tcp"));
            ports.append(port);
        }
        if (!ports.isEmpty()) {
            payload.insert(QStringLiteral("ports"), ports);
        }

        QJsonArray volumes;
        for (int row = 0; row < m_volumes->rowCount(); ++row) {
            const QString hostText = m_volumes->item(row, 0) ? m_volumes->item(row, 0)->text().trimmed() : QString();
            const QString containerText = m_volumes->item(row, 1) ? m_volumes->item(row, 1)->text().trimmed() : QString();
            if (hostText.isEmpty() || containerText.isEmpty()) {
                continue;
            }
            QJsonObject volume;
            volume.insert(QStringLiteral("host"), hostText);
            volume.insert(QStringLiteral("container"), containerText);
            volume.insert(QStringLiteral("ro"), m_volumes->item(row, 2) != nullptr
                                                     && m_volumes->item(row, 2)->checkState() == Qt::Checked);
            volumes.append(volume);
        }
        if (!volumes.isEmpty()) {
            payload.insert(QStringLiteral("volumes"), volumes);
        }

        QJsonArray env;
        const QStringList envLines = m_env->toPlainText().split(QLatin1Char('\n'));
        for (const QString &raw : envLines) {
            const QString line = raw.trimmed();
            if (!line.isEmpty()) {
                env.append(line);
            }
        }
        if (!env.isEmpty()) {
            payload.insert(QStringLiteral("env"), env);
        }

        return payload;
    }

    void submit()
    {
        if (m_api == nullptr) {
            m_error->setText(QStringLiteral("网络客户端未就绪，无法创建容器"));
            m_error->setVisible(true);
            return;
        }

        const QString error = validate();
        if (!error.isEmpty()) {
            m_error->setText(error);
            m_error->setVisible(true);
            return;
        }
        m_error->setVisible(false);

        const QJsonObject payload = buildPayload();
        setBusy(true);

        QPointer<DockerCreateDialog> guard(this);
        m_api->post(QStringLiteral("/api/docker/containers"), payload,
                    [guard, this](bool ok, const QJsonObject &data, const QString &message) {
                        if (!guard) {
                            return;
                        }
                        setBusy(false);
                        if (!ok) {
                            m_error->setText(QStringLiteral("创建失败：%1").arg(message));
                            m_error->setVisible(true);
                            return;
                        }
                        m_createdName = data.value(QStringLiteral("name")).toString();
                        if (m_createdName.isEmpty()) {
                            m_createdName = m_name->text().trimmed();
                        }
                        accept();
                    });
    }

    ApiClient *m_api = nullptr;
    QComboBox *m_image = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_command = nullptr;
    QComboBox *m_restart = nullptr;
    QComboBox *m_network = nullptr;
    QTableWidget *m_ports = nullptr;
    QTableWidget *m_volumes = nullptr;
    QPlainTextEdit *m_env = nullptr;
    QLabel *m_error = nullptr;
    QPushButton *m_submitButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QString m_createdName;
};

} // namespace

namespace {

/** 容器详情对话框：概览（inspect 精简信息）+ 日志 + 资源监控。 */
class DockerDetailDialog : public DialogWindow
{
public:
    DockerDetailDialog(ApiClient *api, const QString &id, const QString &name, QWidget *parent,
                       int initialTab = 0)
        : DialogWindow(parent), m_api(api), m_id(id), m_name(name)
    {
        setWindowTitle(QStringLiteral("容器详情"));
        setModal(true);
        resize(980, 780);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(22, 20, 22, 18);
        root->setSpacing(12);

        auto *header = new QHBoxLayout();
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(8);
        header->addWidget(makeLabel(QStringLiteral("容器详情 · %1").arg(name), QStringLiteral("h2"), this));
        header->addStretch(1);

        m_copyButton = makeButton(QStringLiteral("复制 ID"), QStringLiteral("ghost"), this);
        m_copyButton->setMinimumHeight(28);
        connect(m_copyButton, &QPushButton::clicked, this, [this]() {
            const QString text = m_fullId.isEmpty() ? m_id : m_fullId;
            QGuiApplication::clipboard()->setText(text);
            Toast::show(this, QStringLiteral("容器 ID 已复制到剪贴板"), QStringLiteral("success"));
        });
        header->addWidget(m_copyButton);

        auto *close = makeButton(QStringLiteral("关闭"), QStringLiteral("ghost"), this);
        close->setMinimumHeight(28);
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
        header->addWidget(close);
        root->addLayout(header);

        m_tabs = new QTabWidget(this);
        m_tabs->addTab(buildOverviewTab(), QStringLiteral("概览"));
        m_tabs->addTab(buildLogsTab(), QStringLiteral("日志"));
        m_tabs->addTab(buildStatsTab(), QStringLiteral("监控"));
        connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
            if (index == 1) {
                loadLogs(false);
            } else if (index == 2) {
                loadStats(false);
            }
        });
        root->addWidget(m_tabs, 1);
        if (initialTab > 0 && initialTab < m_tabs->count()) {
            m_tabs->setCurrentIndex(initialTab);
        }

        m_statsTimer = new QTimer(this);
        m_statsTimer->setInterval(2000);
        connect(m_statsTimer, &QTimer::timeout, this, [this]() { loadStats(false); });

        m_logTimer = new QTimer(this);
        m_logTimer->setInterval(2000);
        connect(m_logTimer, &QTimer::timeout, this, [this]() {
            if (m_tabs->currentIndex() == 1) {
                loadLogs(false);
            }
        });

        loadInspect();
        root->setStretch(1, 1);
    }

private:
    QWidget *buildOverviewTab()
    {
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);

        m_infoHint = makeLabel(QStringLiteral("正在读取容器详情…"), QStringLiteral("muted"), page);
        m_infoHint->setWordWrap(true);
        layout->addWidget(m_infoHint);

        auto *grid = new QGridLayout();
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(6);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);

        auto addField = [this, grid, page](int row, int column, const QString &caption, QLabel *&valueLabel) {
            auto *captionLabel = makeLabel(caption, QStringLiteral("muted"), page);
            valueLabel = makeLabel(QStringLiteral("—"), QString(), page);
            valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            valueLabel->setWordWrap(true);
            grid->addWidget(captionLabel, row, column * 2);
            grid->addWidget(valueLabel, row, column * 2 + 1);
        };

        addField(0, 0, QStringLiteral("容器 ID"), m_idValue);
        addField(0, 1, QStringLiteral("状态"), m_stateValue);
        addField(1, 0, QStringLiteral("镜像"), m_imageValue);
        addField(1, 1, QStringLiteral("重启策略"), m_restartValue);
        addField(2, 0, QStringLiteral("启动命令"), m_commandValue);
        addField(2, 1, QStringLiteral("IP 地址"), m_ipValue);
        addField(3, 0, QStringLiteral("创建时间"), m_createdValue);
        addField(3, 1, QStringLiteral("网络"), m_networksValue);

        layout->addLayout(grid);

        layout->addWidget(makeLabel(QStringLiteral("端口映射"), QStringLiteral("h3"), page));

        m_portsTable = new QTableWidget(page);
        m_portsTable->setColumnCount(4);
        m_portsTable->setHorizontalHeaderLabels({QStringLiteral("宿主端口"), QStringLiteral("容器端口"),
                                                 QStringLiteral("协议"), QStringLiteral("绑定地址")});
        configureReadOnlyTable(m_portsTable);
        m_portsTable->setMinimumHeight(84);
        m_portsTable->setMaximumHeight(110);
        layout->addWidget(m_portsTable);

        layout->addWidget(makeLabel(QStringLiteral("挂载"), QStringLiteral("h3"), page));

        m_mountsTable = new QTableWidget(page);
        m_mountsTable->setColumnCount(4);
        m_mountsTable->setHorizontalHeaderLabels({QStringLiteral("类型"), QStringLiteral("宿主路径"),
                                                  QStringLiteral("容器路径"), QStringLiteral("读写")});
        configureReadOnlyTable(m_mountsTable);
        // 宿主/容器路径通常较长：让「类型」「读写」按内容自适应，两条路径平分剩余宽度
        m_mountsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_mountsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_mountsTable->setMinimumHeight(84);
        m_mountsTable->setMaximumHeight(110);
        layout->addWidget(m_mountsTable);

        layout->addWidget(makeLabel(QStringLiteral("环境变量"), QStringLiteral("h3"), page));

        m_envView = new QPlainTextEdit(page);
        m_envView->setReadOnly(true);
        m_envView->setFont(monospaceFont());
        m_envView->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_envView->setPlaceholderText(QStringLiteral("正在读取…"));
        m_envView->setMinimumHeight(70);
        m_envView->setMaximumHeight(110);
        layout->addWidget(m_envView);

        layout->addStretch(1);
        return page;
    }

    QWidget *buildLogsTab()
    {
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);

        auto *toolbar = new QHBoxLayout();
        toolbar->setContentsMargins(0, 0, 0, 0);
        toolbar->setSpacing(8);

        toolbar->addWidget(makeLabel(QStringLiteral("显示行数"), QStringLiteral("muted"), page));

        m_tailBox = new QComboBox(page);
        m_tailBox->addItem(QStringLiteral("最近 100 行"), 100);
        m_tailBox->addItem(QStringLiteral("最近 500 行"), 500);
        connect(m_tailBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            loadLogs(true);
        });
        toolbar->addWidget(m_tailBox);

        m_logAutoRefresh = new QCheckBox(QStringLiteral("自动刷新"), page);
        connect(m_logAutoRefresh, &QCheckBox::toggled, this, [this](bool on) {
            if (on) {
                m_logTimer->start();
            } else {
                m_logTimer->stop();
            }
        });
        toolbar->addWidget(m_logAutoRefresh);

        m_logAutoScroll = new QCheckBox(QStringLiteral("自动滚动到底部"), page);
        m_logAutoScroll->setChecked(true);
        toolbar->addWidget(m_logAutoScroll);

        toolbar->addStretch(1);

        m_logRefreshButton = makeButton(QStringLiteral("刷新"), QStringLiteral("ghost"), page);
        m_logRefreshButton->setMinimumHeight(28);
        connect(m_logRefreshButton, &QPushButton::clicked, this, [this]() { loadLogs(true); });
        toolbar->addWidget(m_logRefreshButton);
        layout->addLayout(toolbar);

        m_logView = new QPlainTextEdit(page);
        m_logView->setReadOnly(true);
        m_logView->setFont(monospaceFont());
        m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_logView->setPlaceholderText(QStringLiteral("正在读取日志…"));
        layout->addWidget(m_logView, 1);

        return page;
    }

    QWidget *buildStatsTab()
    {
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(12);

        auto *grid = new QGridLayout();
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(24);
        grid->setVerticalSpacing(14);

        auto addStat = [this, grid, page](int row, int column, const QString &caption, QLabel *&value) {
            auto *box = new QVBoxLayout();
            box->setContentsMargins(0, 0, 0, 0);
            box->setSpacing(4);
            box->addWidget(makeLabel(caption, QStringLiteral("muted"), page));
            value = makeLabel(QStringLiteral("—"), QStringLiteral("statValue"), page);
            box->addWidget(value);
            grid->addLayout(box, row, column);
        };

        addStat(0, 0, QStringLiteral("CPU 使用率"), m_cpuValue);
        addStat(0, 1, QStringLiteral("内存占用"), m_memValue);
        addStat(1, 0, QStringLiteral("网络流量"), m_netValue);
        addStat(1, 1, QStringLiteral("磁盘读写"), m_ioValue);
        addStat(2, 0, QStringLiteral("进程数"), m_pidValue);

        layout->addLayout(grid);

        m_statsHint = makeLabel(QStringLiteral("资源占用每 2 秒自动刷新。"), QStringLiteral("muted"), page);
        m_statsHint->setWordWrap(true);
        layout->addWidget(m_statsHint);

        layout->addStretch(1);
        return page;
    }

    /** 日志 / 端口 / 挂载表格的统一只读配置。 */
    static void configureReadOnlyTable(QTableWidget *table)
    {
        table->verticalHeader()->setVisible(false);
        table->verticalHeader()->setDefaultSectionSize(30);
        table->horizontalHeader()->setHighlightSections(false);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setShowGrid(false);
        table->setWordWrap(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
    }

    void loadInspect()
    {
        const QPointer<DockerDetailDialog> guard(this);
        m_api->get(QStringLiteral("/api/docker/containers/%1").arg(m_id),
                   [guard, this](bool ok, const QJsonObject &data, const QString &message) {
                       if (!guard) {
                           return;
                       }
                       if (!ok) {
                           m_infoHint->setText(QStringLiteral("读取容器详情失败：%1").arg(message));
                           return;
                       }
                       m_infoHint->setVisible(false);
                       applyInspect(data);
                   });
    }

    void applyInspect(const QJsonObject &data)
    {
        m_fullId = data.value(QStringLiteral("id")).toString();
        const QString shortId = data.value(QStringLiteral("shortId")).toString();
        m_idValue->setText(shortId.isEmpty() ? textOrDash(data, QStringLiteral("id")) : shortId);
        m_idValue->setToolTip(m_fullId);

        m_imageValue->setText(textOrDash(data, QStringLiteral("image")));

        QString command = joinStrings(data.value(QStringLiteral("entrypoint")).toArray(), QStringLiteral(" "));
        const QString cmdText = joinStrings(data.value(QStringLiteral("cmd")).toArray(), QStringLiteral(" "));
        if (!cmdText.isEmpty()) {
            command = command.isEmpty() ? cmdText : command + QLatin1Char(' ') + cmdText;
        }
        m_commandValue->setText(command.isEmpty() ? QStringLiteral("—") : command);

        m_restartValue->setText(textOrDash(data, QStringLiteral("restartPolicy")));

        const QString state = data.value(QStringLiteral("state")).toString();
        const QString status = data.value(QStringLiteral("status")).toString();
        m_stateValue->setText(status.isEmpty() ? stateLabel(state)
                                               : QStringLiteral("%1（%2）").arg(stateLabel(state), status));

        m_ipValue->setText(textOrDash(data, QStringLiteral("ipAddress")));
        m_createdValue->setText(Fmt::dateTime(data.value(QStringLiteral("created")).toString()));

        const QJsonArray networks = data.value(QStringLiteral("networks")).toArray();
        m_networksValue->setText(networks.isEmpty() ? QStringLiteral("—")
                                                    : joinStrings(networks, QStringLiteral("、")));

        // 端口表
        const Theme::Palette pal = Theme::current();
        const QJsonArray ports = data.value(QStringLiteral("ports")).toArray();
        m_portsTable->clearContents();
        m_portsTable->setRowCount(ports.size());
        for (int row = 0; row < ports.size(); ++row) {
            const QJsonObject port = ports.at(row).toObject();
            const QJsonValue hostValue = port.value(QStringLiteral("host"));
            const QString hostText = (hostValue.isNull() || hostValue.isUndefined())
                                         ? QStringLiteral("随机分配")
                                         : QString::number(hostValue.toInt());
            m_portsTable->setItem(row, 0, new QTableWidgetItem(hostText));
            m_portsTable->setItem(row, 1, new QTableWidgetItem(
                                               QString::number(port.value(QStringLiteral("container")).toInt())));
            m_portsTable->setItem(row, 2, new QTableWidgetItem(
                                               textOrDash(port, QStringLiteral("protocol"))));
            const QString hostIp = port.value(QStringLiteral("hostIp")).toString();
            m_portsTable->setItem(row, 3, new QTableWidgetItem(hostIp.isEmpty() ? QStringLiteral("—") : hostIp));
        }

        // 挂载表
        const QJsonArray mounts = data.value(QStringLiteral("mounts")).toArray();
        m_mountsTable->clearContents();
        m_mountsTable->setRowCount(mounts.size());
        for (int row = 0; row < mounts.size(); ++row) {
            const QJsonObject mount = mounts.at(row).toObject();
            auto putCell = [this, row](int column, const QString &text) {
                auto *item = new QTableWidgetItem(text);
                // 路径较长会被省略，鼠标悬停时显示完整内容
                item->setToolTip(text);
                m_mountsTable->setItem(row, column, item);
            };
            putCell(0, textOrDash(mount, QStringLiteral("type")));
            putCell(1, textOrDash(mount, QStringLiteral("source")));
            putCell(2, textOrDash(mount, QStringLiteral("destination")));
            auto *roItem = new QTableWidgetItem(mount.value(QStringLiteral("ro")).toBool()
                                                    ? QStringLiteral("只读")
                                                    : QStringLiteral("读写"));
            roItem->setForeground(mount.value(QStringLiteral("ro")).toBool() ? pal.warning : pal.muted);
            roItem->setToolTip(roItem->text());
            m_mountsTable->setItem(row, 3, roItem);
        }

        // 环境变量
        const QJsonArray env = data.value(QStringLiteral("env")).toArray();
        const QString envText = joinStrings(env, QStringLiteral("\n"));
        m_envView->setPlainText(envText.isEmpty() ? QStringLiteral("（未设置环境变量）") : envText);
    }

    void loadLogs(bool manual)
    {
        const int tail = m_tailBox->currentData().toInt();
        const QPointer<DockerDetailDialog> guard(this);
        m_logRefreshButton->setEnabled(false);

        m_api->get(QStringLiteral("/api/docker/containers/%1/logs?tail=%2").arg(m_id).arg(tail),
                   [guard, this, manual](bool ok, const QJsonObject &data, const QString &message) {
                       if (!guard) {
                           return;
                       }
                       m_logRefreshButton->setEnabled(true);
                       if (!ok) {
                           if (manual) {
                               m_logView->setPlainText(QStringLiteral("读取日志失败：%1").arg(message));
                           }
                           return;
                       }
                       QString text = data.value(QStringLiteral("text")).toString();
                       if (text.isEmpty()) {
                           text = joinStrings(data.value(QStringLiteral("logs")).toArray(), QStringLiteral("\n"));
                       }
                       if (text.trimmed().isEmpty()) {
                           text = QStringLiteral("（暂无日志输出）");
                       }
                       m_logView->setPlainText(text);
                       if (m_logAutoScroll->isChecked()) {
                           auto *bar = m_logView->verticalScrollBar();
                           bar->setValue(bar->maximum());
                       }
                   });
    }

    void loadStats(bool manual)
    {
        if (m_statsPending) {
            return;
        }
        m_statsPending = true;
        const QPointer<DockerDetailDialog> guard(this);

        m_api->get(QStringLiteral("/api/docker/containers/%1/stats").arg(m_id),
                   [guard, this, manual](bool ok, const QJsonObject &data, const QString &message) {
                       if (!guard) {
                           return;
                       }
                       m_statsPending = false;
                       if (!ok) {
                           m_statsHint->setText(QStringLiteral("读取资源占用失败：%1").arg(message));
                           return;
                       }
                       if (!data.value(QStringLiteral("available")).toBool(true)) {
                           const QString hint = data.value(QStringLiteral("message")).toString();
                           m_statsHint->setText(hint.isEmpty() ? QStringLiteral("容器未运行，暂无资源数据")
                                                               : hint);
                           m_cpuValue->setText(QStringLiteral("—"));
                           m_memValue->setText(QStringLiteral("—"));
                           m_netValue->setText(QStringLiteral("—"));
                           m_ioValue->setText(QStringLiteral("—"));
                           m_pidValue->setText(QStringLiteral("—"));
                           Q_UNUSED(manual);
                           return;
                       }

                       m_statsHint->setText(QStringLiteral("资源占用每 2 秒自动刷新。"));
                       m_cpuValue->setText(Fmt::percent(data.value(QStringLiteral("cpuPercent")).toDouble()));
                       m_memValue->setText(QStringLiteral("%1 / %2（%3）")
                                               .arg(Fmt::bytes(qint64(data.value(QStringLiteral("memUsage")).toDouble())),
                                                    Fmt::bytes(qint64(data.value(QStringLiteral("memLimit")).toDouble())),
                                                    Fmt::percent(data.value(QStringLiteral("memPercent")).toDouble())));
                       m_netValue->setText(QStringLiteral("接收 %1 / 发送 %2")
                                               .arg(Fmt::bytes(qint64(data.value(QStringLiteral("netIn")).toDouble())),
                                                    Fmt::bytes(qint64(data.value(QStringLiteral("netOut")).toDouble()))));
                       m_ioValue->setText(QStringLiteral("读 %1 / 写 %2")
                                              .arg(Fmt::bytes(qint64(data.value(QStringLiteral("blockRead")).toDouble())),
                                                   Fmt::bytes(qint64(data.value(QStringLiteral("blockWrite")).toDouble()))));
                       m_pidValue->setText(QString::number(data.value(QStringLiteral("pids")).toInt()));
                   });
    }

public:
    /** 打开对话框：先读详情，再按需启动 2 秒资源采样。 */
    void start()
    {
        m_statsTimer->start();
        exec();
        m_statsTimer->stop();
        m_logTimer->stop();
    }

private:
    ApiClient *m_api = nullptr;
    QString m_id;
    QString m_name;
    QString m_fullId;

    QTabWidget *m_tabs = nullptr;
    QLabel *m_infoHint = nullptr;
    QLabel *m_idValue = nullptr;
    QLabel *m_stateValue = nullptr;
    QLabel *m_imageValue = nullptr;
    QLabel *m_restartValue = nullptr;
    QLabel *m_commandValue = nullptr;
    QLabel *m_ipValue = nullptr;
    QLabel *m_createdValue = nullptr;
    QLabel *m_networksValue = nullptr;

    QTableWidget *m_portsTable = nullptr;
    QTableWidget *m_mountsTable = nullptr;
    QPlainTextEdit *m_envView = nullptr;

    QComboBox *m_tailBox = nullptr;
    QCheckBox *m_logAutoRefresh = nullptr;
    QCheckBox *m_logAutoScroll = nullptr;
    QPushButton *m_logRefreshButton = nullptr;
    QPlainTextEdit *m_logView = nullptr;

    QLabel *m_cpuValue = nullptr;
    QLabel *m_memValue = nullptr;
    QLabel *m_netValue = nullptr;
    QLabel *m_ioValue = nullptr;
    QLabel *m_pidValue = nullptr;
    QLabel *m_statsHint = nullptr;

    QPushButton *m_copyButton = nullptr;
    QTimer *m_statsTimer = nullptr;
    QTimer *m_logTimer = nullptr;
    bool m_statsPending = false;
};

} // namespace

namespace {

/** 统一创建 Docker 列表表格（隐藏表头行号、整行选择、只读、首个拉伸列）。 */
QTableWidget *makeDockerTable(QWidget *parent, const QStringList &headers, int stretchColumn)
{
    auto *table = new QTableWidget(parent);
    const int count = int(headers.size());
    table->setColumnCount(count);
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(40);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setSectionsClickable(false);
    for (int index = 0; index < count; ++index) {
        table->horizontalHeader()->setSectionResizeMode(
            index, index == stretchColumn ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
    }
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    return table;
}

/** 只读文本单元格。 */
QTableWidgetItem *makeCell(const QString &text)
{
    return new QTableWidgetItem(text.isEmpty() ? QStringLiteral("—") : text);
}

} // namespace

DockerPage::DockerPage(ApiClient *api, AppConfig *config, QWidget *parent)
    : BasePage(api, parent), m_config(config)
{
    setPageTitle(QStringLiteral("容器管理"));
    setPageSubtitle(QStringLiteral("管理 Docker 容器、镜像、数据卷与网络"));

    m_headerRefresh = makeButton(QStringLiteral("刷新"), QStringLiteral("ghost"), this);
    m_headerRefresh->setIcon(Icons::icon(Icons::Kind::Refresh, Theme::current().muted, 16));
    connect(m_headerRefresh, &QPushButton::clicked, this, [this]() { refreshAll(true); });
    addHeaderAction(m_headerRefresh);

    // 自动刷新：间隔由配置决定，0 = 手动暂停
    m_autoTimer = new QTimer(this);
    connect(m_autoTimer, &QTimer::timeout, this, [this]() {
        if (!m_config || m_config->dockerRefreshSec <= 0) {
            return;
        }
        if (!isVisible() || m_loading > 0 || m_actions > 0) {
            return;
        }
        refreshAll(false);
    });

    buildUi();
    applyIcons();
    updateBusy();
    // 初始化间隔 combo
    {
        const int sec = m_config ? m_config->dockerRefreshSec : 10;
        const int idx = m_intervalBox ? m_intervalBox->findData(sec) : -1;
        if (m_intervalBox && idx >= 0) {
            m_intervalBox->setCurrentIndex(idx);
        }
        applyRefreshInterval(sec);
    }

    refreshAll(false);
}

void DockerPage::refresh()
{
    refreshAll(false);
}

void DockerPage::reloadTheme()
{
    if (m_rootStack == nullptr) {
        return;
    }
    applyIcons();
    if (m_containerEmpty != nullptr) {
        m_containerEmpty->refreshTheme();
    }
    if (m_imageEmpty != nullptr) {
        m_imageEmpty->refreshTheme();
    }
    if (m_volumeEmpty != nullptr) {
        m_volumeEmpty->refreshTheme();
    }
    if (m_networkEmpty != nullptr) {
        m_networkEmpty->refreshTheme();
    }
    // 重新着色表格中的状态文字
    if (m_containerTable != nullptr) {
        applyContainerFilter();
    }
}

void DockerPage::applyIcons()
{
    const Theme::Palette pal = Theme::current();
    const QColor muted = pal.muted;
    const QColor accent = pal.accent;
    const QColor danger = pal.danger;

    if (m_headerRefresh != nullptr) {
        m_headerRefresh->setIcon(Icons::icon(Icons::Kind::Refresh, muted, 16));
    }
    if (m_retryButton != nullptr) {
        m_retryButton->setIcon(Icons::icon(Icons::Kind::Refresh, pal.accentText, 16));
    }
    if (m_containerRefresh != nullptr) {
        m_containerRefresh->setIcon(Icons::icon(Icons::Kind::Refresh, muted, 16));
    }
    if (m_newContainerButton != nullptr) {
        m_newContainerButton->setIcon(Icons::icon(Icons::Kind::Plus, pal.accentText, 16));
    }
    if (m_imageRefresh != nullptr) {
        m_imageRefresh->setIcon(Icons::icon(Icons::Kind::Refresh, muted, 16));
    }
    if (m_imagePullButton != nullptr) {
        m_imagePullButton->setIcon(Icons::icon(Icons::Kind::Download, pal.accentText, 16));
    }
    if (m_imageDeleteButton != nullptr) {
        m_imageDeleteButton->setIcon(Icons::icon(Icons::Kind::Trash, danger, 16));
    }
    if (m_imagePruneButton != nullptr) {
        m_imagePruneButton->setIcon(Icons::icon(Icons::Kind::Trash, muted, 16));
    }
    if (m_volumeRefresh != nullptr) {
        m_volumeRefresh->setIcon(Icons::icon(Icons::Kind::Refresh, muted, 16));
    }
    if (m_volumeCreateButton != nullptr) {
        m_volumeCreateButton->setIcon(Icons::icon(Icons::Kind::Plus, accent, 16));
    }
    if (m_volumeDeleteButton != nullptr) {
        m_volumeDeleteButton->setIcon(Icons::icon(Icons::Kind::Trash, danger, 16));
    }
    if (m_networkRefresh != nullptr) {
        m_networkRefresh->setIcon(Icons::icon(Icons::Kind::Refresh, muted, 16));
    }
    if (m_networkCreateButton != nullptr) {
        m_networkCreateButton->setIcon(Icons::icon(Icons::Kind::Plus, accent, 16));
    }
    if (m_networkDeleteButton != nullptr) {
        m_networkDeleteButton->setIcon(Icons::icon(Icons::Kind::Trash, danger, 16));
    }
}

void DockerPage::buildUi()
{
    m_rootStack = new QStackedWidget(this);
    m_unavailablePage = buildUnavailable();

    m_normalPage = new QWidget(m_rootStack);
    auto *normal = new QVBoxLayout(m_normalPage);
    normal->setContentsMargins(0, 0, 0, 0);
    normal->setSpacing(14);

    buildStatusBar();
    normal->addWidget(m_statusBar);

    m_tabs = new QTabWidget(m_normalPage);
    m_tabs->addTab(buildContainerTab(), QStringLiteral("容器"));
    m_tabs->addTab(buildImagesTab(), QStringLiteral("镜像"));
    m_tabs->addTab(buildVolumesTab(), QStringLiteral("数据卷"));
    m_tabs->addTab(buildNetworksTab(), QStringLiteral("网络"));
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_available) {
            loadTab(index, false);
        }
    });
    normal->addWidget(m_tabs, 1);

    m_rootStack->addWidget(m_normalPage);
    m_rootStack->addWidget(m_unavailablePage);
    m_rootStack->setCurrentWidget(m_normalPage);
    body()->addWidget(m_rootStack, 1);
}

void DockerPage::buildStatusBar()
{
    auto *card = new QFrame(m_normalPage);
    card->setProperty("card", true);
    m_statusBar = card;

    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(20);

    m_versionLabel = makeLabel(QStringLiteral("Docker 版本 —"), QStringLiteral("mono"), card);
    layout->addWidget(m_versionLabel);

    // 指标块：标题 + 数值
    auto addStat = [this, card, layout](const QString &title) -> QLabel * {
        auto *box = new QWidget(card);
        auto *column = new QVBoxLayout(box);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(1);
        column->addWidget(makeLabel(title, QStringLiteral("muted"), box));
        auto *value = makeLabel(QStringLiteral("—"), QStringLiteral("statValue"), box);
        column->addWidget(value);
        layout->addWidget(box);
        return value;
    };

    m_containerStat = addStat(QStringLiteral("容器（运行/总数）"));
    m_imageStat = addStat(QStringLiteral("镜像数量"));
    m_sizeStat = addStat(QStringLiteral("镜像占用"));
    layout->addStretch(1);

    m_intervalBox = new QComboBox(card);
    m_intervalBox->addItem(QStringLiteral("每 3 秒"), 3);
    m_intervalBox->addItem(QStringLiteral("每 5 秒"), 5);
    m_intervalBox->addItem(QStringLiteral("每 8 秒"), 8);
    m_intervalBox->addItem(QStringLiteral("每 10 秒"), 10);
    m_intervalBox->addItem(QStringLiteral("每 30 秒"), 30);
    m_intervalBox->addItem(QStringLiteral("手动"), 0);
    m_intervalBox->setToolTip(QStringLiteral("Docker 页面自动刷新间隔"));
    connect(m_intervalBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx) {
                const int sec = m_intervalBox->itemData(idx).toInt();
                if (m_config) {
                    m_config->dockerRefreshSec = sec;
                    m_config->normalize();
                    m_config->save();
                }
                applyRefreshInterval(sec);
            });
    layout->addWidget(m_intervalBox);
}

QWidget *DockerPage::buildContainerTab()
{
    auto *page = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 12, 0, 0);
    layout->setSpacing(10);

    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(8);

    m_containerSearch = new QLineEdit(page);
    m_containerSearch->setPlaceholderText(QStringLiteral("搜索容器名称或镜像"));
    m_containerSearch->setClearButtonEnabled(true);
    m_containerSearch->setMinimumWidth(220);
    m_containerSearch->setMaximumWidth(320);
    connect(m_containerSearch, &QLineEdit::textChanged, this, [this](const QString &) {
        applyContainerFilter();
    });
    toolbar->addWidget(m_containerSearch);

    m_containerStateFilter = new QComboBox(page);
    m_containerStateFilter->addItem(QStringLiteral("全部状态"), QStringLiteral("all"));
    m_containerStateFilter->addItem(QStringLiteral("运行中"), QStringLiteral("running"));
    m_containerStateFilter->addItem(QStringLiteral("已停止"), QStringLiteral("stopped"));
    m_containerStateFilter->addItem(QStringLiteral("已暂停"), QStringLiteral("paused"));
    m_containerStateFilter->addItem(QStringLiteral("重启中"), QStringLiteral("restarting"));
    m_containerStateFilter->addItem(QStringLiteral("已创建"), QStringLiteral("created"));
    connect(m_containerStateFilter, &QComboBox::currentIndexChanged, this, [this](int) {
        applyContainerFilter();
    });
    toolbar->addWidget(m_containerStateFilter);

    m_showStopped = new QCheckBox(QStringLiteral("显示已停止"), page);
    m_showStopped->setChecked(true);
    connect(m_showStopped, &QCheckBox::toggled, this, [this](bool) { applyContainerFilter(); });
    toolbar->addWidget(m_showStopped);

    m_containerRefresh = makeButton(QStringLiteral("刷新"), QStringLiteral("ghost"), page);
    m_containerRefresh->setMinimumHeight(28);
    connect(m_containerRefresh, &QPushButton::clicked, this, [this]() { loadContainers(true); });
    toolbar->addWidget(m_containerRefresh);

    m_newContainerButton = makeButton(QStringLiteral("新建容器"), QStringLiteral("primary"), page);
    m_newContainerButton->setMinimumHeight(28);
    connect(m_newContainerButton, &QPushButton::clicked, this, [this]() { openCreateContainer(); });
    toolbar->addWidget(m_newContainerButton);

    toolbar->addStretch(1);

    layout->addLayout(toolbar);

    m_containerStack = new QStackedWidget(page);

    m_containerTable = makeDockerTable(m_containerStack,
                                       {QStringLiteral("名称"), QStringLiteral("镜像"),
                                        QStringLiteral("状态"), QStringLiteral("端口"),
                                        QStringLiteral("创建时间"), QStringLiteral("操作")},
                                       1);
    m_containerTable->verticalHeader()->setDefaultSectionSize(42);
    m_containerTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_containerTable, &QTableWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) { showContainerMenu(pos); });
    connect(m_containerTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QTableWidgetItem *item = m_containerTable->item(row, 0);
        if (item == nullptr) {
            return;
        }
        showContainerDetails(item->data(Qt::UserRole).toString(),
                             item->data(Qt::UserRole + 1).toString());
    });
    m_containerStack->addWidget(m_containerTable);

    m_containerEmpty = new DockerEmptyState(
        Icons::Kind::Docker, QStringLiteral("暂无容器，点击右上角「新建容器」创建第一个容器。"),
        m_containerStack);
    m_containerStack->addWidget(m_containerEmpty);

    layout->addWidget(m_containerStack, 1);
    return page;
}

QWidget *DockerPage::buildImagesTab()
{
    auto *page = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 12, 0, 0);
    layout->setSpacing(10);

    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(8);

    m_imagePullEdit = new QLineEdit(page);
    m_imagePullEdit->setPlaceholderText(QStringLiteral("输入镜像引用，如 nginx:latest"));
    m_imagePullEdit->setClearButtonEnabled(true);
    m_imagePullEdit->setMinimumWidth(240);
    m_imagePullEdit->setMaximumWidth(360);
    connect(m_imagePullEdit, &QLineEdit::returnPressed, this, [this]() { pullImage(); });
    toolbar->addWidget(m_imagePullEdit);

    m_imagePullButton = makeButton(QStringLiteral("拉取镜像"), QStringLiteral("primary"), page);
    m_imagePullButton->setMinimumHeight(28);
    connect(m_imagePullButton, &QPushButton::clicked, this, [this]() { pullImage(); });
    toolbar->addWidget(m_imagePullButton);

    toolbar->addStretch(1);

    m_imageDeleteButton = makeButton(QStringLiteral("删除选中"), QStringLiteral("danger"), page);
    m_imageDeleteButton->setMinimumHeight(28);
    connect(m_imageDeleteButton, &QPushButton::clicked, this, [this]() { deleteImage(); });
    toolbar->addWidget(m_imageDeleteButton);

    m_imagePruneButton = makeButton(QStringLiteral("清理未使用"), QStringLiteral("ghost"), page);
    m_imagePruneButton->setMinimumHeight(28);
    connect(m_imagePruneButton, &QPushButton::clicked, this, [this]() { pruneImages(); });
    toolbar->addWidget(m_imagePruneButton);

    m_imageRefresh = makeButton(QStringLiteral("刷新"), QStringLiteral("ghost"), page);
    m_imageRefresh->setMinimumHeight(28);
    connect(m_imageRefresh, &QPushButton::clicked, this, [this]() { loadImages(true); });
    toolbar->addWidget(m_imageRefresh);

    layout->addLayout(toolbar);

    m_imageStack = new QStackedWidget(page);

    m_imageTable = makeDockerTable(m_imageStack,
                                   {QStringLiteral("仓库标签"), QStringLiteral("镜像 ID"),
                                    QStringLiteral("大小"), QStringLiteral("创建时间")},
                                   0);
    m_imageStack->addWidget(m_imageTable);

    m_imageEmpty = new DockerEmptyState(
        Icons::Kind::Server, QStringLiteral("暂无镜像，可在上方输入镜像引用并点击「拉取镜像」。"),
        m_imageStack);
    m_imageStack->addWidget(m_imageEmpty);

    layout->addWidget(m_imageStack, 1);
    return page;
}

QWidget *DockerPage::buildVolumesTab()
{
    auto *page = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 12, 0, 0);
    layout->setSpacing(10);

    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(8);

    toolbar->addStretch(1);

    m_volumeCreateButton = makeButton(QStringLiteral("新建数据卷"), QStringLiteral("primary"), page);
    m_volumeCreateButton->setMinimumHeight(28);
    connect(m_volumeCreateButton, &QPushButton::clicked, this, [this]() { openCreateVolume(); });
    toolbar->addWidget(m_volumeCreateButton);

    m_volumeDeleteButton = makeButton(QStringLiteral("删除选中"), QStringLiteral("danger"), page);
    m_volumeDeleteButton->setMinimumHeight(28);
    connect(m_volumeDeleteButton, &QPushButton::clicked, this, [this]() { deleteVolume(); });
    toolbar->addWidget(m_volumeDeleteButton);

    m_volumeRefresh = makeButton(QStringLiteral("刷新"), QStringLiteral("ghost"), page);
    m_volumeRefresh->setMinimumHeight(28);
    connect(m_volumeRefresh, &QPushButton::clicked, this, [this]() { loadVolumes(true); });
    toolbar->addWidget(m_volumeRefresh);

    layout->addLayout(toolbar);

    m_volumeStack = new QStackedWidget(page);

    m_volumeTable = makeDockerTable(m_volumeStack,
                                    {QStringLiteral("名称"), QStringLiteral("驱动"),
                                     QStringLiteral("作用域"), QStringLiteral("挂载点")},
                                    3);
    m_volumeStack->addWidget(m_volumeTable);

    m_volumeEmpty = new DockerEmptyState(
        Icons::Kind::Folder, QStringLiteral("暂无数据卷，点击右上角「新建数据卷」创建。"), m_volumeStack);
    m_volumeStack->addWidget(m_volumeEmpty);

    layout->addWidget(m_volumeStack, 1);
    return page;
}

QWidget *DockerPage::buildNetworksTab()
{
    auto *page = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 12, 0, 0);
    layout->setSpacing(10);

    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(8);

    toolbar->addStretch(1);

    m_networkCreateButton = makeButton(QStringLiteral("新建网络"), QStringLiteral("primary"), page);
    m_networkCreateButton->setMinimumHeight(28);
    connect(m_networkCreateButton, &QPushButton::clicked, this, [this]() { openCreateNetwork(); });
    toolbar->addWidget(m_networkCreateButton);

    m_networkDeleteButton = makeButton(QStringLiteral("删除选中"), QStringLiteral("danger"), page);
    m_networkDeleteButton->setMinimumHeight(28);
    connect(m_networkDeleteButton, &QPushButton::clicked, this, [this]() { deleteNetwork(); });
    toolbar->addWidget(m_networkDeleteButton);

    m_networkRefresh = makeButton(QStringLiteral("刷新"), QStringLiteral("ghost"), page);
    m_networkRefresh->setMinimumHeight(28);
    connect(m_networkRefresh, &QPushButton::clicked, this, [this]() { loadNetworks(true); });
    toolbar->addWidget(m_networkRefresh);

    layout->addLayout(toolbar);

    m_networkStack = new QStackedWidget(page);

    m_networkTable = makeDockerTable(m_networkStack,
                                     {QStringLiteral("名称"), QStringLiteral("驱动"),
                                      QStringLiteral("作用域"), QStringLiteral("IPv6"),
                                      QStringLiteral("内部网络")},
                                     0);
    m_networkStack->addWidget(m_networkTable);

    m_networkEmpty = new DockerEmptyState(
        Icons::Kind::Globe, QStringLiteral("暂无自定义网络，点击右上角「新建网络」创建。"), m_networkStack);
    m_networkStack->addWidget(m_networkEmpty);

    layout->addWidget(m_networkStack, 1);
    return page;
}

QWidget *DockerPage::buildUnavailable()
{
    auto *page = new QWidget(m_rootStack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    layout->addStretch(1);

    auto *icon = new QLabel(page);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(Icons::pixmap(Icons::Kind::Docker, Theme::current().muted, 64));
    layout->addWidget(icon, 0, Qt::AlignHCenter);

    auto *title = makeLabel(QStringLiteral("未检测到 Docker 服务"), QStringLiteral("h2"), page);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto *hint = makeLabel(QStringLiteral("请确认服务器已安装 Docker 且守护进程正在运行，然后点击「重试」。"),
                           QStringLiteral("muted"), page);
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_unavailableDetail = makeLabel(QString(), QStringLiteral("muted"), page);
    m_unavailableDetail->setAlignment(Qt::AlignCenter);
    m_unavailableDetail->setWordWrap(true);
    m_unavailableDetail->setVisible(false);
    layout->addWidget(m_unavailableDetail);

    m_retryButton = makeButton(QStringLiteral("重试"), QStringLiteral("primary"), page);
    m_retryButton->setMinimumWidth(120);
    connect(m_retryButton, &QPushButton::clicked, this, [this]() {
        showToast(QStringLiteral("正在重新检测 Docker 服务…"), QStringLiteral("info"));
        refreshAll(true);
    });
    layout->addWidget(m_retryButton, 0, Qt::AlignHCenter);

    layout->addStretch(2);
    return page;
}

void DockerPage::setDockerAvailable(bool available, const QString &detail)
{
    m_available = available;
    if (available) {
        m_rootStack->setCurrentWidget(m_normalPage);
        if (m_unavailableDetail != nullptr) {
            m_unavailableDetail->setVisible(false);
        }
        return;
    }

    if (m_unavailableDetail != nullptr) {
        const QString text = detail.trimmed();
        m_unavailableDetail->setText(text.isEmpty() ? QString()
                                                    : QStringLiteral("服务端返回：%1").arg(text));
        m_unavailableDetail->setVisible(!text.isEmpty());
    }
    if (m_versionLabel != nullptr) {
        m_versionLabel->setText(QStringLiteral("Docker 版本 —"));
    }
    if (m_containerStat != nullptr) {
        m_containerStat->setText(QStringLiteral("—"));
    }
    if (m_imageStat != nullptr) {
        m_imageStat->setText(QStringLiteral("—"));
    }
    if (m_sizeStat != nullptr) {
        m_sizeStat->setText(QStringLiteral("—"));
    }
    m_rootStack->setCurrentWidget(m_unavailablePage);
}

void DockerPage::updateBusy()
{
    const bool busy = m_loading > 0;
    const bool actionBusy = m_actions > 0;

    if (m_headerRefresh != nullptr) {
        m_headerRefresh->setEnabled(!busy && !actionBusy);
    }
    if (m_retryButton != nullptr) {
        m_retryButton->setEnabled(!busy && !actionBusy);
    }
    if (m_containerRefresh != nullptr) {
        m_containerRefresh->setEnabled(!busy && !actionBusy);
    }
    if (m_newContainerButton != nullptr) {
        m_newContainerButton->setEnabled(!actionBusy);
    }
    if (m_imageRefresh != nullptr) {
        m_imageRefresh->setEnabled(!busy && !actionBusy);
    }
    if (m_imagePullButton != nullptr) {
        m_imagePullButton->setEnabled(!actionBusy);
    }
    if (m_imageDeleteButton != nullptr) {
        m_imageDeleteButton->setEnabled(!actionBusy);
    }
    if (m_imagePruneButton != nullptr) {
        m_imagePruneButton->setEnabled(!actionBusy);
    }
    if (m_volumeRefresh != nullptr) {
        m_volumeRefresh->setEnabled(!busy && !actionBusy);
    }
    if (m_volumeCreateButton != nullptr) {
        m_volumeCreateButton->setEnabled(!actionBusy);
    }
    if (m_volumeDeleteButton != nullptr) {
        m_volumeDeleteButton->setEnabled(!actionBusy);
    }
    if (m_networkRefresh != nullptr) {
        m_networkRefresh->setEnabled(!busy && !actionBusy);
    }
    if (m_networkCreateButton != nullptr) {
        m_networkCreateButton->setEnabled(!actionBusy);
    }
    if (m_networkDeleteButton != nullptr) {
        m_networkDeleteButton->setEnabled(!actionBusy);
    }

    // 写操作期间禁用列表，避免重复点击触发并发操作
    if (m_containerTable != nullptr) {
        m_containerTable->setEnabled(!actionBusy);
    }
    if (m_imageTable != nullptr) {
        m_imageTable->setEnabled(!actionBusy);
    }
    if (m_volumeTable != nullptr) {
        m_volumeTable->setEnabled(!actionBusy);
    }
    if (m_networkTable != nullptr) {
        m_networkTable->setEnabled(!actionBusy);
    }
}

void DockerPage::applyRefreshInterval(int sec)
{
    if (sec <= 0) {
        m_autoTimer->stop();
    } else {
        m_autoTimer->start(sec * 1000);
    }
}

void DockerPage::refreshAll(bool manual)
{
    if (m_rootStack == nullptr) {
        return;
    }
    if (api() == nullptr) {
        setDockerAvailable(false, QStringLiteral("网络客户端未就绪，请重新登录后再试"));
        return;
    }
    loadOverview(manual);
    if (m_available) {
        loadTab(m_tabs->currentIndex(), manual);
    }
}

void DockerPage::loadOverview(bool manual)
{
    if (api() == nullptr) {
        setDockerAvailable(false, QStringLiteral("网络客户端未就绪，请重新登录后再试"));
        return;
    }

    ++m_loading;
    updateBusy();

    const QPointer<DockerPage> guard(this);
    api()->get(QStringLiteral("/api/docker/overview"),
               [guard, this, manual](bool ok, const QJsonObject &data, const QString &message) {
                   if (!guard) {
                       return;
                   }
                   --m_loading;
                   updateBusy();

                   if (!ok) {
                       setDockerAvailable(false, message);
                       if (manual) {
                           showToast(QStringLiteral("读取 Docker 概览失败：%1").arg(message),
                                     QStringLiteral("error"));
                       }
                       return;
                   }

                   const bool available = data.value(QStringLiteral("available")).toBool(true);
                   const bool wasAvailable = m_available;
                   setDockerAvailable(available, QString());
                   if (!available) {
                       if (manual) {
                           showToast(QStringLiteral("Docker 服务当前不可用"), QStringLiteral("error"));
                       }
                       return;
                   }

                   const QString version = data.value(QStringLiteral("version")).toString();
                   if (m_versionLabel != nullptr) {
                       m_versionLabel->setText(version.isEmpty()
                                                   ? QStringLiteral("Docker 版本 —")
                                                   : QStringLiteral("Docker 版本 %1").arg(version));
                   }

                   const QJsonObject stats = data.value(QStringLiteral("stats")).toObject();
                   const int total = stats.value(QStringLiteral("containers")).toInt();
                   const int running = stats.value(QStringLiteral("running")).toInt();
                   if (m_containerStat != nullptr) {
                       m_containerStat->setText(QStringLiteral("%1 / %2").arg(running).arg(total));
                   }
                   if (m_imageStat != nullptr) {
                       m_imageStat->setText(QString::number(stats.value(QStringLiteral("images")).toInt()));
                   }
                   if (m_sizeStat != nullptr) {
                       m_sizeStat->setText(
                           Fmt::bytes(qint64(stats.value(QStringLiteral("imagesSize")).toDouble())));
                   }

                   if (!wasAvailable) {
                       // 从不可用恢复后补拉当前页数据
                       loadTab(m_tabs->currentIndex(), false);
                   }
                   if (manual) {
                       showToast(QStringLiteral("Docker 状态已刷新"), QStringLiteral("success"));
                   }
               });
}

void DockerPage::loadTab(int index, bool manual)
{
    if (!m_available) {
        return;
    }
    switch (index) {
    case 0:
        loadContainers(manual);
        break;
    case 1:
        loadImages(manual);
        break;
    case 2:
        loadVolumes(manual);
        break;
    case 3:
        loadNetworks(manual);
        break;
    default:
        break;
    }
}

void DockerPage::loadContainers(bool manual)
{
    if (api() == nullptr) {
        return;
    }

    ++m_loading;
    updateBusy();

    const QPointer<DockerPage> guard(this);
    api()->get(QStringLiteral("/api/docker/containers?all=1"),
               [guard, this, manual](bool ok, const QJsonObject &data, const QString &message) {
                   if (!guard) {
                       return;
                   }
                   --m_loading;
                   updateBusy();

                   if (!ok) {
                       m_containerRows.clear();
                       m_containerTable->clearContents();
                       m_containerTable->setRowCount(0);
                       m_containerEmpty->setText(QStringLiteral("读取容器列表失败：%1").arg(message));
                       m_containerStack->setCurrentWidget(m_containerEmpty);
                       if (manual) {
                           showToast(QStringLiteral("读取容器列表失败：%1").arg(message),
                                     QStringLiteral("error"));
                       }
                       return;
                   }

                   QVector<QJsonObject> rows;
                   const QJsonArray items = data.value(QStringLiteral("items")).toArray();
                   rows.reserve(items.size());
                   for (const QJsonValue &value : items) {
                       if (value.isObject()) {
                           rows.append(value.toObject());
                       }
                   }
                   m_containerRows = rows;
                   applyContainerFilter();
                   if (manual) {
                       showToast(QStringLiteral("已刷新容器列表"), QStringLiteral("success"));
                   }
               });
}

void DockerPage::applyContainerFilter()
{
    if (m_containerTable == nullptr || m_containerStack == nullptr) {
        return;
    }

    const QString keyword = m_containerSearch->text().trimmed();
    const QString filter = m_containerStateFilter->currentData().toString();
    const bool showStopped = m_showStopped->isChecked();

    QVector<QJsonObject> visible;
    visible.reserve(m_containerRows.size());
    for (const QJsonObject &row : m_containerRows) {
        const QString name = row.value(QStringLiteral("name")).toString();
        const QString image = row.value(QStringLiteral("image")).toString();
        const QString state = row.value(QStringLiteral("state")).toString();

        if (!keyword.isEmpty() && !name.contains(keyword, Qt::CaseInsensitive)
            && !image.contains(keyword, Qt::CaseInsensitive)) {
            continue;
        }
        if (!matchesStateFilter(state, filter)) {
            continue;
        }
        if (!showStopped && !isActiveState(state) && state != QLatin1String("paused")) {
            continue;
        }
        visible.append(row);
    }

    fillContainerTable(visible);

    if (visible.isEmpty()) {
        m_containerEmpty->setText(m_containerRows.isEmpty()
                                      ? QStringLiteral("暂无容器，点击右上角「新建容器」创建第一个容器。")
                                      : QStringLiteral("没有符合条件的容器，试试调整搜索或筛选条件。"));
        m_containerStack->setCurrentWidget(m_containerEmpty);
    } else {
        m_containerStack->setCurrentWidget(m_containerTable);
    }
}

void DockerPage::fillContainerTable(const QVector<QJsonObject> &rows)
{
    m_containerTable->setUpdatesEnabled(false);
    m_containerTable->clearContents();
    m_containerTable->setRowCount(rows.size());

    int actionWidth = 0;
    const QFontMetrics metrics(m_containerTable->font());
    int createdWidth = metrics.horizontalAdvance(QStringLiteral("创建时间")) + 24;

    for (int index = 0; index < rows.size(); ++index) {
        const QJsonObject row = rows.at(index);
        const QString id = row.value(QStringLiteral("id")).toString();
        const QString name = row.value(QStringLiteral("name")).toString();
        const QString state = row.value(QStringLiteral("state")).toString();

        auto *nameItem = new QTableWidgetItem(name.isEmpty() ? QStringLiteral("（未命名）") : name);
        nameItem->setData(Qt::UserRole, id);
        nameItem->setData(Qt::UserRole + 1, name);
        nameItem->setData(Qt::UserRole + 2, state);
        nameItem->setToolTip(id.isEmpty() ? name : id);
        m_containerTable->setItem(index, 0, nameItem);

        m_containerTable->setItem(index, 1, makeCell(row.value(QStringLiteral("image")).toString()));

        auto *stateItem = new QTableWidgetItem(stateLabel(state));
        stateItem->setForeground(stateColor(state));
        stateItem->setData(Qt::UserRole, state);
        m_containerTable->setItem(index, 2, stateItem);

        QString ports = row.value(QStringLiteral("portText")).toString();
        if (ports.isEmpty()) {
            ports = joinStrings(row.value(QStringLiteral("ports")).toArray(), QStringLiteral(", "));
        }
        m_containerTable->setItem(index, 3, makeCell(ports));

        QString created = Fmt::timestamp(row.value(QStringLiteral("createdAt")));
        if (created.isEmpty()) {
            created = row.value(QStringLiteral("created")).toString();
        }
        auto *createdItem = makeCell(created);
        createdItem->setToolTip(created);
        m_containerTable->setItem(index, 4, createdItem);
        createdWidth = qMax(createdWidth, metrics.horizontalAdvance(created) + 24);

        // 操作列：常用动作 + 「更多」菜单
        auto *host = new QWidget(m_containerTable);
        auto *ops = new QHBoxLayout(host);
        ops->setContentsMargins(2, 2, 2, 2);
        ops->setSpacing(2);

        auto addLink = [this, host, ops](const QString &text, auto handler) {
            auto *button = makeButton(text, QStringLiteral("link"), host);
            button->setMinimumHeight(22);
            connect(button, &QPushButton::clicked, this, handler);
            ops->addWidget(button);
        };

        // 首按钮与「更多」菜单保持同一套状态→动作映射：
        // 运行/重启中 → 停止；已停止/已创建 → 启动；已暂停 → 恢复（unpause）
        if (state == QLatin1String("paused")) {
            addLink(QStringLiteral("恢复"), [this, id, name]() {
                runContainerAction(id, name, QStringLiteral("unpause"), QStringLiteral("恢复"));
            });
        } else if (isActiveState(state)) {
            addLink(QStringLiteral("停止"), [this, id, name]() {
                runContainerAction(id, name, QStringLiteral("stop"), QStringLiteral("停止"));
            });
        } else {
            addLink(QStringLiteral("启动"), [this, id, name]() {
                runContainerAction(id, name, QStringLiteral("start"), QStringLiteral("启动"));
            });
        }
        addLink(QStringLiteral("重启"), [this, id, name]() {
            runContainerAction(id, name, QStringLiteral("restart"), QStringLiteral("重启"));
        });
        addLink(QStringLiteral("详情"), [this, id, name]() { showContainerDetails(id, name); });
        addLink(QStringLiteral("日志"), [this, id, name]() { openContainerLogs(id, name); });
        addLink(QStringLiteral("更多"), [this, id, name, state]() {
            QMenu *menu = buildContainerMenu(id, name, state);
            menu->exec(QCursor::pos());
            menu->deleteLater();
        });

        m_containerTable->setCellWidget(index, 5, host);
        actionWidth = qMax(actionWidth, host->sizeHint().width());
    }

    // 「操作」列里放的是按钮控件，而 QHeaderView::ResizeToContents 只按表头文字
    // 估算宽度，会把按钮裁掉半个；因此按最宽的一组按钮固定该列宽度。
    // 「创建时间」改为固定宽度并允许省略，避免挤压其余列导致右侧按钮被挤出窗口。
    QHeaderView *header = m_containerTable->horizontalHeader();
    header->setSectionResizeMode(4, QHeaderView::Fixed);
    header->setSectionResizeMode(5, QHeaderView::Fixed);
    m_containerTable->setColumnWidth(4, createdWidth);
    if (actionWidth > 0) {
        m_containerTable->setColumnWidth(5, actionWidth + 16);
    }

    m_containerTable->setUpdatesEnabled(true);
}

QMenu *DockerPage::buildContainerMenu(const QString &id, const QString &name, const QString &state)
{
    auto *menu = new QMenu(this);
    makePopupRounded(menu);

    if (isActiveState(state)) {
        menu->addAction(QStringLiteral("停止"), this, [this, id, name]() {
            runContainerAction(id, name, QStringLiteral("stop"), QStringLiteral("停止"));
        });
    } else {
        menu->addAction(QStringLiteral("启动"), this, [this, id, name]() {
            runContainerAction(id, name, QStringLiteral("start"), QStringLiteral("启动"));
        });
    }

    menu->addAction(QStringLiteral("重启"), this, [this, id, name]() {
        runContainerAction(id, name, QStringLiteral("restart"), QStringLiteral("重启"));
    });

    if (state == QLatin1String("paused")) {
        menu->addAction(QStringLiteral("恢复"), this, [this, id, name]() {
            runContainerAction(id, name, QStringLiteral("unpause"), QStringLiteral("恢复"));
        });
    } else if (isActiveState(state)) {
        menu->addAction(QStringLiteral("暂停"), this, [this, id, name]() {
            runContainerAction(id, name, QStringLiteral("pause"), QStringLiteral("暂停"));
        });
    }

    menu->addSeparator();
    menu->addAction(QStringLiteral("查看详情"), this, [this, id, name]() { showContainerDetails(id, name); });
    menu->addAction(QStringLiteral("查看日志"), this, [this, id, name]() { openContainerLogs(id, name); });
    menu->addAction(QStringLiteral("重命名"), this, [this, id, name]() { renameContainer(id, name); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("删除"), this, [this, id, name]() { deleteContainer(id, name); });
    return menu;
}

void DockerPage::showContainerMenu(const QPoint &pos)
{
    const QModelIndex index = m_containerTable->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    const QTableWidgetItem *item = m_containerTable->item(index.row(), 0);
    if (item == nullptr) {
        return;
    }
    m_containerTable->selectRow(index.row());

    QMenu *menu = buildContainerMenu(item->data(Qt::UserRole).toString(),
                                     item->data(Qt::UserRole + 1).toString(),
                                     item->data(Qt::UserRole + 2).toString());
    menu->exec(m_containerTable->viewport()->mapToGlobal(pos));
    menu->deleteLater();
}

void DockerPage::runContainerAction(const QString &id, const QString &name, const QString &verb,
                                    const QString &label)
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法执行操作"), QStringLiteral("error"));
        return;
    }
    if (m_actions > 0) {
        showToast(QStringLiteral("正在执行其他操作，请稍候"), QStringLiteral("info"));
        return;
    }

    ++m_actions;
    updateBusy();

    const QString display = name.isEmpty() ? id.left(12) : name;
    const QString path = QStringLiteral("/api/docker/containers/%1/%2")
                             .arg(QString::fromLatin1(QUrl::toPercentEncoding(id)), verb);

    const QPointer<DockerPage> guard(this);
    api()->post(path, QJsonObject(),
                [guard, this, display, label](bool ok, const QJsonObject &, const QString &message) {
                    if (!guard) {
                        return;
                    }
                    --m_actions;
                    updateBusy();
                    if (!ok) {
                        showToast(QStringLiteral("%1容器失败：%2").arg(label, message),
                                  QStringLiteral("error"));
                        return;
                    }
                    showToast(QStringLiteral("已%1容器 %2").arg(label, display), QStringLiteral("success"));
                    refreshAll(false);
                });
}

void DockerPage::openCreateContainer()
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法创建容器"), QStringLiteral("error"));
        return;
    }
    if (m_actions > 0) {
        showToast(QStringLiteral("正在执行其他操作，请稍候"), QStringLiteral("info"));
        return;
    }

    DockerCreateDialog dialog(api(), m_imageRefs, m_networkNames, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString name = dialog.createdName();
    showToast(name.isEmpty() ? QStringLiteral("容器创建成功")
                             : QStringLiteral("容器 %1 创建成功").arg(name),
              QStringLiteral("success"));
    refreshAll(false);
}

void DockerPage::openContainerLogs(const QString &id, const QString &name)
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法查看日志"), QStringLiteral("error"));
        return;
    }
    DockerDetailDialog dialog(api(), id, name, this, 1);
    dialog.start();
    refreshAll(false);
}

void DockerPage::showContainerDetails(const QString &id, const QString &name)
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法查看详情"), QStringLiteral("error"));
        return;
    }
    DockerDetailDialog dialog(api(), id, name, this, 0);
    dialog.start();
    refreshAll(false);
}

void DockerPage::renameContainer(const QString &id, const QString &currentName)
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法重命名"), QStringLiteral("error"));
        return;
    }
    if (m_actions > 0) {
        showToast(QStringLiteral("正在执行其他操作，请稍候"), QStringLiteral("info"));
        return;
    }

    DockerInputDialog dialog(QStringLiteral("重命名容器"), QStringLiteral("新名称"), currentName,
                             QStringList(), QString(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString name = dialog.value();
    if (name == currentName) {
        return;
    }

    ++m_actions;
    updateBusy();

    QJsonObject body;
    body.insert(QStringLiteral("name"), name);

    const QPointer<DockerPage> guard(this);
    const QString path =
        QStringLiteral("/api/docker/containers/%1/rename").arg(QString::fromLatin1(QUrl::toPercentEncoding(id)));
    api()->post(path, body, [guard, this, name](bool ok, const QJsonObject &, const QString &message) {
        if (!guard) {
            return;
        }
        --m_actions;
        updateBusy();
        if (!ok) {
            showToast(QStringLiteral("重命名失败：%1").arg(message), QStringLiteral("error"));
            return;
        }
        showToast(QStringLiteral("已重命名为 %1").arg(name), QStringLiteral("success"));
        refreshAll(false);
    });
}

void DockerPage::deleteContainer(const QString &id, const QString &name)
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法删除容器"), QStringLiteral("error"));
        return;
    }
    if (m_actions > 0) {
        showToast(QStringLiteral("正在执行其他操作，请稍候"), QStringLiteral("info"));
        return;
    }

    const QString display = name.isEmpty() ? id.left(12) : name;
    DockerConfirmDialog dialog(
        QStringLiteral("删除容器"),
        QStringLiteral("确定要删除容器「%1」吗？删除后容器内的临时数据将无法恢复。").arg(display),
        {QStringLiteral("强制删除（运行中的容器也会被直接删除）"),
         QStringLiteral("同时删除该容器的匿名数据卷")},
        QStringLiteral("删除"), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const bool force = dialog.isChecked(0);
    const bool removeVolumes = dialog.isChecked(1);

    ++m_actions;
    updateBusy();

    const QString path = QStringLiteral("/api/docker/containers/%1?force=%2&volumes=%3")
                             .arg(QString::fromLatin1(QUrl::toPercentEncoding(id)),
                                  force ? QStringLiteral("1") : QStringLiteral("0"),
                                  removeVolumes ? QStringLiteral("1") : QStringLiteral("0"));

    const QPointer<DockerPage> guard(this);
    api()->del(path, [guard, this, display](bool ok, const QJsonObject &, const QString &message) {
        if (!guard) {
            return;
        }
        --m_actions;
        updateBusy();
        if (!ok) {
            showToast(QStringLiteral("删除容器失败：%1").arg(message), QStringLiteral("error"));
            return;
        }
        showToast(QStringLiteral("已删除容器 %1").arg(display), QStringLiteral("success"));
        refreshAll(false);
    });
}

void DockerPage::pullImage()
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法拉取镜像"), QStringLiteral("error"));
        return;
    }
    if (m_actions > 0) {
        showToast(QStringLiteral("正在执行其他操作，请稍候"), QStringLiteral("info"));
        return;
    }

    const QString ref = m_imagePullEdit->text().trimmed();
    if (ref.isEmpty()) {
        showToast(QStringLiteral("请先填写要拉取的镜像，如 nginx:latest"), QStringLiteral("info"));
        m_imagePullEdit->setFocus();
        return;
    }
    if (!kImagePattern.match(ref).hasMatch() || ref.contains(QLatin1String(".."))
        || ref.contains(QLatin1String("//"))) {
        showToast(QStringLiteral("镜像引用不合法，只能包含字母、数字、点、下划线、短横线、斜杠、冒号和 @"),
                  QStringLiteral("error"));
        return;
    }

    ++m_actions;
    updateBusy();
    m_imagePullButton->setText(QStringLiteral("拉取中…"));

    QJsonObject body;
    body.insert(QStringLiteral("image"), ref);

    const QPointer<DockerPage> guard(this);
    api()->postSlow(QStringLiteral("/api/docker/images/pull"), body, 310000,
                    [guard, this, ref](bool ok, const QJsonObject &, const QString &message) {
                        if (!guard) {
                            return;
                        }
                        --m_actions;
                        updateBusy();
                        m_imagePullButton->setText(QStringLiteral("拉取镜像"));
                        if (!ok) {
                            showToast(QStringLiteral("拉取镜像失败：%1").arg(message),
                                      QStringLiteral("error"));
                            return;
                        }
                        showToast(QStringLiteral("镜像 %1 拉取成功").arg(ref), QStringLiteral("success"));
                        m_imagePullEdit->clear();
                        refreshAll(false);
                    });
}

void DockerPage::deleteImage()
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法删除镜像"), QStringLiteral("error"));
        return;
    }
    if (m_actions > 0) {
        showToast(QStringLiteral("正在执行其他操作，请稍候"), QStringLiteral("info"));
        return;
    }

    const int row = m_imageTable->currentRow();
    const QTableWidgetItem *item = row >= 0 ? m_imageTable->item(row, 0) : nullptr;
    if (item == nullptr) {
        showToast(QStringLiteral("请先在列表中选择要删除的镜像"), QStringLiteral("info"));
        return;
    }

    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) {
        showToast(QStringLiteral("无法获取该镜像的 ID，请刷新后重试"), QStringLiteral("error"));
        return;
    }
    const QString display = item->text();

    DockerConfirmDialog dialog(
        QStringLiteral("删除镜像"),
        QStringLiteral("确定要删除镜像「%1」吗？该操作不可撤销。").arg(display),
        {QStringLiteral("强制删除（即使仍被容器引用）")}, QStringLiteral("删除"), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const bool force = dialog.isChecked(0);

    ++m_actions;
    updateBusy();

    const QString path = QStringLiteral("/api/docker/images/%1?force=%2")
                             .arg(QString::fromLatin1(QUrl::toPercentEncoding(id)),
                                  force ? QStringLiteral("1") : QStringLiteral("0"));

    const QPointer<DockerPage> guard(this);
    api()->del(path, [guard, this, display](bool ok, const QJsonObject &, const QString &message) {
        if (!guard) {
            return;
        }
        --m_actions;
        updateBusy();
        if (!ok) {
            showToast(QStringLiteral("删除镜像失败：%1").arg(message), QStringLiteral("error"));
            return;
        }
        showToast(QStringLiteral("已删除镜像 %1").arg(display), QStringLiteral("success"));
        refreshAll(false);
    });
}

void DockerPage::pruneImages()
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法清理镜像"), QStringLiteral("error"));
        return;
    }
    if (m_actions > 0) {
        showToast(QStringLiteral("正在执行其他操作，请稍候"), QStringLiteral("info"));
        return;
    }

    DockerConfirmDialog dialog(QStringLiteral("清理未使用镜像"),
                               QStringLiteral("将删除所有没有被容器使用的镜像。清理过程可能需要较长时间，"
                                              "请勿关闭客户端。"),
                               {QStringLiteral("同时清理无标签（悬空）镜像")}, QStringLiteral("立即清理"),
                               this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const bool all = dialog.isChecked(0);

    ++m_actions;
    updateBusy();
    m_imagePruneButton->setText(QStringLiteral("清理中…"));

    QJsonObject body;
    body.insert(QStringLiteral("all"), all);

    const QPointer<DockerPage> guard(this);
    api()->postSlow(QStringLiteral("/api/docker/images/prune"), body, 130000,
                    [guard, this](bool ok, const QJsonObject &, const QString &message) {
                        if (!guard) {
                            return;
                        }
                        --m_actions;
                        updateBusy();
                        m_imagePruneButton->setText(QStringLiteral("清理未使用"));
                        if (!ok) {
                            showToast(QStringLiteral("清理镜像失败：%1").arg(message),
                                      QStringLiteral("error"));
                            return;
                        }
                        showToast(QStringLiteral("已清理未使用的镜像"), QStringLiteral("success"));
                        refreshAll(false);
                    });
}

void DockerPage::openCreateVolume()
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法创建数据卷"), QStringLiteral("error"));
        return;
    }
    if (m_actions > 0) {
        showToast(QStringLiteral("正在执行其他操作，请稍候"), QStringLiteral("info"));
        return;
    }

    DockerInputDialog dialog(QStringLiteral("新建数据卷"), QStringLiteral("数据卷名称"), QString(),
                             {QStringLiteral("local")}, QStringLiteral("local"), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("name"), dialog.value());
    const QString driver = dialog.driver();
    if (!driver.isEmpty()) {
        body.insert(QStringLiteral("driver"), driver);
    }

    const QString name = dialog.value();
    ++m_actions;
    updateBusy();

    const QPointer<DockerPage> guard(this);
    api()->post(QStringLiteral("/api/docker/volumes"), body,
                [guard, this, name](bool ok, const QJsonObject &, const QString &message) {
                    if (!guard) {
                        return;
                    }
                    --m_actions;
                    updateBusy();
                    if (!ok) {
                        showToast(QStringLiteral("创建数据卷失败：%1").arg(message),
                                  QStringLiteral("error"));
                        return;
                    }
                    showToast(QStringLiteral("已创建数据卷 %1").arg(name), QStringLiteral("success"));
                    refreshAll(false);
                });
}

void DockerPage::deleteVolume()
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法删除数据卷"), QStringLiteral("error"));
        return;
    }
    if (m_actions > 0) {
        showToast(QStringLiteral("正在执行其他操作，请稍候"), QStringLiteral("info"));
        return;
    }

    const int row = m_volumeTable->currentRow();
    const QTableWidgetItem *item = row >= 0 ? m_volumeTable->item(row, 0) : nullptr;
    if (item == nullptr) {
        showToast(QStringLiteral("请先在列表中选择要删除的数据卷"), QStringLiteral("info"));
        return;
    }
    const QString name = item->data(Qt::UserRole).toString();
    if (name.isEmpty()) {
        showToast(QStringLiteral("无法获取该数据卷名称，请刷新后重试"), QStringLiteral("error"));
        return;
    }

    DockerConfirmDialog dialog(
        QStringLiteral("删除数据卷"),
        QStringLiteral("确定要删除数据卷「%1」吗？卷内的数据将被永久删除且无法恢复，请谨慎操作。").arg(name),
        {QStringLiteral("强制删除（即使仍有容器在使用）")}, QStringLiteral("永久删除"), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const bool force = dialog.isChecked(0);

    ++m_actions;
    updateBusy();

    const QString path = QStringLiteral("/api/docker/volumes/%1?force=%2")
                             .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)),
                                  force ? QStringLiteral("1") : QStringLiteral("0"));

    const QPointer<DockerPage> guard(this);
    api()->del(path, [guard, this, name](bool ok, const QJsonObject &, const QString &message) {
        if (!guard) {
            return;
        }
        --m_actions;
        updateBusy();
        if (!ok) {
            showToast(QStringLiteral("删除数据卷失败：%1").arg(message), QStringLiteral("error"));
            return;
        }
        showToast(QStringLiteral("已删除数据卷 %1").arg(name), QStringLiteral("success"));
        refreshAll(false);
    });
}

void DockerPage::openCreateNetwork()
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法创建网络"), QStringLiteral("error"));
        return;
    }
    if (m_actions > 0) {
        showToast(QStringLiteral("正在执行其他操作，请稍候"), QStringLiteral("info"));
        return;
    }

    DockerInputDialog dialog(QStringLiteral("新建网络"), QStringLiteral("网络名称"), QString(),
                             {QStringLiteral("bridge"), QStringLiteral("host"), QStringLiteral("none"),
                              QStringLiteral("overlay"), QStringLiteral("macvlan"),
                              QStringLiteral("ipvlan")},
                             QStringLiteral("bridge"), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("name"), dialog.value());
    const QString driver = dialog.driver();
    if (!driver.isEmpty()) {
        body.insert(QStringLiteral("driver"), driver);
    }

    const QString name = dialog.value();
    ++m_actions;
    updateBusy();

    const QPointer<DockerPage> guard(this);
    api()->post(QStringLiteral("/api/docker/networks"), body,
                [guard, this, name](bool ok, const QJsonObject &, const QString &message) {
                    if (!guard) {
                        return;
                    }
                    --m_actions;
                    updateBusy();
                    if (!ok) {
                        showToast(QStringLiteral("创建网络失败：%1").arg(message), QStringLiteral("error"));
                        return;
                    }
                    showToast(QStringLiteral("已创建网络 %1").arg(name), QStringLiteral("success"));
                    refreshAll(false);
                });
}

void DockerPage::deleteNetwork()
{
    if (api() == nullptr) {
        showToast(QStringLiteral("网络客户端未就绪，无法删除网络"), QStringLiteral("error"));
        return;
    }
    if (m_actions > 0) {
        showToast(QStringLiteral("正在执行其他操作，请稍候"), QStringLiteral("info"));
        return;
    }

    const int row = m_networkTable->currentRow();
    const QTableWidgetItem *item = row >= 0 ? m_networkTable->item(row, 0) : nullptr;
    if (item == nullptr) {
        showToast(QStringLiteral("请先在列表中选择要删除的网络"), QStringLiteral("info"));
        return;
    }
    const QString name = item->data(Qt::UserRole).toString();
    if (name.isEmpty()) {
        showToast(QStringLiteral("无法获取该网络名称，请刷新后重试"), QStringLiteral("error"));
        return;
    }

    DockerConfirmDialog dialog(
        QStringLiteral("删除网络"),
        QStringLiteral("确定要删除网络「%1」吗？仍有容器使用时需要先断开连接。").arg(name),
        QStringList(), QStringLiteral("删除"), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    ++m_actions;
    updateBusy();

    const QString path =
        QStringLiteral("/api/docker/networks/%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(name)));

    const QPointer<DockerPage> guard(this);
    api()->del(path, [guard, this, name](bool ok, const QJsonObject &, const QString &message) {
        if (!guard) {
            return;
        }
        --m_actions;
        updateBusy();
        if (!ok) {
            showToast(QStringLiteral("删除网络失败：%1").arg(message), QStringLiteral("error"));
            return;
        }
        showToast(QStringLiteral("已删除网络 %1").arg(name), QStringLiteral("success"));
        refreshAll(false);
    });
}

void DockerPage::loadImages(bool manual)
{
    if (api() == nullptr) {
        return;
    }

    ++m_loading;
    updateBusy();

    const QPointer<DockerPage> guard(this);
    api()->get(QStringLiteral("/api/docker/images"),
               [guard, this, manual](bool ok, const QJsonObject &data, const QString &message) {
                   if (!guard) {
                       return;
                   }
                   --m_loading;
                   updateBusy();

                   if (!ok) {
                       m_imageTable->clearContents();
                       m_imageTable->setRowCount(0);
                       m_imageEmpty->setText(QStringLiteral("读取镜像列表失败：%1").arg(message));
                       m_imageStack->setCurrentWidget(m_imageEmpty);
                       if (manual) {
                           showToast(QStringLiteral("读取镜像列表失败：%1").arg(message),
                                     QStringLiteral("error"));
                       }
                       return;
                   }

                   const QJsonArray items = data.value(QStringLiteral("items")).toArray();
                   m_imageRefs.clear();
                   m_imageTable->setUpdatesEnabled(false);
                   m_imageTable->clearContents();
                   m_imageTable->setRowCount(int(items.size()));

                   for (int index = 0; index < items.size(); ++index) {
                       const QJsonObject item = items.at(index).toObject();
                       const QString repoTag = item.value(QStringLiteral("repoTag")).toString();
                       const QString display = repoTag.isEmpty() || repoTag == QLatin1String("<none>:<none>")
                                                   ? QStringLiteral("<none>（无标签镜像）")
                                                   : repoTag;
                       if (!repoTag.isEmpty() && repoTag != QLatin1String("<none>:<none>")) {
                           m_imageRefs.append(repoTag);
                       }

                       auto *refItem = new QTableWidgetItem(display);
                       refItem->setData(Qt::UserRole, item.value(QStringLiteral("id")).toString());
                       refItem->setToolTip(display);
                       m_imageTable->setItem(index, 0, refItem);

                       m_imageTable->setItem(
                           index, 1, makeCell(item.value(QStringLiteral("shortId")).toString()));
                       m_imageTable->setItem(
                           index, 2,
                           makeCell(Fmt::bytes(qint64(item.value(QStringLiteral("sizeBytes")).toDouble()))));

                       QString created = Fmt::dateTime(item.value(QStringLiteral("created")).toString());
                       if (created.isEmpty()) {
                           created = item.value(QStringLiteral("createdSince")).toString();
                       }
                       m_imageTable->setItem(index, 3, makeCell(created));
                   }
                   m_imageTable->setUpdatesEnabled(true);

                   if (items.isEmpty()) {
                       m_imageEmpty->setText(
                           QStringLiteral("暂无镜像，可在上方输入镜像引用并点击「拉取镜像」。"));
                       m_imageStack->setCurrentWidget(m_imageEmpty);
                   } else {
                       m_imageStack->setCurrentWidget(m_imageTable);
                   }
                   if (manual) {
                       showToast(QStringLiteral("已刷新镜像列表"), QStringLiteral("success"));
                   }
               });
}

void DockerPage::loadVolumes(bool manual)
{
    if (api() == nullptr) {
        return;
    }

    ++m_loading;
    updateBusy();

    const QPointer<DockerPage> guard(this);
    api()->get(QStringLiteral("/api/docker/volumes"),
               [guard, this, manual](bool ok, const QJsonObject &data, const QString &message) {
                   if (!guard) {
                       return;
                   }
                   --m_loading;
                   updateBusy();

                   if (!ok) {
                       m_volumeNames.clear();
                       m_volumeTable->clearContents();
                       m_volumeTable->setRowCount(0);
                       m_volumeEmpty->setText(QStringLiteral("读取数据卷列表失败：%1").arg(message));
                       m_volumeStack->setCurrentWidget(m_volumeEmpty);
                       if (manual) {
                           showToast(QStringLiteral("读取数据卷列表失败：%1").arg(message),
                                     QStringLiteral("error"));
                       }
                       return;
                   }

                   const QJsonArray items = data.value(QStringLiteral("items")).toArray();
                   m_volumeNames.clear();
                   m_volumeTable->setUpdatesEnabled(false);
                   m_volumeTable->clearContents();
                   m_volumeTable->setRowCount(int(items.size()));

                   for (int index = 0; index < items.size(); ++index) {
                       const QJsonObject item = items.at(index).toObject();
                       const QString name = item.value(QStringLiteral("name")).toString();
                       m_volumeNames.append(name);

                       auto *nameItem = new QTableWidgetItem(name);
                       nameItem->setData(Qt::UserRole, name);
                       nameItem->setToolTip(item.value(QStringLiteral("mountpoint")).toString());
                       m_volumeTable->setItem(index, 0, nameItem);

                       m_volumeTable->setItem(
                           index, 1, makeCell(item.value(QStringLiteral("driver")).toString()));
                       m_volumeTable->setItem(
                           index, 2, makeCell(item.value(QStringLiteral("scope")).toString()));
                       m_volumeTable->setItem(
                           index, 3, makeCell(item.value(QStringLiteral("mountpoint")).toString()));
                   }
                   m_volumeTable->setUpdatesEnabled(true);

                   if (items.isEmpty()) {
                       m_volumeEmpty->setText(
                           QStringLiteral("暂无数据卷，点击右上角「新建数据卷」创建。"));
                       m_volumeStack->setCurrentWidget(m_volumeEmpty);
                   } else {
                       m_volumeStack->setCurrentWidget(m_volumeTable);
                   }
                   if (manual) {
                       showToast(QStringLiteral("已刷新数据卷列表"), QStringLiteral("success"));
                   }
               });
}

void DockerPage::loadNetworks(bool manual)
{
    if (api() == nullptr) {
        return;
    }

    ++m_loading;
    updateBusy();

    const QPointer<DockerPage> guard(this);
    api()->get(QStringLiteral("/api/docker/networks"),
               [guard, this, manual](bool ok, const QJsonObject &data, const QString &message) {
                   if (!guard) {
                       return;
                   }
                   --m_loading;
                   updateBusy();

                   if (!ok) {
                       m_networkNames.clear();
                       m_networkTable->clearContents();
                       m_networkTable->setRowCount(0);
                       m_networkEmpty->setText(QStringLiteral("读取网络列表失败：%1").arg(message));
                       m_networkStack->setCurrentWidget(m_networkEmpty);
                       if (manual) {
                           showToast(QStringLiteral("读取网络列表失败：%1").arg(message),
                                     QStringLiteral("error"));
                       }
                       return;
                   }

                   const QJsonArray items = data.value(QStringLiteral("items")).toArray();
                   m_networkNames.clear();
                   m_networkTable->setUpdatesEnabled(false);
                   m_networkTable->clearContents();
                   m_networkTable->setRowCount(int(items.size()));

                   for (int index = 0; index < items.size(); ++index) {
                       const QJsonObject item = items.at(index).toObject();
                       const QString name = item.value(QStringLiteral("name")).toString();
                       m_networkNames.append(name);

                       auto *nameItem = new QTableWidgetItem(name);
                       nameItem->setData(Qt::UserRole, name);
                       nameItem->setToolTip(item.value(QStringLiteral("id")).toString());
                       m_networkTable->setItem(index, 0, nameItem);

                       m_networkTable->setItem(
                           index, 1, makeCell(item.value(QStringLiteral("driver")).toString()));
                       m_networkTable->setItem(
                           index, 2, makeCell(item.value(QStringLiteral("scope")).toString()));
                       m_networkTable->setItem(index, 3,
                                               makeCell(item.value(QStringLiteral("ipv6")).toBool()
                                                            ? QStringLiteral("是")
                                                            : QStringLiteral("否")));
                       m_networkTable->setItem(index, 4,
                                               makeCell(item.value(QStringLiteral("internal")).toBool()
                                                            ? QStringLiteral("是")
                                                            : QStringLiteral("否")));
                   }
                   m_networkTable->setUpdatesEnabled(true);

                   if (items.isEmpty()) {
                       m_networkEmpty->setText(
                           QStringLiteral("暂无自定义网络，点击右上角「新建网络」创建。"));
                       m_networkStack->setCurrentWidget(m_networkEmpty);
                   } else {
                       m_networkStack->setCurrentWidget(m_networkTable);
                   }
                   if (manual) {
                       showToast(QStringLiteral("已刷新网络列表"), QStringLiteral("success"));
                   }
               });
}


