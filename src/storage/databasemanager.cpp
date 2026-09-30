#include "storage/databasemanager.h"

#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace dm {

DatabaseManager::DatabaseManager(QObject *parent)
    : QObject(parent), m_connectionName(QStringLiteral("device-monitor-%1").arg(QUuid::createUuid().toString()))
{
}

DatabaseManager::~DatabaseManager()
{
    if (m_db.isValid())
        m_db.close();
    m_db = {};
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool DatabaseManager::initialize(const QString &databasePath)
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(databasePath);
    if (!m_db.open()) {
        setError(QStringLiteral("无法打开数据库：%1").arg(m_db.lastError().text()));
        return false;
    }

    QSqlQuery pragmas(m_db);
    pragmas.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragmas.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragmas.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    return executeSchema() && seedDemoData();
}

QString DatabaseManager::lastError() const { return m_lastError; }

void DatabaseManager::setError(const QString &message) const
{
    m_lastError = message;
    emit const_cast<DatabaseManager *>(this)->databaseError(message);
}

bool DatabaseManager::executeSchema()
{
    static const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS devices (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, host TEXT NOT NULL, port INTEGER NOT NULL, unit_id INTEGER NOT NULL, polling_ms INTEGER NOT NULL, enabled INTEGER NOT NULL, simulated INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS points (id INTEGER PRIMARY KEY, device_id INTEGER NOT NULL REFERENCES devices(id) ON DELETE CASCADE, name TEXT NOT NULL, unit TEXT NOT NULL, register_address INTEGER NOT NULL, scale REAL NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS samples (id INTEGER PRIMARY KEY AUTOINCREMENT, device_id INTEGER NOT NULL, point_id INTEGER NOT NULL, value REAL NOT NULL, quality INTEGER NOT NULL, timestamp TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_samples_device_time ON samples(device_id, timestamp DESC)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS alarm_rules (id INTEGER PRIMARY KEY, device_id INTEGER NOT NULL, point_id INTEGER NOT NULL UNIQUE, name TEXT NOT NULL, low_limit REAL NOT NULL, high_limit REAL NOT NULL, enabled INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS alarm_events (id INTEGER PRIMARY KEY AUTOINCREMENT, rule_id INTEGER NOT NULL, device_id INTEGER NOT NULL, point_id INTEGER NOT NULL, device_name TEXT NOT NULL, point_name TEXT NOT NULL, message TEXT NOT NULL, value REAL NOT NULL, state INTEGER NOT NULL, triggered_at TEXT NOT NULL, recovered_at TEXT, acknowledged INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_alarm_time ON alarm_events(triggered_at DESC)")
    };

    for (const auto &statement : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(statement)) {
            setError(QStringLiteral("建表失败：%1").arg(query.lastError().text()));
            return false;
        }
    }
    return true;
}

bool DatabaseManager::seedDemoData()
{
    QSqlQuery count(m_db);
    if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM devices")) || !count.next())
        return false;
    if (count.value(0).toInt() > 0)
        return true;

    if (!m_db.transaction())
        return false;

    const QStringList names = {QStringLiteral("温度"), QStringLiteral("压力"),
                               QStringLiteral("转速"), QStringLiteral("电流"), QStringLiteral("振动")};
    const QStringList units = {QStringLiteral("°C"), QStringLiteral("MPa"),
                               QStringLiteral("rpm"), QStringLiteral("A"), QStringLiteral("mm/s")};
    for (int deviceIndex = 1; deviceIndex <= 10; ++deviceIndex) {
        QSqlQuery deviceQuery(m_db);
        deviceQuery.prepare(QStringLiteral("INSERT INTO devices(name,host,port,unit_id,polling_ms,enabled,simulated) VALUES(?,?,?,?,?,?,?)"));
        deviceQuery.addBindValue(QStringLiteral("生产设备 %1").arg(deviceIndex, 2, 10, QLatin1Char('0')));
        deviceQuery.addBindValue(QStringLiteral("127.0.0.1"));
        deviceQuery.addBindValue(1502 + deviceIndex);
        deviceQuery.addBindValue(deviceIndex);
        deviceQuery.addBindValue(1000);
        deviceQuery.addBindValue(1);
        deviceQuery.addBindValue(1);
        if (!deviceQuery.exec()) {
            m_db.rollback();
            setError(deviceQuery.lastError().text());
            return false;
        }
        const int deviceId = deviceQuery.lastInsertId().toInt();

        for (int pointIndex = 0; pointIndex < names.size(); ++pointIndex) {
            const int pointId = deviceId * 100 + pointIndex + 1;
            QSqlQuery pointQuery(m_db);
            pointQuery.prepare(QStringLiteral("INSERT INTO points(id,device_id,name,unit,register_address,scale) VALUES(?,?,?,?,?,?)"));
            pointQuery.addBindValue(pointId);
            pointQuery.addBindValue(deviceId);
            pointQuery.addBindValue(names.at(pointIndex));
            pointQuery.addBindValue(units.at(pointIndex));
            pointQuery.addBindValue(pointIndex);
            pointQuery.addBindValue(pointIndex == 2 ? 10.0 : 0.1);
            if (!pointQuery.exec()) {
                m_db.rollback();
                setError(pointQuery.lastError().text());
                return false;
            }

            const QList<QPair<double, double>> limits = {{15, 85}, {0.1, 1.4}, {500, 2800}, {0, 30}, {0, 8}};
            QSqlQuery ruleQuery(m_db);
            ruleQuery.prepare(QStringLiteral("INSERT INTO alarm_rules(id,device_id,point_id,name,low_limit,high_limit,enabled) VALUES(?,?,?,?,?,?,1)"));
            ruleQuery.addBindValue(pointId);
            ruleQuery.addBindValue(deviceId);
            ruleQuery.addBindValue(pointId);
            ruleQuery.addBindValue(QStringLiteral("%1范围告警").arg(names.at(pointIndex)));
            ruleQuery.addBindValue(limits.at(pointIndex).first);
            ruleQuery.addBindValue(limits.at(pointIndex).second);
            if (!ruleQuery.exec()) {
                m_db.rollback();
                setError(ruleQuery.lastError().text());
                return false;
            }
        }
    }
    return m_db.commit();
}

QList<DeviceConfig> DatabaseManager::devices() const
{
    QList<DeviceConfig> result;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT id,name,host,port,unit_id,polling_ms,enabled,simulated FROM devices ORDER BY id"))) {
        setError(query.lastError().text());
        return result;
    }
    while (query.next()) {
        DeviceConfig d;
        d.id = query.value(0).toInt(); d.name = query.value(1).toString();
        d.host = query.value(2).toString(); d.port = query.value(3).toInt();
        d.unitId = query.value(4).toInt(); d.pollingIntervalMs = query.value(5).toInt();
        d.enabled = query.value(6).toBool(); d.simulated = query.value(7).toBool();
        result.append(d);
    }
    return result;
}

QList<PointConfig> DatabaseManager::points() const
{
    QList<PointConfig> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT id,device_id,name,unit,register_address,scale FROM points ORDER BY device_id,id"));
    while (query.next()) {
        PointConfig p;
        p.id = query.value(0).toInt(); p.deviceId = query.value(1).toInt();
        p.name = query.value(2).toString(); p.unit = query.value(3).toString();
        p.registerAddress = query.value(4).toInt(); p.scale = query.value(5).toDouble();
        result.append(p);
    }
    return result;
}

QList<AlarmRule> DatabaseManager::alarmRules() const
{
    QList<AlarmRule> result;
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT id,device_id,point_id,name,low_limit,high_limit,enabled FROM alarm_rules"));
    while (query.next()) {
        AlarmRule r;
        r.id = query.value(0).toInt(); r.deviceId = query.value(1).toInt();
        r.pointId = query.value(2).toInt(); r.name = query.value(3).toString();
        r.lowLimit = query.value(4).toDouble(); r.highLimit = query.value(5).toDouble();
        r.enabled = query.value(6).toBool(); result.append(r);
    }
    return result;
}

bool DatabaseManager::deviceIdExists(int deviceId) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT 1 FROM devices WHERE id=? LIMIT 1"));
    query.addBindValue(deviceId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.next();
}

bool DatabaseManager::saveDevice(DeviceConfig &device)
{
    QSqlQuery query(m_db);
    if (device.id == 0) {
        query.prepare(QStringLiteral("INSERT INTO devices(name,host,port,unit_id,polling_ms,enabled,simulated) VALUES(?,?,?,?,?,?,?)"));
    } else {
        query.prepare(QStringLiteral("UPDATE devices SET name=?,host=?,port=?,unit_id=?,polling_ms=?,enabled=?,simulated=? WHERE id=?"));
    }
    query.addBindValue(device.name); query.addBindValue(device.host); query.addBindValue(device.port);
    query.addBindValue(device.unitId); query.addBindValue(device.pollingIntervalMs);
    query.addBindValue(device.enabled); query.addBindValue(device.simulated);
    if (device.id != 0) query.addBindValue(device.id);
    if (!query.exec()) { setError(query.lastError().text()); return false; }
    const bool newlyCreated = device.id == 0;
    if (newlyCreated)
        device.id = query.lastInsertId().toInt();
    if (newlyCreated) {
        const QStringList names = {QStringLiteral("温度"), QStringLiteral("压力"), QStringLiteral("转速"),
                                   QStringLiteral("电流"), QStringLiteral("振动")};
        const QStringList units = {QStringLiteral("°C"), QStringLiteral("MPa"), QStringLiteral("rpm"),
                                   QStringLiteral("A"), QStringLiteral("mm/s")};
        const QList<QPair<double, double>> limits = {{15, 85}, {0.1, 1.4}, {500, 2800}, {0, 30}, {0, 8}};
        for (int i = 0; i < names.size(); ++i) {
            const int pointId = device.id * 100 + i + 1;
            QSqlQuery pointQuery(m_db);
            pointQuery.prepare(QStringLiteral("INSERT INTO points(id,device_id,name,unit,register_address,scale) VALUES(?,?,?,?,?,?)"));
            pointQuery.addBindValue(pointId); pointQuery.addBindValue(device.id);
            pointQuery.addBindValue(names.at(i)); pointQuery.addBindValue(units.at(i));
            pointQuery.addBindValue(i); pointQuery.addBindValue(i == 2 ? 10.0 : 0.1);
            if (!pointQuery.exec()) { setError(pointQuery.lastError().text()); return false; }
            QSqlQuery ruleQuery(m_db);
            ruleQuery.prepare(QStringLiteral("INSERT INTO alarm_rules(id,device_id,point_id,name,low_limit,high_limit,enabled) VALUES(?,?,?,?,?,?,1)"));
            ruleQuery.addBindValue(pointId); ruleQuery.addBindValue(device.id); ruleQuery.addBindValue(pointId);
            ruleQuery.addBindValue(QStringLiteral("%1范围告警").arg(names.at(i)));
            ruleQuery.addBindValue(limits.at(i).first); ruleQuery.addBindValue(limits.at(i).second);
            if (!ruleQuery.exec()) { setError(ruleQuery.lastError().text()); return false; }
        }
    }
    return true;
}

bool DatabaseManager::updateDevice(int originalDeviceId, const DeviceConfig &device)
{
    if (originalDeviceId <= 0 || device.id <= 0) {
        setError(QStringLiteral("设备 ID 必须大于 0"));
        return false;
    }

    if (device.id == originalDeviceId) {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("UPDATE devices SET name=?,host=?,port=?,unit_id=?,polling_ms=?,enabled=?,simulated=? WHERE id=?"));
        query.addBindValue(device.name); query.addBindValue(device.host); query.addBindValue(device.port);
        query.addBindValue(device.unitId); query.addBindValue(device.pollingIntervalMs);
        query.addBindValue(device.enabled); query.addBindValue(device.simulated); query.addBindValue(originalDeviceId);
        if (!query.exec()) { setError(query.lastError().text()); return false; }
        return query.numRowsAffected() == 1;
    }

    if (deviceIdExists(device.id)) {
        setError(QStringLiteral("设备 ID %1 已存在").arg(device.id));
        return false;
    }
    if (!m_db.transaction()) {
        setError(m_db.lastError().text());
        return false;
    }

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral("INSERT INTO devices(id,name,host,port,unit_id,polling_ms,enabled,simulated) VALUES(?,?,?,?,?,?,?,?)"));
    insert.addBindValue(device.id); insert.addBindValue(device.name); insert.addBindValue(device.host);
    insert.addBindValue(device.port); insert.addBindValue(device.unitId); insert.addBindValue(device.pollingIntervalMs);
    insert.addBindValue(device.enabled); insert.addBindValue(device.simulated);
    if (!insert.exec()) {
        m_db.rollback(); setError(insert.lastError().text()); return false;
    }

    const QStringList relatedTables = {QStringLiteral("points"), QStringLiteral("alarm_rules"),
                                       QStringLiteral("samples"), QStringLiteral("alarm_events")};
    for (const auto &table : relatedTables) {
        QSqlQuery update(m_db);
        update.prepare(QStringLiteral("UPDATE %1 SET device_id=? WHERE device_id=?").arg(table));
        update.addBindValue(device.id); update.addBindValue(originalDeviceId);
        if (!update.exec()) {
            m_db.rollback(); setError(update.lastError().text()); return false;
        }
    }

    QSqlQuery removeOld(m_db);
    removeOld.prepare(QStringLiteral("DELETE FROM devices WHERE id=?"));
    removeOld.addBindValue(originalDeviceId);
    if (!removeOld.exec() || removeOld.numRowsAffected() != 1) {
        m_db.rollback(); setError(removeOld.lastError().text()); return false;
    }
    if (!m_db.commit()) {
        setError(m_db.lastError().text());
        return false;
    }
    return true;
}

bool DatabaseManager::removeDevice(int deviceId)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM devices WHERE id=?"));
    query.addBindValue(deviceId);
    if (!query.exec()) { setError(query.lastError().text()); return false; }
    return true;
}

bool DatabaseManager::insertSamples(const QList<TelemetrySample> &samples)
{
    if (samples.isEmpty()) return true;
    if (!m_db.transaction()) return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO samples(device_id,point_id,value,quality,timestamp) VALUES(?,?,?,?,?)"));
    for (const auto &sample : samples) {
        query.addBindValue(sample.deviceId); query.addBindValue(sample.pointId);
        query.addBindValue(sample.value); query.addBindValue(static_cast<int>(sample.quality));
        query.addBindValue(sample.timestamp.toUTC().toString(Qt::ISODateWithMs));
        if (!query.exec()) { m_db.rollback(); setError(query.lastError().text()); return false; }
    }
    if (!m_db.commit()) { setError(m_db.lastError().text()); return false; }
    return true;
}

QList<TelemetrySample> DatabaseManager::querySamples(int deviceId, const QDateTime &from,
                                                      const QDateTime &to, int limit, int offset) const
{
    QList<TelemetrySample> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT s.device_id,s.point_id,d.name,p.name,p.unit,s.value,s.quality,s.timestamp FROM samples s JOIN devices d ON d.id=s.device_id JOIN points p ON p.id=s.point_id WHERE (?=0 OR s.device_id=?) AND s.timestamp BETWEEN ? AND ? ORDER BY s.timestamp DESC LIMIT ? OFFSET ?"));
    query.addBindValue(deviceId); query.addBindValue(deviceId);
    query.addBindValue(from.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(to.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(limit); query.addBindValue(offset);
    if (!query.exec()) { setError(query.lastError().text()); return result; }
    while (query.next()) {
        TelemetrySample s;
        s.deviceId = query.value(0).toInt(); s.pointId = query.value(1).toInt();
        s.deviceName = query.value(2).toString(); s.pointName = query.value(3).toString();
        s.unit = query.value(4).toString(); s.value = query.value(5).toDouble();
        s.quality = static_cast<DataQuality>(query.value(6).toInt());
        s.timestamp = QDateTime::fromString(query.value(7).toString(), Qt::ISODateWithMs).toLocalTime();
        result.append(s);
    }
    return result;
}

int DatabaseManager::sampleCount(int deviceId, const QDateTime &from, const QDateTime &to) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM samples WHERE (?=0 OR device_id=?) AND timestamp BETWEEN ? AND ?"));
    query.addBindValue(deviceId); query.addBindValue(deviceId);
    query.addBindValue(from.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(to.toUTC().toString(Qt::ISODateWithMs));
    return query.exec() && query.next() ? query.value(0).toInt() : 0;
}

bool DatabaseManager::saveAlarmEvent(AlarmEvent &event)
{
    QSqlQuery query(m_db);
    if (event.state == AlarmState::Recovered) {
        query.prepare(QStringLiteral("UPDATE alarm_events SET state=?,recovered_at=?,value=? WHERE rule_id=? AND recovered_at IS NULL"));
        query.addBindValue(static_cast<int>(event.state));
        query.addBindValue(event.recoveredAt.toUTC().toString(Qt::ISODateWithMs));
        query.addBindValue(event.value); query.addBindValue(event.ruleId);
    } else if (event.state == AlarmState::Acknowledged) {
        query.prepare(QStringLiteral("UPDATE alarm_events SET acknowledged=1,state=? WHERE rule_id=? AND recovered_at IS NULL"));
        query.addBindValue(static_cast<int>(event.state)); query.addBindValue(event.ruleId);
    } else {
        query.prepare(QStringLiteral("INSERT INTO alarm_events(rule_id,device_id,point_id,device_name,point_name,message,value,state,triggered_at,acknowledged) VALUES(?,?,?,?,?,?,?,?,?,?)"));
        query.addBindValue(event.ruleId); query.addBindValue(event.deviceId); query.addBindValue(event.pointId);
        query.addBindValue(event.deviceName); query.addBindValue(event.pointName); query.addBindValue(event.message);
        query.addBindValue(event.value); query.addBindValue(static_cast<int>(event.state));
        query.addBindValue(event.triggeredAt.toUTC().toString(Qt::ISODateWithMs)); query.addBindValue(event.acknowledged);
    }
    if (!query.exec()) { setError(query.lastError().text()); return false; }
    if (event.id == 0 && event.state == AlarmState::Active) event.id = query.lastInsertId().toLongLong();
    return true;
}

QList<AlarmEvent> DatabaseManager::alarmEvents(int limit) const
{
    QList<AlarmEvent> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id,rule_id,device_id,point_id,device_name,point_name,message,value,state,triggered_at,recovered_at,acknowledged FROM alarm_events ORDER BY triggered_at DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec()) return result;
    while (query.next()) {
        AlarmEvent e;
        e.id=query.value(0).toLongLong(); e.ruleId=query.value(1).toInt(); e.deviceId=query.value(2).toInt(); e.pointId=query.value(3).toInt();
        e.deviceName=query.value(4).toString(); e.pointName=query.value(5).toString(); e.message=query.value(6).toString(); e.value=query.value(7).toDouble();
        e.state=static_cast<AlarmState>(query.value(8).toInt()); e.triggeredAt=QDateTime::fromString(query.value(9).toString(),Qt::ISODateWithMs).toLocalTime();
        e.recoveredAt=QDateTime::fromString(query.value(10).toString(),Qt::ISODateWithMs).toLocalTime(); e.acknowledged=query.value(11).toBool(); result.append(e);
    }
    return result;
}

bool DatabaseManager::acknowledgeAlarm(qint64 eventId)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE alarm_events SET acknowledged=1,state=? WHERE id=?"));
    query.addBindValue(static_cast<int>(AlarmState::Acknowledged)); query.addBindValue(eventId);
    return query.exec();
}

} // namespace dm
