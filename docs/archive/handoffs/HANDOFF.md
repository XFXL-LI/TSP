# TSP 项目交接记录

更新时间：2026-07-24

## 当前目标

定位 ESP32-S3 项目在长期运行时出现的数据缺少和重复问题，重点区分：

- 传感器或 RS485 采集缺失
- MCU 内部事件队列丢弃
- LCD、HJ212 软件串口接收溢出
- SD 卡存储失败
- DTU/HJ212 上传失败
- 数据已经送达但 ACK 丢失，触发重试并产生重复数据

当前阶段只增加诊断能力，不切换回旧版 SoftwareSerial，也暂不调整业务队列、重试和存储策略。

## 已确认的开发环境

- 主控：ESP32-S3
- Arduino ESP32 Core：3.3.7
- EspSoftwareSerial：8.1.0
- Ds1302：1.1.0
- modbus-esp8266：4.1.0
- ClosedCube SHT31D：1.5.1
- Flash Size：16MB
- Partition Scheme：3M APP / 9M FATFS / 16MB
- PSRAM：Disabled
- CPU Frequency：240MHz
- 调试串口波特率：9600
- 当前开发板上传端口：COM5

Arduino FQBN 配置：

```text
esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default
```

## 串口分配

| 功能 | 类型 | RX | TX | 波特率 |
| --- | --- | ---: | ---: | ---: |
| LCD | 软件串口 | GPIO15 | GPIO16 | 9600 |
| RS485 | 软件串口 | GPIO4 | GPIO5 | 9600 |
| HJ212 DTU | 软件串口 | GPIO12 | GPIO13 | 9600 |
| LED 灯珠屏 | 软件串口，只发送 | 不使用 | GPIO0 | 9600 |
| TTL | 硬件 Serial1 | GPIO9 | GPIO10 | 9600 |
| Remote DTU | 硬件 Serial2 | GPIO38 | GPIO37 | 115200 |
| USB 调试 | Serial | USB | USB | 9600 |

LED 接口不需要接收回复，因此已将 `LED_RXD` 改为 `-1`，同时移除了启动时向 LED 屏发送的初始化文字。

## 本次诊断改动

每批采集数据会生成一个只在本次开机周期内有效的 `trace_id`，用于串联以下日志：

```text
[DIAG] COLLECT trace=...
[DIAG] PROCESS trace=...
[DIAG] STORE trace=...
[DIAG] TX_ATTEMPT trace=...
[DIAG] TX_RESULT trace=...
```

另外新增：

```text
[DIAG] EVENT_DROP ...
[DIAG] UART_OVERFLOW port=LCD ...
[DIAG] UART_OVERFLOW port=485 ...
[DIAG] UART_OVERFLOW port=HJ212 ...
```

诊断结果解释：

- `COLLECT` 中传感器数量不足：优先检查传感器或 RS485。
- 有 `COLLECT`，没有对应 `PROCESS`：检查事件队列和数据处理任务。
- `EVENT_DROP`：MCU 内部队列已满，代码确实清除了旧消息。
- `STORE ok=0` 或 `skipped=sd_not_ready`：本地存储链路失败。
- `UART_OVERFLOW`：对应的软件串口接收缓冲区发生溢出。
- `TX_RESULT ack=0`：没有收到有效的 HJ212 `CN=9014` ACK。
- 服务端收到数据，但 MCU 记录 `ack=0` 并重试：重复数据可能由 ACK 丢失造成。

`DataTime` 数值：

- 0：实时数据
- 1：分钟数据
- 2：小时数据
- 3：日数据

## 已发现但尚未修复的风险

1. `EventBus::publish()` 在订阅者队列满时会清除队列中的全部旧事件，只保留最新事件，这会造成确定的数据丢失。
2. 被队列清除的引用计数消息没有统一释放机制，长期高压运行可能产生内存泄漏。
3. LCD 任务收到完整 JSON 后会同步等待业务处理；等待期间无法持续读取 LCD 软件串口，可能溢出。
4. HJ212 发送队列深度为 10，发送任务每次处理后延迟 15 秒；DTU异常或突发数据会造成积压。
5. HJ212 数据已经送达但 `CN=9014` ACK 丢失时，当前逻辑会重发同一数据，服务端需要按 MN、CN、QN、DataTime 等字段去重。
6. 文件存储使用追加写入，没有业务级去重。
7. 部分请求/响应仅按 EventID 匹配；LCD 与 DTU 同时发出同类命令时存在响应串线风险。

这些问题在第一轮压力测试取得证据之前不要同时修改，以免无法确定真正原因。

## 编译验证

已使用 Arduino IDE 自带的 Arduino CLI 编译通过：

```text
Sketch uses 616168 bytes (19%) of program storage space. Maximum is 3145728 bytes.
Global variables use 25896 bytes (7%) of dynamic memory, leaving 301784 bytes for local variables.
Maximum is 327680 bytes.
```

ESP32 链接器不能可靠地在包含中文的构建输出路径中生成 ELF。命令行验证时使用了纯英文临时目录：

```text
C:\Users\XFXL\AppData\Local\Temp\TSP_diag_build
```

Arduino IDE 正常编译时如果再次遇到路径错误，应将项目或构建缓存放到纯英文路径。

## 公司电脑同步步骤

公司电脑原先安装的是旧版 SoftwareSerial。Arduino 全局库不会随 Git 仓库同步，拉取源码后需要单独安装 `EspSoftwareSerial 8.1.0`。

先保护公司电脑上的旧代码：

```powershell
git status
git switch -c backup/company-legacy
git add -A
git commit -m "Backup company version with legacy SoftwareSerial"
```

再同步当前版本：

```powershell
git switch main
git pull --ff-only origin main
```

如果公司目录还不是 Git 仓库，先将原目录改名备份，再执行：

```powershell
git clone https://github.com/XFXL-LI/TSP.git
```

旧库不要和 `EspSoftwareSerial 8.1.0` 同时放在 Arduino 的全局 libraries 目录中，否则 Arduino 可能选择错误的头文件。

## 建议的压力测试顺序

1. 烧录后保持正常采集至少 1 至 2 小时。
2. 保存 USB 调试串口中全部 `[DIAG]` 日志。
3. 连续操作 LCD，观察是否出现 `UART_OVERFLOW port=LCD`。
4. 模拟 HJ212 网络短时中断后恢复，观察发送重试、待发送文件和服务端数据。
5. 对照相同 `trace_id` 的采集、处理、存储和上传日志。
6. 对照服务端 QN/DataTime，确认重复发生在首次发送还是重试阶段。
7. 完成第一轮测试后，再分别修改队列策略、LCD异步处理或HJ212重试策略。

## 新 Codex 会话提示词

```text
请继续处理 ESP32-S3 的 TSP 项目。

请先读取 README.md、HANDOFF.md、git log 和当前 git status。
当前使用 ESP32 Core 3.3.7、EspSoftwareSerial 8.1.0、
16MB Flash、3M APP / 9M FATFS，调试串口为 9600。

当前目标是通过 [DIAG] COLLECT、PROCESS、STORE、TX_RESULT、
EVENT_DROP 和 UART_OVERFLOW 日志定位数据缺失与重复。
公司电脑原先使用旧版 SoftwareSerial，请不要直接切换库；
先核对源码分支、Arduino 库版本和 HANDOFF.md 中的风险清单。
```
