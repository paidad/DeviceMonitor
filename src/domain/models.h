#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

namespace dm {

enum class DataQuality { Good, Uncertain, Bad };
enum class DeviceState { Offline, Connecting, Online, Error };
enum class AlarmState { Active, Recovered, Acknowledged };

struct DeviceConfig {
    int id = 0;
    QString name;
    QString host = QStringLiteral("127.0.0.1");
    int port = 502;
    int unitId = 1;
    int pollingIntervalMs = 1000;
    bool enabled = true;
    bool simulated = true;
};

struct PointConfig {
    int id = 0;
    int deviceId = 0;
    QString name;
    QString unit;
    int registerAddress = 0;
    double scale = 1.0;
};

struct TelemetrySample {
    int deviceId = 0;
    int pointId = 0;
    QString deviceName;
    QString pointName;
    QString unit;
    double value = 0.0;
    DataQuality quality = DataQuality::Good;
    QDateTime timestamp;
};

struct AlarmRule {
    int id = 0;
    int deviceId = 0;
    int pointId = 0;
    QString name;
    double lowLimit = 0.0;
    double highLimit = 100.0;
    bool enabled = true;
};

struct AlarmEvent {
    qint64 id = 0;
    int ruleId = 0;
    int deviceId = 0;
    int pointId = 0;
    QString deviceName;
    QString pointName;
    QString message;
    double value = 0.0;
    AlarmState state = AlarmState::Active;
    QDateTime triggeredAt;
    QDateTime recoveredAt;
    bool acknowledged = false;
};

inline QString qualityText(DataQuality quality)
{
    switch (quality) {
    case DataQuality::Good: return QStringLiteral("良好");
    case DataQuality::Uncertain: return QStringLiteral("不确定");
    case DataQuality::Bad: return QStringLiteral("异常");
    }
    return {};
}

} // namespace dm

Q_DECLARE_METATYPE(dm::DeviceConfig)
Q_DECLARE_METATYPE(dm::TelemetrySample)
Q_DECLARE_METATYPE(QList<dm::TelemetrySample>)
Q_DECLARE_METATYPE(dm::AlarmEvent)
