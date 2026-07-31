# TSP 固件 2.0.1 变更记录

日期：2026-07-30

## 修改目标

排查并降低 HJ212 实时数据缺分钟、10 分钟/小时边界发送积压，以及发送失败后待补传报文不能及时恢复的问题。

本版本不修改小时统计的采样与计算逻辑。`DataManager` 中“每 3 分钟更新一次小时统计”的行为保持原样，待取得长期运行数据后再单独评估。

## 版本标识

固件版本从 `2.0.0` 更新为 `2.0.1`，同步修改：

- `TSP.ino`
- `src/inc/sys_init.h`
- `src/app/configManager/system_json.h`

设备启动时新增日志：

```text
Firmware version: 2.0.1
```

## HJ212 与 CSQ 串口锁

旧逻辑在持有 `SERIAL_HJ212` 互斥锁时等待 3 秒，CSQ 重试期间可能连续占用串口 6～9 秒，导致实时数据获取锁超时。

2.0.1 调整为：

- 只在发送 CSQ 命令和接收回复时持有串口锁；
- CSQ 重试间隔放在锁外；
- 获取锁失败时输出 `CSQ_LOCK_BUSY`；
- 保持 CSQ 查询与 HJ212 上传串行访问同一物理接口。

## HJ212 发送节流

旧逻辑每处理一条数据后固定延时 15 秒。整点同时产生实时、10 分钟和小时数据时，容易形成发送积压。

2.0.1 将固定间隔缩短为 1 秒。

现场使用的透明传输 DTU 为银尔达 `M100M-B2`。厂商建议数据包逐包发送，包间隔约 600 ms；固件使用 1000 ms 安全间隔。

DTU ACK 接收也调整为：

- 最长仍等待 5 秒；
- 收到响应后连续静默 600 ms 即返回；
- 避免收到完整 ACK 后仍占用串口约 5 秒。

## Pending 报文恢复

旧逻辑只有在 CSQ 从异常恢复为正常时调用 `fileRestore()`。当 CSQ 正常、但 TCP/平台 ACK 丢失时，报文虽然写入 SD 卡，却可能长期不补传。

2.0.1 在每次取得有效 CSQ 后都尝试扫描 pending 目录：

- 每批最多处理 3 个文件；
- 已有恢复任务运行时不会重复创建；
- 成功并收到 ACK 后删除 pending 文件；
- 失败时保留文件等待下一轮。

## 数据事件队列

旧逻辑在订阅者队列满时会清空队列中的全部旧消息，只保留最新消息。

2.0.1 对 `PROCESSED_DATA_COLLECTED` 使用背压策略：

- 不再清空 REAL、MIN、HOUR、DAY 旧数据；
- 队列接近满载时输出 `QUEUE_PRESSURE`；
- 等待订阅任务释放队列空间，保持数据顺序。

主要队列增加名称：

- `HJ212_2017`
- `HJ212_2025`
- `STORAGE`
- `LED`
- `ALARM_TASK`
- `DATA_MANAGER`
- `OTA`

非监测数据事件仍保留原来的溢出处理方式，后续可单独重构其载荷释放策略。

## 诊断日志

HJ212 发送日志现在同时包含：

- `trace`
- `type`
- `timestamp`
- `attempt`
- `ack`
- `bytes`

数据类型：

```text
0 = 实时数据
1 = 10分钟数据
2 = 小时数据
3 = 日数据
```

示例：

```text
[DIAG] TX_ATTEMPT trace=61 type=2 timestamp=20260730100000 attempt=1 bytes=...
[DIAG] TX_RESULT trace=61 type=2 timestamp=20260730100000 attempt=1 ack=1 response_bytes=...
```

队列压力示例：

```text
[DIAG] QUEUE_PRESSURE event=2 queue=HJ212_2017 waiting=18 spaces=2
```

## 其他锁修复

`systemInfo.mutex` 改为在所有业务任务启动前创建，避免启动阶段 SHT30/CSQ 任务先访问空互斥锁。

## 建议验证项目

1. 启动日志显示 `Firmware version: 2.0.1`。
2. 连续运行至少 2～3 小时并跨越整点。
3. 每个 `PROCESS` 的 `type/timestamp` 都能找到对应 `TX_RESULT`。
4. 正常网络下不再频繁出现 `HJ212 serial busy`。
5. 断网产生 `.pkt`，恢复后能够补传并删除。
6. 观察是否出现 `QUEUE_PRESSURE`，并记录具体队列名称。
7. SD 卡持续出现 `STORE ... ok=1`。
