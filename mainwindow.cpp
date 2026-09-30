#include "mainwindow.h"

#include "acquisition/acquisitionworker.h"
#include "alarm/alarmengine.h"
#include "storage/databasemanager.h"
#include "ui/datetimepicker.h"
#include "ui/latestdatamodel.h"
#include "logging/filelogger.h"

#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTimeAxis>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLineSeries>
#include <QListWidget>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTableWidget>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QValueAxis>
#include <QVBoxLayout>

namespace {

constexpr int AlarmStateRole = Qt::UserRole + 1;

class AlarmStatusDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        const QVariant stateData = index.data(AlarmStateRole);
        if (!stateData.isValid()) return;

        const auto state = static_cast<dm::AlarmState>(stateData.toInt());
        const bool darkTheme = option->palette.color(QPalette::Base).lightness() < 128;
        QColor textColor;
        QColor selectedTextColor;
        if (state == dm::AlarmState::Active) {
            textColor = darkTheme ? QColor(QStringLiteral("#f87171")) : QColor(QStringLiteral("#dc2626"));
            selectedTextColor = QColor(QStringLiteral("#fecaca"));
        } else if (state == dm::AlarmState::Acknowledged) {
            textColor = darkTheme ? QColor(QStringLiteral("#4ade80")) : QColor(QStringLiteral("#15803d"));
            selectedTextColor = QColor(QStringLiteral("#bbf7d0"));
        } else {
            return;
        }

        option->palette.setColor(QPalette::Text, textColor);
        option->palette.setColor(QPalette::HighlightedText, selectedTextColor);
    }
};

QString stateText(dm::DeviceState state)
{
    switch (state) {
    case dm::DeviceState::Offline: return QStringLiteral("离线");
    case dm::DeviceState::Connecting: return QStringLiteral("连接中");
    case dm::DeviceState::Online: return QStringLiteral("在线");
    case dm::DeviceState::Error: return QStringLiteral("异常");
    }
    return {};
}
QString alarmStateText(dm::AlarmState state)
{
    switch (state) {
    case dm::AlarmState::Active: return QStringLiteral("活动");
    case dm::AlarmState::Recovered: return QStringLiteral("已恢复");
    case dm::AlarmState::Acknowledged: return QStringLiteral("已确认");
    }
    return {};
}
QTableWidgetItem *item(const QString &text)
{
    auto *result = new QTableWidgetItem(text);
    result->setFlags(result->flags() & ~Qt::ItemIsEditable);
    return result;
}
QLabel *pageTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("font-size:26px;font-weight:700;"));
    return label;
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setupUi();
    setupServices();
}

MainWindow::~MainWindow()
{
    if (m_workerThread && m_workerThread->isRunning()) {
        QMetaObject::invokeMethod(m_worker, "stop", Qt::BlockingQueuedConnection);
        m_workerThread->quit();
        m_workerThread->wait(3000);
    }
}

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("DeviceMonitor · 设备监控与数据管理平台"));
    resize(1440, 900);
    setMinimumSize(1100, 700);

    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("appRoot"));
    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    auto *side = new QWidget(root);
    side->setObjectName(QStringLiteral("sidebar"));
    side->setFixedWidth(220);
    auto *sideLayout = new QVBoxLayout(side);
    auto *brand = new QLabel(QStringLiteral("◉  DEVICE MONITOR"));
    brand->setObjectName(QStringLiteral("brandLabel"));
    sideLayout->addWidget(brand);
    m_navigation = new QListWidget;
    m_navigation->addItems({QStringLiteral("总览仪表盘"), QStringLiteral("设备管理"), QStringLiteral("实时监控"),
                            QStringLiteral("历史数据"), QStringLiteral("告警中心"), QStringLiteral("运行日志"),
                            QStringLiteral("设备模拟器")});
    sideLayout->addWidget(m_navigation, 1);
    auto *themePanel = new QWidget(side);
    themePanel->setObjectName(QStringLiteral("themePanel"));
    auto *themeLayout = new QHBoxLayout(themePanel);
    themeLayout->setContentsMargins(14, 8, 14, 8);
    themeLayout->setSpacing(8);
    m_lightThemeButton = new QToolButton(themePanel);
    m_lightThemeButton->setObjectName(QStringLiteral("themeLightButton"));
    m_lightThemeButton->setText(QStringLiteral("☀"));
    m_lightThemeButton->setToolTip(QStringLiteral("切换为浅色主题"));
    m_lightThemeButton->setAccessibleName(QStringLiteral("浅色主题"));
    m_lightThemeButton->setFixedHeight(40);
    m_darkThemeButton = new QToolButton(themePanel);
    m_darkThemeButton->setObjectName(QStringLiteral("themeDarkButton"));
    m_darkThemeButton->setText(QStringLiteral("☾"));
    m_darkThemeButton->setToolTip(QStringLiteral("切换为深色主题"));
    m_darkThemeButton->setAccessibleName(QStringLiteral("深色主题"));
    m_darkThemeButton->setFixedHeight(40);
    themeLayout->addWidget(m_lightThemeButton);
    themeLayout->addWidget(m_darkThemeButton);
    sideLayout->addWidget(themePanel);
    auto *version = new QLabel(QStringLiteral("Qt 6 · C++17\n本地工业监控平台"));
    version->setObjectName(QStringLiteral("versionLabel"));
    sideLayout->addWidget(version);

    m_pages = new QStackedWidget;
    m_pages->addWidget(createDashboardPage());
    m_pages->addWidget(createDevicesPage());
    m_pages->addWidget(createRealtimePage());
    m_pages->addWidget(createHistoryPage());
    m_pages->addWidget(createAlarmsPage());
    m_pages->addWidget(createLogsPage());
    m_pages->addWidget(createSimulatorPage());
    connect(m_navigation, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_navigation->setCurrentRow(0);
    rootLayout->addWidget(side);
    rootLayout->addWidget(m_pages, 1);
    setCentralWidget(root);
    connect(m_lightThemeButton, &QToolButton::clicked, this, [this] { applyTheme(false); });
    connect(m_darkThemeButton, &QToolButton::clicked, this, [this] { applyTheme(true); });
    applyTheme(QSettings().value(QStringLiteral("appearance/darkTheme"), true).toBool());
    statusBar()->showMessage(QStringLiteral("正在初始化…"));
}

QString MainWindow::themeStyleSheet(bool darkTheme) const
{
    const QString common = QStringLiteral(R"(
      QWidget{font-family:"Microsoft YaHei UI";font-size:13px}
      QLabel{background:transparent}
      QListWidget{border:0;padding:12px 8px;font-size:15px}
      QListWidget::item{padding:13px 16px;margin:3px 0;border-radius:7px}
      QListWidget::item:selected{background:#2563eb;color:#ffffff}
      QTableView,QTableWidget{selection-background-color:#2563eb;selection-color:#ffffff;outline:0}
      QTableView::item:selected,QTableWidget::item:selected{background:#2563eb;color:#ffffff;border-top:1px solid #60a5fa;border-bottom:1px solid #60a5fa}
      QTableView::item:selected:!active,QTableWidget::item:selected:!active{background:#1d4ed8;color:#ffffff}
      QHeaderView::section:checked{background:#2563eb;color:#ffffff}
      QPushButton{background:#2563eb;color:#ffffff;border:0;border-radius:6px;padding:8px 16px}
      QPushButton:hover{background:#3b82f6}
      QPushButton:disabled{background:#cbd5e1;color:#64748b}
      QSpinBox{padding-right:44px;min-height:30px}
      QSpinBox::up-button{subcontrol-origin:border;subcontrol-position:top right;width:38px;border-bottom:1px solid;border-top-right-radius:5px}
      QSpinBox::down-button{subcontrol-origin:border;subcontrol-position:bottom right;width:38px;border-top:1px solid;border-bottom-right-radius:5px}
      QSpinBox::up-button:hover,QSpinBox::down-button:hover{background:#2563eb}
      QSpinBox::up-button:pressed,QSpinBox::down-button:pressed{background:#1d4ed8}
      QSpinBox::up-arrow,QSpinBox::down-arrow{width:14px;height:9px}
      QSpinBox::up-arrow:hover,QSpinBox::up-arrow:pressed{image:url(:/icons/spin-up-light.svg)}
      QSpinBox::down-arrow:hover,QSpinBox::down-arrow:pressed{image:url(:/icons/spin-down-light.svg)}
      QComboBox{padding-right:30px}
      QComboBox::drop-down{subcontrol-origin:padding;subcontrol-position:top right;width:25px;border-left-width:1px;border-left-style:solid}
      QComboBox::down-arrow{width:9px;height:7px}
      QWidget#dateTimePicker QLineEdit{border-top-right-radius:0;border-bottom-right-radius:0}
      QToolButton#dateTimeDropButton{font-family:"Segoe UI Symbol";font-size:12px;padding:0;margin:0;border-left:0;border-top-right-radius:5px;border-bottom-right-radius:5px}
      QLabel#brandLabel{font-size:17px;font-weight:700;color:#3b82f6;padding:22px 12px}
      QLabel#versionLabel{padding:10px 16px 16px 16px}
      QWidget#metricCard{border-radius:8px}
      QLabel#metricCaption{font-size:13px}
      QLabel#metricValue{font-size:30px;font-weight:700}
      QLabel#infoPanel{padding:20px;border-radius:8px;font-size:15px}
      QLabel#mutedLabel{padding:12px}
      QWidget#themePanel{border-radius:8px}
      QToolButton#themeLightButton,QToolButton#themeDarkButton{font-family:"Segoe UI Symbol";font-size:21px;border:1px solid;border-radius:7px;padding:4px;min-width:72px}
      QToolButton#themeLightButton:hover,QToolButton#themeDarkButton:hover{border-color:#3b82f6}
      QToolButton#themeLightButton[themeActive="true"],QToolButton#themeDarkButton[themeActive="true"]{background:#2563eb;color:#ffffff;border-color:#2563eb}
      QCalendarWidget QComboBox#calendarYearCombo{border-radius:4px;padding:4px 8px}
      QCalendarWidget QComboBox#calendarYearCombo:hover{border-color:#60a5fa}
    )");

    if (darkTheme) {
        return QStringLiteral(R"(
          QMainWindow,QWidget{background:#0f172a;color:#e2e8f0}
          QWidget#sidebar,QListWidget{background:#111827}
          QListWidget::item:hover{background:#1e293b}
          QTableView,QTableWidget,QPlainTextEdit{background:#111827;alternate-background-color:#172033;border:1px solid #273449;border-radius:7px;gridline-color:#273449}
          QTableView::item:hover,QTableWidget::item:hover{background:#23334d}
          QHeaderView::section{background:#1e293b;color:#cbd5e1;border:0;border-right:1px solid #334155;padding:8px}
          QLineEdit,QSpinBox,QComboBox{background:#1e293b;color:#e2e8f0;border:1px solid #334155;border-radius:5px;padding:6px}
          QComboBox::drop-down{border-left-color:#334155}
          QComboBox QAbstractItemView{background:#1e293b;color:#e2e8f0;selection-background-color:#2563eb;selection-color:#ffffff;outline:0}
          QSpinBox::up-button,QSpinBox::down-button{background:#334155;border-color:#475569}
          QSpinBox::up-arrow{image:url(:/icons/spin-up-light.svg)}
          QSpinBox::down-arrow{image:url(:/icons/spin-down-light.svg)}
          QCalendarWidget QComboBox#calendarYearCombo{background:#1e293b;color:#ffffff;border:1px solid #475569}
          QCalendarWidget QTableView{background:#111827;color:#e2e8f0;border:0}
          QToolButton#dateTimeDropButton{background:#1e293b;border:1px solid #334155}
          QToolButton#dateTimeDropButton:hover,QToolButton#dateTimeDropButton:pressed{background:#1e293b}
          QFrame#dateTimePickerPopup{background:#0f172a;border:1px solid #475569;border-radius:7px}
          QStatusBar{background:#111827;color:#e2e8f0}
          QLabel#versionLabel,QLabel#metricCaption,QLabel#mutedLabel{color:#94a3b8}
          QWidget#metricCard,QLabel#infoPanel{background:#172033;color:#cbd5e1}
          QLabel#metricValue{color:#ffffff}
          QWidget#themePanel{background:#172033}
          QToolButton#themeLightButton,QToolButton#themeDarkButton{background:#111827;color:#cbd5e1;border-color:#334155}
        )") + common;
    }

    return QStringLiteral(R"(
      QMainWindow,QWidget{background:#f4f7fb;color:#172033}
      QWidget#sidebar,QListWidget{background:#ffffff}
      QListWidget::item:hover{background:#e8efff}
      QTableView,QTableWidget,QPlainTextEdit{background:#ffffff;alternate-background-color:#f1f5f9;border:1px solid #cbd5e1;border-radius:7px;gridline-color:#dbe3ee}
      QTableView::item:hover,QTableWidget::item:hover{background:#dbeafe}
      QHeaderView::section{background:#e9eef5;color:#334155;border:0;border-right:1px solid #cbd5e1;padding:8px}
          QLineEdit,QSpinBox,QComboBox{background:#ffffff;color:#172033;border:1px solid #b8c4d4;border-radius:5px;padding:6px}
          QComboBox::drop-down{border-left-color:#b8c4d4}
      QComboBox QAbstractItemView{background:#ffffff;color:#172033;selection-background-color:#2563eb;selection-color:#ffffff;outline:0}
      QSpinBox::up-button,QSpinBox::down-button{background:#e2e8f0;border-color:#b8c4d4}
      QSpinBox::up-arrow{image:url(:/icons/spin-up-dark.svg)}
      QSpinBox::down-arrow{image:url(:/icons/spin-down-dark.svg)}
      QCalendarWidget QComboBox#calendarYearCombo{background:#ffffff;color:#172033;border:1px solid #b8c4d4}
      QCalendarWidget QTableView{background:#ffffff;color:#172033;border:0}
      QToolButton#dateTimeDropButton{background:#ffffff;border:1px solid #b8c4d4}
      QToolButton#dateTimeDropButton:hover,QToolButton#dateTimeDropButton:pressed{background:#ffffff}
      QFrame#dateTimePickerPopup{background:#ffffff;border:1px solid #b8c4d4;border-radius:7px}
      QStatusBar{background:#ffffff;color:#334155;border-top:1px solid #dbe3ee}
      QLabel#versionLabel,QLabel#metricCaption,QLabel#mutedLabel{color:#64748b}
      QWidget#metricCard,QLabel#infoPanel{background:#ffffff;color:#334155}
      QLabel#metricValue{color:#0f172a}
      QWidget#themePanel{background:#f1f5f9}
      QToolButton#themeLightButton,QToolButton#themeDarkButton{background:#ffffff;color:#475569;border-color:#cbd5e1}
    )") + common;
}

void MainWindow::applyTheme(bool darkTheme)
{
    m_darkTheme = darkTheme;
    setStyleSheet(themeStyleSheet(darkTheme));
    if (m_chart)
        m_chart->setTheme(darkTheme ? QChart::ChartThemeDark : QChart::ChartThemeLight);

    const auto updateButton = [](QToolButton *button, bool active) {
        if (!button) return;
        button->setProperty("themeActive", active);
        button->style()->unpolish(button);
        button->style()->polish(button);
    };
    updateButton(m_lightThemeButton, !darkTheme);
    updateButton(m_darkThemeButton, darkTheme);
    QSettings().setValue(QStringLiteral("appearance/darkTheme"), darkTheme);
}

QWidget *MainWindow::createMetricCard(const QString &title, QLabel **valueLabel, const QString &accent)
{
    auto *card = new QWidget;
    card->setObjectName(QStringLiteral("metricCard"));
    card->setStyleSheet(QStringLiteral("border-left:4px solid %1").arg(accent));
    auto *layout = new QVBoxLayout(card);
    auto *caption = new QLabel(title);
    caption->setObjectName(QStringLiteral("metricCaption"));
    *valueLabel = new QLabel(QStringLiteral("0"));
    (*valueLabel)->setObjectName(QStringLiteral("metricValue"));
    layout->addWidget(caption);
    layout->addWidget(*valueLabel);
    return card;
}

QWidget *MainWindow::createDashboardPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(20);
    layout->addWidget(pageTitle(QStringLiteral("运行总览")));
    auto *cards = new QHBoxLayout;
    cards->addWidget(createMetricCard(QStringLiteral("设备总数"), &m_totalDevicesLabel, QStringLiteral("#3b82f6")));
    cards->addWidget(createMetricCard(QStringLiteral("在线设备"), &m_onlineDevicesLabel, QStringLiteral("#22c55e")));
    cards->addWidget(createMetricCard(QStringLiteral("本次采样"), &m_samplesLabel, QStringLiteral("#8b5cf6")));
    cards->addWidget(createMetricCard(QStringLiteral("活动告警"), &m_activeAlarmsLabel, QStringLiteral("#ef4444")));
    layout->addLayout(cards);
    auto *hint = new QLabel(QStringLiteral("系统以 1 秒周期采集 10 台模拟设备的 50 个测点。可在“设备模拟器”页面触发断线与越限场景。"));
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("infoPanel"));
    layout->addWidget(hint);
    layout->addStretch();
    return page;
}

QWidget *MainWindow::createDevicesPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 28, 28, 28);
    auto *bar = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("新增设备"));
    auto *toggle = new QPushButton(QStringLiteral("启用 / 停用"));
    auto *remove = new QPushButton(QStringLiteral("删除"));
    bar->addWidget(pageTitle(QStringLiteral("设备管理")));
    bar->addStretch(); bar->addWidget(add); bar->addWidget(toggle); bar->addWidget(remove);
    layout->addLayout(bar);
    m_devicesTable = new QTableWidget;
    m_devicesTable->setColumnCount(8);
    m_devicesTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("设备名称"), QStringLiteral("主机地址"), QStringLiteral("端口"), QStringLiteral("站号"), QStringLiteral("周期/ms"), QStringLiteral("类型"), QStringLiteral("状态")});
    m_devicesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_devicesTable->horizontalHeader()->setHighlightSections(false);
    m_devicesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_devicesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_devicesTable->setAlternatingRowColors(true);
    m_devicesTable->setToolTip(QStringLiteral("双击任意一行可修改设备信息"));
    layout->addWidget(m_devicesTable);
    connect(add, &QPushButton::clicked, this, &MainWindow::addDevice);
    connect(remove, &QPushButton::clicked, this, &MainWindow::removeSelectedDevice);
    connect(toggle, &QPushButton::clicked, this, &MainWindow::toggleSelectedDevice);
    connect(m_devicesTable, &QTableWidget::cellDoubleClicked, this, &MainWindow::editDevice);
    return page;
}

QWidget *MainWindow::createRealtimePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 28, 28, 28);
    auto *bar = new QHBoxLayout;
    m_chartPointCombo = new QComboBox;
    bar->addWidget(pageTitle(QStringLiteral("实时监控")));
    bar->addStretch(); bar->addWidget(new QLabel(QStringLiteral("趋势测点："))); bar->addWidget(m_chartPointCombo);
    layout->addLayout(bar);
    auto *split = new QSplitter(Qt::Vertical);
    m_realtimeTable = new QTableView;
    m_realtimeTable->setAlternatingRowColors(true);
    m_realtimeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_realtimeTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_realtimeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_realtimeTable->horizontalHeader()->setHighlightSections(false);
    split->addWidget(m_realtimeTable);
    m_series = new QLineSeries;
    m_chart = new QChart;
    m_chart->addSeries(m_series);
    m_chart->setTitle(QStringLiteral("实时趋势（最近 60 个采样点）"));
    m_chart->setTheme(QChart::ChartThemeDark);
    m_chart->legend()->hide();
    auto *axisX = new QDateTimeAxis;
    axisX->setFormat(QStringLiteral("HH:mm:ss"));
    m_chart->addAxis(axisX, Qt::AlignBottom); m_series->attachAxis(axisX);
    auto *axisY = new QValueAxis;
    axisY->setRange(0, 100);
    m_chart->addAxis(axisY, Qt::AlignLeft); m_series->attachAxis(axisY);
    auto *chartView = new QChartView(m_chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    split->addWidget(chartView);
    split->setSizes({380, 360});
    layout->addWidget(split);
    return page;
}

QWidget *MainWindow::createHistoryPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->addWidget(pageTitle(QStringLiteral("历史数据")));
    auto *filters = new QHBoxLayout;
    m_historyDeviceCombo = new QComboBox;
    m_historyFrom = new dm::DateTimePicker(QDateTime::currentDateTime().addDays(-1), false);
    m_historyTo = new dm::DateTimePicker(QDateTime::currentDateTime().addDays(1), true);
    auto *query = new QPushButton(QStringLiteral("查询"));
    auto *exportButton = new QPushButton(QStringLiteral("导出 CSV"));

    auto *deviceFilter = new QHBoxLayout;
    deviceFilter->setSpacing(8);
    deviceFilter->addWidget(new QLabel(QStringLiteral("设备")));
    deviceFilter->addWidget(m_historyDeviceCombo);
    auto *fromFilter = new QHBoxLayout;
    fromFilter->setSpacing(8);
    fromFilter->addWidget(new QLabel(QStringLiteral("开始时间")));
    fromFilter->addWidget(m_historyFrom);
    auto *toFilter = new QHBoxLayout;
    toFilter->setSpacing(8);
    toFilter->addWidget(new QLabel(QStringLiteral("结束时间")));
    toFilter->addWidget(m_historyTo);
    auto *filterActions = new QHBoxLayout;
    filterActions->setSpacing(8);
    filterActions->addWidget(query);
    filterActions->addWidget(exportButton);

    filters->setSpacing(20);
    filters->addLayout(deviceFilter);
    filters->addLayout(fromFilter);
    filters->addLayout(toFilter);
    filters->addLayout(filterActions);
    layout->addLayout(filters);
    m_historyTable = new QTableWidget;
    m_historyTable->setColumnCount(6);
    m_historyTable->setHorizontalHeaderLabels({QStringLiteral("设备"), QStringLiteral("测点"), QStringLiteral("数值"), QStringLiteral("单位"), QStringLiteral("质量"), QStringLiteral("时间")});
    m_historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_historyTable->horizontalHeader()->setHighlightSections(false);
    m_historyTable->setAlternatingRowColors(true);
    m_historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyTable->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_historyTable);
    auto *pager = new QHBoxLayout;
    m_historyPrevious = new QPushButton(QStringLiteral("上一页"));
    m_historyNext = new QPushButton(QStringLiteral("下一页"));
    m_historyPageLabel = new QLabel(QStringLiteral("第 1 页"));
    pager->addStretch(); pager->addWidget(m_historyPrevious); pager->addWidget(m_historyPageLabel); pager->addWidget(m_historyNext);
    layout->addLayout(pager);
    connect(query, &QPushButton::clicked, this, [this] { refreshHistory(0); });
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportHistoryCsv);
    connect(m_historyPrevious, &QPushButton::clicked, this, [this] { refreshHistory(m_historyPage - 1); });
    connect(m_historyNext, &QPushButton::clicked, this, [this] { refreshHistory(m_historyPage + 1); });
    return page;
}

QWidget *MainWindow::createAlarmsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 28, 28, 28);
    auto *bar = new QHBoxLayout;
    auto *ack = new QPushButton(QStringLiteral("确认选中告警"));
    bar->addWidget(pageTitle(QStringLiteral("告警中心"))); bar->addStretch(); bar->addWidget(ack);
    layout->addLayout(bar);
    m_alarmTable = new QTableWidget;
    m_alarmTable->setColumnCount(8);
    m_alarmTable->setHorizontalHeaderLabels({QStringLiteral("事件ID"), QStringLiteral("规则ID"), QStringLiteral("设备"), QStringLiteral("测点"), QStringLiteral("数值"), QStringLiteral("状态"), QStringLiteral("触发时间"), QStringLiteral("说明")});
    m_alarmTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_alarmTable->horizontalHeader()->setHighlightSections(false);
    m_alarmTable->setItemDelegateForColumn(5, new AlarmStatusDelegate(m_alarmTable));
    m_alarmTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_alarmTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_alarmTable->setAlternatingRowColors(true);
    layout->addWidget(m_alarmTable);
    connect(ack, &QPushButton::clicked, this, &MainWindow::acknowledgeSelectedAlarm);
    return page;
}

QWidget *MainWindow::createLogsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->addWidget(pageTitle(QStringLiteral("运行日志")));
    m_logView = new QPlainTextEdit;
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    layout->addWidget(m_logView);
    return page;
}

QWidget *MainWindow::createSimulatorPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->addWidget(pageTitle(QStringLiteral("设备模拟器")));
    auto *description = new QLabel(QStringLiteral("模拟器在后台线程中生成温度、压力、转速、电流和振动数据。使用下面的开关验证断线重连与告警状态机。"));
    description->setWordWrap(true);
    description->setObjectName(QStringLiteral("infoPanel"));
    layout->addWidget(description);
    auto *disconnect = new QCheckBox(QStringLiteral("模拟全部设备断线"));
    auto *overload = new QCheckBox(QStringLiteral("模拟设备 01 温度越限"));
    disconnect->setStyleSheet(QStringLiteral("font-size:16px;padding:12px"));
    overload->setStyleSheet(QStringLiteral("font-size:16px;padding:12px"));
    layout->addWidget(disconnect); layout->addWidget(overload);
    m_simulatorStatsLabel = new QLabel;
    m_simulatorStatsLabel->setObjectName(QStringLiteral("mutedLabel"));
    layout->addWidget(m_simulatorStatsLabel); layout->addStretch();
    connect(disconnect, &QCheckBox::toggled, this, [this](bool on) {
        if (m_worker) QMetaObject::invokeMethod(m_worker, "setDisconnected", Qt::QueuedConnection, Q_ARG(bool, on));
    });
    connect(overload, &QCheckBox::toggled, this, [this](bool on) {
        if (m_worker) QMetaObject::invokeMethod(m_worker, "setOverload", Qt::QueuedConnection, Q_ARG(bool, on));
    });
    return page;
}

void MainWindow::setupServices()
{
    m_database = new dm::DatabaseManager(this);
    m_alarmEngine = new dm::AlarmEngine(this);
    m_latestModel = new dm::LatestDataModel(this);
    m_realtimeTable->setModel(m_latestModel);
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    dm::FileLogger::initialize(dataDir + QStringLiteral("/logs"));
    if (!m_database->initialize(dataDir + QStringLiteral("/device-monitor.db"))) {
        QMessageBox::critical(this, QStringLiteral("数据库错误"), m_database->lastError());
        return;
    }
    connect(m_database, &dm::DatabaseManager::databaseError, this,
            [this](const QString &message) { appendLog(QStringLiteral("ERROR"), message); });
    m_alarmEngine->setRules(m_database->alarmRules());
    connect(m_alarmEngine, &dm::AlarmEngine::alarmChanged, this, &MainWindow::handleAlarm);
    refreshDevices();
    refreshAlarms();
    for (const auto &point : m_database->points())
        m_chartPointCombo->addItem(QStringLiteral("设备%1 · %2").arg(point.deviceId).arg(point.name), point.id);

    m_workerThread = new QThread(this);
    m_worker = new dm::AcquisitionWorker;
    m_worker->configure(m_database->devices(), m_database->points());
    m_worker->moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::started, m_worker, &dm::AcquisitionWorker::start);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &dm::AcquisitionWorker::samplesReady, this, &MainWindow::handleSamples, Qt::QueuedConnection);
    connect(m_worker, &dm::AcquisitionWorker::deviceStateChanged, this, &MainWindow::handleDeviceState, Qt::QueuedConnection);
    connect(m_worker, &dm::AcquisitionWorker::logMessage, this, &MainWindow::appendLog, Qt::QueuedConnection);
    m_workerThread->start();

    auto *flushTimer = new QTimer(this);
    flushTimer->setInterval(2000);
    connect(flushTimer, &QTimer::timeout, this, &MainWindow::flushSamples);
    flushTimer->start();
    auto *chartTimer = new QTimer(this);
    chartTimer->setInterval(500);
    connect(chartTimer, &QTimer::timeout, this, &MainWindow::refreshChart);
    chartTimer->start();
    statusBar()->showMessage(QStringLiteral("系统运行中 · 数据库：%1/device-monitor.db").arg(dataDir));
    appendLog(QStringLiteral("INFO"), QStringLiteral("应用初始化完成"));
    refreshDashboard();
}

void MainWindow::handleSamples(const QList<dm::TelemetrySample> &samples)
{
    m_latestModel->updateSamples(samples);
    m_pendingSamples.append(samples);
    m_totalSamples += samples.size();
    for (const auto &sample : samples) {
        m_alarmEngine->evaluate(sample);
        auto &buffer = m_chartBuffers[sample.pointId];
        buffer.append(QPointF(sample.timestamp.toMSecsSinceEpoch(), sample.value));
        while (buffer.size() > 60) buffer.removeFirst();
    }
    m_simulatorStatsLabel->setText(QStringLiteral("累计生成 %1 条采样，本批 %2 条；数据库每 2 秒批量提交。")
                                       .arg(m_totalSamples).arg(samples.size()));
    refreshDashboard();
}

void MainWindow::handleAlarm(dm::AlarmEvent event)
{
    m_database->saveAlarmEvent(event);
    appendLog(event.state == dm::AlarmState::Active ? QStringLiteral("WARN") : QStringLiteral("INFO"), event.message);
    refreshAlarms();
    refreshDashboard();
}

void MainWindow::handleDeviceState(int deviceId, dm::DeviceState state)
{
    m_deviceStates[deviceId] = state;
    refreshDevices();
    refreshDashboard();
}

void MainWindow::appendLog(const QString &level, const QString &message)
{
    dm::FileLogger::write(level, message);
    if (m_logView)
        m_logView->appendPlainText(QStringLiteral("[%1] [%2] %3")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), level, message));
}

void MainWindow::flushSamples()
{
    if (m_pendingSamples.isEmpty() || !m_database) return;
    QList<dm::TelemetrySample> batch;
    batch.swap(m_pendingSamples);
    if (!m_database->insertSamples(batch))
        appendLog(QStringLiteral("ERROR"), QStringLiteral("采样批量写入失败，采集服务继续运行"));
}

void MainWindow::refreshChart()
{
    if (!m_series || m_chartPointCombo->currentIndex() < 0) return;
    const auto points = m_chartBuffers.value(m_chartPointCombo->currentData().toInt());
    m_series->replace(points);
    if (points.isEmpty()) return;
    if (auto *axisX = qobject_cast<QDateTimeAxis *>(m_chart->axes(Qt::Horizontal).value(0)))
        axisX->setRange(QDateTime::fromMSecsSinceEpoch(points.first().x()),
                        QDateTime::fromMSecsSinceEpoch(points.last().x() + 1000));
    double minimum = points.first().y();
    double maximum = minimum;
    for (const auto &point : points) {
        minimum = qMin(minimum, point.y());
        maximum = qMax(maximum, point.y());
    }
    const double padding = qMax(1.0, (maximum - minimum) * 0.15);
    if (auto *axisY = qobject_cast<QValueAxis *>(m_chart->axes(Qt::Vertical).value(0)))
        axisY->setRange(minimum - padding, maximum + padding);
}

void MainWindow::refreshDevices()
{
    if (!m_database || !m_devicesTable) return;
    const auto devices = m_database->devices();
    m_devicesTable->setRowCount(devices.size());
    m_historyDeviceCombo->blockSignals(true);
    m_historyDeviceCombo->clear();
    m_historyDeviceCombo->addItem(QStringLiteral("全部设备"), 0);
    for (int row = 0; row < devices.size(); ++row) {
        const auto &device = devices.at(row);
        const QStringList values = {QString::number(device.id), device.name, device.host,
            QString::number(device.port), QString::number(device.unitId), QString::number(device.pollingIntervalMs),
            device.simulated ? QStringLiteral("模拟") : QStringLiteral("Modbus TCP"),
            device.enabled ? stateText(m_deviceStates.value(device.id, dm::DeviceState::Offline)) : QStringLiteral("已停用")};
        for (int column = 0; column < values.size(); ++column)
            m_devicesTable->setItem(row, column, item(values.at(column)));
        m_historyDeviceCombo->addItem(device.name, device.id);
    }
    m_historyDeviceCombo->blockSignals(false);
}

void MainWindow::refreshDashboard()
{
    if (!m_database) return;
    const auto devices = m_database->devices();
    int online = 0;
    for (const auto &device : devices)
        if (m_deviceStates.value(device.id) == dm::DeviceState::Online) ++online;
    m_totalDevicesLabel->setText(QString::number(devices.size()));
    m_onlineDevicesLabel->setText(QStringLiteral("%1 / %2").arg(online).arg(devices.size()));
    m_samplesLabel->setText(QString::number(m_totalSamples));
    m_activeAlarmsLabel->setText(QString::number(m_alarmEngine->activeAlarms().size()));
}

int MainWindow::selectedHistoryDeviceId() const
{
    return m_historyDeviceCombo ? m_historyDeviceCombo->currentData().toInt() : 0;
}

void MainWindow::refreshHistory(int page)
{
    if (!m_database) return;
    m_historyPage = qMax(0, page);
    const int total = m_database->sampleCount(selectedHistoryDeviceId(), m_historyFrom->dateTime(), m_historyTo->dateTime());
    const int pageCount = qMax(1, (total + HistoryPageSize - 1) / HistoryPageSize);
    if (m_historyPage >= pageCount) m_historyPage = pageCount - 1;
    const auto samples = m_database->querySamples(selectedHistoryDeviceId(), m_historyFrom->dateTime(),
                                                   m_historyTo->dateTime(), HistoryPageSize,
                                                   m_historyPage * HistoryPageSize);
    m_historyTable->setRowCount(samples.size());
    for (int row = 0; row < samples.size(); ++row) {
        const auto &sample = samples.at(row);
        const QStringList values = {sample.deviceName, sample.pointName, QString::number(sample.value, 'f', 2),
            sample.unit, dm::qualityText(sample.quality), sample.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))};
        for (int column = 0; column < values.size(); ++column)
            m_historyTable->setItem(row, column, item(values.at(column)));
    }
    m_historyPageLabel->setText(QStringLiteral("第 %1 / %2 页 · %3 条").arg(m_historyPage + 1).arg(pageCount).arg(total));
    m_historyPrevious->setEnabled(m_historyPage > 0);
    m_historyNext->setEnabled(m_historyPage + 1 < pageCount);
}

void MainWindow::exportHistoryCsv()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出历史数据"),
                                                       QStringLiteral("telemetry.csv"), QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;
    const int total = m_database->sampleCount(selectedHistoryDeviceId(), m_historyFrom->dateTime(), m_historyTo->dateTime());
    const auto samples = m_database->querySamples(selectedHistoryDeviceId(), m_historyFrom->dateTime(),
                                                   m_historyTo->dateTime(), qMax(total, 1), 0);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QChar(0xFEFF) << QStringLiteral("设备,测点,数值,单位,质量,时间\n");
    for (const auto &sample : samples)
        out << '"' << sample.deviceName << QStringLiteral("\",\"") << sample.pointName << QStringLiteral("\",")
            << sample.value << QStringLiteral(",\"") << sample.unit << QStringLiteral("\",\"")
            << dm::qualityText(sample.quality) << QStringLiteral("\",") << sample.timestamp.toString(Qt::ISODateWithMs) << '\n';
    statusBar()->showMessage(QStringLiteral("已导出 %1 条记录到 %2").arg(samples.size()).arg(path), 5000);
}

void MainWindow::refreshAlarms()
{
    if (!m_database || !m_alarmTable) return;
    const auto events = m_database->alarmEvents();
    m_alarmTable->setRowCount(events.size());
    for (int row = 0; row < events.size(); ++row) {
        const auto &event = events.at(row);
        const QStringList values = {QString::number(event.id), QString::number(event.ruleId), event.deviceName,
            event.pointName, QString::number(event.value, 'f', 2), alarmStateText(event.state),
            event.triggeredAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), event.message};
        for (int column = 0; column < values.size(); ++column)
            m_alarmTable->setItem(row, column, item(values.at(column)));
        m_alarmTable->item(row, 5)->setData(AlarmStateRole, static_cast<int>(event.state));
    }
}

void MainWindow::addDevice()
{
    dm::DeviceConfig device;
    device.name = QStringLiteral("新设备");
    if (!showDeviceDialog(device, QStringLiteral("新增设备"))) return;
    if (m_database->saveDevice(device)) {
        refreshDevices();
        reloadAcquisitionConfiguration();
        appendLog(QStringLiteral("INFO"), QStringLiteral("新增设备：%1").arg(device.name));
    }
}

bool MainWindow::showDeviceDialog(dm::DeviceConfig &device, const QString &title, int originalDeviceId)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(380);
    QFormLayout form(&dialog);
    QSpinBox deviceId;
    QLineEdit name(device.name);
    QLineEdit host(device.host);
    QSpinBox port, unit, polling;
    port.setButtonSymbols(QAbstractSpinBox::PlusMinus);
    unit.setButtonSymbols(QAbstractSpinBox::PlusMinus);
    polling.setButtonSymbols(QAbstractSpinBox::PlusMinus);
    port.setRange(1, 65535); port.setValue(device.port);
    unit.setRange(1, 247); unit.setValue(device.unitId);
    polling.setRange(200, 60000); polling.setValue(device.pollingIntervalMs);
    QCheckBox enabled(QStringLiteral("启用设备")); enabled.setChecked(device.enabled);
    QCheckBox simulated(QStringLiteral("使用内置模拟器")); simulated.setChecked(device.simulated);
    if (originalDeviceId > 0) {
        deviceId.setRange(1, 999999);
        deviceId.setValue(device.id);
        deviceId.setButtonSymbols(QAbstractSpinBox::PlusMinus);
        form.addRow(QStringLiteral("设备 ID"), &deviceId);
    }
    form.addRow(QStringLiteral("名称"), &name);
    form.addRow(QStringLiteral("主机地址"), &host);
    form.addRow(QStringLiteral("端口"), &port);
    form.addRow(QStringLiteral("站号"), &unit);
    form.addRow(QStringLiteral("采集周期/ms"), &polling);
    form.addRow(&enabled);
    form.addRow(&simulated);
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons.button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    while (dialog.exec() == QDialog::Accepted) {
        const QString deviceName = name.text().trimmed();
        const QString deviceHost = host.text().trimmed();
        if (deviceName.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("校验失败"), QStringLiteral("设备名称不能为空"));
            continue;
        }
        if (deviceHost.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("校验失败"), QStringLiteral("主机地址不能为空"));
            continue;
        }
        if (originalDeviceId > 0) {
            const int newDeviceId = deviceId.value();
            if (newDeviceId != originalDeviceId && m_database->deviceIdExists(newDeviceId)) {
                QMessageBox::warning(&dialog, QStringLiteral("ID 重复"),
                                     QStringLiteral("设备 ID %1 已存在，请重新输入其他 ID。").arg(newDeviceId));
                deviceId.setFocus();
                deviceId.selectAll();
                continue;
            }
            device.id = newDeviceId;
        }
        device.name = deviceName;
        device.host = deviceHost;
        device.port = port.value();
        device.unitId = unit.value();
        device.pollingIntervalMs = polling.value();
        device.enabled = enabled.isChecked();
        device.simulated = simulated.isChecked();
        return true;
    }
    return false;
}

void MainWindow::editDevice(int row, int column)
{
    Q_UNUSED(column)
    if (row < 0 || !m_devicesTable->item(row, 0)) return;
    const int deviceId = m_devicesTable->item(row, 0)->text().toInt();
    for (auto device : m_database->devices()) {
        if (device.id != deviceId) continue;
        const QString oldName = device.name;
        if (!showDeviceDialog(device, QStringLiteral("修改设备 · %1").arg(oldName), deviceId)) return;
        if (!m_database->updateDevice(deviceId, device)) {
            QMessageBox::warning(this, QStringLiteral("保存失败"), m_database->lastError());
            return;
        }
        if (device.id != deviceId) {
            m_deviceStates.remove(deviceId);
            m_chartBuffers.clear();
        }
        if (!device.enabled) m_deviceStates[device.id] = dm::DeviceState::Offline;
        refreshDevices();
        reloadAcquisitionConfiguration();
        for (int tableRow = 0; tableRow < m_devicesTable->rowCount(); ++tableRow) {
            if (m_devicesTable->item(tableRow, 0)->text().toInt() == deviceId) {
                m_devicesTable->selectRow(tableRow);
                break;
            }
        }
        appendLog(QStringLiteral("INFO"), QStringLiteral("修改设备：%1 → %2").arg(oldName, device.name));
        statusBar()->showMessage(QStringLiteral("设备“%1”已更新").arg(device.name), 4000);
        return;
    }
}

void MainWindow::removeSelectedDevice()
{
    const int row = m_devicesTable->currentRow();
    if (row < 0) return;
    const int id = m_devicesTable->item(row, 0)->text().toInt();
    if (QMessageBox::question(this, QStringLiteral("删除设备"),
                              QStringLiteral("确定删除选中设备及其测点配置吗？")) != QMessageBox::Yes) return;
    if (m_database->removeDevice(id)) {
        refreshDevices();
        reloadAcquisitionConfiguration();
    }
}

void MainWindow::toggleSelectedDevice()
{
    const int row = m_devicesTable->currentRow();
    if (row < 0) return;
    const int id = m_devicesTable->item(row, 0)->text().toInt();
    for (auto device : m_database->devices()) {
        if (device.id == id) {
            device.enabled = !device.enabled;
            m_database->saveDevice(device);
            break;
        }
    }
    refreshDevices();
    reloadAcquisitionConfiguration();
}

void MainWindow::reloadAcquisitionConfiguration()
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection,
                              Q_ARG(QList<dm::DeviceConfig>, m_database->devices()),
                              Q_ARG(QList<dm::PointConfig>, m_database->points()));
    m_alarmEngine->setRules(m_database->alarmRules());
}

void MainWindow::acknowledgeSelectedAlarm()
{
    const int row = m_alarmTable->currentRow();
    if (row < 0) return;
    const QTableWidgetItem *statusItem = m_alarmTable->item(row, 5);
    if (!statusItem) return;

    const QVariant stateData = statusItem->data(AlarmStateRole);
    if (!stateData.isValid()) return;
    const auto state = static_cast<dm::AlarmState>(stateData.toInt());
    if (state == dm::AlarmState::Active) {
        QMessageBox::warning(this, QStringLiteral("无法确认告警"),
                             QStringLiteral("该设备为“活动”状态，请先恢复该设备状态。"));
        return;
    }

    const qint64 eventId = m_alarmTable->item(row, 0)->text().toLongLong();
    const int ruleId = m_alarmTable->item(row, 1)->text().toInt();
    m_database->acknowledgeAlarm(eventId);
    m_alarmEngine->acknowledge(ruleId);
    refreshAlarms();
    refreshDashboard();
}
