#pragma once

#include "domain/models.h"

#include <QObject>
#include <QHash>

class QTimer;

namespace dm {

class IDeviceClient;

class AcquisitionWorker final : public QObject
{
    Q_OBJECT
public:
    explicit AcquisitionWorker(QObject *parent = nullptr);

public slots:
    void configure(const QList<dm::DeviceConfig> &devices, const QList<dm::PointConfig> &points);
    void start();
    void stop();
    void setDisconnected(bool disconnected);
    void setOverload(bool overload);

signals:
    void samplesReady(const QList<dm::TelemetrySample> &samples);
    void deviceStateChanged(int deviceId, dm::DeviceState state);
    void logMessage(const QString &level, const QString &message);
    void finished();

private slots:
    void poll();

private:
    double simulatedValue(const PointConfig &point) const;
    void rebuildProtocolClients();

    QList<DeviceConfig> m_devices;
    QList<PointConfig> m_points;
    QTimer *m_timer = nullptr;
    bool m_disconnected = false;
    bool m_overload = false;
    qint64 m_tick = 0;
    QHash<int, IDeviceClient *> m_clients;
};

} // namespace dm
