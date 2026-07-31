# TSP ESP32-S3 新会话接力文档

更新时间：2026-07-31  
当前固件版本：`2.0.4`  
当前状态：`2.0.3` 已完成约 74 分钟硬件取证；`2.0.4` 已按证据完成
CSQ 保留和 LED 队列定向修复并编译通过，等待重新烧录和长时间测试。

## 1. 新会话首先要做什么

1. 完整阅读本文件。
2. 阅读同目录下的：
   - `CHANGELOG_2.0.3.md`
   - `CHANGELOG_2.0.4.md`
   - `DEV_LOG_2026-07-31.md`
3. 除非用户明确要求，不要继续修改小时统计的采集和结算逻辑。
4. 用户下一步应烧录 `2.0.4`，验证 CSQ 瞬时失败、LED 队列和堆内存趋势。

## 2. 唯一正确的当前源码目录

本轮实际修改和编译的源码是：

```text
D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\github_compile_source_1933cb0\TSP
```

注意：工作区内还有另一个目录：

```text
D:\ChatGPT-Pro\TSP-ESP32-S3\TSP
```

它是早期/原始源码副本，不是当前 `2.0.3` 修改版。新会话不能在未核对的情况下修改错误目录。

## 3. 项目和硬件背景

- 主控：ESP32-S3
- Arduino ESP32 Core：3.3.7
- Flash：16 MB
- 分区：3 MB APP / 9 MB FATFS / 16 MB
- PSRAM：Disabled
- CPU：240 MHz
- HJ212 DTU：银尔达 M100M-B2
- DTU 厂家确认逐包发送的参考间隔约为 600 ms；固件使用 1000 ms 安全间隔。
- SHT30 是独立 I2C 温湿度传感器，不走 RS485。
- 屏幕实时查询 `get_data` 当前请求约 10 个传感器 ID。
- 屏幕历史查询 `get_records` 每次只查询一个时间点，不会一次读取整段历史。

主要串口：

| 功能 | 端口/引脚 | 波特率 |
| --- | --- | ---: |
| LCD | 软件串口，RX GPIO15 / TX GPIO16 | 9600 |
| RS485 | 软件串口，RX GPIO4 / TX GPIO5 | 9600 |
| HJ212 DTU | 软件串口，RX GPIO12 / TX GPIO13 | 9600 |
| Remote DTU | Serial2，RX GPIO38 / TX GPIO37 | 115200 |
| USB 日志 | Serial | 9600 |

## 4. 用户当前目标

长期运行时不能出现：

- HJ212 实时数据缺失几分钟；
- 小时数据缺失一小时；
- 屏幕操作后 ESP32 堆内存持续下降并重启；
- CSQ 查询与 HJ212 实时上传、pending 补传争抢串口；
- SD/pending 文件出现前部大量零字节；
- pending 补传造成明显瞬时内存压力。

用户明确要求：

- 小时统计采集逻辑暂时不改。
- 修改位置要有版本注释。
- 固件要有明确版本号。
- 每次修改同步维护 Markdown 开发记录。

## 5. 旧日志已经确认的事实

主要分析日志：

```text
C:\Users\LK\Desktop\日志\8-56日志.txt
```

旧日志运行的是 Firmware `2.0.2`。

已确认：

- 第一次运行约 15:11–16:41，屏幕查询约 95 次，堆内存从约 23 KB 降到约 4 KB 后重启。
- 第二次运行约 16:42–18:20，屏幕查询约 138 次，再次从约 23 KB 降到约 4.5 KB 后重启。
- 异常栈使用 `.build_202\TSP.ino.elf` 解码后，最终失败位置是：

```text
operator new
→ std::map clone
→ DataManager::processQuery()
→ pkg->processed_data_map = _last_real_snapshot;
```

这表示系统低内存时，屏幕 `get_data` 复制完整实时 `map` 成为最终触发点。它不代表所有下降都来自单一泄漏；临时队列、动态 JSON、map 节点和其他任务共同造成了堆压力与碎片。

日志还显示：

- `AllProcessedDataPacket` 和 `JSONCmdData` 基本都有删除记录。
- HJ212 成功 ACK 数量较多，但仍有少量终态发送失败和构包失败。
- 存在 `CSQ_LOCK_BUSY`，但没有证据证明它是全部数据缺失的唯一根因。
- pending 恢复任务在低内存时申请临时 5 KiB 栈，会进一步压缩最大连续内存块。
- SD 上曾发现前部大量零字节、尾部才有 HJ212 内容的 `.invalid` 文件。
- `QUEUE_PRESSURE queue=LED` 出现过，尚未观察到对应的明确 `EVENT_DROP`，继续监控即可。

## 6. Firmware 2.0.3 已完成的修改

### 6.1 屏幕实时查询

旧流程：

```text
get_data
→ 创建 JSONCmdData
→ 创建临时 EventBus 响应队列
→ 复制完整 _last_real_snapshot std::map
→ 动态 String 拼接 JSON
→ 返回并逐项释放
```

新流程：

```text
get_data
→ 直接读取原 JSON 中的 ids
→ 锁定实时快照，按 ID 读取所需值
→ 写入固定数组
→ 使用固定 1024 字节缓冲区生成 JSON
→ 直接回复屏幕
```

特点：

- 不复制完整 `std::map`。
- 不创建临时 EventBus 队列。
- 不创建临时 `AllProcessedDataPacket`。
- `get_data` 最多支持 16 个 ID，当前屏幕约 10 个，容量足够。
- 实时快照锁和 `systemInfo` 锁最长等待 100 ms。
- 返回字段保持兼容。
- 诊断标记：`GET_DATA_DIRECT`。

其他屏幕操作未改变协议：

- `login/logout`
- `get_config/set_config`
- `get_records`
- `gal_data`
- `upload`
- `restart`
- `dtu_command`

`get_records` 仍是低频、单时间点查询；增加 SD 互斥后可能短暂等待，但不会与保存和补传并发破坏 FAT。

### 6.2 HJ212 串口长期调度

- HJ212 实时上传、CSQ、时间查询、IP 设置和命令统一由 `DTUManager` 获取 `SERIAL_HJ212` 锁。
- 实时上传登记“等待中/发送中”状态并具有优先权。
- CSQ 遇到上传等待、正在上传或刚上传完成时返回 `CSQ_DEFERRED`。
- `updateSetupTask` 收到 deferred 后保留上次 CSQ，不把它当成断网。
- CSQ 查询周期保持 60 秒。
- HJ212 逐包间隔保持 1000 ms。
- 诊断标记：
  - `CSQ_DEFER`
  - `HJ_TX_DEFER`
  - `TX_ATTEMPT`
  - `TX_RESULT`

### 6.3 pending 补传

- 删除每次补传临时创建的 5 KiB FreeRTOS 任务。
- 复用常驻维护任务，并将其固定栈从 4 KiB 调整到 6 KiB。
- 每五分钟扫描一次，每个周期最多恢复一个 pending 包。
- 重建失败可能是暂时内存不足，因此保留 marker，后续再试，不立即隔离。
- 诊断标记：
  - `RECOVERY_RETAIN`
  - `RECOVERY_DONE`

### 6.4 SD/FAT 完整性

- `filesysManager` 增加 SD 互斥锁。
- 串行化：
  - 实时/分钟/小时/日数据保存
  - 屏幕历史读取
  - pending 保存
  - pending 扫描和读取
  - pending 删除和隔离
- pending 写完后重新打开文件并逐字节对比。
- 验证失败会删除本次错误文件并记录：

```text
PENDING_WRITE_VERIFY_FAIL
```

## 7. 明确没有修改的内容

- 分钟、小时、日统计的计算方式。
- 小时边界结算逻辑。
- HJ212 2017/2025 报文业务内容。
- 屏幕 JSON 字段结构。
- M100M-B2 的逐包发送原则。

## 8. 当前编译结果

Arduino CLI 已完整编译成功：

```text
Sketch uses 620472 bytes (19%) of program storage space.
Global variables use 25936 bytes (7%) of dynamic memory,
leaving 301744 bytes for local variables.
```

编译输出目录：

```text
D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\github_compile_source_1933cb0\TSP\.build_203
```

主要文件：

```text
.build_203\TSP.ino.bin
.build_203\TSP.ino.elf
.build_203\TSP.ino.merged.bin
```

Arduino FQBN：

```text
esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default
```

## 9. 下一轮测试步骤

1. 在 Arduino IDE 中打开当前源码目录的 `TSP.ino`。
2. 编译并重新烧录，因为 `2.0.3` 已修改固件代码。
3. 启动日志必须看到：

```text
Firmware version: 2.0.3
```

4. 清理测试用 SD 数据后，从设备启动开始连续记录 USB 日志。
5. 正常运行至少 2 小时，期间持续操作 LCD：
   - 实时查询；
   - 历史查询；
   - 页面切换；
   - 用户实际会执行的其他操作。
6. 同时保留 SD 卡中的：
   - 日期目录，例如 `E:\20260731`
   - `E:\history`
   - `E:\pending`
7. 将完整日志和以上目录交给新会话分析。

## 10. 新日志判定标准

正常现象：

- `GET_DATA_DIRECT ids=10 ...`
- `free` 和 `largest` 有小幅波动，但不持续单向下降到几 KB。
- 上传繁忙时偶发 `CSQ_DEFER`。
- 大部分发送为 `TX_RESULT ... ack=1`。
- 有待补传数据时每五分钟最多出现一次 `RECOVERY_DONE`。

需要重点告警：

- `PENDING_WRITE_VERIFY_FAIL`
- `SD_LOCK_BUSY` 持续高频出现
- `DATA_QUERY_OOM`
- `HJ_BUILD_FAIL`
- `TX_RESULT ... ack=0`
- `EVENT_DROP`
- `UART_OVERFLOW`
- 新生成的 `.pkt` 文件前部出现大量零字节
- `free` 或 `largest` 随每次屏幕查询持续下降
- 小时边界没有生成小时文件或没有上传小时数据

注意：用户关闭设备后日志仍在采集造成的后续失败，不应误判为设备运行故障。

## 11. 关键源码文件

```text
TSP.ino
src\app\permissionManager\permissionManager.cpp
src\app\dataManager\dataManager.cpp
src\app\dtuManager\dtuManager.cpp
src\app\filesysManager\filesysManager.cpp
src\system\system\system.cpp
src\system\event\eventBus.cpp
src\inc\sys_init.h
```

版本记录：

```text
CHANGELOG_2.0.3.md
DEV_LOG_2026-07-31.md
```

## 12. 尚未完成和不能过早下结论的事项

- `2.0.3` 只完成了代码与编译验证，尚未获得烧录后的长时间硬件日志。
- 尚不能宣称实时/小时数据缺失已经完全解决，必须通过新日志、SD 文件和服务器数据联合确认。
- LED 队列压力需要继续观察，但没有新证据前不要与本次稳定性修改混在一起大改。
- 若仍发生内存下降，应使用 `.build_203\TSP.ino.elf` 解码新崩溃栈，不能继续使用旧的 `.build_202` ELF。

## 13. 推荐的新会话首条消息

复制下面内容到新会话：

```text
请继续处理 TSP ESP32-S3 项目。

当前实际源码目录是：
D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\github_compile_source_1933cb0\TSP

请先完整读取：
SESSION_HANDOFF_2026-07-31.md
CHANGELOG_2.0.3.md
DEV_LOG_2026-07-31.md

当前 Firmware 2.0.3 已编译通过，小时统计逻辑明确不允许修改。
下一步先根据我提供的新日志和 SD 卡目录验证屏幕内存、HJ212 实时/小时上传、
CSQ 延后调度、pending 补传和 SD 文件完整性。没有证据前不要继续扩大修改范围。
```

## 14. 后续维护建议

以后每次重要修改采用“三文件结构”：

1. `SESSION_HANDOFF_YYYY-MM-DD.md`：只保留新会话必须知道的完整状态。
2. `DEV_LOG_YYYY-MM-DD.md`：记录当天任务、证据、分析和修改。
3. `CHANGELOG_X.Y.Z.md`：记录具体固件版本的代码变化和兼容性。

每次准备开启新会话时，只更新或新建一份最新的 `SESSION_HANDOFF`，不要让新会话依赖聊天记录推断项目状态。
