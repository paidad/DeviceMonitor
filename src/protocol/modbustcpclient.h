#pragma once

#include "protocol/ideviceclient.h"

#ifdef DEVICE_MONITOR_HAS_SERIALBUS
#include <QModbusDataUnit>
class QModbusTcpClient;
#endif
class QTimer;

namespace dm {

class ModbusTcpClient final : public IDeviceClient
{
    Q_OBJECT
public:
    explicit ModbusTcpClient(QObject *parent = nullptr);
    ~ModbusTcpClient() override;

    void connectDevice(const DeviceConfig &config) override;
    void disconnectDevice() override;
    void readPoints(const QList<PointConfig> &points) override;

private:
    void scheduleReconnect();
    DeviceConfig m_config;
    QTimer *m_reconnectTimer = nullptr;
    int m_reconnectAttempt = 0;
    bool m_manualDisconnect = false;
#ifdef DEVICE_MONITOR_HAS_SERIALBUS
    QModbusTcpClient *m_client = nullptr;
#endif
};

} // namespace dm
