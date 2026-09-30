#include "mainwindow.h"
#include "domain/models.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Portfolio"));
    QApplication::setApplicationName(QStringLiteral("DeviceMonitor"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    qRegisterMetaType<dm::DeviceConfig>();
    qRegisterMetaType<QList<dm::DeviceConfig>>();
    qRegisterMetaType<QList<dm::PointConfig>>();
    qRegisterMetaType<QList<dm::TelemetrySample>>();
    qRegisterMetaType<dm::AlarmEvent>();
    qRegisterMetaType<dm::DeviceState>();
    MainWindow window;
    window.show();
    return app.exec();
}
