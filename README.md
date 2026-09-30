# DeviceMonitor

基于 Qt 6、C++17 与 CMake 的工业设备监控和数据管理桌面平台。项目包含后台采集、实时曲线、SQLite 历史数据、告警状态机、CSV 导出、设备管理和故障模拟，可直接作为 Qt/C++ 求职作品集项目演示。

## 功能

- 10 台设备、每台 5 个测点的内置实时模拟器
- 后台工作线程采集，queued signal 向界面分发数据
- SQLite WAL 模式、事务批量写入和历史分页查询
- 温度、压力、转速、电流和振动实时曲线
- 上下限告警、恢复、确认及事件持久化
- 断线、重连和越限场景模拟
- 设备配置管理、按日期自动保存并保留最近 30 天的运行日志、CSV 导出
- Qt SerialBus 可选接入真实 Modbus TCP 设备

## 构建

需要 Qt 6.5 或更高版本，组件：Widgets、Sql、Charts、Network、Test。安装 SerialBus 后会自动启用真实 Modbus TCP 客户端；未安装时模拟器功能不受影响。

```powershell
cmake -S . -B build/dev -G Ninja -DCMAKE_PREFIX_PATH=D:/Software/Qt/6.7.3/msvc2022_64
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
```

使用 MSVC 命令行构建前需打开 **Developer PowerShell for VS 2022**；直接使用 Qt Creator 时由 Kit 自动配置编译环境。

生成可分发 ZIP：

```powershell
cmake --build build/release --target package
```

运行后数据库位于 Qt 的 `AppDataLocation` 目录。日志保存在其 `logs` 子目录中，文件名为
`device-monitor-YYYY-MM-DD.log`，程序会自动清理超过 30 天的日志。首次启动自动创建表结构和演示设备。

## 架构

```text
Simulator / ModbusTcpClient
            │ queued signal
            ▼
     AcquisitionWorker ──► LatestDataModel ──► Widgets / Qt Charts
            │
            ├────────────► AlarmEngine ──────► alarm_events
            └────────────► batch buffer ─────► SQLite
```

`IDeviceClient` 隔离协议实现，`DatabaseManager` 负责持久化，`AlarmEngine` 维护告警状态，主窗口只负责页面编排和数据展示。
