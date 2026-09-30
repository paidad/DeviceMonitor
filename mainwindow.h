#pragma once

#include "domain/models.h"
//测试git分支管理用的
#include <QHash>
#include <QMainWindow>
#include <QPointF>

class QChart; class QComboBox; class QLabel; class QLineSeries;
class QListWidget; class QPlainTextEdit; class QPushButton; class QStackedWidget;
class QTableView; class QTableWidget; class QThread; class QToolButton;

namespace dm { class AcquisitionWorker; class AlarmEngine; class DatabaseManager; class DateTimePicker; class LatestDataModel; }

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void handleSamples(const QList<dm::TelemetrySample> &samples);
    void handleAlarm(dm::AlarmEvent event);
    void handleDeviceState(int deviceId, dm::DeviceState state);
    void appendLog(const QString &level, const QString &message);
    void flushSamples();
    void refreshChart();
    void refreshHistory(int page = 0);
    void exportHistoryCsv();
    void addDevice();
    void editDevice(int row, int column);
    void removeSelectedDevice();
    void toggleSelectedDevice();
    void acknowledgeSelectedAlarm();
    void applyTheme(bool darkTheme);

private:
    QWidget *createDashboardPage();
    QWidget *createDevicesPage();
    QWidget *createRealtimePage();
    QWidget *createHistoryPage();
    QWidget *createAlarmsPage();
    QWidget *createLogsPage();
    QWidget *createSimulatorPage();
    QWidget *createMetricCard(const QString &title, QLabel **valueLabel, const QString &accent);
    void setupUi();
    void setupServices();
    void refreshDevices();
    void refreshAlarms();
    void refreshDashboard();
    void reloadAcquisitionConfiguration();
    bool showDeviceDialog(dm::DeviceConfig &device, const QString &title, int originalDeviceId = 0);
    QString themeStyleSheet(bool darkTheme) const;
    int selectedHistoryDeviceId() const;

    dm::DatabaseManager *m_database = nullptr;
    dm::AlarmEngine *m_alarmEngine = nullptr;
    dm::LatestDataModel *m_latestModel = nullptr;
    dm::AcquisitionWorker *m_worker = nullptr;
    QThread *m_workerThread = nullptr;
    QListWidget *m_navigation = nullptr;
    QStackedWidget *m_pages = nullptr;
    QTableWidget *m_devicesTable = nullptr;
    QTableView *m_realtimeTable = nullptr;
    QTableWidget *m_historyTable = nullptr;
    QTableWidget *m_alarmTable = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QComboBox *m_chartPointCombo = nullptr;
    QComboBox *m_historyDeviceCombo = nullptr;
    dm::DateTimePicker *m_historyFrom = nullptr;
    dm::DateTimePicker *m_historyTo = nullptr;
    QLabel *m_historyPageLabel = nullptr;
    QPushButton *m_historyPrevious = nullptr;
    QPushButton *m_historyNext = nullptr;
    QLineSeries *m_series = nullptr;
    QChart *m_chart = nullptr;
    QLabel *m_totalDevicesLabel = nullptr;
    QLabel *m_onlineDevicesLabel = nullptr;
    QLabel *m_samplesLabel = nullptr;
    QLabel *m_activeAlarmsLabel = nullptr;
    QLabel *m_simulatorStatsLabel = nullptr;
    QToolButton *m_lightThemeButton = nullptr;
    QToolButton *m_darkThemeButton = nullptr;
    QList<dm::TelemetrySample> m_pendingSamples;
    QHash<int, QList<QPointF>> m_chartBuffers;
    QHash<int, dm::DeviceState> m_deviceStates;
    qint64 m_totalSamples = 0;
    int m_historyPage = 0;
    bool m_darkTheme = true;
    static constexpr int HistoryPageSize = 100;
};
