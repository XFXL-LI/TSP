# TSP Firmware 2.0.22 变更记录

日期：2026-09-04  
版本类型：通用原始数据版  
当前状态：2026-09-07构建已正式打包，`packaged-not-hardware-verified`

## 版本目标

2.0.22在2.0.21基础上修复HJ212正常数据上传与旧pending补传之间的串口优先级
冲突。现场日志中，旧pending包于10:04:12先占用`SERIAL_HJ212`等待ACK，新的实时
包约1.5秒后到达并在等待串口锁3000 ms后产生`HJ_TX_DEFER reason=serial_busy`，随后
转存为新的pending。该现象与气体校准和RS485采集无关。

## 1. pending改为空闲调度

- 网络维护任务只设置`pendingRecoveryRequested`，不再直接发送pending报文；
- HJ212-2017或2025发送任务完成当前实时/分钟/小时/日报文并等待3000 ms后，只有在
  自身接收队列为空时才执行一次pending恢复；
- 取得恢复请求后再次检查队列，关闭检查与领取请求之间的竞争窗口；
- 十分钟、小时等边界同时排队的正常报文全部发送完成后，pending才允许进入；
- pending实际尝试发送后再次等待3000 ms，保持前后报文间隔；
- 仍保留每5分钟请求一次恢复、每轮扫描最多4个候选、最多实际发送1条的限制。

没有新增FreeRTOS任务。pending恢复从6 KB维护任务迁入已有8 KB HJ212发送任务，并
继续使用OTA业务活动保护。

## 2. 串口低优先级保护

`DTUManager`新增pending专用发送入口和独立的实时发送等待/活动状态：

- 实时与统计报文继续最多等待串口锁3000 ms；
- pending发现实时发送正在等待或进行中时立即延后；
- pending只等待串口锁100 ms，取得锁后再次检查实时等待者；
- 普通实时接口保持原有布尔返回语义，OTA的M状态发送路径无需改变；
- CSQ和网络校时仍由已有统一上传活动状态避让，不降低正常报文优先级。

## 3. 延后与失败分离

新增`Hj212SendResult::DEFERRED`和`PendingRecoveryResult::DEFERRED`：

- `DEFERRED`表示没有执行串口发送；
- pending文件保持不变；
- 不增加连续失败次数；
- 不触发30分钟冷却；
- 恢复请求自动重新挂起，等待下一个HJ212队列空闲点；
- 只有真正发送后缺少有效ACK才记为`SEND_FAILED`。

新增或扩展诊断：

- `HJ_POLICY ... source=live|pending`；
- `PENDING_RECOVERY_REQUESTED`；
- `PENDING_TX_DEFER reason=live_queue|live_queue_before_send|live_priority|live_waiting|serial_busy`；
- `RECOVERY_CYCLE ... deferred=<数量>`。

## 4. 保持不变

- 小时统计和有效样本取样逻辑未改；
- HJ212逐包间隔保持3000 ms；
- HJ212 ACK超时默认5秒、普通发送默认3次、pending单次尝试保持不变；
- 9014 ACK校验、QN、报文构造和pending文件格式未改；
- SHT30保持独立；
- 气体日常采集、校准和通用版不封顶策略未改；
- DEBUG保持开启；
- 未烧录、未擦除MCU、未操作设备FFat。

## 5. 构建结果

- 当时源码（historical source location）：迁移前 `tmp` 副本，现由迁移安全备份保存
- 构建目录：`firmware_workspace/build/firmware-2.0.22-hj212-live-priority`
- 发布包：`firmware_workspace/releases/2.0.22/2026-09-04_hj212-live-priority-final_compiled-not-hardware-verified`
- Sketch：675,960字节；
- 全局变量：27,320字节；
- 应用BIN：676,112字节；
- 应用BIN SHA-256：`CA036D98F3210103FF35FD43BE1495E48F92F29CAB02B309417E39A9C7BBE182`；
- 源码指纹：`9C075B0A7370D87EFF2AAFE84342CFEFC6A2F386D7B434649282E3F57AE68192`；
- ESP32-S3镜像checksum和validation hash均有效；
- 状态：`compiled-not-hardware-verified`。

同版本目录中名称不带`final`的包是加入“真正发送前再次检查实时队列”之前的中间
构建，已放置`DO_NOT_USE.md`，不得烧录或OTA。

## 6. 首次实机验证重点

1. 启动日志必须显示`Firmware version: 2.0.22`；
2. 正常报文应显示`HJ_POLICY ... source=live`和后续`TX_ATTEMPT`；
3. pending发送应只出现在正常HJ212队列清空之后，并显示`source=pending`；
4. 如果pending主动让路，应出现`PENDING_TX_DEFER`，同时`RECOVERY_CYCLE`中的
   `deferred`增加、`send_failed`不增加；
5. 旧pending无ACK仍应保留并按既有三次失败/30分钟冷却规则处理；
6. 重点观察正常实时包是否还因pending产生`HJ_TX_DEFER reason=serial_busy`；
7. 连续观察实时、分钟、小时包、OTA状态、SD保存、堆内存和任务栈。

## 7. 2026-09-07同版本气体节流与轻量诊断

保持版本号2.0.22不变，依据K-7S厂家“气体模块询问间隔不小于200 ms”的要求，
增加公共气体Modbus访问层。正常采集、同一模块失败重试以及校准读写均从上一笔气体
事务结束后计时，间隔达到200 ms后才启动下一笔事务。原有500 ms响应超时和最多3次
正常读取尝试不变；非气体传感器间隔不变。校准命令成功后，首次`0x6006`状态查询按
既有1000 ms状态周期执行，不再在下一次任务轮询时立即读取。

新增轻量诊断：

- `GAS_QUERY`记录来源、读写、从站、寄存器、尝试序号、实际事务间隔、补充等待时间、
  通信耗时和结果；
- `HJ_SERIAL_BUSY`记录请求方、当前串口占用来源、持锁时间以及实时/上传等待状态；
- `PENDING_WRITE_HEALTH`按一次完整保存操作累计写入失败和前128字节全零事件；
- `COLLECT`和`GET_DATA_DIRECT`增加跨任务累计的`min_free/min_largest`低水位，HJ发送和
  pending失败路径也参与低水位采样。

冻结项保持不变：小时统计、HJ212逐包3000 ms、默认ACK超时5秒/重试3次、SHT30独立、
DEBUG开启、pending文件格式和恢复规则均未修改。

构建结果：

- 构建目录：`firmware_workspace/build/firmware-2.0.22-gas-pacing-diagnostics-20260907`；
- Sketch：678,492字节；
- 全局变量：27,360字节；
- 应用BIN：678,640字节；
- SHA-256：`A8ECE4A080D82D944468C601513B15D26FBB835A7EC554B4F235081BC1DA6A60`；
- 源码指纹：`84CD14958E077AE1FE4294C4C566F8CF39FE89952C5CB929301AF2CF4C553213`；
- ESP32-S3镜像checksum和validation hash有效；
- 状态：`compiled-not-hardware-verified`，未烧录、未操作设备FFat。

## 8. 2026-09-10正式源码迁移与Release索引

- 当前variant：`standard`；
- 当前正式源码：`firmware/standard/TSP`；
- 构建输入：88；
- 当前源码指纹：`84CD14958E077AE1FE4294C4C566F8CF39FE89952C5CB929301AF2CF4C553213`；
- 迁移测试构建：
  `firmware_workspace/build/firmware-2.0.22-repo-migration-test-20260910`；
- 迁移验证结论：
  `MIGRATION BUILD VERIFIED - EXPECTED NONDETERMINISTIC METADATA ONLY`；
- 正式Release：
  `firmware_workspace/releases/2.0.22/20260907-gas-pacing-diagnostics`；
- Release状态：`packaged-not-hardware-verified`。

迁移测试BIN与2026-09-07 BIN不是bit-identical。全部不同字节已归因于ESP32 Core 3.3.7
编译日期/时间、ELF SHA、image checksum和validation hash等非确定性构建元数据，
未发现无法解释的业务payload、常量、配置或代码布局差异。Release仍使用9月7日构建的
应用BIN，尚未取得现场硬件验证状态。
