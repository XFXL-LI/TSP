# Firmware 2.0.9

日期：2026-08-14

状态：Remote DTU `upload_status` JSON协议版本1已实现，并通过ESP32 Core 3.3.7
完整编译和ESP32-S3镜像结构校验；尚未烧录、尚未与新版发送端实机联调。默认正式
回退版本仍为2026-08-10实机验证的Firmware 2.0.4，Firmware 2.0.8继续作为LCD
normal ACK已通过实机联调的基线。

## 修改原因

Firmware 2.0.8及更早版本通过英文`Ready`、逐次写入字节进度和英文最终结果控制
Remote DTU OTA。现场曾出现发送端在MCU真正Ready之前发送、文件偏移未重置为0，
以及MCU失败后发送端仍继续发送剩余BIN的问题。旧文本无法稳定表达会话、早期拒绝
原因和明确的停止状态。

Firmware 2.0.9按根目录`REMOTE_DTU_OTA_SENDER_PROTOCOL.md`实现机器可读的
`upload_status` JSON，并保留显式legacy兼容路径。

## 请求与兼容模式

### protocol-v1

发送端请求：

```json
{"operation":"upload","protocol":1,"size":630864}
```

只有请求明确包含整数`protocol=1`时进入新协议。MCU只发送`upload_status` JSON，
不混入旧英文Ready、逐包字节进度或英文成功/失败文本。

### legacy

旧发送端请求：

```json
{"operation":"upload","size":630864}
```

请求不包含`protocol`时保持Firmware 2.0.8旧行为。登录方式、英文Ready、800字节
分包建议、1000ms间隔、逐次写入字节进度和英文最终结果不变。

同一次升级只会选择一种模式，不会由英文和JSON同时触发发送。

## protocol-v1状态

- 请求通过格式、协议、大小、OTA分区和DTU资源预检后，分配非零`session`并发送
  `accepted`；
- MCU完成LCD准备、业务安全静默、DTU互斥锁和`esp_ota_begin()`后发送`ready`；
- `ready`包含`chunk_size=800`、`interval_ms=1000`和
  `inactivity_timeout_ms=90000`；
- 只有`ready`允许发送端从BIN偏移0开始发送；
- MCU先发送一次`transferring,progress=0`，之后按首次跨过5%阈值发送总体进度；
- 完整接收声明字节后先发送`verifying`，再执行`esp_ota_end()`镜像校验；
- 设置启动分区成功后发送`success`和`restarting_in_ms=3000`，随后重启；
- 已建立会话失败发送`failed`，包含稳定`reason`、`written`、`total`和`progress`；
- 会话建立前拒绝发送`rejected,session=0`和稳定`reason`。

首批拒绝和失败原因覆盖：

`unsupported_protocol`、`invalid_size`、`session_busy`、
`partition_unavailable`、`image_too_large`、`dtu_unavailable`、
`business_quiesce_timeout`、`dtu_mutex_timeout`、`flash_begin_failed`、
`flash_write_failed`、`data_inactivity_timeout`、`session_timeout`、
`received_size_mismatch`、`image_validation_failed`和`boot_partition_failed`。

## 实现约束

- Remote DTU状态使用固定256字节栈缓冲和`snprintf()`，没有为JSON新增动态大型缓冲；
- 没有新增FreeRTOS任务；继续使用开机预创建的唯一`OtaUploadTask`；
- Remote会话计数与LCD OTA会话相互独立，非零会话只在预检通过后创建；
- `protocol`和`size`必须为整数；缺失、字符串、负数、0或小数不会被截断误接受；
- 协议JSON统一以ASCII字段和值及`\r\n`结尾发送；
- protocol-v1不再按每个Flash写入返回文本，只按5%输出进度，降低DTU控制流量；
- LCD `preparing/transferring/verifying/restarting/failed/normal`与normal ACK机制保持；
- 小时统计逻辑未修改；
- HJ212完整报文逐包间隔保持3000ms；
- SHT30继续独立处理；
- `DEBUG`保持开启。

## 2026-08-14编译验证

- ESP32 Core：3.3.7；
- Sketch：630712字节，占程序空间20%；
- 全局变量：26816字节，占动态内存8%，启动前静态余量300864字节；
- 应用BIN：630864字节；
- 应用BIN SHA-256：
  `F202BA3A5784B936F2D089410BED1AA019276F36CFFEB6A3616AF16B2F3B6849`；
- 源码输入指纹：
  `3E7ED8C9EA903B3292C8E8E8189CBCECADEB0AAC97C6474F3FEAC48958CFCD40`；
- esptool 5.1.0识别为ESP32-S3、16MB，checksum和validation hash有效；
- 构建状态：`compiled-not-hardware-verified`；
- 构建目录：
  `D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.9-upload-status`。

Codex没有执行MCU烧录。

## 首次联调要求

1. 新发送端使用`protocol-v1`，发送630864字节应用BIN的实际长度，不写死示例；
2. 确认先收到非零会话`accepted`，此时没有任何BIN字节；
3. 等待同一会话`ready`后重新检查长度、首字节`0xE9`并从偏移0发送；
4. 检查单包不超过800字节、相邻包开始时间不少于1000ms；
5. 检查MCU按5%发送`transferring`，`verifying`后发送端立即停止文件定时器；
6. 成功路径验证`success`、3秒重启、Firmware 2.0.9启动、LCD normal ACK和业务恢复；
7. 分别验证unsupported protocol、invalid size、image too large和已有会话拒绝；
8. 分别验证数据中断、镜像校验失败和Flash失败后`failed`使发送端立即停止；
9. 使用不带`protocol`的旧发送端回归legacy路径，确认旧英文行为仍可使用；
10. 保存完整MCU/发送端串口日志、所用BIN大小和SHA-256。

当前协议仍不包含分包序号、单包CRC/ACK/重传、断点续传、签名、防降级和自动回滚；
这些能力不得由发送端自行扩展，后续需要独立升级协议。
