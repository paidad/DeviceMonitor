# DeviceMonitor · 设备监控与数据管理平台

基于 **Qt 6 / C++17 / CMake** 的工业设备监控上位机演示项目，覆盖「采集 → 实时展示 → 曲线 → 告警 → 持久化 → 导出」完整数据链路。内置设备模拟器，无需任何真实硬件即可运行全流程；同时保留基于 Modbus TCP 接入真实设备的实现。

> 这是一个用于学习与求职展示的个人项目，重点在于工程设计和可演示的功能，而非生产级现场适配。

## 亮点

- **完整链路，可离线演示**：内置模拟器周期生成 10 台设备 × 5 个测点的数据，开箱即跑，不依赖硬件。
- **采集与界面彻底解耦**：采集对象通过 `moveToThread()` 移入工作线程，采样以 queued signal 投递回 UI 线程，界面控件只在主线程被修改。
- **告警状态机**：按规则 ID 维护活动告警，避免持续越限时重复刷屏；支持恢复与确认，并区分「活动 / 已恢复 / 已确认」三态配色。
- **数据落库不卡界面**：采样先缓冲，每 2 秒在一个 SQLite 事务中批量提交；WAL 模式 + 复合索引支撑按设备、按时间范围的分页查询。
- **可验证**：Qt Test 覆盖告警状态机、数据库入库与设备 ID 变更、日志按天切分与过期清理等核心逻辑。
- **双主题**：深色 / 浅色切换，选择通过 `QSettings` 持久化，图表主题同步跟随。

## 功能页面

| 页面 | 主要能力 |
| --- | --- |
| 运行总览 | 设备总数、在线设备数、本次采样总数、活动告警数四张指标卡 |
| 设备管理 | 新增 / 双击修改 / 删除 / 启用停用设备，支持模拟与 Modbus TCP 两种类型 |
| 实时监控 | 最新数据表（`QAbstractTableModel`）+ 单测点实时趋势曲线（最近 60 个采样点） |
| 历史数据 | 设备 + 起止时间筛选、分页查询、导出 CSV（UTF-8 BOM） |
| 告警中心 | 告警事件列表、状态着色、确认操作（活动中的告警禁止直接确认） |
| 运行日志 | 界面内实时日志，同时按天写入磁盘文件 |
| 设备模拟器 | 「模拟全部设备断线」「模拟设备 01 温度越限」两个场景开关 |

## 内置测点

模拟器与默认演示数据包含以下 5 类测点，每台设备各有 5 个：

| 测点 | 单位 | 默认告警范围 |
| --- | --- | --- |
| 温度 | °C | 15 – 85 |
| 压力 | MPa | 0.1 – 1.4 |
| 转速 | rpm | 500 – 2800 |
| 电流 | A | 0 – 30 |
| 振动 | mm/s | 0 – 8 |

数据由正弦波动叠加随机噪声生成，因此曲线连续且带真实感；开启越限开关后，设备 01 的温度会被强制推到 95 °C 左右以触发告警。

## 架构

```text
Simulator  /  ModbusTcpClient
        │  (IDeviceClient 接口)
        │ queued signal
        ▼
  AcquisitionWorker ──► LatestDataModel ──► Widgets / Qt Charts
        │
        ├──────────────► AlarmEngine ──────► alarm_events
        └──────────────► batch buffer ─────► SQLite (samples)
```

目录结构与职责划分：

```text
src/
├── domain/       models.h             设备、测点、采样、告警的数据结构与枚举
├── protocol/     ideviceclient.h      采集协议抽象接口
│                 modbustcpclient.*    Modbus TCP 实现，含指数退避重连
├── acquisition/  acquisitionworker.*  工作线程内的采集调度与模拟数据生成
├── alarm/        alarmengine.*        阈值判断与告警状态机
├── storage/      databasemanager.*    SQLite 建表、演示数据、增删改查
├── logging/      filelogger.*         按天切分的文件日志与过期清理
└── ui/           latestdatamodel.*    实时数据模型
                  datetimepicker.*     自绘日期时间选择器
mainwindow.*     页面编排与各页面数据刷新
assets/icons/    深色 / 浅色主题下的 SpinBox 箭头图标
```

核心设计要点：

- `IDeviceClient` 隔离协议实现，模拟器与真实 Modbus 客户端对上层的表现完全一致。
- `DatabaseManager` 独占持久化职责，并在设备 ID 变更时用事务迁移关联的测点、规则、采样与告警记录。
- `AlarmEngine` 只依赖采样与规则，不感知界面；状态变化通过信号抛出。
- 主窗口只负责页面组装、定时刷新与数据展示，不承载业务逻辑。

## 构建运行

### 环境要求

- Qt **6.5 或更高版本**，组件：`Widgets`、`Sql`、`Charts`、`Network`、`Test`
- CMake 3.19+
- 支持 C++17 的编译器（MSVC 2022 / GCC / Clang）

`SerialBus` 为**可选**组件：安装后自动启用真实 Modbus TCP 客户端；未安装时仅打印一条提示，模拟器功能不受影响。

### 编译

```powershell
cmake -S . -B build/dev -G Ninja -DCMAKE_PREFIX_PATH=<你的Qt路径>
cmake --build build/dev
```

使用 MSVC 命令行构建前请先打开 **Developer PowerShell for VS 2022**；直接使用 Qt Creator 时由 Kit 自动配置编译环境。

### 运行测试

```powershell
ctest --test-dir build/dev --output-on-failure
```

### 打包分发

```powershell
cmake --build build/release --target package
```

打包目标会自动执行 Qt 部署脚本，产出 Windows 可直接运行的 ZIP 包。

## 数据与日志位置

程序首次启动会在 Qt 的 `AppDataLocation` 目录下自动创建数据库与日志：

```text
<AppDataLocation>/device-monitor.db       SQLite 数据库（WAL 模式）
<AppDataLocation>/logs/device-monitor-YYYY-MM-DD.log
```

- 首次启动自动建表，并写入 10 台演示设备、50 个测点及对应告警规则。
- 日志按天切分，单文件超过 2 MB 自动轮转为 `.1`，超过 30 天的历史日志会被清理（该策略有测试覆盖）。

## 接入真实设备

在「设备管理」中把设备的「使用内置模拟器」取消勾选，填写主机地址、端口、站号与采集周期即可。客户端会按测点地址范围合并成一次保持寄存器读取，并用测点自带的缩放系数把原始值换算为工程值；断线后按 `1s / 2s / 4s / 8s …` 指数退避重连，上限 30 秒。

**已知限制**：当前每台设备配置的「采集周期 / ms」尚未真正生效，采集仍由统一的 1000 ms 定时器驱动。另外项目未在真实硬件上做过联调，实际寄存器映射需依据设备厂家说明扩展。

## 技术栈

C++17 · Qt 6（Widgets / Charts / Sql / Network / Test / 可选 SerialBus）· SQLite · CMake · Qt Test

