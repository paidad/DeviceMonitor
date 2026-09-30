#include "logging/filelogger.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTextStream>

namespace {
QString g_logDirectory;
QMutex g_mutex;
int g_retentionDays = 30;
QDate g_lastCleanupDate;
constexpr qint64 MaximumLogSize = 2 * 1024 * 1024;

QString logFileName(const QDate &date)
{
    return QStringLiteral("device-monitor-%1.log").arg(date.toString(QStringLiteral("yyyy-MM-dd")));
}

void removeExpiredLogs(const QDate &today)
{
    if (g_logDirectory.isEmpty()) return;

    const QRegularExpression managedLogPattern(
        QStringLiteral(R"(^device-monitor-(\d{4}-\d{2}-\d{2})\.log(?:\.1)?$)"));
    QDir directory(g_logDirectory);
    const QStringList files = directory.entryList(QDir::Files | QDir::NoDotAndDotDot);

    for (const QString &fileName : files) {
        const QRegularExpressionMatch match = managedLogPattern.match(fileName);
        if (!match.hasMatch()) continue;

        const QDate fileDate = QDate::fromString(match.captured(1), QStringLiteral("yyyy-MM-dd"));
        if (fileDate.isValid() && fileDate.daysTo(today) > g_retentionDays)
            directory.remove(fileName);
    }
}
}

void dm::FileLogger::initialize(const QString &logDirectory, int retentionDays)
{
    QMutexLocker locker(&g_mutex);
    g_logDirectory = QDir::cleanPath(logDirectory);
    g_retentionDays = qMax(1, retentionDays);
    g_lastCleanupDate = QDate::currentDate();

    if (!g_logDirectory.isEmpty()) {
        QDir().mkpath(g_logDirectory);
        removeExpiredLogs(g_lastCleanupDate);
    }
}

void dm::FileLogger::write(const QString &level, const QString &message)
{
    QMutexLocker locker(&g_mutex);
    if (g_logDirectory.isEmpty()) return;

    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.date();
    if (g_lastCleanupDate != today) {
        QDir().mkpath(g_logDirectory);
        removeExpiredLogs(today);
        g_lastCleanupDate = today;
    }

    const QString filePath = QDir(g_logDirectory).filePath(logFileName(today));
    QFile file(filePath);
    if (file.exists() && file.size() >= MaximumLogSize) {
        QFile::remove(filePath + QStringLiteral(".1"));
        QFile::rename(filePath, filePath + QStringLiteral(".1"));
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QStringLiteral("[%1] [%2] %3\n")
                  .arg(now.toString(Qt::ISODateWithMs), level, message);
}
