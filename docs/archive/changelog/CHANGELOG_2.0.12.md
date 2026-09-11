# Firmware 2.0.12

日期：2026-08-19

状态：撤销 Firmware 2.0.11 不安全的 LCD 2048 字节 EspSoftwareSerial 缓冲修改，
恢复 Firmware 2.0.10 相同的 1024 字节软件串口配置；保留 14 因子配置所需的 JSON
内存峰值修复和明确响应。已通过 ESP32 Core 3.3.7 完整编译与 ESP32-S3 镜像校验，
尚未烧录验证。Codex 未执行烧录。

## 2.0.11 实机结论

用户烧录 2.0.11 后设备启动即重启，该版本判定为不可用，禁止继续烧录。原因是项目
使用的 EspSoftwareSerial 在未显式给定 ISR 容量时，会按字节缓冲容量乘约 10 自动
创建 `uint32_t` ISR 边沿缓冲：

- 1024 字节RX缓冲对应约 40KB ISR缓冲；
- 2048 字节RX缓冲对应约 80KB ISR缓冲；
- 2.0.11 单个LCD端口因此额外长期占用约 41984 字节动态堆，并要求约 80KB连续
  分配，存在启动期分配失败风险；
- 编译输出中的“全局变量 26816 字节”不包含这种开机动态分配。

2.0.11 的构建目录和清单已明确标记为 `rejected-hardware-boot-loop`，不得使用其中BIN。

## 2.0.12 修改

- `SerialManager::SoftwarePortInit()`恢复为
  `sw->begin(..., false, 1024)`，与2.0.10调用方式及动态缓冲规模一致；
- 不使用2048字节LCD软件串口缓冲，也不增加任何其他软件串口缓冲；
- 保留权限层取得operation后、转交业务前执行`json.clear()`，ConfigManager二次
  解析时不再同时保留第一棵完整cJSON树；
- 保留`set_config`成功/失败即时响应，失败不再无响应等待`Response timeout`；
- 保留请求长度、cJSON错误偏移、空闲堆、最大连续堆块和UART overflow诊断；
- 源代码默认10因子紧凑请求约631字节，14因子约849字节，均处于原1024字节
  接收容量范围内；LCD任务也会在接收过程中持续读取缓冲。

## 保持不变

- 2.0.10的K-7S采集、状态判定、ppb基准值和四种单位换算不变；
- sensors继续使用原有`unit`字段，不增加`displayUnit`；
- 校准逻辑未修改，CO不钳制到1ppm；
- 小时统计未修改；HJ212完整报文逐包间隔保持3000ms；
- SHT30独立处理；`DEBUG`保持开启；两套OTA协议未修改。

## 编译与镜像验证

- ESP32 Core：3.3.7；
- 分区：16MB Flash，3M APP / 9M FATFS；
- Sketch：637976字节，占程序空间20%；
- 全局变量：26816字节，占动态内存8%，启动前静态余量300864字节；
- 应用BIN：638128字节；
- 应用BIN SHA-256：
  `C061BC1485809BEE658B7DCF956BF0BDBAEA522D3A6A7B216C59488CE5517CF9`；
- 源码输入指纹：
  `BC8650DD281604D7110ED9A569E21DD3C055101E21A5DE23FF6FD9C34045CAC6`；
- esptool 5.1.0识别为ESP32-S3、16MB，checksum和validation hash均有效；
- 构建状态：`compiled-not-hardware-verified`；
- 构建目录：
  `D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.12-lcd-config-memory-safe`。

## 首次实机验证

1. 启动后先确认版本为2.0.12且不再发生启动重启循环。
2. 记录初始化完成后的空闲堆和最大连续堆块，与2.0.10运行日志比较。
3. LCD下发完整14因子`sensors`配置，确认收到
   `operation=set_config,code=OK,config=sensors`。
4. 执行`get_config/sensors`确认14项；随后重启，确认加载14项并运行14个采集器。
5. 若失败，保留`Invalid set_config JSON`、`UART_OVERFLOW`、reset reason和完整重启
   前后日志。
