# TSP Remote DTU OTA 发送端开发任务交接

交接日期：2026-08-14  
任务状态：Firmware 2.0.9 MCU协议已实现并编译通过；发送端可开始对接，待实机联调  
协议版本：1  

## 1. 任务目标

为 TSP ESP32-S3 主板实现新的 Remote DTU OTA 发送端，使发送端只在 MCU 明确
返回 `state=ready` 后发送原始应用 BIN，并能根据会话状态安全停止、记录结果和
等待重启。

正式协议以以下文档为准：

`D:\ChatGPT-Pro\TSP-ESP32-S3\REMOTE_DTU_OTA_SENDER_PROTOCOL.md`

LCD 屏幕协议仅供了解整体流程：

`D:\ChatGPT-Pro\TSP-ESP32-S3\LCD_OTA_INTERACTION_PROTOCOL.md`

发送端不得等待或解析 LCD 的 `ota_status`；发送端只处理 Remote DTU 串口上的
`upload` 和 `upload_status`。

## 2. 当前版本边界

### 2.1 Firmware 2.0.8 当前实际行为

- 已完成 627872 字节远程 OTA、镜像校验、重启和业务恢复实机验证；
- LCD `preparing` ACK、`normal` 2 秒重发、normal ACK 和约 3 秒普通查询恢复已通过；
- MCU 对 Remote DTU 仍发送旧英文 `Ready`、字节进度和英文成功/失败结果；
- MCU 2.0.8 尚未发送本文要求的 `upload_status` JSON。

因此，新 JSON 模式不能直接以 Firmware 2.0.8 作为正式联调依据。

### 2.2 Firmware 2.0.9 当前实现

MCU已在Firmware 2.0.9中实现`upload_status` JSON，并通过ESP32 Core 3.3.7
完整编译和esptool镜像校验，尚未烧录或实机联调：

- 应用BIN：630864字节；
- SHA-256：
  `F202BA3A5784B936F2D089410BED1AA019276F36CFFEB6A3616AF16B2F3B6849`；
- 源码输入指纹：
  `3E7ED8C9EA903B3292C8E8E8189CBCECADEB0AAC97C6474F3FEAC48958CFCD40`；
- 构建目录：
  `D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.9-upload-status`。

发送端现在可以按本文完成开发、模拟测试和串口对接准备。MCU实机烧录仍需用户明确
授权，不能把“已编译”写成“已实机通过”。

### 2.3 兼容模式

发送端应提供两个明确、互斥的工作模式：

| 模式 | 适用固件 | 启动 BIN 的依据 |
| --- | --- | --- |
| `legacy` | Firmware 2.0.9及更早兼容版本 | 现有英文Ready协议 |
| `protocol-v1` | Firmware 2.0.9及后续兼容版本 | 当前会话的 `state=ready` JSON |

同一次升级中禁止同时启用两种触发条件，避免重复启动文件发送。

## 3. 发送端必须实现的流程

### 3.1 文件预检

在发送升级请求前检查：

1. 文件存在且可读；
2. 文件长度大于 0；
3. 文件为 ESP32 应用 BIN；
4. 首字节为 `0xE9`；
5. 不接受 merged BIN、bootloader、partitions、boot_app0 或压缩包；
6. 文件长度必须实时读取，不得写死 627072、627872 或其他示例值；
7. 保存文件名、实际长度和本地 SHA-256 到任务日志。

### 3.2 登录

沿用现有登录流程：

```json
{"operation":"login","user":"zhouwei","pass":"123456"}
```

收到登录成功响应后，发送端才可以发起升级请求。账号和密码由现场配置提供，示例值
不得写死到正式程序中。

### 3.3 发起请求

`protocol-v1` 模式发送：

```json
{"operation":"upload","protocol":1,"size":630864}
```

其中`630864`只是当前Firmware 2.0.9编译应用BIN的示例大小。正式程序必须填入
用户实际选择文件的长度。

请求发送完成后必须保持等待状态，严禁立即发送任何 BIN 字节。

### 3.4 等待 MCU 状态

MCU 状态统一为单行 JSON：

```json
{"operation":"upload_status","protocol":1,"session":1,"state":"accepted","size":630864}
```

```json
{"operation":"upload_status","protocol":1,"session":1,"state":"ready","size":630864,"chunk_size":800,"interval_ms":1000,"inactivity_timeout_ms":90000}
```

只有当前非零 `session` 的 `state=ready` 允许开始发送 BIN。

以下条件均不能代替 `ready`：

- 登录成功；
- `upload` 请求已成功写入串口；
- 收到 `accepted`；
- LCD 已回复准备 ACK；
- 等待了固定的 2 秒、5 秒或其他时间；
- 收到旧会话的 `ready`。

### 3.5 发送 BIN

收到当前会话的 `ready` 后重新执行：

1. 核对返回的 `size` 等于当前文件实际长度；
2. 将文件读取偏移重置为 0；
3. 再次核对首字节为 `0xE9`；
4. 读取 MCU 返回的 `chunk_size` 和 `interval_ms`；
5. 从偏移 0 开始发送原始 BIN。

当前预计参数为：

- 每包最多 800 字节；
- 相邻包发送开始时间至少间隔 1000ms；
- 最后一包只发送实际剩余字节，不补零；
- 不添加包头、包尾、换行符、JSON、十六进制文本或 Base64；
- 发送 BIN 期间不得夹入登录、查询或其他控制 JSON。

发送端至少保存：

- 当前 `session`；
- 文件总长度；
- 下一次文件读取偏移；
- 已发送字节数；
- MCU 最后确认的 `written`；
- 最后一条 MCU 状态和接收时间；
- 文件发送定时器或异步任务句柄。

## 4. 状态机要求

```text
IDLE
  │ 发送 upload
  ▼
WAIT_ACCEPTED
  │ accepted
  ▼
WAIT_READY ───────────── rejected ──► STOPPED
  │ ready
  ▼
SENDING_BIN
  │ transferring：只更新总体进度
  │ verifying：立即停止文件发送
  ▼
VERIFYING
  ├── success ────────────► WAIT_RESTART
  └── failed ─────────────► STOPPED
```

各状态动作：

| `state` | 发送端动作 |
| --- | --- |
| `accepted` | 保存会话并继续等待；禁止发送 BIN |
| `ready` | 完成文件复检后，从偏移 0 开始发送 |
| `transferring` | 更新 MCU 总体写入进度；继续按规定节奏发送 |
| `verifying` | 立即关闭发送定时器，取消所有未发数据块 |
| `success` | 标记成功、保存日志、停止发送并等待 MCU 重启 |
| `failed` | 立即停止发送，不在旧会话续传 |
| `rejected` | 不发送 BIN，显示并保存拒绝原因 |

任意状态收到当前会话的 `failed`，都必须进入 `STOPPED`。非零旧会话的状态不得改变
当前会话。遇到未知 `state` 时必须停止自动推进并保留原始报文。

## 5. 串口接收与JSON分帧

Remote DTU 控制报文要求：

- 波特率：115200；
- 数据格式：8N1；
- 编码：UTF-8/ASCII；
- 单行紧凑 JSON；
- 行尾：`\r\n`；
- 建议单行不超过 256 字节。

发送端必须使用增量接收缓冲并按 `\r\n` 提取完整控制行。不得假设一次串口读取恰好
得到一条完整 JSON，也不得假设一次读取只有一条 JSON。

解析失败的单行必须记录原始字节或安全转义后的内容，然后丢弃该坏行并继续处理下一
行，不能让一条缺引号、截断或乱码报文永久污染后续控制状态。

## 6. 进度和终止规则

MCU 的 `transferring` 示例：

```json
{"operation":"upload_status","protocol":1,"session":1,"state":"transferring","written":315432,"total":630864,"progress":50}
```

该进度是 MCU 已写入 OTA 分区的总体进度，不是单包 ACK。当前协议不支持根据该进度
重传某一个丢失分包。

收到以下状态时必须取消尚未发出的全部文件块：

- `verifying`；
- `success`；
- `failed`；
- `rejected`；
- 未知 `state`；
- 当前串口连接中断或控制状态无法确认。

禁止在失败后继续发送剩余 BIN，禁止在旧 `session` 上续传。重新升级必须建立新
会话，并从文件偏移 0 开始。

## 7. 超时要求

| 阶段 | 发送端要求 |
| --- | --- |
| 等待 `accepted` | 建议 5 秒；超时提示无响应，不发送 BIN |
| 等待 `ready` | 至少 100 秒；MCU 业务静默上限为 90 秒 |
| 分包发送 | 使用 MCU 返回的 `interval_ms` |
| MCU 数据无活动超时 | 读取 MCU 返回的 `inactivity_timeout_ms` |
| 收到 `verifying` 后等待结果 | 建议 30 秒 |
| 等待重连 | 由现场链路决定；不得复用旧会话 |

控制 JSON 丢失或串口断开时，不得猜测 MCU 已经 ready 并继续发送。

## 8. 错误处理

发送端必须原样显示并保存 MCU 返回的 `reason`。首批需要支持：

- `invalid_request`；
- `unsupported_protocol`；
- `invalid_size`；
- `session_busy`；
- `partition_unavailable`；
- `image_too_large`；
- `dtu_unavailable`；
- `business_quiesce_timeout`；
- `dtu_mutex_timeout`；
- `flash_begin_failed`；
- `flash_write_failed`；
- `data_inactivity_timeout`；
- `session_timeout`；
- `received_size_mismatch`；
- `image_validation_failed`；
- `boot_partition_failed`；
- `internal_error`。

未知 `reason` 不得替换为空字符串或统一改写，必须显示并保存原值。

## 9. 发送端验收用例

### 9.1 文件预检

- 正确应用 BIN 可以进入待发送状态；
- 空文件、压缩包、merged BIN、bootloader、partitions 和首字节非 `0xE9` 文件被拒绝；
- 文件大小和 SHA-256 正确记录；
- 示例大小没有被写死。

### 9.2 Ready 门禁

- `accepted` 后不出现任何 BIN 字节；
- 等待 30～90 秒仍不会因固定延迟提前发送；
- 只有当前会话的 `ready` 触发发送；
- 旧会话和未知会话的 `ready` 被忽略并记录。

### 9.3 正常传输

- ready 后从偏移 0 开始，首字节为 `0xE9`；
- 单包不超过 MCU 返回的 `chunk_size`；
- 相邻包间隔不小于 `interval_ms`；
- 最后一包不补零；
- 进度按 MCU 状态更新，不作为单包 ACK；
- `verifying` 后不再发送任何 BIN 字节；
- `success` 后保存最终会话记录并等待重启。

### 9.4 失败立即停止

- 传输中收到 `failed` 后立即取消发送队列和定时器；
- 失败后串口不再出现剩余 BIN；
- 新重试使用新 `session`，并从偏移 0 开始；
- `rejected` 时从未发送 BIN。

### 9.5 串口健壮性

- 一条 JSON 被拆成多次接收仍能正确解析；
- 一次接收包含多条 JSON 时逐条解析；
- 缺引号、缺括号、乱码或截断行被丢弃，下一条正确 JSON 仍能解析；
- 未知字段被忽略；
- 未知 `state` 停止自动推进；
- 串口断开后停止所有自动发送。

### 9.6 兼容模式隔离

- `legacy` 模式只响应旧英文协议；
- `protocol-v1` 模式只响应当前会话的 JSON `ready`；
- 两种模式不能同时触发同一个文件发送任务。

## 10. 建议的开发阶段

1. 完成文件预检、SHA-256和首字节检查；
2. 完成 `\r\n` 增量接收和 JSON 状态解析；
3. 完成会话隔离和状态机；
4. 使用模拟 MCU 报文验证 accepted、ready、进度、成功、失败和未知状态；
5. 完成文件发送调度、立即停止和连接中断处理；
6. 增加 `legacy` / `protocol-v1` 显式模式选择；
7. 使用Firmware 2.0.9构建进行串口实机联调（MCU烧录需用户明确授权）；
8. 保存完整联调日志和所用 BIN 的大小、SHA-256。

## 11. 发送端交付物

发送端负责人完成后请提供：

- 源码分支或可审查的修改包；
- 可运行测试版本；
- 状态机说明；
- 文件预检结果截图或日志；
- 模拟协议测试报告；
- 实机联调原始串口日志；
- 测试 BIN 文件名、大小和 SHA-256；
- 已知限制和未完成项。

## 12. 当前不在本任务范围内

- LCD 页面和 `ota_status` 处理；
- 修改 MCU 2.0.8；
- 分包序号、单包 CRC 和单包重传；
- 断点续传；
- 固件签名和防降级；
- 新固件健康检查和自动回滚；
- 将 merged BIN 或压缩包作为 OTA 输入。

这些能力如后续增加，必须升级协议并进行独立联调，不能由发送端自行扩展当前协议。

## 13. 当前结论

LCD OTA交互和normal恢复前置条件已经实机通过，Firmware 2.0.9 MCU协议也已实现并
编译通过。Remote DTU发送端任务可以正式开始，双方完成模拟测试后进入新JSON端到端
实机联调；在明确授权烧录和取得完整日志前，2.0.9状态仍是未硬件验证。
