#pragma once

#include "domain/models.h"

#include <QObject>

namespace dm {

class IDeviceClient : public QObject
{
    Q_OBJECT
public:
    explicit IDeviceClient(QObject *parent = nullptr) : QObject(parent) {}
    ~IDeviceClient() override = default;

    virtual void connectDevice(const DeviceConfig &config) = 0;
    virtual void disconnectDevice() = 0;
    virtual void readPoints(const QList<PointConfig> &points) = 0;

signals:
    void stateChanged(dm::DeviceState state);
    void samplesReady(const QList<dm::TelemetrySample> &samples);
    void errorOccurred(const QString &message);
};

} // namespace dm
