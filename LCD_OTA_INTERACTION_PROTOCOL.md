# TSP 主板 OTA 与 LCD 屏幕交互协议

文档版本：1.1
报文字段 `protocol`：1
更新日期：2026-08-14
适用设备：TSP ESP32-S3 主板与大彩 HMI LCD 屏幕
当前状态：Firmware 2.0.8 主板侧 normal ACK 已实现，并已与 LCD V1.0.12 完成
一次成功远程 OTA、重启和恢复实机联调

> 重要：Firmware 2.0.6 尚未发送本文定义的 `ota_status` JSON。主板侧功能从
> Firmware 2.0.7 开始提供 OTA 状态，并已完成一次成功远程 OTA；但 LCD 没有
> 响应重启后的两次 `normal`。Firmware 2.0.8 增加 `normal` ACK、2 秒重试和
> 30 秒上限；2026-08-14 实测首条 `normal` 未获 ACK，第二条重发后收到 ACK，
> LCD 约 3 秒后恢复普通查询。默认正式回退版本仍为 Firmware 2.0.4。

## 0. 2026-08-14 实机联调结论

- 627872 字节固件完整接收、校验、重启并启动 Firmware 2.0.8；
- `preparing` ACK、5% 进度、`verifying` 和 `restarting` 状态均正常；
- 重启后 16:12:06.375 发送首条 `normal`，LCD 未回复；
- 16:12:08.365 主板按 2 秒机制重发，16:12:08.524 记录 normal ACK 成功；
- 16:12:11.613 LCD 恢复 `get_data`，normal ACK 后约 3.09 秒恢复业务；
- 无 normal ACK timeout、UART overflow、JSON 接收错误或 OTA 失败日志。

## 1. 目标

当 TSP ESP32-S3 主板通过 Remote DTU 进行 OTA 升级时：

- LCD 明确显示“主板正在升级”，避免用户误操作；
- LCD 停止 `get_data`、历史查询、配置和校准等普通命令；
- 主板向 LCD 显示准备、传输、校验、重启、失败和恢复正常状态；
- OTA 期间即使 LCD 未升级、未收到通知或仍持续发送，主板也不会因接收缓冲积压而溢出；
- LCD 故障、未连接或未回复 ACK 时，不得阻止主板 OTA；
- OTA 成功或失败后，LCD 和主板业务都能恢复到明确状态。

本文只规定主板与 LCD 之间的交互。Remote DTU 上传工具仍按现有协议发送
升级请求和固件 BIN，不通过 LCD 传输固件。

## 2. 串口与报文格式

| 项目 | 规定 |
| --- | --- |
| 接口 | 主板 LCD 专用串口 |
| 波特率 | 9600 |
| 数据格式 | 8N1 |
| 文本编码 | UTF-8/ASCII；协议字段和值只使用 ASCII |
| 报文格式 | 单行紧凑 JSON |
| 报文结尾 | `\r\n` |
| 建议最大长度 | 不超过 256 字节 |

发送时不得附加 BOM、注释、二进制头或 JSON 以外的说明文字。

主板实际发送示例：

```text
{"operation":"ota_status","protocol":1,"session":123456,"state":"preparing","progress":0,"total":624032}\r\n
```

## 3. 公共字段

主板发送的 OTA 状态统一使用 `ota_status`：

```json
{
  "operation": "ota_status",
  "protocol": 1,
  "session": 123456,
  "state": "preparing",
  "progress": 0,
  "total": 624032
}
```

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `operation` | 字符串 | 是 | 固定为 `ota_status` |
| `protocol` | 整数 | 是 | 本文协议版本，当前固定为 `1` |
| `session` | 无符号整数 | 是 | 本次 OTA 会话编号；同一次升级保持不变 |
| `state` | 字符串 | 是 | OTA 状态，见第 4 节 |
| `progress` | 整数 | 是 | 0～100；非传输阶段也必须提供 |
| `total` | 整数 | 否 | 固件总字节数，准备和传输阶段建议提供 |
| `reason` | 字符串 | 条件必填 | `state=failed` 时必须提供错误码 |
| `version` | 字符串 | 否 | 重启或恢复正常时可提供固件版本 |

`session=0` 专门用于主板重启后的 `normal` 状态。LCD 收到 `normal` 时应无条件
恢复正常，不因会话编号与升级前不同而拒绝。

## 4. 状态定义与 LCD 行为

| `state` | 含义 | LCD 建议显示 | LCD 行为 |
| --- | --- | --- | --- |
| `preparing` | 主板准备进入 OTA | 主板准备升级，请勿断电 | 立即停止所有普通命令，进入升级页面并回复一次 ACK |
| `transferring` | 主板正在接收并写入固件 | 主板升级中：`progress`% | 保持升级页面，不发送普通命令 |
| `verifying` | 固件接收完成，正在校验 | 正在校验固件，请勿断电 | 保持升级页面，不恢复查询 |
| `restarting` | 校验成功，准备重启 | 升级成功，主板正在重启 | 保持升级页面，等待 `normal` |
| `failed` | OTA 失败，旧固件继续运行 | 升级失败：按 `reason` 映射显示 | 保持失败页面，等待 `normal` |
| `normal` | 主板正常运行 | 正常业务页面 | 清理升级状态，返回主页，恢复普通查询 |

LCD 在任意时刻收到 `preparing`、`transferring`、`verifying` 或 `restarting`，
都必须进入升级页面。这样即使 LCD 在 OTA 中途重启并错过 `preparing`，收到下一次
进度更新后仍能恢复正确页面。

LCD 收到未知 `state` 时不得崩溃或恢复普通查询，应显示“主板升级状态未知”，
并等待下一条有效状态。

## 5. ACK 规则

### 5.1 主板通知 LCD

主板收到合法的 Remote DTU 升级请求后，在暂停业务之前发送：

```json
{"operation":"ota_status","protocol":1,"session":123456,"state":"preparing","progress":0,"total":624032}
```

### 5.2 LCD 必须立即执行

1. 停止 `get_data` 定时器；
2. 停止历史查询、配置、校准及其他普通命令；
3. 取消或清理正在等待的普通命令状态；
4. 跳转到主板升级页面；
5. 回复一次准备 ACK。

LCD 回复：

```json
{"operation":"ota_status_ack","protocol":1,"session":123456,"state":"preparing","code":"OK"}
```

| 字段 | 规定 |
| --- | --- |
| `operation` | 固定为 `ota_status_ack` |
| `protocol` | 固定为 `1` |
| `session` | 必须与主板通知一致 |
| `state` | 固定为 `preparing` |
| `code` | 成功固定为 `OK` |

主板最多等待 ACK 2000 毫秒：

- 收到本次 `session` 的正确 ACK：记录成功并继续；
- 未收到、LCD 未连接或仍为旧程序：记录 `lcd_ack_timeout` 警告并继续；
- 旧会话 ACK、错误 `session` 或格式错误：忽略并继续等待，直到超时。

LCD ACK 只用于确认页面切换，不是主板 OTA 成功条件。进度状态不要求 ACK。

### 5.3 normal 恢复 ACK

LCD 收到任意合法 `state=normal` 时，必须先于会话和页面锁定判断无条件处理，
然后回复：

```json
{"operation":"ota_status_ack","protocol":1,"session":0,"state":"normal","code":"OK"}
```

- 首条 `normal`：立即清除 OTA 锁定、返回主页并启动一次 3 秒恢复定时器；
- 重复 `normal`：每次补发 ACK，但不重复跳页、不重新锁定、不重启或延长定时器；
- 未收到 `normal`：不得凭空发送恢复 ACK；
- 主板收到合法 ACK 后立即停止重发并记录
  `LCD_OTA_NORMAL_ACK result=ok`；
- 主板 30 秒内未收到 ACK：停止重发并记录 `result=timeout`，但不得把它判定为
  OTA 失败或暂停普通业务。

## 6. 完整交互时序

### 6.1 正常成功流程

1. Remote DTU 上传工具向主板发送合法 OTA 请求；
2. 主板向 LCD 发送 `preparing`；
3. LCD 停止普通命令、进入升级页面并回复 ACK；
4. 主板等待当前采集、存储和串口业务进入安全点；
5. 主板取得 Remote DTU 串口并初始化 OTA 分区；
6. 主板向 Remote DTU 上传工具返回 `Ready to start OTA`；
7. 主板向 LCD 发送 `transferring, progress=0`；
8. 上传工具开始发送固件 BIN；
9. 主板每跨过 5% 向 LCD 更新一次进度；
10. 固件接收完成后，主板向 LCD 发送 `verifying`；
11. 镜像校验和启动分区设置成功后，主板发送 `restarting`；
12. 主板约 3 秒后重启；
13. 主板完成配置、SD 和主要任务初始化后立即发送 `normal`；
14. LCD 返回正常页面并回复 normal ACK；
15. 主板收到 ACK 后停止重发；LCD 首次 `normal` 后约 3 秒恢复普通查询。

### 6.2 传输进度

主板只在进度首次达到以下阈值时发送：

```text
0、5、10、15、20……90、95、100
```

示例：

```json
{"operation":"ota_status","protocol":1,"session":123456,"state":"transferring","progress":45,"total":624032}
```

禁止每收到一个 800 字节包就通知 LCD。LCD 进度是显示信息，不参与 Remote DTU
分包确认，也不得作为固件发送端的 ACK。

### 6.3 校验与重启

接收完成、开始校验：

```json
{"operation":"ota_status","protocol":1,"session":123456,"state":"verifying","progress":100,"total":624032}
```

校验成功并设置启动分区后：

```json
{"operation":"ota_status","protocol":1,"session":123456,"state":"restarting","progress":100,"version":"2.0.8"}
```

主板重启并完成初始化后：

```json
{"operation":"ota_status","protocol":1,"session":0,"state":"normal","progress":0,"version":"2.0.8"}
```

主板立即发送第一条 `normal`；未收到 ACK 时在 2、4、6……28 秒继续发送，达到
30 秒时停止并记录超时，不在 30 秒边界再发送。重试由现有 LCD 接收任务调度，
不得阻塞主板初始化或普通业务。

## 7. 失败流程

失败状态示例：

```json
{"operation":"ota_status","protocol":1,"session":123456,"state":"failed","progress":65,"reason":"data_inactivity_timeout"}
```

主板发生失败时应按以下顺序处理：

1. 中止未完成的 OTA 写入；
2. 不切换启动分区；
3. 释放 Remote DTU 串口；
4. 恢复采集、存储、HJ212 和维护业务；
5. 向 LCD 发送 `failed`；
6. 保留失败页面约 3 秒；
7. 向 LCD 发送 `normal`；
8. LCD 返回正常页面并回复 normal ACK；
9. 主板收到 ACK 后停止重发，LCD 按首次 `normal` 的3秒定时恢复普通查询。

LCD 收到 `failed` 后不得自行立即恢复 `get_data`，必须等待主板发送 `normal`。

## 8. 错误码定义

### 8.1 Firmware 2.0.8 会向 LCD 发送的失败码

以下错误发生在主板已经发送 `preparing`、建立 LCD 会话之后。Firmware 2.0.8
会通过 `state=failed` 和 `reason` 发送给 LCD：

| `reason` | 含义 | LCD 建议显示 | 是否可重试 |
| --- | --- | --- | --- |
| `business_quiesce_timeout` | 90 秒内业务任务未能全部退出安全区 | 主板业务繁忙，无法进入升级 | 稍后重试；重复出现需检查任务 |
| `dtu_mutex_timeout` | 15 秒内未取得 Remote DTU 串口锁 | 远程升级通道忙 | 稍后重试 |
| `flash_begin_failed` | 无法开始写入目标 OTA 分区 | 无法初始化升级分区 | 可重试；重复出现需维护主板 |
| `flash_write_failed` | 写 Flash 过程中底层写入失败 | 固件写入失败 | 可重试；重复出现需检查 Flash |
| `data_inactivity_timeout` | OTA 已进入接收状态，但连续 90 秒没有收到固件数据 | 固件传输中断 | 检查连接后重新升级 |
| `session_timeout` | 单次 OTA 总时间超过 20 分钟 | 升级传输超时 | 检查速度或连接后重试 |
| `received_size_mismatch` | 实际接收字节数与请求声明大小不一致 | 固件接收不完整 | 重新连接并重新升级 |
| `image_validation_failed` | 字节数收满，但 ESP32 镜像结构或内部校验不通过 | 固件校验失败 | 核对 BIN 和传输完整性后重试 |
| `boot_partition_failed` | 镜像有效，但无法把目标分区设置为下次启动分区 | 无法切换到新固件 | 可重试；重复出现需维护主板 |

### 8.2 会话建立前的拒绝码和通用预留码

以下情况发生时，Firmware 2.0.7 还没有建立 LCD 会话，因此不会发送
`preparing` 或 `failed`；当前只在主板或上传端日志中报告。错误码名称保留在
协议词典中，供后续统一上传端响应时使用，LCD 可预先加入显示映射：

| `reason` | 含义 | LCD 建议显示 | 是否可重试 |
| --- | --- | --- | --- |
| `invalid_request` | OTA 请求 JSON 缺少必要字段或格式非法 | 升级请求格式错误 | 修正请求后可重试 |
| `invalid_size` | 固件大小为 0、负数或超出允许数值范围 | 升级文件大小无效 | 选择正确文件后可重试 |
| `session_busy` | 已有 OTA 会话正在进行，又收到新的升级请求 | 主板正在执行其他升级 | 等当前会话结束后重试 |
| `partition_unavailable` | 找不到可写入的下一 OTA 分区 | 主板升级分区不可用 | 一般需要维护主板固件或分区 |
| `image_too_large` | 固件大小超过下一 OTA 分区容量 | 升级文件过大 | 使用正确构建或分区后重试 |
| `dtu_unavailable` | Remote DTU 串口或互斥资源不可用 | 远程升级通道不可用 | 检查主板或 DTU 后重试 |
| `internal_error` | 未归类的主板内部错误 | 主板升级内部错误 | 保存日志后联系维护人员 |

### 8.3 LCD 交互警告码

以下是主板日志警告，不应终止主板 OTA：

| 错误码 | 含义 | 处理 |
| --- | --- | --- |
| `lcd_ack_timeout` | 主板发送 `preparing` 后 2 秒内未收到 LCD ACK | 主板继续 OTA，并进入 LCD 输入排空模式 |
| `lcd_ack_session_mismatch` | LCD ACK 的 `session` 与当前 OTA 不一致 | 忽略该 ACK，继续等待或超时后继续 OTA |
| `lcd_status_send_failed` | 主板无法完整发送某条 LCD 状态 | 记录日志并继续 OTA，LCD 不得成为升级阻塞条件 |
| `LCD_OTA_NORMAL_ACK result=timeout` | 30 秒未收到 normal ACK | 停止重发；只记录恢复确认失败，不判定 OTA 失败 |
| `LCD_OTA_NORMAL_ACK result=rejected` | normal ACK 的协议、session 或 code 无效 | 消费该控制报文并继续有限重试 |

### 8.4 为后续完整性和安全校验预留的错误码

以下错误码为后续加入分包校验、SHA-256、签名和回滚时预留。LCD 程序建议现在
一并加入显示映射，避免后续再次修改页面逻辑：

| `reason` | 含义 | LCD 建议显示 |
| --- | --- | --- |
| `model_mismatch` | 固件目标设备型号与当前主板不一致 | 固件型号不匹配 |
| `version_rejected` | 版本不允许升级或违反防降级规则 | 固件版本不允许安装 |
| `chunk_crc_error` | 某个传输分包 CRC 校验失败 | 固件分包校验失败 |
| `chunk_sequence_error` | 分包丢失、重复或顺序错误 | 固件分包顺序错误 |
| `sha256_mismatch` | OTA 分区完整 SHA-256 与发布清单不一致 | 固件完整性校验失败 |
| `signature_invalid` | 固件数字签名不是可信发布者签发 | 固件签名无效 |
| `health_check_failed` | 新固件启动后未通过采集、存储等健康检查 | 新固件启动检查失败 |
| `rollback_triggered` | 新固件启动失败，Bootloader 已回滚到旧固件 | 新固件异常，已恢复旧版本 |

LCD 收到未识别的 `reason` 时统一显示：

```text
主板升级失败（未知错误）
错误码：<reason>
```

必须保留并显示原始错误码，便于现场日志定位。

## 9. 主板 OTA 期间的 LCD 输入处理

主板 LCD 接收逻辑分为三个状态：

| 主板内部状态 | 行为 |
| --- | --- |
| `NORMAL` | 正常解析并处理 LCD JSON |
| `WAIT_LCD_ACK` | 只接受当前会话的 `ota_status_ack`，其他命令读取后丢弃 |
| `OTA_DRAIN` | 持续读取 LCD 串口并直接丢弃，不解析、不执行业务、不回复 |

这里的“持续读取”只是排空串口缓冲，不代表继续处理 LCD 业务。

如果 OTA 期间彻底停止读取，而旧 LCD 仍持续发送 `get_data`，主板 1024 字节 LCD
接收缓冲会被填满。OTA 结束后再读取时会得到截断或拼接的半包 JSON，造成
`UART_OVERFLOW` 和解析错误。因此 OTA 期间必须继续排空输入。

OTA 成功重启或失败恢复前，主板应：

1. 排空 LCD 接收缓冲；
2. 清除未完成的 JSON 字符串；
3. 重置括号层级、接收标志和超时计时；
4. 切回 `NORMAL`；
5. 发送 `normal` 状态。

## 10. LCD 程序实现要求

1. `ota_status` 的处理优先级必须高于普通页面和普通数据响应；
2. 收到任何非 `normal` OTA 状态，都要停止普通查询；
3. 只有收到 `normal` 才恢复 `get_data`；
4. `preparing` ACK 必须在完成停止定时器和切换页面后发送；
5. `preparing` ACK 同一会话只发送一次；每收到一条合法 `normal` 都回复 normal ACK；
6. 进度只接受 0～100，异常值按边界显示，但不得导致程序崩溃；
7. 新 `session` 的 `preparing` 可覆盖旧的失败或升级页面；
8. 非零且与当前会话不一致的旧进度消息应忽略；
9. `session=0,state=normal` 必须始终接受；
10. 未知状态或错误码必须显示原始值，不能直接恢复普通业务；
11. LCD 升级页面不得继续发送心跳式 `get_data`；
12. LCD 自身重启后若收到 `transferring` 或其他 OTA 状态，应直接进入升级页面；
13. 重复 `normal` 不得重启或延长3秒恢复定时器。

## 11. 固定超时和频率

| 项目 | 当前规定 |
| --- | ---: |
| 等待 LCD 准备 ACK | 2000 ms |
| LCD 进度通知间隔 | 每跨过 5% 一次 |
| 主板业务静默等待上限 | 90 秒 |
| Remote DTU 串口锁等待上限 | 15 秒 |
| OTA 无数据超时 | 90 秒 |
| OTA 总会话上限 | 20 分钟 |
| 成功后重启等待 | 3 秒 |
| 失败页面建议保留 | 3 秒 |
| `normal` 重发间隔 | 2000 ms |
| `normal` ACK 等待总上限 | 30000 ms |
| LCD 首条 `normal` 后普通查询恢复 | 约 3 秒 |

## 12. 兼容性原则

- LCD 没有连接：主板等待 ACK 2 秒后继续升级；
- LCD 是旧程序：主板继续排空其输入，不处理旧 LCD 请求；
- LCD 在 OTA 中途重启：收到下一条 OTA 状态后重新进入升级页面；
- LCD 丢失进度消息：下一次 5% 更新会覆盖显示，不影响主板 OTA；
- LCD 丢失 `normal` 或 normal ACK：主板每2秒重发，收到ACK或达到30秒后停止；
- LCD 串口异常：只记录警告，不得中止主板 OTA；
- 主板 OTA 失败：继续运行旧固件，并通过 `failed`、`normal` 恢复 LCD；
- 未实现本文协议的 Firmware 2.0.6：LCD 不应假设能够收到 `ota_status`。

## 13. 联调验收清单

### 13.1 正常成功

- LCD 收到 `preparing` 后立即停止 `get_data`；
- LCD 正确回复当前 `session` ACK；
- 进度从 0 更新到 100；
- LCD 显示 `verifying` 和 `restarting`；
- 主板成功重启；
- LCD 收到 `normal` 后返回主页、回复 ACK，并在约3秒后恢复查询；
- 主板记录 `LCD_OTA_NORMAL_ACK result=ok`并停止重发；
- 主板无 LCD `UART_OVERFLOW` 或损坏 JSON。

### 13.2 LCD 不回复 ACK

- 主板约 2 秒后记录 `lcd_ack_timeout`；
- 主板仍能完成 OTA；
- LCD 故障不导致主板升级失败。

### 13.3 旧 LCD 持续发送

- 主板 OTA 期间持续排空 LCD 输入；
- 不处理旧 LCD 的 `get_data`；
- 不产生持续缓冲溢出；
- OTA 失败恢复或成功重启后，只处理新到达的完整 JSON。

### 13.4 传输中断

- 90 秒无固件数据后主板发送 `failed`；
- `reason=data_inactivity_timeout`；
- 主板不重启、不切换启动分区；
- 主板业务恢复后发送 `normal`；
- LCD 返回正常页面。

### 13.5 镜像校验失败

- LCD 先显示 `verifying`；
- 主板发送 `failed`；
- `reason=image_validation_failed`；
- 主板继续运行旧固件；
- LCD 收到 `normal` 后恢复。

### 13.6 LCD 中途重启

- LCD 错过 `preparing` 后，收到下一条 `transferring` 仍进入升级页面；
- LCD 不恢复普通查询；
- 最终收到 `normal` 后正常恢复。

### 13.7 normal 恢复确认

- 丢弃第一条 `normal`：下一条应在约2秒后到达；
- 丢弃第一个 normal ACK：主板再次发送 `normal`，LCD再次ACK，但3秒恢复定时器
  不重新开始；
- LCD完全不回复：主板约30秒后记录`result=timeout`，业务继续运行；
- LCD在超时后回复迟到ACK：主板可记录确认，但不得恢复已结束的重发计时；
- OTA失败恢复同样完成`failed → normal → ACK`。
