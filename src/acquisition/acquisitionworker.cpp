#include "acquisition/acquisitionworker.h"
#include "protocol/ideviceclient.h"
#include "protocol/modbustcpclient.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QTimer>
#include <QtMath>
#include <utility>

namespace dm {

AcquisitionWorker::AcquisitionWorker(QObject *parent) : QObject(parent) {}

void AcquisitionWorker::configure(const QList<DeviceConfig> &devices, const QList<PointConfig> &points)
{
    m_devices = devices;
    m_points = points;
    if (!m_timer) return;

    rebuildProtocolClients();
    for (const auto &device : m_devices) {
        if (!device.enabled) {
            emit deviceStateChanged(device.id, DeviceState::Offline);
        } else if (device.simulated) {
            emit deviceStateChanged(device.id, m_disconnected ? DeviceState::Offline : DeviceState::Online);
        }
    }
}

void AcquisitionWorker::start()
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(1000);
        connect(m_timer, &QTimer::timeout, this, &AcquisitionWorker::poll);
    }
    for (const auto &device : m_devices) {
        if (device.enabled && device.simulated)
            emit deviceStateChanged(device.id, DeviceState::Online);
    }
    emit logMessage(QStringLiteral("INFO"), QStringLiteral("采集服务已启动：%1 台设备").arg(m_devices.size()));
    rebuildProtocolClients();
    m_timer->start();
    poll();
}

void AcquisitionWorker::stop()
{
    if (m_timer) m_timer->stop();
    for (auto *client : std::as_const(m_clients)) client->disconnectDevice();
    qDeleteAll(m_clients);
    m_clients.clear();
    for (const auto &device : m_devices)
        emit deviceStateChanged(device.id, DeviceState::Offline);
    emit logMessage(QStringLiteral("INFO"), QStringLiteral("采集服务已停止"));
    emit finished();
}

void AcquisitionWorker::setDisconnected(bool disconnected)
{
    if (m_disconnected == disconnected) return;
    m_disconnected = disconnected;
    for (const auto &device : m_devices) {
        if (device.enabled)
            emit deviceStateChanged(device.id, disconnected ? DeviceState::Offline : DeviceState::Connecting);
    }
    emit logMessage(disconnected ? QStringLiteral("WARN") : QStringLiteral("INFO"),
                    disconnected ? QStringLiteral("模拟网络已断开，将持续尝试重连")
                                 : QStringLiteral("网络恢复，设备正在重连"));
    if (!disconnected) {
        QTimer::singleShot(700, this, [this] {
            for (const auto &device : m_devices)
                if (device.enabled) emit deviceStateChanged(device.id, DeviceState::Online);
            emit logMessage(QStringLiteral("INFO"), QStringLiteral("全部模拟设备重连成功"));
        });
    }
}

void AcquisitionWorker::setOverload(bool overload)
{
    m_overload = overload;
    emit logMessage(QStringLiteral("INFO"), overload ? QStringLiteral("已启用越限数据场景")
                                                     : QStringLiteral("已恢复正常数据场景"));
}

double AcquisitionWorker::simulatedValue(const PointConfig &point) const
{
    const double wave = qSin((m_tick + point.deviceId * 3) / 12.0);
    const double noise = QRandomGenerator::global()->generateDouble() - 0.5;
    double value = 0;
    switch (point.registerAddress) {
    case 0: value = 42 + 10 * wave + 2 * noise; break;
    case 1: value = 0.65 + 0.18 * wave + 0.06 * noise; break;
    case 2: value = 1650 + 500 * wave + 80 * noise; break;
    case 3: value = 14 + 5 * wave + noise; break;
    default: value = 3.2 + 1.7 * wave + 0.3 * noise; break;
    }
    if (m_overload && point.deviceId == 1 && point.registerAddress == 0)
        value = 95 + 3 * noise;
    return value;
}

void AcquisitionWorker::poll()
{
    ++m_tick;
    if (m_disconnected) return;

    QList<TelemetrySample> batch;
    const QDateTime now = QDateTime::currentDateTime();
    for (const auto &device : m_devices) {
        if (!device.enabled) continue;
        if (!device.simulated) {
            if (auto *client = m_clients.value(device.id)) {
                QList<PointConfig> devicePoints;
                for (const auto &point : m_points) if (point.deviceId == device.id) devicePoints.append(point);
                client->readPoints(devicePoints);
            }
            continue;
        }
        for (const auto &point : m_points) {
            if (point.deviceId != device.id) continue;
            TelemetrySample sample;
            sample.deviceId = device.id; sample.pointId = point.id;
            sample.deviceName = device.name; sample.pointName = point.name; sample.unit = point.unit;
            sample.value = simulatedValue(point); sample.quality = DataQuality::Good; sample.timestamp = now;
            batch.append(sample);
        }
    }
    if (!batch.isEmpty()) emit samplesReady(batch);
}

void AcquisitionWorker::rebuildProtocolClients()
{
    for (auto *client : std::as_const(m_clients)) client->disconnectDevice();
    qDeleteAll(m_clients);
    m_clients.clear();
    for (const auto &device : m_devices) {
        if (!device.enabled || device.simulated) continue;
        auto *client = new ModbusTcpClient(this);
        connect(client, &IDeviceClient::stateChanged, this,
                [this, id = device.id](DeviceState state) { emit deviceStateChanged(id, state); });
        connect(client, &IDeviceClient::samplesReady, this, &AcquisitionWorker::samplesReady);
        connect(client, &IDeviceClient::errorOccurred, this,
                [this, name = device.name](const QString &error) {
                    emit logMessage(QStringLiteral("ERROR"), QStringLiteral("%1：%2").arg(name, error));
                });
        m_clients.insert(device.id, client);
        client->connectDevice(device);
    }
}

} // namespace dm
