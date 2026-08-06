# TSP 固件 2.0.2 变更记录

日期：2026-07-30

## 修改目标

修复 2.0.1 实测中出现的持续内存下降、HJ212 报文退化为 14 字节空包、
pending 无法恢复以及 ESP32 `abort()` / `LoadProhibited` 崩溃问题。

本版本不修改分钟、小时、日统计的采样与计算逻辑。

## 根因

2.0.1 为事件队列增加名称诊断后，每个临时 LCD/DTU 请求响应队列也被写入
`queueNames`。临时队列虽然通过 `vQueueDelete()` 销毁，但对应的
`std::map` 名称节点没有删除，造成每次 `get_data` / `get_records` 请求后
都有内存残留。

现场日志中空闲堆由约 22 KB 持续下降到约 5 KB。低内存使 HJ212 `String`
分配失败，最终生成 `##0002&&...` 形式的 14 字节空包，并进一步触发堆结构
异常和系统重启。

## 修复内容

### 临时队列内存泄漏

- 未显式命名的短生命周期队列不再写入 `queueNames`。
- 只有 `HJ212_2017`、`STORAGE`、`LED` 等长期任务队列保留诊断名称。
- 事件发布时不再为每个订阅队列构造临时 `String` 名称。

### HJ212 组包保护

- 检查 `cp`、协议正文和最终报文的 `String::reserve()` 返回值。
- 内存不足时返回空报文并输出 `HJ_BUILD_FAIL`，禁止继续拼接残缺报文。
- 移除未使用的完整报文临时格式化缓冲。
- 新增完整性校验：帧头、长度、必要字段、结尾和 CRC 必须全部正确。

### pending 保护与恢复

- 保存前校验 HJ212 完整性，拒绝 14 字节空包并输出 `PENDING_REJECT`。
- 读取到损坏 pending 时输出 `PENDING_INVALID`。
- 尝试从对应的 raw/min/hour 本地记录重新构建，成功时输出
  `PENDING_REBUILT`。
- 无法重建的损坏文件改名为 `.invalid`，避免永久阻塞后续补传。

### 补传任务内存控制

- CSQ 查询周期恢复为 60 秒。
- 网络正常时 pending 维护扫描间隔改为 5 分钟；网络从异常恢复时仍立即扫描。
- 补传任务栈由 8 KB 调整为 5 KB。
- 连续内存不足时不再反复强行创建任务，输出 `RECOVERY_DEFER`。
- 任务结束日志增加 `stack_high_water`，用于继续校准栈大小。

## 版本标识

固件版本更新为 `2.0.2`，同步修改：

- `TSP.ino`
- `src/inc/sys_init.h`
- `src/app/configManager/system_json.h`

启动日志应显示：

```text
Firmware version: 2.0.2
```

## 建议验证

1. 清空旧的 `pending` 后启动，连续操作 LCD `get_data`，确认空闲堆不再单向下降。
2. 连续运行并跨越整点，确认实时、分钟和小时文件持续生成。
3. 正常报文不应出现 `bytes=14`。
4. 不应重复出现 `Failed to create pending packet recovery task`。
5. 断网后确认生成有效 `.pkt`，网络恢复后成功补传并删除。
6. 如出现 `HJ_BUILD_FAIL`、`PENDING_REJECT`、`PENDING_INVALID` 或
   `RECOVERY_DEFER`，保留完整日志用于后续分析。
