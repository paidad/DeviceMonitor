#pragma once

#include "domain/models.h"

#include <QDateTime>
#include <QObject>
#include <QSqlDatabase>

namespace dm {

class DatabaseManager final : public QObject
{
    Q_OBJECT
public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager() override;

    bool initialize(const QString &databasePath);
    QString lastError() const;

    QList<DeviceConfig> devices() const;
    QList<PointConfig> points() const;
    QList<AlarmRule> alarmRules() const;
    bool deviceIdExists(int deviceId) const;
    bool saveDevice(DeviceConfig &device);
    bool updateDevice(int originalDeviceId, const DeviceConfig &device);
    bool removeDevice(int deviceId);

    bool insertSamples(const QList<TelemetrySample> &samples);
    QList<TelemetrySample> querySamples(int deviceId, const QDateTime &from,
                                        const QDateTime &to, int limit, int offset) const;
    int sampleCount(int deviceId, const QDateTime &from, const QDateTime &to) const;

    bool saveAlarmEvent(AlarmEvent &event);
    QList<AlarmEvent> alarmEvents(int limit = 500) const;
    bool acknowledgeAlarm(qint64 eventId);

signals:
    void databaseError(const QString &message);

private:
    bool executeSchema();
    bool seedDemoData();
    void setError(const QString &message) const;

    QString m_connectionName;
    mutable QString m_lastError;
    QSqlDatabase m_db;
};

} // namespace dm
