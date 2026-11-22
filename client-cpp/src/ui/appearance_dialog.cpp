#include "ui/appearance_dialog.h"

#include "ui/widgets.h"

#include <QColorDialog>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

struct AccentEntry {
    const char *hex;
    const char *name;
};

const AccentEntry kAccents[] = {
    {"#2f81f7", "飞牛蓝"},
    {"#23c9a4", "薄荷青"},
    {"#8b5cf6", "星云紫"},
    {"#f97316", "落日橙"},
    {"#ef4444", "珊瑚红"},
    {"#eab308", "琥珀金"},
};

const int kAccentCount = int(sizeof(kAccents) / sizeof(kAccents[0]));

QIcon colorDot(const QString &hex)
{
    QPixmap pm(32, 32);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    p.setBrush(QColor(hex));
    p.drawEllipse(QRectF(1, 1, 14, 14));
    p.end();
    return QIcon(pm);
}

} // namespace

AppearanceDialog::AppearanceDialog(Theme::Id id, bool light, const QString &accent, QWidget *parent)
    : QDialog(parent), m_id(id), m_light(light),
      m_accent(accent.isEmpty() ? QStringLiteral("#2f81f7") : accent)
{
    setWindowTitle(QStringLiteral("自定义外观"));
    setModal(true);
    setMinimumWidth(460);
    buildUi();
}

void AppearanceDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(14);

    root->addWidget(makeLabel(QStringLiteral("自定义外观"), QStringLiteral("h2"), this));
    root->addWidget(makeLabel(QStringLiteral("主题、颜色模式与强调色可以任意组合，改动会立即预览。"),
                              QStringLiteral("muted"), this));

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);

    grid->addWidget(makeLabel(QStringLiteral("主题"), QStringLiteral("muted"), this), 0, 0);
    m_themeBox = new QComboBox(this);
    const Theme::Id ids[] = {Theme::Id::Win7, Theme::Id::Win10, Theme::Id::Win11, Theme::Id::Glass};
    for (Theme::Id themeId : ids) {
        m_themeBox->addItem(Theme::label(themeId), int(themeId));
    }
    m_themeBox->setMinimumWidth(200);
    grid->addWidget(m_themeBox, 0, 1, 1, 2);

    grid->addWidget(makeLabel(QStringLiteral("颜色模式"), QStringLiteral("muted"), this), 1, 0);
    m_modeBox = new QComboBox(this);
    m_modeBox->addItem(QStringLiteral("深色"), QStringLiteral("dark"));
    m_modeBox->addItem(QStringLiteral("浅色"), QStringLiteral("light"));
    m_modeBox->setMinimumWidth(200);
    grid->addWidget(m_modeBox, 1, 1, 1, 2);

    grid->addWidget(makeLabel(QStringLiteral("强调色"), QStringLiteral("muted"), this), 2, 0);
    m_accentBox = new QComboBox(this);
    for (int i = 0; i < kAccentCount; ++i) {
        const QString hex = QString::fromLatin1(kAccents[i].hex);
        m_accentBox->addItem(colorDot(hex), QString::fromUtf8(kAccents[i].name), hex);
    }
    m_accentBox->setMinimumWidth(150);
    grid->addWidget(m_accentBox, 2, 1);

    m_colorButton = makeButton(QStringLiteral("自定义…"), QStringLiteral("ghost"), this);
    grid->addWidget(m_colorButton, 2, 2);

    auto *hexRow = new QHBoxLayout();
    hexRow->setContentsMargins(0, 0, 0, 0);
    hexRow->setSpacing(8);
    m_swatch = new QLabel(this);
    m_swatch->setFixedSize(18, 18);
    hexRow->addWidget(m_swatch);
    m_hexLabel = makeLabel(QString(), QStringLiteral("mono"), this);
    hexRow->addWidget(m_hexLabel, 1);
    grid->addLayout(hexRow, 3, 1, 1, 2);

    grid->setColumnStretch(1, 1);
    root->addLayout(grid);
    root->addStretch(1);

    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(10);

    auto *resetButton = makeButton(QStringLiteral("恢复默认"), QStringLiteral("ghost"), this);
    buttons->addWidget(resetButton);
    buttons->addStretch(1);

    auto *cancelButton = makeButton(QStringLiteral("取消"), QStringLiteral("ghost"), this);
    auto *applyButton = makeButton(QStringLiteral("应用"), QStringLiteral("primary"), this);
    buttons->addWidget(cancelButton);
    buttons->addWidget(applyButton);
    root->addLayout(buttons);

    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        m_loading = true;
        m_id = Theme::Id::Win11;
        m_light = false;
        m_accent = QStringLiteral("#2f81f7");
        m_themeBox->setCurrentIndex(m_themeBox->findData(int(m_id)));
        m_modeBox->setCurrentIndex(0);
        m_accentBox->setCurrentIndex(0);
        m_loading = false;
        syncAccentBox();
        updateAccentUi();
        emitPreview();
    });
    connect(m_colorButton, &QPushButton::clicked, this, &AppearanceDialog::pickColor);

    connect(m_themeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_loading || index < 0) {
            return;
        }
        m_id = Theme::Id(m_themeBox->itemData(index).toInt());
        emitPreview();
    });
    connect(m_modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_loading) {
            return;
        }
        m_light = m_modeBox->currentData().toString() == QLatin1String("light");
        emitPreview();
    });
    connect(m_accentBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_loading) {
            return;
        }
        const QString hex = m_accentBox->currentData().toString();
        if (hex.isEmpty() || hex.compare(m_accent, Qt::CaseInsensitive) == 0) {
            return;
        }
        m_accent = hex;
        updateAccentUi();
        emitPreview();
    });

    m_loading = true;
    m_themeBox->setCurrentIndex(qMax(0, m_themeBox->findData(int(m_id))));
    m_modeBox->setCurrentIndex(m_light ? 1 : 0);
    m_loading = false;
    syncAccentBox();
    updateAccentUi();
}

void AppearanceDialog::syncAccentBox()
{
    if (!m_accentBox) {
        return;
    }
    const bool wasLoading = m_loading;
    m_loading = true;
    int index = m_accentBox->findData(m_accent);
    if (index < 0) {
        m_accentBox->addItem(colorDot(m_accent),
                             QStringLiteral("自定义 %1").arg(m_accent.toUpper()), m_accent);
        index = m_accentBox->count() - 1;
    }
    m_accentBox->setCurrentIndex(index);
    m_loading = wasLoading;
}

void AppearanceDialog::pickColor()
{
    const QColor initial(m_accent);
    const QColor picked = QColorDialog::getColor(initial.isValid() ? initial : QColor(QStringLiteral("#2f81f7")),
                                                 this, QStringLiteral("选择强调色"));
    if (!picked.isValid()) {
        return;
    }
    m_accent = picked.name(QColor::HexRgb);
    syncAccentBox();
    updateAccentUi();
    emitPreview();
}

void AppearanceDialog::updateAccentUi()
{
    if (m_swatch) {
        m_swatch->setStyleSheet(
            QStringLiteral("background:%1; border:1px solid rgba(0,0,0,0.25); border-radius:9px;")
                .arg(m_accent));
    }
    if (m_hexLabel) {
        m_hexLabel->setText(m_accent.toUpper());
    }
}

void AppearanceDialog::emitPreview()
{
    emit previewChanged(m_id, m_light, m_accent);
}
