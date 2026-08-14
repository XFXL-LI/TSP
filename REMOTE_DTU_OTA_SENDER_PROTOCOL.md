# TSP Remote DTU OTA 发送端交互协议

协议版本：1.0
制定日期：2026-08-14
适用设备：TSP ESP32-S3 主板与 Remote DTU OTA 发送端
当前状态：协议已确定；发送端可以开始开发，MCU 计划在 Firmware 2.0.9 单独实现

> 重要：Firmware 2.0.8 及更早版本仍向 Remote DTU 发送英文文本 `Ready`、字节进度和
> 成功/失败结果，尚未发送本文定义的 `upload_status` JSON。发送端只有在安装了
> 实现本文协议的新 MCU 固件后，才能以本文 JSON 作为正式联调依据。计划中的首个
> MCU JSON 协议版本为 Firmware 2.0.9。

## 1. 目标

本文规定 Remote DTU OTA 发送端与 TSP ESP32-S3 主板之间的升级控制协议，解决：

- 发送端在 MCU 尚未准备好时提前发送 BIN；
- 使用固定延迟代替真实 `Ready` 握手；
- BIN 未从第 0 字节开始发送；
- 失败后发送端仍继续发送剩余 BIN；
- 英文文本难以稳定解析、无法表达早期拒绝原因；
- 升级进度、校验和最终结果缺少统一机器可读格式。

控制消息统一为单行 JSON。固件内容仍使用原始 ESP32 应用 BIN，不使用 JSON、
Base64、压缩包或 merged BIN 封装。

## 2. 与 LCD OTA 协议的关系

Remote DTU 发送端和 LCD 使用不同串口、不同操作名和不同业务目的：

| 对象 | 操作名 | 用途 |
| --- | --- | --- |
| Remote DTU OTA 发送端 | `upload`、`upload_status` | 控制固件传输和判断升级结果 |
| LCD 屏幕 | `ota_status`、`ota_status_ack` | 显示升级页面和停止普通查询 |

发送端不得等待或解析 LCD 的 `ota_status`。LCD 也不参与固件分包传输。

LCD 协议另见：

`D:\ChatGPT-Pro\TSP-ESP32-S3\LCD_OTA_INTERACTION_PROTOCOL.md`

## 3. 串口和报文格式

| 项目 | 规定 |
| --- | --- |
| 接口 | MCU Remote DTU 专用串口 |
| 波特率 | 115200 |
| 数据格式 | 8N1 |
| 控制报文 | 单行紧凑 JSON |
| 文本编码 | UTF-8/ASCII；协议字段和值只使用 ASCII |
| 控制报文结尾 | `\r\n` |
| 建议最大长度 | 不超过 256 字节 |
| 固件数据 | 原始二进制字节流 |

控制 JSON 前后不得附加 BOM、时间标签、调试文字或十六进制头。

发送端必须使用增量接收缓冲，按 `\r\n` 拆分完整控制行。不得假设一次串口接收
调用一定能取得一条完整 JSON，也不得假设一次接收只包含一条 JSON。

## 4. 公共字段

MCU 发给发送端的状态统一使用 `upload_status`：

```json
{"operation":"upload_status","protocol":1,"session":1,"state":"ready"}
```

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `operation` | 字符串 | 是 | MCU 状态固定为 `upload_status` |
| `protocol` | 整数 | 是 | 当前固定为 `1` |
| `session` | 无符号整数 | 是 | 本次 OTA 会话编号；非零会话内保持不变 |
| `state` | 字符串 | 是 | 状态，见第 6 节 |
| `size` | 无符号整数 | 按状态 | 请求声明或 MCU 接受的固件总字节数 |
| `written` | 无符号整数 | 传输后必填 | MCU 已成功写入 OTA 分区的字节数 |
| `total` | 无符号整数 | 传输后必填 | 本次 OTA 固件总字节数 |
| `progress` | 整数 | 传输后必填 | 0～100 |
| `chunk_size` | 整数 | `ready` 必填 | 发送端单包发送字节数 |
| `interval_ms` | 整数 | `ready` 必填 | 相邻发送包之间的最小间隔 |
| `inactivity_timeout_ms` | 整数 | `ready` 必填 | MCU 等待后续固件数据的无活动超时 |
| `reason` | 字符串 | 失败/拒绝必填 | 稳定错误码 |
| `restarting_in_ms` | 整数 | 成功时必填 | MCU 计划重启前的等待时间 |

发送端必须忽略不认识的扩展字段，但不得忽略不认识的 `state`。遇到未知状态时应
停止自动推进并保留原始报文供维护人员检查。

## 5. 发送端发起升级

发送端应先完成现有登录流程。登录成功示例：

```json
{"operation":"login","user":"zhouwei","pass":"123456"}
```

MCU 返回：

```json
{"operation":"login","code":"OK","message":"admin"}
```

随后发送升级请求：

```json
{"operation":"upload","protocol":1,"size":627872}
```

`627872` 只是 Firmware 2.0.8 当前测试 BIN 的示例大小。发送端必须读取用户实际
选择文件的长度填入 `size`，不得把示例值写死。

发送 `upload` 后，发送端必须保持等待状态，不得发送任何 BIN 字节。

## 6. MCU 状态定义

| `state` | 含义 | 发送端动作 |
| --- | --- | --- |
| `accepted` | 请求格式和大小已初步接受，MCU 正在通知 LCD 并等待业务静默 | 继续等待，严禁发送 BIN |
| `ready` | OTA 分区和串口已准备完成 | 从 BIN 第 0 字节开始发送 |
| `transferring` | MCU 正在接收并写入 BIN | 更新进度，继续按规定节奏发送 |
| `verifying` | 所有声明字节已接收，MCU 正在校验镜像 | 停止发送，等待最终结果 |
| `success` | 镜像有效且启动分区切换成功 | 停止发送，等待 MCU 重启 |
| `failed` | 已建立的 OTA 会话失败 | 立即停止发送，不自动续传旧会话 |
| `rejected` | 请求在建立 OTA 会话前被拒绝 | 不发送 BIN，修正原因后建立新会话 |

只有 `state=ready` 是允许开始发送 BIN 的条件。`accepted`、固定等待时间、LCD ACK、
登录成功和发送请求成功都不能代替 `ready`。

## 7. 标准成功流程

### 7.1 MCU 接受请求

```json
{"operation":"upload_status","protocol":1,"session":1,"state":"accepted","size":627872}
```

收到 `accepted` 只能说明请求进入准备阶段。业务静默可能需要数秒到数十秒，发送端
必须继续等待；不得使用 2 秒、5 秒或其他固定延迟后自动发送。

### 7.2 MCU 准备完成

```json
{"operation":"upload_status","protocol":1,"session":1,"state":"ready","size":627872,"chunk_size":800,"interval_ms":1000,"inactivity_timeout_ms":90000}
```

发送端收到当前会话的 `ready` 后必须依次执行：

1. 再次核对实际文件长度等于 `size`；
2. 把文件读取位置重置到偏移 0；
3. 核对第一个字节为 ESP32 镜像魔数 `0xE9`；
4. 读取 MCU 返回的 `chunk_size` 和 `interval_ms`；
5. 从偏移 0 开始发送原始 BIN。

任何一项核对失败都不得开始传输。

### 7.3 发送 BIN

当前参数：

- 每包最多 800 字节；
- 相邻包发送开始时间至少间隔 1000ms；
- 最后一包发送实际剩余字节，不补零；
- 不添加包头、包尾、换行符或 JSON；
- 不转为十六进制文本或 Base64；
- 发送期间不再发送登录、查询或其他控制 JSON。

发送端应保存以下本地状态：

- 当前 `session`；
- 文件总长度；
- 下一次文件读取偏移；
- 已发送字节数；
- MCU 最后确认的 `written`；
- 最后一条 MCU 状态及接收时间。

### 7.4 MCU 进度通知

MCU 每首次跨过 5% 阈值发送一次，不按每个 800 字节包发送：

```json
{"operation":"upload_status","protocol":1,"session":1,"state":"transferring","written":313936,"total":627872,"progress":50}
```

正常阈值为：

```text
0、5、10、15、20……90、95、100
```

该进度表示 MCU 已写入的总体进度，不是单包 ACK。当前协议仍依赖发送间隔和最终
镜像校验，不支持根据该进度重传某一个丢失分包。

### 7.5 MCU 校验

```json
{"operation":"upload_status","protocol":1,"session":1,"state":"verifying","written":627872,"total":627872,"progress":100}
```

收到 `verifying` 后，发送端必须关闭 BIN 发送定时器，不能重复发送最后一包。

### 7.6 MCU 成功

```json
{"operation":"upload_status","protocol":1,"session":1,"state":"success","written":627872,"total":627872,"progress":100,"restarting_in_ms":3000}
```

发送端收到 `success` 后应：

1. 将传输状态标记为成功；
2. 停止所有文件发送；
3. 保存会话、文件名、大小和最终结果日志；
4. 等待 MCU 重启和连接恢复；
5. 不在旧 `session` 上自动重发。

## 8. 失败和拒绝

### 8.1 已建立会话失败

```json
{"operation":"upload_status","protocol":1,"session":1,"state":"failed","reason":"image_validation_failed","written":627872,"total":627872,"progress":100}
```

收到 `failed` 后，发送端必须立即：

1. 停止发送定时器；
2. 取消尚未发出的所有文件块；
3. 不再向串口发送剩余 BIN；
4. 保存原始 `reason`、`written` 和 `total`；
5. 等用户操作或重新建立一个新的 `session`。

禁止在失败后从原偏移继续发送。当前版本不支持断点续传。

### 8.2 建立会话前拒绝

```json
{"operation":"upload_status","protocol":1,"session":0,"state":"rejected","reason":"image_too_large","size":4000000}
```

收到 `rejected` 后不得发送 BIN。`session=0` 表示正式 OTA 会话尚未创建。

### 8.3 错误码

| `reason` | 含义 | 发送端处理 |
| --- | --- | --- |
| `invalid_request` | 请求 JSON 格式错误或缺少必要字段 | 修正请求后重试 |
| `unsupported_protocol` | MCU 不支持请求中的协议版本 | 使用双方支持的协议版本 |
| `invalid_size` | 文件长度为 0、负数或不在允许范围 | 重新读取正确文件长度 |
| `session_busy` | MCU 已有 OTA 会话 | 等待当前会话结束 |
| `partition_unavailable` | 找不到下一 OTA 分区 | 停止并维护 MCU |
| `image_too_large` | 文件超过 OTA 分区容量 | 选择正确应用 BIN |
| `dtu_unavailable` | Remote DTU 串口资源不可用 | 检查设备后重试 |
| `business_quiesce_timeout` | 业务未能在 90 秒内静默 | 稍后重试；重复发生需检查 MCU |
| `dtu_mutex_timeout` | 未能取得 Remote DTU 串口锁 | 稍后重试 |
| `flash_begin_failed` | OTA 分区写入初始化失败 | 可重试；重复发生需维护 MCU |
| `flash_write_failed` | Flash 写入失败 | 停止发送并保存日志 |
| `data_inactivity_timeout` | 连续 90 秒未收到固件数据 | 检查发送端和链路后新建会话 |
| `session_timeout` | OTA 总会话超过 20 分钟 | 检查发送速度和链路 |
| `received_size_mismatch` | MCU 接收长度与声明不一致 | 从偏移 0 重新升级 |
| `image_validation_failed` | ESP32 镜像结构或内部校验失败 | 核对文件、首字节和传输完整性 |
| `boot_partition_failed` | 无法设置下次启动分区 | 停止并维护 MCU |
| `internal_error` | 未归类的 MCU 内部错误 | 保存完整日志后维护 |

为后续分包校验、安全认证和回滚预留：

`model_mismatch`、`version_rejected`、`chunk_crc_error`、
`chunk_sequence_error`、`sha256_mismatch`、`signature_invalid`、
`health_check_failed`、`rollback_triggered`。

发送端收到未知 `reason` 时必须显示并保存原始值，不能把它替换成空字符串。

## 9. 发送端状态机

```text
IDLE
  │ 发送 upload JSON
  ▼
WAIT_ACCEPTED
  │ accepted
  ▼
WAIT_READY ─────────────── rejected ──► STOPPED
  │ ready
  ▼
SENDING_BIN
  │ transferring：更新进度
  │ verifying
  ▼
VERIFYING
  ├── success ────────────► WAIT_RESTART
  └── failed ─────────────► STOPPED
```

任意状态收到当前 `session` 的 `failed` 都必须进入 `STOPPED`。收到其他非零旧
`session` 的状态不得改变当前会话。

## 10. 超时和重试规则

| 阶段 | 建议发送端超时 | 处理 |
| --- | ---: | --- |
| 等待 `accepted` | 5 秒 | 提示请求无响应，不发送 BIN |
| 等待 `ready` | 至少 100 秒 | MCU 业务静默上限为 90 秒，禁止固定数秒后发送 |
| BIN 分包发送 | 使用 MCU 返回的 `interval_ms` | 当前为每包间隔 1000ms |
| MCU 数据无活动超时 | MCU 返回的 `inactivity_timeout_ms` | 当前为 90000ms |
| 等待 `success/failed` | 收到 `verifying` 后建议 30 秒 | 超时后停止自动操作并保留日志 |
| 等待重连 | 由现场链路决定 | 不自动复用旧会话 |

控制 JSON 丢失时，发送端不得猜测 MCU 状态并继续推进。当前协议尚未定义控制状态
查询命令；连接异常时应停止自动发送，避免把 BIN 写入普通命令解析器。

## 11. 文件选择和校验

远程 OTA 只能选择应用 BIN，例如：

```text
TSP.ino.bin
```

不得选择：

- `TSP.ino.merged.bin`；
- `TSP.ino.bootloader.bin`；
- `TSP.ino.partitions.bin`；
- `boot_app0.bin`；
- `.7z`、`.zip` 或其他压缩包。

当前 Firmware 2.0.8 测试应用 BIN：

- 文件大小：627872 字节；
- 首字节：`0xE9`；
- SHA-256：
  `90E066D25798BBB76752F74E156CAF18E2EE4F58A441D31D9343FD7020FD8DB1`。

以上数值只用于 2.0.8 当前测试包。后续版本必须使用对应发布清单中的大小和哈希。

## 12. 性能和 DTU 负担

本协议不会明显增加 DTU 负担：

- 627872 字节、800 字节一包约为 785 个发送包；
- 旧方案对每次写入返回文本，最多产生约 785 行进度；
- 本协议按 5% 返回进度，整个会话约 21 条进度，加少量状态，通常不超过 25 条；
- 所有控制 JSON 均不超过 256 字节，总控制流量约 3～5KB；
- 控制流量相对 627872 字节固件不足 1%；
- Remote DTU 串口为 115200，分散在约 13 分钟会话中，带宽占用可忽略。

MCU 应使用固定 256 字节缓冲和 `snprintf()` 构造状态，不因发送 JSON 动态建立大型
对象。进度不得改回每个 800 字节包发送。

## 13. Firmware 2.0.8 及更早版本旧文本兼容说明

Firmware 2.0.8 当前 Remote DTU 实际返回：

```text
Ready to start OTA, size: 627872 byte, inactivity timeout: 90 seconds
The single packet sent is 800 bytes, with a sending interval of 1000ms
```

传输期间返回：

```text
800/627872
1600/627872
...
627872/627872
```

完成和结果：

```text
OTA download complete! Finalizing...
OTA success! System restarting in 3 seconds...
```

或：

```text
OTA failed: image_validation_failed. Written 627872/627872 bytes.
```

在 MCU 和发送端同时切换到本文 JSON 协议前，现有发送端仍应按旧文本工作。不得在
同一次升级中把“等待旧文本”和“等待新 JSON”配置成两个都能触发发送的并行条件，
否则可能重复启动文件发送。

## 14. 联调验收清单

### 14.1 正常成功

- 发送 `upload` 后收到 `accepted`；
- `accepted` 后发送端没有发送任何 BIN 字节；
- 收到 `ready` 后从偏移 0 开始，首字节为 `0xE9`；
- 单包不超过 800 字节，间隔不少于 1000ms；
- 进度按 5% 更新，不按每包刷屏；
- 收到 `verifying` 后停止文件发送；
- 收到 `success` 后保存结果并等待 MCU 重启。

### 14.2 MCU 准备较慢

- `accepted` 后等待 30～90 秒仍不提前发送；
- 只以当前会话的 `ready` 作为发送触发条件。

### 14.3 失败立即停止

- 传输中收到 `failed` 后取消所有待发送包；
- 串口不再出现剩余 BIN；
- 新重试使用新 `session`，并从文件偏移 0 开始。

### 14.4 文件错误

- merged BIN、压缩包、空文件和超过分区的文件在发送前或 `rejected` 后停止；
- 首字节不是 `0xE9` 时发送端本地直接拒绝。

### 14.5 会话隔离

- 旧 `session` 的进度不覆盖当前页面；
- 未知 `state` 停止自动推进并保留原始报文；
- 未知 `reason` 原样显示和记录。

## 15. 2026-08-13 失败案例说明

现场日志中，发送端发出：

```json
{"operation":"upload","size":627072}
```

MCU 约 35 秒后才准备完成。第一批进入 OTA 写入的数据首字节为 `0x63`，而正确
ESP32 应用 BIN 首字节应为 `0xE9`，MCU 因此在写入 0 字节时立即失败。失败后
发送端仍继续发送剩余 BIN，普通 Remote DTU JSON 解析器随后从二进制中识别出
固件内嵌的 JSON 格式字符串。

该案例证明发送端必须同时满足：

1. 只在 `state=ready` 后开始；
2. 每次开始前重置文件偏移到 0；
3. 核对首字节 `0xE9`；
4. 收到 `failed` 后立即停止剩余数据。
