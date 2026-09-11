# Firmware 2.0.7

日期：2026-08-13

状态：主板侧 LCD OTA 交互已实现，并通过 ESP32 Core 3.3.7 全量编译及
ESP32-S3 镜像结构校验；尚未烧录、尚未与新版 LCD 联调。默认正式回退版本仍为
2026-08-10 实机验证的 Firmware 2.0.4。

## 修改原因

Firmware 2.0.6 的单任务 OTA 已完成三轮现场测试：一次完整传输在镜像校验阶段
安全失败并恢复旧业务，一次完整传输成功、重启并恢复业务，另一次因上传请求/Ready
握手没有建立而未进入 OTA。测试同时发现，OTA 暂停业务约 13 分钟期间，旧 LCD
仍持续发送查询，恢复时出现两条受损 JSON。该问题与 Remote DTU 镜像校验失败不是
同一串口问题，根因是 LCD 接收缓冲和半包解析状态在 OTA 期间积压。

## LCD OTA 状态协议

主板新增协议版本 1 的单行 JSON 状态：

- `preparing`：准备升级，包含会话编号和固件总字节数；
- `transferring`：开始传输为 0%，之后每跨过 5% 更新一次；
- `verifying`：字节接收完成，正在执行 ESP32 镜像校验；
- `restarting`：镜像有效且启动分区切换成功，约 3 秒后重启；
- `failed`：升级失败，携带稳定的 `reason` 错误码；
- `normal`：主板正常运行，开机或失败恢复后发送两次，间隔约 500ms。

所有主板状态报文均使用固定 256 字节栈缓冲构造，采用 `\r\n` 结束，不在 OTA
过程中动态建立大 JSON 对象。LCD 状态只用于页面显示，不参与 Remote DTU 固件
分包确认。

## 准备 ACK 与兼容策略

- 主板发送 `preparing` 后最多等待 LCD ACK 2000ms；
- ACK 必须匹配协议版本、当前 `session`、`state=preparing` 和 `code=OK`；
- ACK 缺失、旧会话或格式错误只记录警告，不阻止主板 OTA；
- 进度、校验、失败和重启状态均不等待 ACK；
- LCD 未连接或仍运行旧程序时，OTA 仍可继续。

## LCD 输入处理

LCD 接收任务新增三个内部状态：

- `Normal`：正常解析并执行 LCD JSON；
- `WaitAck`：只接受当前 OTA 会话的准备 ACK，普通命令读取后丢弃；
- `Drain`：持续读取并丢弃 LCD 输入，不解析、不执行业务、不回复。

每次状态切换都会增加解析器代次，LCD 接收任务据此清除半包字符串、括号层级、
接收标志和超时状态。这样即使旧 LCD 在 OTA 期间持续发送 `get_data`，1024 字节
SoftwareSerial RX 缓冲也会被持续排空，失败恢复后不会继续解析 OTA 前留下的残包。

LCD 接收循环不再把整个接收等待过程计为活动业务；只有真正执行普通 LCD 命令时
才进入 `BusinessActivityGuard`。因此业务静默后 LCD 任务仍可排空输入。

## 串口发送互斥

`SerialManager::println()` 改为在一次 LCD/DTU 串口互斥锁内写完正文、`\r\n` 并
刷新，返回实际写入字节数。`PermissionSystem::sendMsg()` 对已知 LCD 和 DTU 串口
统一通过该接口发送，防止普通响应和 OTA 状态从不同任务同时写入而互相穿插。

## 当前主板会发送的失败码

- `business_quiesce_timeout`；
- `dtu_mutex_timeout`；
- `flash_begin_failed`；
- `flash_write_failed`；
- `data_inactivity_timeout`；
- `session_timeout`；
- `received_size_mismatch`；
- `image_validation_failed`；
- `boot_partition_failed`。

请求无效、镜像过大、分区不可用、DTU 资源不可用或已有 OTA 会话等情况发生在
LCD 会话创建之前，Firmware 2.0.7 只在主板/上传端日志中拒绝，不向 LCD 发送
`preparing` 或 `failed`。完整字段、LCD 显示映射和预留错误码见根目录
`LCD_OTA_INTERACTION_PROTOCOL.md`。

## 固定约束核对

- 小时统计算法未修改；
- HJ212 完整报文逐包间隔保持 3000ms；
- SHT30 代码未修改，继续独立处理；
- `DEBUG` 已确认保持开启；
- 2026-08-10 的采集周期漂移、秒级校时、RTC 优先启动、首次有效 CSQ 后校时、
  24 小时网络校时和 HJ212 上传优先级避让全部保留；
- Remote DTU OTA 仍采用 800 字节分包、1000ms 发送间隔和现有 Ready 握手。

## 2026-08-13 完整编译

- ESP32 Core：3.3.7；
- 分区：16MB Flash，3M APP / 9M FATFS；
- Sketch：626916 字节，占程序空间 19%；
- 全局变量：26800 字节，占动态内存 8%，启动前静态余量 300880 字节；
- 应用 BIN：627072 字节；
- 应用 BIN SHA-256：
  `4F37E77B9F52B0A154CD2F94B20ABB71E1C55D3EB723D9911DB1DECD4B30BB86`；
- 源码输入指纹：
  `AF23FBD1DE343752459A6144794599A428C499AC13E5CEAE6316E545E11E1596`；
- esptool 5.1.0 识别为 ESP32-S3、16MB，镜像 checksum 和 validation hash 均有效；
- 构建状态：`compiled-not-hardware-verified`；
- 构建目录：
  `D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\current`。

Codex 本次没有执行 MCU 烧录。首次实机联调应分别覆盖新版 LCD 正常 ACK、LCD
不回复 ACK、旧 LCD 持续发送、传输中断恢复、镜像校验失败恢复和成功 OTA 重启。

