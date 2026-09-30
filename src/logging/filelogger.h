#pragma once

#include <QString>

namespace dm {

class FileLogger
{
public:
    static void initialize(const QString &logDirectory, int retentionDays = 30);
    static void write(const QString &level, const QString &message);
};

} // namespace dm
