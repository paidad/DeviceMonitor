cmake_minimum_required(VERSION 3.19)
project(DeviceMonitor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Sql Charts Network Test)
find_package(Qt6 6.5 QUIET COMPONENTS SerialBus)

qt_standard_project_setup()

set(DEVICE_MONITOR_SOURCES
    resources.qrc
    src/domain/models.h
    src/storage/databasemanager.h
    src/storage/databasemanager.cpp
    src/alarm/alarmengine.h
    src/alarm/alarmengine.cpp
    src/acquisition/acquisitionworker.h
    src/acquisition/acquisitionworker.cpp
    src/protocol/ideviceclient.h
    src/protocol/modbustcpclient.h
    src/protocol/modbustcpclient.cpp
    src/logging/filelogger.h
    src/logging/filelogger.cpp
    src/ui/latestdatamodel.h
    src/ui/latestdatamodel.cpp
    src/ui/datetimepicker.h
    src/ui/datetimepicker.cpp
)

qt_add_executable(DeviceMonitor
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    ${DEVICE_MONITOR_SOURCES}

)

target_include_directories(DeviceMonitor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(DeviceMonitor PRIVATE Qt6::Core Qt6::Widgets Qt6::Sql Qt6::Charts Qt6::Network)

if(TARGET Qt6::SerialBus)
    target_link_libraries(DeviceMonitor PRIVATE Qt6::SerialBus)
    target_compile_definitions(DeviceMonitor PRIVATE DEVICE_MONITOR_HAS_SERIALBUS=1)
else()
    message(STATUS "Qt SerialBus was not found; real Modbus TCP support is disabled. The simulator remains available.")
endif()

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(DeviceMonitorTests
        tests/test_core.cpp
        src/domain/models.h
        src/alarm/alarmengine.h
        src/alarm/alarmengine.cpp
        src/storage/databasemanager.h
        src/storage/databasemanager.cpp
        src/logging/filelogger.h
        src/logging/filelogger.cpp
    )
    target_include_directories(DeviceMonitorTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(DeviceMonitorTests PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
    add_test(NAME DeviceMonitorTests COMMAND DeviceMonitorTests)
endif()

include(GNUInstallDirs)

install(TARGETS DeviceMonitor
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET DeviceMonitor
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
install(FILES README.md DESTINATION .)

set(CPACK_PACKAGE_NAME "DeviceMonitor")
set(CPACK_PACKAGE_VENDOR "Portfolio")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt industrial device monitoring and data management platform")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP")
include(CPack)
