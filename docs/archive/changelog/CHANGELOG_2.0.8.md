# Firmware 2.0.8

日期：2026-08-14

状态：主板侧 LCD `normal` 恢复确认机制已实现，并通过 ESP32 Core 3.3.7
完整编译和 ESP32-S3 镜像结构校验；尚未烧录、尚未与 LCD V1.0.12 实机联调。
默认正式回退版本仍为 2026-08-10 实机验证的 Firmware 2.0.4。

## 修改原因

Firmware 2.0.7 已完成一次完整远程 OTA 并成功启动，但主板重启后仅以 500ms
间隔发送两次 `normal`。LCD 没有立即退出升级页，约 25 分钟后才由屏幕本地
超时机制恢复。日志只能证明主板写出了两帧，不能确认 LCD 是否收到或处理。

LCD V1.0.12 新增以下恢复确认报文：

```json
{"operation":"ota_status_ack","protocol":1,"session":0,"state":"normal","code":"OK"}
```

## MCU 修改

- 主业务初始化完成后立即发送第一条 `session=0,state=normal`；
- 未收到 ACK 时每 2000ms 重发一次；
- 从第一条开始计时，达到 30000ms 时停止，不在 30000ms 边界再发送；
- 收到合法 ACK 后立即停止，并记录
  `[DIAG] LCD_OTA_NORMAL_ACK result=ok`；
- 重复 ACK 只记录 DEBUG，不重复切换状态；
- 非法协议、非零 session 或非 OK ACK 被消费并记录为 `rejected`；
- 30 秒无 ACK 记录
  `[DIAG] LCD_OTA_NORMAL_ACK result=timeout elapsed_ms=...`，不判定为 OTA 失败，
  不阻断采集、存储、HJ212 或普通 LCD 业务；
- OTA 失败后的 `failed → normal` 恢复与重启启动使用同一套 ACK/重试机制；
- 新 OTA 会话开始时取消仍在进行的 `normal` 重试，避免准备升级后误发恢复状态。

重试由现有 LCD 接收任务非阻塞调度，没有新增 FreeRTOS 任务、软件定时器或动态
缓冲；普通 LCD JSON 仍按原流程处理。LCD `ota_status_ack` 在业务命令之前识别，
不会进入普通 Permission 命令分发。

## 固定约束

- 小时统计算法未修改；
- HJ212 完整报文逐包间隔保持 3000ms；
- SHT30 代码未修改，继续独立处理；
- `DEBUG` 保持开启；
- 2026-08-10 的采集周期漂移、RTC/DTU 秒级校时、RTC 优先启动、首次有效 CSQ
  后校时、24 小时空闲网络校时和 HJ212 上传优先级避让全部保留；
- Remote DTU OTA 的固件接收、Ready、800 字节分包和 1000ms 间隔未修改。

## 2026-08-14 编译验证

- ESP32 Core：3.3.7；
- Sketch：627720 字节，占程序空间 19%；
- 全局变量：26808 字节，占动态内存 8%，启动前静态余量 300872 字节；
- 应用 BIN：627872 字节；
- 应用 BIN SHA-256：
  `90E066D25798BBB76752F74E156CAF18E2EE4F58A441D31D9343FD7020FD8DB1`；
- 源码输入指纹：
  `AEB27CA06DEF242E68B1C191F0D8A8CC75CA630AC64D6C9512AED8717396EE7D`；
- esptool 5.1.0：ESP32-S3、16MB，checksum 和 validation hash 均有效；
- 构建状态：`compiled-not-hardware-verified`；
- 构建目录：
  `D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.8-normal-ack`。

Codex 未执行 MCU 烧录。首次实机联调必须覆盖首帧成功、首帧丢失、ACK 丢失、
重复 ACK、30 秒无 ACK、LCD 复位、成功 OTA 重启和 OTA 失败恢复。
