#include "alarm/alarmengine.h"
#include "logging/filelogger.h"
#include "storage/databasemanager.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class CoreTests final : public QObject
{
    Q_OBJECT
private slots:
    void alarmTriggersOnceAndRecovers();
    void databaseSeedsAndPersistsSamples();
    void loggerCreatesDailyFileAndCleansExpiredLogs();
};

void CoreTests::alarmTriggersOnceAndRecovers()
{
    dm::AlarmEngine engine;
    dm::AlarmRule rule;
    rule.id = 1; rule.pointId = 101; rule.deviceId = 1; rule.lowLimit = 10; rule.highLimit = 20;
    engine.setRules({rule});
    QSignalSpy spy(&engine, &dm::AlarmEngine::alarmChanged);
    dm::TelemetrySample sample;
    sample.deviceId = 1; sample.pointId = 101; sample.pointName = QStringLiteral("温度");
    sample.timestamp = QDateTime::currentDateTime(); sample.value = 30;
    engine.evaluate(sample);
    engine.evaluate(sample);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(engine.activeAlarms().size(), 1);
    sample.value = 15;
    engine.evaluate(sample);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(engine.activeAlarms().size(), 0);
}

void CoreTests::databaseSeedsAndPersistsSamples()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    dm::DatabaseManager database;
    QVERIFY2(database.initialize(directory.filePath(QStringLiteral("test.db"))), qPrintable(database.lastError()));
    QCOMPARE(database.devices().size(), 10);
    QCOMPARE(database.points().size(), 50);
    dm::TelemetrySample sample;
    sample.deviceId = 1; sample.pointId = 101; sample.value = 42.5;
    sample.timestamp = QDateTime::currentDateTime();
    QVERIFY(database.insertSamples({sample}));
    QCOMPARE(database.sampleCount(1, sample.timestamp.addSecs(-1), sample.timestamp.addSecs(1)), 1);

    dm::DeviceConfig renamedDevice = database.devices().first();
    QCOMPARE(renamedDevice.id, 1);
    renamedDevice.id = 20;
    QVERIFY2(database.updateDevice(1, renamedDevice), qPrintable(database.lastError()));
    QVERIFY(!database.deviceIdExists(1));
    QVERIFY(database.deviceIdExists(20));
    QCOMPARE(database.sampleCount(20, sample.timestamp.addSecs(-1), sample.timestamp.addSecs(1)), 1);
    int movedPointCount = 0;
    for (const auto &point : database.points())
        if (point.deviceId == 20) ++movedPointCount;
    QCOMPARE(movedPointCount, 5);

    dm::DeviceConfig duplicateDevice = database.devices().at(1);
    const int originalSecondId = duplicateDevice.id;
    duplicateDevice.id = 20;
    QVERIFY(!database.updateDevice(originalSecondId, duplicateDevice));
    QVERIFY(database.deviceIdExists(originalSecondId));
}

void CoreTests::loggerCreatesDailyFileAndCleansExpiredLogs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QDate today = QDate::currentDate();
    const QString expiredName = QStringLiteral("device-monitor-%1.log")
        .arg(today.addDays(-31).toString(QStringLiteral("yyyy-MM-dd")));
    const QString recentName = QStringLiteral("device-monitor-%1.log")
        .arg(today.addDays(-30).toString(QStringLiteral("yyyy-MM-dd")));
    const QString legacyName = QStringLiteral("device-monitor.log");
    const QString unrelatedName = QStringLiteral("notes.log");

    for (const QString &fileName : {expiredName, recentName, legacyName, unrelatedName}) {
        QFile file(directory.filePath(fileName));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("existing\n");
    }

    dm::FileLogger::initialize(directory.path(), 30);
    QVERIFY(!QFile::exists(directory.filePath(expiredName)));
    QVERIFY(QFile::exists(directory.filePath(recentName)));
    QVERIFY(QFile::exists(directory.filePath(legacyName)));
    QVERIFY(QFile::exists(directory.filePath(unrelatedName)));

    dm::FileLogger::write(QStringLiteral("INFO"), QStringLiteral("中文日志测试"));
    const QString todayName = QStringLiteral("device-monitor-%1.log")
        .arg(today.toString(QStringLiteral("yyyy-MM-dd")));
    QFile todayFile(directory.filePath(todayName));
    QVERIFY(todayFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(todayFile.readAll());
    QVERIFY(content.contains(QStringLiteral("[INFO]")));
    QVERIFY(content.contains(QStringLiteral("中文日志测试")));
}

QTEST_MAIN(CoreTests)
#include "test_core.moc"
