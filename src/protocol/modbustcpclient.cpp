#include "protocol/modbustcpclient.h"

#include <QTimer>
#include <QtGlobal>

#ifdef DEVICE_MONITOR_HAS_SERIALBUS
#include <QModbusDevice>
#include <QModbusReply>
#include <QModbusTcpClient>
#endif

namespace dm {

ModbusTcpClient::ModbusTcpClient(QObject *parent) : IDeviceClient(parent)
{
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this] { connectDevice(m_config); });
#ifdef DEVICE_MONITOR_HAS_SERIALBUS
    m_client = new QModbusTcpClient(this);
    connect(m_client, &QModbusClient::stateChanged, this, [this](QModbusDevice::State state) {
        if (state == QModbusDevice::ConnectedState) {
            m_reconnectAttempt = 0;
            emit stateChanged(DeviceState::Online);
        } else if (state == QModbusDevice::ConnectingState) {
            emit stateChanged(DeviceState::Connecting);
        } else if (state == QModbusDevice::UnconnectedState) {
            emit stateChanged(DeviceState::Offline);
            scheduleReconnect();
        }
    });
    connect(m_client, &QModbusClient::errorOccurred, this, [this](QModbusDevice::Error error) {
        if (error != QModbusDevice::NoError) emit errorOccurred(m_client->errorString());
    });
#endif
}

ModbusTcpClient::~ModbusTcpClient() = default;

void ModbusTcpClient::connectDevice(const DeviceConfig &config)
{
    m_config = config;
    m_manualDisconnect = false;
#ifdef DEVICE_MONITOR_HAS_SERIALBUS
    m_client->setConnectionParameter(QModbusDevice::NetworkAddressParameter, config.host);
    m_client->setConnectionParameter(QModbusDevice::NetworkPortParameter, config.port);
    m_client->setTimeout(1500);
    m_client->setNumberOfRetries(2);
    emit stateChanged(DeviceState::Connecting);
    if (!m_client->connectDevice()) emit errorOccurred(m_client->errorString());
#else
    emit stateChanged(DeviceState::Error);
    emit errorOccurred(QStringLiteral("当前 Qt 套件未安装 SerialBus，无法连接真实 Modbus 设备"));
#endif
}

void ModbusTcpClient::disconnectDevice()
{
    m_manualDisconnect = true;
    m_reconnectTimer->stop();
#ifdef DEVICE_MONITOR_HAS_SERIALBUS
    m_client->disconnectDevice();
#else
    emit stateChanged(DeviceState::Offline);
#endif
}

void ModbusTcpClient::scheduleReconnect()
{
#ifdef DEVICE_MONITOR_HAS_SERIALBUS
    if (m_manualDisconnect || !m_config.enabled || m_reconnectTimer->isActive()) return;
    const int delayMs = qMin(30000, 1000 * (1 << qMin(m_reconnectAttempt, 5)));
    ++m_reconnectAttempt;
    m_reconnectTimer->start(delayMs);
#endif
}

void ModbusTcpClient::readPoints(const QList<PointConfig> &points)
{
#ifdef DEVICE_MONITOR_HAS_SERIALBUS
    if (points.isEmpty() || m_client->state() != QModbusDevice::ConnectedState) return;
    int first = points.first().registerAddress;
    int last = first;
    for (const auto &point : points) { first = qMin(first, point.registerAddress); last = qMax(last, point.registerAddress); }
    QModbusDataUnit request(QModbusDataUnit::HoldingRegisters, first, last - first + 1);
    if (auto *reply = m_client->sendReadRequest(request, m_config.unitId)) {
        connect(reply, &QModbusReply::finished, this, [this, reply, points, first] {
            if (reply->error() != QModbusDevice::NoError) {
                emit errorOccurred(reply->errorString()); reply->deleteLater(); return;
            }
            const auto result = reply->result();
            QList<TelemetrySample> samples;
            for (const auto &point : points) {
                TelemetrySample sample;
                sample.deviceId=m_config.id; sample.pointId=point.id; sample.deviceName=m_config.name;
                sample.pointName=point.name; sample.unit=point.unit; sample.timestamp=QDateTime::currentDateTime();
                sample.value=result.value(point.registerAddress-first)*point.scale; samples.append(sample);
            }
            emit samplesReady(samples); reply->deleteLater();
        });
    }
#else
    Q_UNUSED(points)
#endif
}

} // namespace dm
