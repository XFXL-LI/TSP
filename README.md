# TSP 空气微站环境监测终端

本项目是基于 ESP32-S3 的空气微站/TSP 环境监测终端固件，用于采集颗粒物、气态污染物、气象和噪声数据，并通过 HJ 212 协议上传到监控平台。项目同时提供本地显示、历史存储、断点续传、远程配置、传感器标定、告警和 OTA 升级等功能。

当前固件版本：`2.0.0`

## 主要功能

- 采集 PM1、PM2.5、PM10、TSP、SO₂、NO₂、CO、O₃、温度、湿度、大气压、风速、风向和噪声。
- 支持 HJ 212-2017 和项目预留的 HJ 212-2025 打包流程。
- 实时数据按采集周期发送；分钟数据每完成一个 10 分钟窗口发送一次；小时、日数据在统计周期结束后发送。
- 历史数据保存到 SD 卡，支持按单个时间或时间范围查询。
- HJ 212 发送失败时保存完整待传报文，网络恢复后按批次补传。
- 支持 M100M-B2 DTU 的网络时间、CSQ、TCP 目标配置和透明传输。
- 支持 LCD、本项目 LED 控制卡和外部远程串口接口。
- 支持传感器校准、配置文件读写、告警、温湿度控制和 OTA 升级。

## 数据处理链路

```text
传感器
  │ Modbus RTU / TTL
  ▼
collectorManager
  │ RAW_DATA_COLLECTED
  ▼
DataManager
  ├─ 实时数据 REAL_DATA
  ├─ 10分钟数据 MIN_DATA
  ├─ 小时数据 HOUR_DATA
  └─ 日数据 DAY_DATA
       │
       ├─ HJ212 打包与 DTU 上传
       ├─ SD 卡历史数据
       ├─ pending 断点续传
       └─ LCD / LED 显示
```

跨任务数据通过 `EventBus` 传递。新增订阅者时必须正确处理数据结构的 `retain()` / `release()`，避免内存泄漏或悬挂指针。

## 硬件与开发环境

| 项目 | 当前配置 |
|---|---|
| 主控 | ESP32-S3 |
| Flash | 16 MB |
| Arduino Core | `esp32:esp32:esp32s3` |
| 分区 | `app3M_fat9M_16MB` |
| CPU | 240 MHz |
| PSRAM | disabled |
| 默认采集周期 | 60 s |
| 默认 HJ 212 版本 | 2017 |
| 现场调试串口 | COM29，9600，8N1（以现场实际端口为准） |

### 串口分配

| 名称 | RX | TX | 波特率 | 用途 |
|---|---:|---:|---:|---|
| `SERIAL_485` | GPIO 4 | GPIO 5 | 9600 | 风速风向、气象百叶箱、气体模组 |
| `SERIAL_TTL` | GPIO 9 | GPIO 10 | 9600 | SDS069 颗粒物传感器 |
| `SERIAL_DTU` | GPIO 38 | GPIO 37 | 115200 | Remote DTU、远程命令和配置 |
| `SERIAL_HJ212` | GPIO 12 | GPIO 13 | 9600 | HJ 212 DTU 透明传输 |
| `SERIAL_LCD` | GPIO 15 | GPIO 16 | 9600 | LCD/本地操作界面 |
| `SERIAL_LED` | GPIO 45 | GPIO 0 | 9600 | LED 控制卡专用 485 协议 |

## 传感器配置

| 物理设备 | 从站/接口 | 数据地址 | 项目因子 |
|---|---|---|---|
| SDS069 多通道颗粒物 | TTL，地址 1 | PM1=`0x10`、PM2.5=`0x12`、PM10=`0x14`、TSP=`0x16`，每项 2 个寄存器 | `a34005`、`a34004`、`a34002`、`a34001` |
| XM8189B 风速风向 | RS485，地址 1 | 风速=`0`、风向=`1`，原始值除以 100 | `a01007`、`a01008` |
| SN-300BYH-M 气象百叶箱 | RS485，地址 2 | 湿度=`500`、温度=`501`、噪声=`502`、气压=`505` | `a01002`、`a01001`、`L90`、`a01006` |
| O₃ 模组 | RS485，地址 3 | 浓度=`0x6001` | `w34011` |
| NO₂ 模组 | RS485，地址 4 | 浓度=`0x6001` | `a21004` |
| CO 模组 | RS485，地址 5 | 浓度=`0x6001` | `a21005` |
| SO₂ 模组 | RS485，地址 6 | 浓度=`0x6001` | `a21026` |

详细接线、Modbus Poll 设置和装配前测试步骤见 [单设备测试文档](output/sensor_test_docs/) 目录。

## HJ 212 数据

| 数据类型 | `DataTime` | CN | 当前发送时机 |
|---|---|---:|---|
| 实时数据 | `REAL_DATA` | 2011 | 每个有效采集周期 |
| 10 分钟数据 | `MIN_DATA` | 2051 | 每个完整 10 分钟窗口结束后 |
| 小时数据 | `HOUR_DATA` | 2061 | 跨小时后发送上一小时数据 |
| 日数据 | `DAY_DATA` | 2031 | 跨日后发送上一日数据 |

HJ 212 报文由 `TSP/src/module/pack212` 生成。发送端需要检查：

- 包头、数据段长度和 CRC 是否正确；
- `QN`、`ST`、`CN`、`PW`、`MN` 和 `Flag` 是否与平台一致；
- `DataTime` 是否为原始采集或统计窗口时间；
- 平台是否返回包含 `CN=9014` 的应答。

## 历史数据与断点续传

### SD 卡目录

```text
/sdcard/YYYYMMDD/raw/HH/MM.dat    实时数据
/sdcard/YYYYMMDD/min/HH/MM.dat    10分钟数据
/sdcard/YYYYMMDD/hour/HH.dat      小时数据
/sdcard/YYYYMMDD/day/day.dat      日数据
/sdcard/pending/<type>/<time>.pkt 待补传的完整 HJ212 报文
```

历史记录采用 36 字节定长二进制结构：

```cpp
#pragma pack(push, 1)
struct fileStorage {
    uint64_t timestamp;
    char sensor_id[15];
    float value;
    float min_val;
    float max_val;
    uint8_t is_valid;
};
#pragma pack(pop)
```

发送失败时，系统优先保存完整 `.pkt` 报文。网络恢复后扫描 `/sdcard/pending`，补传成功并收到平台确认后才删除文件；补传失败时保留文件等待下次恢复。

## 配置文件

配置文件保存在设备文件系统中。文件缺失时，`ConfigManager` 会写入源码中的默认模板。

| 文件 | 作用 |
|---|---|
| `/config.json` | 采集周期、上传周期、DTU/MQTT服务器 |
| `/hj212.json` | 平台 IP、MN、PW、ST、Flag、协议版本和重试参数 |
| `/model.json` | 监测因子名称、单位和告警阈值 |
| `/switch.json` | 存储、日志、HJ 212、Remote DTU、MQTT 等功能开关 |
| `/tempControl.json` | 温湿度控制阈值 |
| `/alarm.json` | 告警因子、上下限和告警开关 |
| `/system.json` | 产品信息 |

`/hj212.json` 使用 `pv` 选择协议版本：

```json
{
  "ip": "39.101.67.255:8002",
  "mn": "设备MN号",
  "pw": "123456",
  "st": "31",
  "flag": "9",
  "pv": "2017",
  "timeout": 5,
  "retry_times": 3
}
```

生产设备必须修改默认 IP、MN、PW 等示例值，不应直接使用源码模板连接正式平台。

## 远程串口接口

远程 JSON 命令以 `&` 作为一条消息的结束符。当前主要操作包括：

- `get_data`：查询实时/统计数据；
- `get_config`、`set_config`：查询或修改配置文件；
- `get_records`：查询 SD 卡历史记录；
- `gal_data`：执行传感器校准；
- `dtu_command`：向指定 DTU 发送配置命令；
- OTA 相关操作：上传并更新固件。

历史小时数据查询示例：

```json
{
  "operation": "get_records",
  "param": "hour",
  "time": "20260707130000"
}&
```

范围查询示例：

```json
{
  "operation": "get_records",
  "param": "hour",
  "time": "20260707130000-20260707150000"
}&
```

`param` 支持 `real`/`raw`、`min`、`hour` 和 `day`。时间必须使用十进制 `YYYYMMDDHHMMSS`，不得将 `14` 误作为数字进制。

完整命令和响应格式见 [远程接口文档](开发文档/远程接口文档.pdf)。

## 编译与烧录

### Arduino IDE

1. 使用 Arduino IDE 打开 `TSP/TSP.ino`。
2. 安装 ESP32 Arduino Core 及工程所需库。
3. 选择 `ESP32S3 Dev Module`。
4. 设置 `FlashSize=16M`、`PartitionScheme=app3M_fat9M_16MB`、`CPUFreq=240`、`FlashMode=qio`、`PSRAM=disabled`。
5. 编译并烧录，现场端口通常为 COM29，实际以设备管理器为准。

### arduino-cli

在仓库根目录执行：

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default" --build-path ".build2" "TSP"
```

烧录更新前应备份现场配置。仅更新应用程序时，避免擦除整片 Flash，否则可能清除配置、历史或其他现场数据。

## 启动验证

烧录后至少检查以下日志和功能：

1. 出现 `Serial init success!`，各串口和任务正常启动；
2. 配置文件加载成功，传感器数量与项目一致；
3. 出现 `Starting batch polling cycle`，各因子产生有效数据；
4. HJ 212 实时包持续生成，平台返回 `CN=9014`；
5. 跨 10 分钟边界产生 `CN=2051`；
6. SD 卡能够写入历史记录；
7. 断网后生成 pending 文件，恢复网络后补传并删除已确认文件；
8. LED 动态区域 41/42 正常轮播，LCD 数据与平台数据一致；
9. 连续运行期间无 `abort()`、看门狗复位和持续堆内存下降。

## SD 历史数据导出

仓库提供 [历史数据导出脚本](tools/export_sd_history_to_excel.py)，可以从 SD 卡备份目录生成 Excel：

```powershell
# 全部数据
python tools/export_sd_history_to_excel.py D:\sdcard_backup -o output\history_all.xlsx --mode all

# 指定一天
python tools/export_sd_history_to_excel.py D:\sdcard_backup -o output\history_20260709.xlsx --mode day --day 20260709

# 日期范围
python tools/export_sd_history_to_excel.py D:\sdcard_backup -o output\history_range.xlsx --mode range --date-from 20260701 --date-to 20260709
```

## 项目目录

```text
TSP/
├─ TSP.ino                         Arduino 工程入口
├─ src/system/system/              系统初始化、任务、HJ212发送和网络恢复
├─ src/system/event/               EventBus 事件总线
├─ src/app/collectorManager/       传感器注册与采集
├─ src/app/dataManager/            实时/分钟/小时/日统计
├─ src/app/filesysManager/         历史记录与断点续传
├─ src/app/configManager/          配置文件管理
├─ src/app/dtuManager/             M100M-B2 DTU 命令与透明传输
├─ src/app/ledManager/             LED 动态数据显示
├─ src/module/pack212/             HJ212-2017/2025 报文
└─ src/module/Serial/              串口资源与互斥锁

开发文档/                         原理图、协议和传感器说明书
output/project_docs/               项目设计、开发和测试文档
output/sensor_test_docs/           单设备装配前测试规程
tools/                             历史数据导出等辅助工具
```

## 相关文档

- [项目设计文档](output/project_docs/空气微站_TSP项目设计文档_V1.0.docx)
- [项目开发文档](output/project_docs/空气微站_TSP开发文档_V1.0.docx)
- [项目测试文档](output/project_docs/空气微站_TSP测试文档_V1.0.docx)
- [RJGF 008-2021 项目测试方案](output/docs/RJGF008-2021_网格化环境空气质量监测仪_项目测试方案.docx)
- [HJ 212-2017](开发文档/hj212-2017.pdf)
- [HJ 212-2025](开发文档/hj212-2025.pdf)
- [传感器与设备说明书](开发文档/传感器说明书/)
- [装配前单设备测试文档](output/sensor_test_docs/)

## 当前注意事项

- `model_json.h` 中大气压 `a01006` 的单位目前仍写为 `dB`，正确工程单位应为 `kPa`，正式平台联调前需要统一。
- 气体采集器当前直接使用 `0x6001` 原始值，装配前必须确认模组单位和 `0x2031` 小数点配置与固件一致。
- CO 的项目模型当前使用 `ppb`，而部分认证或平台数据可能使用 `μmol/mol`/`ppm`，上线前必须确认换算规则。
- LED 控制卡采用项目专用 485 协议，不是 Modbus RTU；动态模板必须预先配置索引 41 和 42。
- M100M-B2 的供电、电平和接口以实物铭牌及原厂资料为准，不能套用相近型号参数。
- 根目录 README 作为当前项目入口；`TSP/README.md` 保留旧版 Modbus/JSON 接口记录，仅用于历史追溯。

## 开发约束

- 串口访问统一通过 `SerialManager`，共享串口必须获取对应互斥锁。
- 采集器只有在通信、范围和换算均通过后才能设置 `is_valid=true`。
- 新增因子时同步更新 `model_json.h`、采集器注册、HJ 212 映射、LED/LCD 显示和测试文档。
- 修改历史文件结构时必须考虑旧版本数据兼容和 Excel 导出脚本。
- 修改 HJ 212 字段、时间或 CN 时，应同时测试实时、10 分钟、小时、日数据和断点续传。
- 提交前至少完成编译、启动日志、传感器采集、HJ 212 抓包和断网恢复验证。

## 项目状态

项目处于持续维护和现场验证阶段。生产烧录、校准和平台参数变更应保留版本、设备 SN、操作人、日期及复测记录。
