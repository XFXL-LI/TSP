# TSP ESP32-S3 项目当前交接文档

最后更新：2026-08-20
当前阶段：设备运行2.0.12时确认14因子HJ212十分钟包和小时包存在瞬时构包内存峰值；
清空旧pending后问题仍可复现。Firmware 2.0.13已完成单缓冲区构包和无分配校验，
完整编译及镜像校验通过、尚未烧录。14因子全量`set_config`在碎片堆下仍可能失败，
局部查询/合并接口留待后续单独实施。2.0.10的K-7S采集和单位换算继续保留；默认
正式发布和回退包仍为2.0.4。

> 本文件是新会话的首要入口。新会话必须先完整读取本文件及
> `CHANGELOG_2.0.13.md`、`CHANGELOG_2.0.12.md`、`CHANGELOG_2.0.11.md`、`CHANGELOG_2.0.10.md`、`CHANGELOG_2.0.9.md`、
> `CHANGELOG_2.0.8.md`、`CHANGELOG_2.0.7.md`、
> `CHANGELOG_2.0.6.md`、`CHANGELOG_2.0.5.md`及`CHANGELOG_2.0.4.md`，先区分
> 2.0.13待HJ212构包实机验证状态、2.0.12的HJ212与LCD配置复现状态、2.0.11禁止使用状态、
> 2.0.10待四气体/LCD/HJ212完整联调状态、
> 2.0.9连续运行与待Remote发送端联调状态、
> 2.0.8成功联调状态、
> 2.0.7 OTA成功但LCD恢复未确认状态、2.0.6 OTA现场测试状态、
> 2.0.5用户冒烟状态与2.0.4正式实机验证状态，再决定是否烧录。

## 最新记录：2026-08-18 Firmware 2.0.9连续运行与损坏pending文件

日志 `C:/Users/LK/Desktop/ESP-LOG/8.17.1807.log` 覆盖2026-08-17
18:07至2026-08-18 11:04。18:07:47正常上电后，Firmware 2.0.9连续运行
约16小时56分钟，完成1017轮严格60秒周期采集；10个collector持续正常，
未出现非预期重启、UART overflow、JSON错误或SD保存失败。

该日志同时确认：

1. LCD normal状态在发送后约180ms收到ACK，恢复链路正常。
2. 实时、10分钟、小时、日数据分别正常生成和保存；全部1138次STORE均为
   ok=1。
3. HJ212仅有一包实时数据在3次发送后无ACK，随后正确保存为pending，并在
   约3分钟后补发成功、自动删除。
4. 112条Modbus timeout均为单次尝试级超时，重试后仍形成10项有效采集。
5. SHT30出现5次-101，但均自行恢复，未造成系统任务停止。

发现一个早于本次启动的历史损坏文件：

- 路径：`/sdcard/pending/1/20260817174000.pkt`
- 类型：10分钟数据，type=1
- 大小：15字节，明显小于正常HJ212分钟包
- 重建结果：对应源文件
  `/sdcard/20260817/min/17/40.dat`不存在，rebuild_unavailable
- 当前行为：每约5分钟重复输出PENDING_INVALID和RECOVERY_RETAIN；不影响
  采集、SD保存和正常上传，但会增加日志及SD扫描

结合文件时间、截断大小和后续约17小时未再产生同类损坏文件，判断其大概率是
此前停机、烧录引起的复位或写文件期间突然断电留下的一次性残留，不作为
Firmware 2.0.9正常运行故障。

当前维护建议：

1. 先备份SD卡，再只删除上述无法重建的15字节文件。
2. 当前2.0.9无需因此回退或紧急修改。
3. 长期无人值守或批量部署前，将以下内容记录为后续维护版本的防御性改进：
   - pending先写临时文件，flush、close和校验成功后再重命名为pkt；
   - 扫描到损坏pkt时只尝试重建一次；
   - 无法重建时改名为bad或移入pending_bad，停止反复扫描；
   - 同一路径只记录一次告警，并限制隔离文件数量和总容量；
   - OTA重启前等待SD写任务空闲并关闭当前文件。

本记录只新增文档，不代表已经修改上述pending逻辑。小时统计、HJ212逐包
3000ms、SHT30独立、DEBUG开启及未经授权不得烧录等约束保持不变。

## 0. 2026-08-14 Firmware 2.0.9 Remote DTU协议状态

- 当前唯一开发源码已更新为Firmware 2.0.9；默认正式回退包仍为2.0.4；
- 请求明确包含整数`protocol=1`时进入`protocol-v1`，不带`protocol`时保持旧
  英文legacy路径；
- JSON状态覆盖`accepted/ready/transferring/verifying/success/failed/rejected`；
- Remote非零会话与LCD会话独立；会话前拒绝固定`session=0`；
- `ready`明确返回`chunk_size=800`、`interval_ms=1000`和
  `inactivity_timeout_ms=90000`；
- 传输进度按首次跨过5%输出，不按每次Flash写入刷JSON；
- 状态使用固定256字节缓冲和`snprintf()`，没有新增任务或动态大型缓冲；
- 完整编译：Sketch 630712字节，全局变量26816字节，应用BIN 630864字节；
- 应用BIN SHA-256：
  `F202BA3A5784B936F2D089410BED1AA019276F36CFFEB6A3616AF16B2F3B6849`；
- 源码输入指纹：
  `3E7ED8C9EA903B3292C8E8E8189CBCECADEB0AAC97C6474F3FEAC48958CFCD40`；
- 构建目录：`firmware_workspace\build\firmware-2.0.9-upload-status`；
- 2026-08-14构建完成时状态为`compiled-not-hardware-verified`，Codex未执行烧录；
  后续用户已将2.0.8远程升级至2.0.9，并完成LCD normal ACK及约17小时连续运行
  验证；新版Remote发送端的完整协议联调仍未完成；
- 正式协议和发送端任务分别见根目录`REMOTE_DTU_OTA_SENDER_PROTOCOL.md`和
  `REMOTE_DTU_OTA_SENDER_TASK_HANDOFF.md`；
- 详细修改和测试要求见`CHANGELOG_2.0.9.md`。

## 0.1 2026-08-14 Firmware 2.0.8 LCD normal恢复确认状态

- 当前唯一开发源码已更新为Firmware 2.0.8；默认硬件验证发布包仍为2.0.4；
- 2.0.7已完成一次627072字节远程OTA，准备ACK、进度、校验、重启和2.0.7启动
  均成功，但LCD未响应重启后的两次`normal`，约25分钟后由屏幕超时恢复；
- LCD V1.0.12收到任何`normal`后无条件恢复并回复
  `session=0,state=normal,code=OK` ACK；
- MCU初始化后立即发送首条`normal`，之后每2秒非阻塞重发，收到ACK立即停止；
- 从首条开始30秒无ACK则只记录`LCD_OTA_NORMAL_ACK result=timeout`，不判定为
  OTA失败，不影响普通业务；
- OTA失败后的恢复也使用相同机制，新OTA开始会取消旧的`normal`重试；
- 没有新增任务、软件定时器或动态缓冲；DEBUG和所有冻结约束保持不变；
- 完整编译：Sketch 627720字节，全局变量26808字节，应用BIN 627872字节；
- 应用BIN SHA-256：
  `90E066D25798BBB76752F74E156CAF18E2EE4F58A441D31D9343FD7020FD8DB1`；
- 源码输入指纹：
  `AEB27CA06DEF242E68B1C191F0D8A8CC75CA630AC64D6C9512AED8717396EE7D`；
- 2026-08-14完成627872字节远程OTA、校验、重启和2.0.8启动；第一条normal
  未获ACK，第二条按2秒机制重发后成功，LCD约3.09秒后恢复`get_data`；
- 日志中无normal ACK timeout、`UART_OVERFLOW`、JSON接收错误或OTA失败；
- 烧录和远程OTA由用户操作，Codex没有执行烧录；
- 详细修改和测试要求见`CHANGELOG_2.0.8.md`。

## 0.2 2026-08-13～14 Firmware 2.0.7 LCD OTA交互状态

- 当前唯一源码目录已更新为Firmware 2.0.7；默认硬件验证发布包仍为2.0.4；
- 主板新增`preparing/transferring/verifying/restarting/failed/normal`状态JSON；
- `preparing`等待LCD ACK最多2秒，LCD未连接、旧程序或ACK超时都不阻止OTA；
- OTA期间LCD任务持续读取并丢弃输入，不解析普通命令，避免1024字节RX缓冲积压；
- LCD解析器在OTA状态切换时清除半包、括号层级和超时状态；
- 传输进度每跨过5%通知一次，不逐800字节分包通知；
- LCD/DTU普通响应和OTA状态使用串口互斥保护的完整单行发送；
- DEBUG已确认开启，小时统计、HJ212 3000ms间隔和SHT30代码未修改；
- 完整编译：Sketch 626916字节，全局变量26800字节，应用BIN 627072字节；
- 应用BIN SHA-256：
  `4F37E77B9F52B0A154CD2F94B20ABB71E1C55D3EB723D9911DB1DECD4B30BB86`；
- 源码输入指纹：
  `AF23FBD1DE343752459A6144794599A428C499AC13E5CEAE6316E545E11E1596`；
- esptool镜像checksum和validation hash均有效；
- 2026-08-14已完成一次完整远程OTA并成功启动2.0.7；LCD恢复确认未通过；
- 主板/LCD完整对接见根目录`LCD_OTA_INTERACTION_PROTOCOL.md`；
- GitHub分支`codex/firmware-2.0.7-lcd-ota`，提交`e2c079f`，草稿PR #4。

## 0.3 2026-08-12～13 Firmware 2.0.6单任务OTA状态

- 当前唯一源码目录已更新为Firmware 2.0.6；默认硬件验证发布包仍为2.0.4；
- 2.0.5实机日志连续运行约15小时22分钟，最大连续内部堆块始终为7668字节，
  证明运行中动态申请8KB OTA worker存在明确失败风险；
- 2.0.6删除动态worker，开机一次创建6KB单OTA任务；同一任务平时以优先级4
  监听，升级时提升为11，失败恢复后重新降为4；
- 1KB接收缓冲改为静态缓冲；新增OTA前后空闲堆、最大连续块、会话最低堆和
  任务栈高水位日志；
- 业务仍通过活动计数在安全点协作静默，不删除或强制挂起任务；失败会释放DTU、
  恢复业务且不重启；
- 连续两次干净编译均为624032字节；最新构建SHA-256为
  `A98A7E32D798E47C270286A5717295EF4F649116D33E98A50FF3C4B397877C44`；
- 源码输入指纹为
  `429466118EFA865D58FACE8D56B1E659BCA7C54744C13C046C989BB77E04E9CA`；
- 两次应用BIN哈希不同但源码输入指纹和容量一致，当前工具链存在非确定构建元数据；
- 2.0.6随后已烧录并完成现场OTA测试：一次镜像校验失败后安全恢复，一次完整
  OTA成功并重启恢复业务，另一次因上传请求/Ready握手未建立而没有进入OTA；
- 分包序号、分包CRC、应用层ACK/重传以及升级后自动回滚仍未加入；
- 完整修改和验证要求见`CHANGELOG_2.0.6.md`。

## 0.4 2026-08-11 Firmware 2.0.5整合状态

- 当前唯一源码目录已更新为Firmware 2.0.5，但默认硬件验证发布包仍为2.0.4；
- 2.0.5以8月10日当前源码为底座，完整保留采集漂移、秒级时间、RTC优先启动、
  首次有效CSQ后校时、24小时调度和HJ212上传避让修复；
- 已合入具名任务优先级、115200调试串口、日志互斥、Remote DTU 2048字节RX
  缓冲、`UPLOAD_RES`引用释放和独立OTA模块；
- LCD和Remote DTU解析任务继续保持优先级10；
- OTA使用活动业务计数确认静默，不再固定等待250ms，也不强制删除或挂起任务；
- OTA仍缺少分包序号、CRC、应用层ACK和重传，不能作为可靠OTA正式上线；
- 最终完整编译应用BIN为624016字节，SHA-256为
  `00BA20C9909A84FC6252FA3EE2D44B7219F3DCB3BB142D4E5AC2B844BFD24E44`；
- 用户随后自行烧录了显示2.0.5的构建并完成约15小时22分钟冒烟运行，但确切
  烧录BIN哈希未与本文件记录绑定，且OTA没有被触发；
- 完整清单、风险和测试要求见`CHANGELOG_2.0.5.md`。

## 1. 当前目录与 Git 状态

实际烧录源码目录：

```text
D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\Firmware_2.0.3_original\TSP
```

### 1.1 2026-08-10 工作区整理后的唯一入口

当前唯一实际源码仍为：

```text
D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\Firmware_2.0.3_original\TSP
```

后续查看状态、编译、发布包校验和受保护烧录统一从以下目录进入：

```text
D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace
```

其中：

- `status.cmd`：显示当前源码、DEBUG状态、默认发布包、构建大小和哈希；
- `build-current.cmd`：使用ESP32 Core 3.3.7原参数完整编译，不烧录；
- `validate-release.cmd`：校验默认发布包四段BIN的SHA-256，不烧录；
- `flash-verified.cmd COM9 FLASH`：受保护烧录入口，只有明确提供端口和确认词才执行；
- `releases\2.0.4\2026-08-10_hw-verified`：当前默认已上机验证发布包；
- `archive`：整理前历史构建和临时副本，不得作为烧录来源。

已验证应用BIN为623440字节，SHA-256：
`6CE8B44EA4BF509815A7CE57470B1CC79CA84F85355BF6E4F2D1CB6EA13A4773`。
统一脚本重新编译结果的空间占用和源文件指纹一致，但因ESP32 Core嵌入编译时间，BIN
哈希不同，仍标记为未实机验证。常规发布包不包含merged.bin，四段烧录不擦除FFat。

本次整理没有修改固件业务逻辑，也没有烧录。小时统计、HJ212 3000ms逐包间隔、SHT30
独立处理和DEBUG开启状态均保持不变。

注意：目录名仍包含 `Firmware_2.0.3_original`，但其中实际源码版本已经是
Firmware 2.0.12。

Firmware 2.0.9 Remote DTU JSON改动目前只在上述实际源码目录中，尚未提交或推送。
Firmware 2.0.6对应GitHub分支为`codex/firmware-2.0.6-single-ota`、草稿PR #2。
Firmware 2.0.7对应GitHub分支为`codex/firmware-2.0.7-lcd-ota`、草稿PR #4。
Firmware 2.0.8对应GitHub分支为`codex/firmware-2.0.8-normal-ack`、提交
`4869c6f`、草稿PR #5。

以下是早期2.0.4历史Git工作树，不是当前2.0.7开发入口：

```text
D:\ChatGPT-Pro\TSP-ESP32-S3\worktrees\firmware-2.0.4-csq-led
```

当前 Git 信息：

```text
remote: https://github.com/XFXL-LI/TSP.git
branch: agent/firmware-2.0.4-csq-led
commit: 以该分支当前 HEAD 为准
PR: https://github.com/XFXL-LI/TSP/pull/1
```

2026-08-06 已重新核对实际烧录目录中的 76 个 `.ino/.cpp/.h/.c` 正式源码
文件，并把 FFat 成功返回值、空内容保护、写入字节数校验、默认报警值、气压
单位和设备版本号等有效差异同步到上述 Git 分支。实际目录的 `system.cpp` 仅有
DEBUG 说明注释/空白差异，业务行为与 Git 分支相同；Git 中的 `config.cpp` 已
统一为 UTF-8 编码，未改变其中业务逻辑。

项目原始代码位于：

```text
D:\air\TSP
```

该目录是 Firmware 2.0.0 基线。原始代码与当前代码均有 76 个编译源码
文件；其中 45 个完全一致，26 个有实质修改，另外 5 个仅存在换行或格式
差异。不要使用 `D:\air\TSP` 覆盖当前源码，否则会丢失数据稳定性修复。

## 2. 必须遵守的限制

1. **小时统计逻辑暂时不允许修改。**
2. HJ212 逐包间隔保持 **3000 ms**。
3. 先分析串口日志、SD 卡文件和平台接收数据，再决定是否修改代码。
4. 不允许因为单条错误日志直接推断数据丢失或直接改代码。
5. SHT30 的 `-101` 问题作为独立问题处理，不应混入当前数据稳定性修改。
6. 未经新证据确认，不改变现有传感器有效值判断规则。
7. 当前测试期间不要清空或删除 SD 卡数据，除非用户再次明确要求。

## 3. 小时统计冻结说明

当前小时统计仍保持原有规则：

- 每 3 分钟向小时统计累积一次有效采样；
- 整点结算上一小时；
- 小时均值进入日统计；
- 分钟、小时、日数据的边界、存储和分发规则未修改。

Firmware 2.0.4 仅向数据包增加 `trace_id` 等诊断信息，没有改变上述计算
规则。此前出现“平台小时数据没有 L90、但实时数据能识别 L90”的问题暂时
搁置，不在本轮数据稳定性测试中修改小时统计。

## 4. 当前因子和屏幕约束

- 程序最多按 14 个因子考虑。
- 当前硬件测试配置启用了 10 个因子：
  `a34005`、`a34004`、`a34002`、`a34001`、`a01001`、`a01006`、
  `a01007`、`a01008`、`a01002`、`L90`。
- 噪声 `LA` 和 `L90` 共用同一个噪声采集器，暂不调整。
- TVOC 后续采用独立读取方案，当前不处理。
- 文档统一称双向交互屏为“大彩 HMI 屏”；源码中仍沿用 `LCD`、
  `SERIAL_LCD` 等历史命名。
- 文档统一称只接收数据、不回复的灯珠设备为“LED 灯珠灯箱”；源码中仍沿用
  `LED`、`SERIAL_LED` 等命名。
- LED 灯珠灯箱端程序和 485 报文格式不需要修改。
- ESP32 端把一轮 LED 灯珠灯箱显示预算控制为 55 秒，给下一分钟数据预留约
  5 秒：
  - 10 个因子时每个约 5500 ms；
  - 14 个因子时每个约 3928 ms。
- 55 秒只控制 LED 轮显任务，不改变 60 秒传感器采集周期，也不阻塞
  HJ212 上传和 SD 保存任务。

## 5. Firmware 2.0.4 核心修改

### 5.1 数据对象生命周期和堆内存

- 修复采集无效时 `SystemRuntimeStatus` 报警对象未释放造成的持续堆泄漏。
- 报警发布者按订阅者数量增加引用，订阅者处理后释放，发布者释放初始引用。
- 修复临时 EventBus 队列名称节点残留问题。
- 增加数据包引用计数和释放诊断。

### 5.2 HJ212 构包和发送

- 实时 HJ212 上传优先于 CSQ 等维护命令。
- 所有 HJ212 串口访问统一由 `DTUManager` 仲裁。
- HJ212 2017/2025 构包检查 `String::reserve()` 结果。
- 构包后检查帧头、长度、必要字段、结尾和 CRC。
- 构包失败时不发送残缺的 14 字节空包。
- HJ212 逐包间隔当前为 3000 ms。

### 5.3 CSQ

- CSQ 查询与上传冲突时返回 `CSQ_DEFERRED`，不抢占实时上传。
- `CSQ_DEFERRED` 或无效 CSQ 后约 5 秒重试。
- 正常 CSQ 查询周期保持 60 秒。
- CSQ 返回 `99` 时保留上一有效值，并记录 `CSQ_INVALID_KEEP`。
- pending 维护计时不再依赖本轮 CSQ 查询成功，避免60秒同相位长期冲突。

### 5.4 Pending 和 SD 恢复

- 网络发送失败时把完整报文保存到 SD 卡 `pending`。
- pending 写入后重新读取并逐字节校验。
- 常驻维护任务按约 5 分钟间隔扫描，每批最多恢复 1 个文件。
- 网络从异常恢复时允许立即触发扫描。
- HJ212 因临时低内存无法构包时写入 `REBUILD_FROM_SD` 标记。
- 恢复任务可从对应 raw/min/hour/day SD 数据重新构包并补传。
- 无法临时重建时保留标记，不立即隔离。
- SD 存储、历史查询和 pending 恢复使用统一互斥保护。

### 5.5 大彩 HMI 屏和 LED 灯珠灯箱

- 大彩 HMI 屏 `get_data` 直接从受保护快照复制所请求的因子，不再建立临时 EventBus
  队列，也不复制完整 `std::map`。
- LED 灯珠灯箱只显示实时包，分钟、小时、日包立即释放并记录
  `LED_SKIP_NONREAL`。
- LED 灯珠灯箱每轮开始前合并积压快照，只保留最新实时数据并记录
  `LED_COALESCE`。
- LED 灯珠灯箱一轮显示预算为 55 秒。

### 5.6 PM 采集诊断

- PM1、PM2.5 无有效样本时记录：

```text
PM_COLLECT_INVALID id=... read_failed=... zero=... invalid_register=...
```

- 该诊断只区分 Modbus 读取失败、零值和异常寄存器值。
- “零值无效”的原有规则未修改。

## 6. 编译和版本状态

2026-08-10 当前实际源码已使用 ESP32 core 3.3.7 和原项目 FQBN/分区参数完成
完整集成编译，不再只是修改单元的分别编译：

```text
Sketch: 623296 bytes (19%)
Global variables: 25936 bytes (7%)
Free for local variables: 301744 bytes
```

生成的应用二进制为 623440 字节。随后已通过 COM9 烧录到确认的 ESP32-S3
（MAC `e0:72:a1:d2:55:c4`），所有写入段 Hash 校验通过；应用最高写入地址仍为
`0x000A8FFF`，未触及从 `0x00610000` 开始的 FFat 分区。

2026-08-04 14:49 的当前实际源码编译已经成功：

```text
Sketch: 622584 bytes (19%)
Global variables: 25936 bytes (7%)
Free for local variables: 301744 bytes
```

Arduino IDE 通过 COM9 完成烧录，全部写入段 Hash 校验通过。本次生成的应用
二进制为 622736 字节；已核对构建副本和二进制字符串，确认 FFat 成功返回值
`0`、空内容保护、实际字节数检查及相应日志均已进入固件。

普通烧录的最高擦写地址为 `0x000A8FFF`，FFat 分区起始地址为
`0x00610000`，所以板内已有 FFat 配置没有被本次烧录擦除。

主要版本宏：

```text
TSP.ino: 2.0.4
src/inc/sys_init.h: 2.0.4
```

`src/app/configManager/system_json.h` 中的设备版本号已经同步修正为 `2.0.4`，
与 `TSP.ino`、`src/inc/sys_init.h` 及启动日志保持一致。该修正只影响设备信息
查询显示，不改变数据稳定性业务逻辑。

## 7. 已完成的日志验证

### 7.1 2026-08-03 11:03～13:29 日志

日志文件：

```text
C:\Users\LK\Desktop\ESP-LOG\esp32_2026-08-03.log
```

结果：

- `trace=1～146` 连续；设备时间 `11:03:59～13:28:59` 每分钟连续。
- 产生 146 个实时包、14 个十分钟包、2 个小时包。
- 完整结束的 `trace=1～145` 全部完成处理和 SD 保存。
- 14 个十分钟包和 2 个小时包均保存成功并收到上传 ACK。
- 8 个实时包首次发送失败，全部进入 pending 并最终补传成功删除。
- 启动前遗留的 `08:50:59` pending 文件也补传成功删除。
- 没有重启、崩溃、看门狗、内存分配失败、`HJ_BUILD_FAIL`、
  `QUEUE_PRESSURE`、`EVENT_DROP`、SD 锁冲突或存储失败。
- 最大连续堆在稳定阶段保持约 7668 字节；可用堆有短时波动，但能够恢复，
  未出现此前持续下降到约 5 KB 后崩溃的趋势。
- 最后的 `trace=146` 日志在写入过程中被截断，不能算作数据缺失。

### 7.2 2026-08-03 14:44～14:58 正式测试前短日志

日志文件：

```text
C:\Users\LK\Desktop\ESP-LOG\1.txt
```

结果：

- 正常上电启动并显示 Firmware 2.0.4。
- 成功加载 10 个因子、SD 卡及所有配置文件。
- 启动扫描 pending 为 0。
- `trace=1～15` 连续，每分钟一个实时包。
- 15 个实时包和 1 个十分钟包全部 `STORE ok=1`。
- 14 个实时包首次发送即收到 ACK。
- `trace=7 / 20260803145019` 首次连续 3 次无 ACK，随后进入 pending，
  被维护任务补传成功并删除。
- 无崩溃、重启、内存分配失败、队列压力、事件丢弃或 HJ212 构包失败。
- 初始化后堆稳定在约 15.1 KB，最大连续块稳定约 7668 字节。
- 文件开头 152 行重复 `_DIRECT...` 是串口日志残留/截断，不是固件异常。

### 7.3 2026-08-03 16:02～2026-08-04 14:03 正式测试第一段

- SD 卡中共有 1322 个实时 raw 文件、132 个十分钟 min 文件、22 个小时
  hour 文件和 1 个日 day 文件；每个文件均为 400 字节，10 条因子记录结构
  完整。
- 已取得的串口日志片段中，205 个实时包、21 个十分钟包和 4 个小时包均能
  对应到 `STORE ok=1`。
- 日志片段内 19 个首次发送失败的报文均保存到 pending，并在后续维护扫描中
  补传成功；检查时未遗留 pending 文件。
- 该段测试因 2026-08-04 14:50 主动烧录新固件而结束，不属于异常重启。
- 该段之后的烧录后正式运行证据已经取得，最新结论见 7.6。

### 7.4 2026-08-04 FFat 写入修正状态

- `config.cpp` 中两处 `writeFFAT()` 成功判断已经由 `== 3` 修正为 `== 0`。
- `writeFFAT()` 现在拒绝空内容，并核对实际写入字节数与预期字节数；成功和
  不完整写入都会打印路径及字节数。
- 风速、风向和气压的源码默认 `alarmLimit` 为 `999`，气压默认单位为
  `kPa`；这些默认值只在板内没有对应 FFat 配置时生效。
- 编译、烧录及烧录包内容核对已完成。2026-08-04 15:00，通过大彩 HMI 屏
  登录后保存传感器配置，串口记录：

```text
FFAT write success: path=/model.json bytes=577
```

- 大彩 HMI 屏收到 `set_config code=OK` 后执行软件重启；重启日志显示
  `/model.json` 解析成功、加载成功，10 个传感器配置全部恢复，气压单位为
  `kPa`。因此 FFat 的保存、实际字节数判断、持久化和重启回读实机验证通过。
- 同一日志中 SD 卡初始化失败，`trace=8～9` 的 raw、十分钟和小时数据均记录
  `STORE ... skipped=sd_not_ready`。实时 HJ212 报文仍收到 ACK，但该段不能
  用于正式数据全链路验证；装回 SD 卡并确认初始化成功后再继续正式测试。
- SHT30 仍出现 `-101`，继续作为独立问题处理。

### 7.5 2026-08-04 正式运行日志级别

- 15:08 曾临时注释 `DEBUG` 宏，使正式运行只输出 `INFO` 以上日志。
- 用户观察到实时数据表现存在断续疑问后，于 18:07 手动重新启用
  `src/system/system/system.cpp` 中的 `#define DEBUG` 并重新烧录。
- 当前实际源码和 18:09 以后正式运行的固件均为 **DEBUG 已开启**；新会话
  不要误判为 INFO 固件，也不要自行再次关闭 DEBUG。
- 关闭 DEBUG 本身只切换日志过滤级别，没有包住其他业务代码；但大量串口
  输出变化可能改变任务运行时序。在平台、日志和 SD 三方证据不足时，不据此
  修改业务逻辑。
- 后续长时间证据显示 DEBUG 开启状态下采集、处理和 SD 保存连续；小时统计和
  HJ212 3000毫秒逐包间隔均未修改。

### 7.6 2026-08-04 18:09～2026-08-05 09:28 正式运行

串口日志：

```text
C:\Users\LK\Desktop\ESP-LOG\esp32_2026-08-04-18.log
```

SD 卡证据：

```text
E:\20260804
E:\20260805
E:\pending
E:\history
```

采集、处理和保存结论：

- 当前启动周期从 18:09 开始，SD raw 到次日 09:28 连续 920 分钟。
- 日志覆盖 `trace=2～920`，共 919 条，无缺号、重复或重启；每次均采集
  10 个因子。`trace=1 / 18:09` 虽不在日志开头，但对应 raw 文件存在。
- 02:01 自动校时后，相邻设备时间有一次只差 33 秒；trace 和分钟文件连续，
  不是漏采。
- 日志内共有 1027 个处理包：实时 919、十分钟 92、小时 15、日 1；全部
  一一对应 `STORE ok=1` 和 SD 文件，也全部有最终 `TX_RESULT`。
- SD 共 1034 个文件：20260804 为 raw 357、min 36、hour 6、day 1；
  20260805 为 raw 569、min 56、hour 9。逐文件检查均为 400 字节、10 条
  因子记录、ID 完整、时间戳一致、有效标志正常，无非法数值或损坏结构。
- 18:01～18:06 属于此前一段运行，18:07～18:08 无 raw；当前长时间启动
  周期从 18:09 开始。若该两分钟不是人工烧录/重启，需要另找更早日志定位。

HJ212 和 pending 结论：

- 1027 个正常数据包中，943 个在本轮最多 3 次尝试内取得 ACK；84 个最终
  无 ACK，但 84 个全部成功、完整写入 pending。
- 日志范围内 83 个新 pending 后续补传成功删除；另有启动前遗留的
  `20260804180619` 也补传成功。
- 已完成的 83 个恢复中，中位恢复时间约 3.15 分钟，72 个在 10 分钟内
  恢复，最长约 24.42 分钟。
- 当前唯一遗留文件：

```text
E:\pending\0\20260805092020.pkt
```

- 该文件为 400 字节，声明内容 388 字节，保存 CRC 与重新计算结果均为
  `543B`，CRLF 结尾正确；10 个因子值与
  `E:\20260805\raw\09\20.dat` 完全对应。09:21 首发 3 次无 ACK，09:26
  补传仍无 ACK，日志在 09:29 结束。不要删除，SD 卡装回设备后继续观察恢复。
- 实时包最终无 ACK 为 57/919，十分钟 19/92，小时 8/15，日 0/1。
  因此设备侧看到的短时断续集中在 DTU/服务器 ACK 波动，不是采集或 SD 漏存。
- 平台导出数据尚未提供；ACK 只能证明通信应答，最终平台入库完整性仍需平台
  数据与 SD/pending 结果交叉确认。

稳定性和外设结论：

- 无 `QUEUE_PRESSURE`、`EVENT_DROP`、`UART_OVERFLOW`、SD 锁失败、pending
  校验失败、构包失败、重启、崩溃、看门狗或内存分配失败。
- 最大连续堆长期稳定约 7668 字节；可用堆短时波动后能够恢复，日志末尾约
  14568 字节，未见持续泄漏趋势。
- 大彩 HMI 屏共有 928 次 `get_data code=OK`，0 次 NG；LED 灯珠灯箱实时
  轮显 919 次，与日志内实时采集数一致。
- Modbus 有 146 次瞬时读取超时，但最终每次采集均取得 10 个有效因子，未
  形成缺项。风速 926 个 raw 样本全部为 0，属于现场数据现象，不是文件损坏。
- SHT30 在本段出现 1779 次 `-101`，未记录到成功读取，仍按独立问题处理。
- `E:\history` 为空属正常；已知四种数据类型均保存于日期目录，未知类型才
  使用 `history/other.dat`。

当前判断：本地数据链路和 pending 保护机制通过本段正式运行验证，暂不修改
数据逻辑。下一步先恢复 `20260805092020.pkt`，再取得平台同一时段导出数据。

### 7.7 2026-08-05 MCU 开发板、GPIO48 和 DEBUG 核查

- 当前实际使用的开发板购买型号为
  `ESP32-S3-N16R8（带天线座）_焊接`，商家说明兼容
  `ESP32-S3-WROOM-1` 模组；实物为 44 针、双 Type-C、外置天线座的
  ESP32-S3-DevKitC/YD 系兼容板。
- 板载四脚可寻址 RGB 状态灯的数据输入通过标记为 `0` 的零欧姆电阻连接
  GPIO48。断电通断测试已经确认：零欧姆电阻两端及 RGB 灯右上角引脚均与
  GPIO48 导通，RGB 灯右下角接 GND；上电测得 RGB 灯电源约为 4.2 V。
- 当前固件的 1 位 SDMMC 引脚为：`CMD=GPIO48`、`CLK=GPIO47`、
  `DAT0=GPIO21`。因此 GPIO48 同时接 SD 卡 CMD 和板载 RGB 灯的数据输入。
- RGB 数据端属于输入支路，可能增加 SD CMD 的负载或引起 RGB 误动作，但
  当前正式运行已经证明 SD 连续保存，现有证据不能把此前单次 SD 初始化失败
  归因于该共线设计。
- 当前决定是不拆零欧姆电阻、不改 SD 引脚、不增加 RGB 控制代码，也不把
  GPIO48/RGB 共线混入本轮数据稳定性修改；只有新的 SD 初始化或通信证据指向
  该支路时，再作为独立硬件对照项处理。
- 2026-08-05 再次核对 `DEBUG` 宏：它只在系统初始化时把日志级别切换为
  `DEBUG` 或 `INFO`，没有包住采集、SD 保存、HJ212 上传、ACK 判断或 pending
  业务代码。`COLLECT`、`PROCESS`、`STORE`、`TX_ATTEMPT`、`TX_RESULT` 和
  `RECOVERY_DONE` 均为 INFO 以上日志。
- 今日实际源码中 `system.cpp` 仅新增了 DEBUG 说明注释和空白差异；
  `#define DEBUG` 当前仍然生效，没有形成业务代码修改。关闭 DEBUG 理论上不会
  关闭上传，但会减少串口输出并改变部分任务时序；正式测试基线继续保持 DEBUG
  开启，未经明确测试切换不重新烧录。

### 7.8 2026-08-06～08-10 数据复核与分钟时间轴修复

新串口日志：

```text
C:\Users\LK\Desktop\ESP-LOG\20260805-18.03.log
```

SD 数据副本：

```text
D:\TSP-SD-TestData
```

复核结论：

- 日志覆盖约 98 小时38分钟，5911 次实时采集 trace 连续；实时、十分钟、小时、
  日共6606个处理包全部对应成功SD保存。SD副本中的7332个文件、73370条记录
  均可完整解码，日志成功保存项无一缺失。
- SD完整时段存在13个raw分钟缺口和5个重复分钟。日志时段内为10个缺口和
  3个重复分钟；大彩HMI屏对这10个缺口的单点历史查询也全部返回无记录。
- 采集任务没有停止，问题是时间戳没有稳定地每周期前进一个分钟。主要原因是
  `CollectTask` 在 `vTaskDelayUntil()` 后重置 `xLastWakeTime`，使唤醒迟延累计；
  另一个原因是RTC/DTU校时丢弃秒并把系统秒强制归零。
- 109个已闭合小时文件全部存在，且按当前小时算法重新计算完全一致。三个小时
  因缺少位于三分钟采样点的raw分钟，仅有19个输入样本；小时算法本身未修改。

2026-08-10 先实施并完成编译、随后于 16:47 完成烧录的 Firmware 2.0.4 修复：

1. 删除 `vTaskDelayUntil()` 后的 `xLastWakeTime = xTaskGetTickCount()`，保持固定
   FreeRTOS 周期基准。
2. DTU和RTC时间链路保留秒，内部使用14位 `YYYYMMDDhhmmss`；时间初始化、系统
   校时和有效性检查继续兼容旧12位值。
3. ESP32 3.3.7 原项目参数下，修改涉及的 `system.cpp` 与 `dtuManager.cpp` 先分别
   编译通过；随后完整集成编译、COM9 烧录和写入 Hash 校验均已通过。
4. 小时统计、HJ212 3000ms逐包间隔、SHT30、大彩HMI屏、LED灯珠灯箱和DEBUG
   开关均未修改，DEBUG仍为开启状态。
5. 启动时改为RTC立即初始化系统时间，不再阻塞等待DTU取时；首次有效CSQ后约
   5秒进入首次校时尝试，在HJ212串口空闲时完成，随后恢复正常60秒CSQ周期。
6. 运行期DTU校时使用 `millis()` 固定24小时间隔；失败或上传繁忙时5分钟后重试，
   不再使用会被CSQ延迟改变实际周期的 `countTime >= 720`。
7. DTU取时会避让实时和pending上传，并在系统秒10～40的安全区间执行；网络与
   系统偏差不超过3秒时只验证不重设，超过3秒才同时更新系统时间和RTC。
8. 优化后的两个修改单元已再次按ESP32 3.3.7原项目参数编译通过；烧录和首轮
   上机结果见 7.9，24小时周期校时仍需继续运行验证。

下一轮正式测试应重点核对：raw每个墙钟分钟恰好一个400字节文件；不再出现
800字节重复分钟；闭合十分钟窗口通常为10次输入；分钟数可被3整除的采样点完整，
闭合小时恢复20个输入；同时继续核对SD、HJ212 ACK和平台数据。
校时链路还应核对 `TIME_BOOT_RTC`、`TIME_SYNC_DEFER`、`TIME_SYNC_VERIFIED` 和
`TIME_SYNC_APPLIED`，确认网络校时不会占用HJ212实时发送窗口。

当前用户确认SD卡没有pending文件，数据副本也未包含pending目录。现有日志没有
`20260805092020.pkt` 的恢复或删除证据，因此只能确认它当前不存在，不能认定为
已经由固件成功补传。

### 7.9 2026-08-10 时间修复首轮上机验证

- 当前源码完整集成编译成功：Sketch 623296 字节，动态内存 25936 字节；应用
  二进制为623440字节。
- COM9 已识别为 ESP32-S3，MAC 为 `e0:72:a1:d2:55:c4`；烧录全部写入段 Hash
  校验通过，未擦除 FFat。
- 插回测试SD卡后受控重启，启动日志确认 Firmware 2.0.4、FFat/SD初始化成功，
  并记录 `TIME_BOOT_RTC timestamp=20260810164735`，证明RTC启动链路保留秒。
- `trace=1～8` 的设备时间为 `16:47:55～16:54:55`，相邻采集严格相差60秒；
  8个实时包均完成处理，并分别保存到唯一的 `raw/16/47.dat～54.dat`，全部
  `STORE ok=1`，未出现重复分钟或 `sd_not_ready`。
- 首次网络校时到期时与实时上传相遇，正确记录
  `TIME_SYNC_DEFER reason=upload_priority`，没有抢占实时上传。5分钟后进入重试，
  在安全秒区收到 `20260810165413`，与系统时间偏差2秒，记录
  `TIME_SYNC_VERIFIED ... applied=0`，未重设系统时间或RTC。
- 8个实时包中7个首次发送收到ACK；`trace=4 / 16:50:55` 三次无ACK后完整写入
  pending，约1分钟后补传收到ACK并删除，`RECOVERY_DONE` 正常。
- 本轮未出现重启、崩溃、看门狗、`QUEUE_PRESSURE`、`EVENT_DROP`、
  `UART_OVERFLOW`、构包失败或SD写入失败。SHT30仍有 `-101`，继续独立处理。
- 当前只完成约8分钟首轮验证。仍需保持设备运行至少24小时，确认长期每分钟唯一、
  闭合小时恢复20个输入，并取得第一次24小时周期网络校时证据。

## 8. SHT30 独立问题

2026-08-13用户判断此前问题大概率与GND未同步有关；调整任务优先级后当前表现也已
自行恢复。本轮不再处理SHT30，不修改其代码。以下内容仅保留为历史诊断记录。

SHT30 读取代码与 `D:\air\TSP` 原始代码完全一致：

- SDA：GPIO8
- SCL：GPIO18
- 地址：0x44
- I2C 频率：30 kHz
- 轮询周期：30 秒
- 使用 `ClosedCube_SHT31D` 的 `periodicFetchData()`

此前出现：

```text
Failed to read from SHT30 sensor, error code: -101
```

已知硬件状态：SHT30 模块使用 5 V 供电，SDA、SCL 空闲电压均实测约
3.8 V，硬件暂时无法修改。`-101` 在当前库中表示 CRC 错误，也可能包含
不完整读取被误判的情况。

该问题应在独立会话中分析。当前数据稳定性会话不修改 SHT30 代码。较早的
`1.txt` 中曾连续成功 31 次，但最新 2026-08-04 18:10～2026-08-05 09:29
日志中出现 1779 次 `-101` 且未记录成功读取，因此问题仍然明确存在。

## 9. 正式两天测试关注项

正式测试期间应保留原始、未筛选的串口日志，并尽量开启主机接收时间戳及
按天自动分文件。

重点检查：

1. `COLLECT trace` 是否连续。
2. 每个实时 `PROCESS` 是否有对应 `STORE ok=1`。
3. 分钟 `type=1`、小时 `type=2`、日 `type=3` 是否按边界生成。
4. `TX_ATTEMPT` 后是否有 `TX_RESULT ack=1`；如 `ack=0`，是否保存 pending。
5. pending 是否在后续扫描中补传成功并删除。
6. 是否出现 `HJ_BUILD_FAIL`、`PENDING_REJECT`、`PENDING_INVALID`、
   `PENDING_WRITE_VERIFY_FAIL` 或 `RECOVERY_RETAIN`。
7. 是否出现 `QUEUE_PRESSURE`、`EVENT_DROP`、`UART_OVERFLOW`。
8. 可用堆和最大连续块是否长期稳定，是否出现单向下降。
9. 是否出现重启、`abort()`、`LoadProhibited`、看门狗或分配失败。
10. SD 卡 raw/min/hour/day 文件是否与日志中的时间戳一一对应。
11. 平台接收数据是否与 SD 卡及最终补传结果一致。

测试后需要收集：

- 完整串口日志；
- SD 卡对应日期目录；
- SD 卡 `pending` 和 `history`；
- 平台相同时段的实时、分钟、小时、日数据导出结果。

当前实际 MCU 使用的 SD 卡在 Windows 中曾挂载为 `E:`。盘符可能变化，执行
任何删除操作前必须重新确认容量、卷标和目录结构，不得仅凭盘符判断。

当前 `E:\pending\0\20260805092020.pkt` 是有效且尚未补传完成的正式数据，
不得删除、移动或改写；装回设备后先观察其是否补传成功消失。

## 10. 当前下一步：Firmware 2.0.9 Remote DTU发送端联调

1. 发送端同事先依据根目录两份Remote DTU文档完成`protocol-v1`状态机和模拟测试。
2. 用户明确授权后才可烧录
   `firmware_workspace\build\firmware-2.0.9-upload-status`；未经授权不得自行烧录。
3. 登录后发送`{"operation":"upload","protocol":1,"size":630864}`，确认先收到
   非零会话`accepted`，此阶段串口没有任何BIN字节。
4. 只在同一会话`ready`后，从文件偏移0和首字节`0xE9`开始按800字节、1000ms发送。
5. 确认MCU按5%返回`transferring`，完整接收后依次返回`verifying`和`success`。
6. 成功重启后确认Firmware 2.0.9、LCD normal ACK、`get_data`、采集、SD保存和HJ212。
7. 验证`unsupported_protocol/invalid_size/image_too_large`均在会话前`rejected`。
8. 验证传输中断和错误BIN返回`failed`后，发送端立即停止所有剩余BIN。
9. 使用不带`protocol`的旧发送端回归legacy英文路径，两个模式不得同时触发发送。
10. 保存MCU与发送端完整原始日志、实际BIN大小和SHA-256，再更新硬件验证结论。

## 11. 新会话工作顺序

1. 完整读取根目录`FIRMWARE_START_HERE.md`、`FIRMWARE_VERSION_LOCATIONS.md`和
   `ARDUINO_IDE_2.0.4_SETTINGS.md`。
2. 完整读取本文件及`CHANGELOG_2.0.12.md`至`CHANGELOG_2.0.4.md`。
3. 区分2.0.4正式回退、2.0.8 LCD实机基线、2.0.9连续运行基线、2.0.10气体首次
   上机状态、2.0.11禁止使用状态和2.0.12内存安全修复待验证状态。
4. 保持小时统计冻结、HJ212逐包3000ms、SHT30独立和DEBUG开启。
5. 未经用户明确授权不得烧录MCU。
6. 对Remote日志按`upload→accepted→ready→transferring→verifying→
   success/failed`建立时间线，同时核对LCD状态链，再决定是否修改代码。
7. 会话结束前更新构建哈希、实机结论、遗留问题和下一步。

## 12. 新会话启动提示词

```text
请继续处理TSP ESP32-S3 Firmware项目。

实际源码目录：
D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\Firmware_2.0.3_original\TSP

当前开发源码为Firmware 2.0.12。2.0.8已完成627872字节远程OTA、LCD normal重发、
ACK和普通查询恢复实机联调；2.0.9已完成约17小时连续运行；2.0.10已实现K-7S
四气体状态采集、ppb基准原始值和四种可配置输出单位，用户首次上机时发现LCD完整
14因子set_config在二次JSON解析时失败。2.0.11因扩大软件串口RX/ISR缓冲导致用户
烧录后启动即重启，禁止使用；2.0.12恢复1024字节串口配置并保留JSON修复，完整
编译及镜像校验通过，尚未烧录验证。
默认正式回退仍是固定四段Firmware 2.0.4发布包。

请先完整读取根目录三份入口文档、PROJECT_HANDOFF_CURRENT.md、
CHANGELOG_2.0.12.md至CHANGELOG_2.0.4.md、LCD_OTA_INTERACTION_PROTOCOL.md、
REMOTE_DTU_OTA_SENDER_PROTOCOL.md和REMOTE_DTU_OTA_SENDER_TASK_HANDOFF.md。

小时统计禁止修改；HJ212逐包间隔保持3000ms；SHT30独立处理；DEBUG保持开启；
未经明确授权不得烧录MCU。
```

## 13. 2026-08-19 Firmware 2.0.10 气体采集与单位配置

用户已确认并授权实施四个 K-7S 气体模组方案。当前开发源码已升级为 Firmware
2.0.10，完成内容如下：

- O3/NO2/CO/SO2 每周期分别只读取一次 `0x6000` 起始的两个寄存器，成功即返回；
  失败最多 3 次，500ms 应用层超时，150ms 重试间隔；
- 按 K-7S 状态位处理预热、保护、故障、超量程和告警；
- DataPacket、SD 和统计继续保存 `0x6001` 原始整数，统一解释为 ppb 基准值；
- 四气体均支持 `ppb/ppm/ug/m3/mg/m3`，默认 O3/NO2/SO2 为 `ug/m3`、CO 为
  `mg/m3`；
- LCD 实时/历史、LED 显示与阈值、HJ212 实时/统计统一按配置单位换算；
- 单位精度为 ppb 0 位、ppm 3 位、ug/m3 2 位、mg/m3 2 位；
- `get_config/set_config` 继续使用现有 `unit` 字段，不新增 JSON 字段；
- 气体非法单位回退默认值；LCD 保存 sensors 配置时先归一化；
- 移除发往业务串口的 ASCII 启动字符串；
- HJ212 2025 无效非噪声数据改用 `Flag=D`；
- 校准逻辑、小时统计、HJ212 3000ms 包间隔、SHT30、LCD OTA 和 Remote DTU OTA
  均未修改，DEBUG 保持开启。

完整编译和镜像校验已通过：Sketch 636764 字节，全局变量 26816 字节，应用 BIN
636912 字节，SHA-256 为
`25EEEEF057BE8EBE55262B008775826EBD12882AB167F43E98ED0CD13CE64E82`。构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.10-gas-units`

状态为 `compiled-not-hardware-verified`，本次没有烧录 MCU。详细内容见
`CHANGELOG_2.0.10.md`。

现有设备 FFat 的 `/model.json` 不会被源码默认模板覆盖。若设备仍是 10 因子配置，
必须由 LCD 使用现有 `set_config` 下发完整 14 因子 sensors JSON 并重启。LCD 还需
确保气体 `unit` 可选择四个合法值，并同步检查 `alarmLimit` 与所选单位的对应关系。

## 14. Firmware 2.0.12 下一步

1. 未经用户明确授权，不得烧录 MCU。
2. 烧录后先重新下发完整 14 因子 sensors JSON，不能只发送四条增量配置；确认
   LCD收到`code=OK,config=sensors`，再用`get_config/sensors`核对14项。
3. 重启后确认配置加载和collector数量均为14；若失败，保留新增的长度、错误偏移、
   空闲堆、最大连续堆块和UART overflow原始日志。
4. 实机核对四个地址均返回状态 `0x8000`，浓度原始值与手持调试结果一致。
5. 依次切换四种单位，核对 LCD 实时/历史、LED 和 HJ212 输出的一致性。
6. 重点核对 CO 原始值 1586：ppb 为 1586、ppm 为 1.586、mg/m3 默认显示 1.82；
   不把项目文本的 0～1ppm 当作钳制上限。
7. 通过拔线或错误地址验证只在失败时重试，最多 3 次，并检查无串口污染。
8. 保留完整 MCU、LCD 和 HJ212 平台日志，再决定校准流程和报警阈值。

## 15. 2026-08-19 Firmware 2.0.11 LCD 14因子配置失败版本

Firmware 2.0.10首次上机时，LCD下发完整14因子`sensors`配置。权限层对同一字符串
第一次解析成功，ConfigManager第二次解析失败；旧失败路径没有发布响应，所以2秒后
LCD只收到`Response timeout`，配置未写入。串口日志正文被日志模块约256字节缓冲
截短，不等同于LCD报文丢失。

2.0.11曾完成：LCD RX缓冲提高到2048字节；转交业务前释放第一次cJSON树；所有
`set_config`失败路径立即返回明确NG；新增报文长度、错误偏移、空闲堆和最大连续块
诊断。用户烧录后设备启动即重启。随后核对所用EspSoftwareSerial实现确认，2048
字节RX会联动生成约80KB ISR缓冲，比原配置额外占用约41984字节动态堆并要求大块
连续分配，因此2.0.11已判定为`rejected-hardware-boot-loop`，禁止使用。

完整编译：Sketch 637996字节，全局变量26816字节，应用BIN 638144字节；SHA-256：
`7C423DF2A4287185FDD2D2CD80D02431EE7E674EC91B3BAECF8926B05DE5C3C7`；源码输入
指纹：`10E4DE462513E7CFD02F15E8B1F52BF33FE911AB89B443A71B06D59FFCF4C24C`；esptool
识别为ESP32-S3且checksum和validation hash有效。构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.11-lcd-config`

上述构建目录已添加`DO_NOT_FLASH.md`。烧录由用户执行，Codex没有烧录MCU。详细内容见
`CHANGELOG_2.0.11.md`。

## 16. 2026-08-19 Firmware 2.0.12 内存安全修复

2.0.12已把`SerialManager::SoftwarePortInit()`恢复为2.0.10相同的
`sw->begin(..., false, 1024)`，不再扩大LCD字节缓冲及关联ISR缓冲。14因子紧凑请求
为849字节，原1024容量可容纳；本次问题的核心修复继续是转交ConfigManager前释放
第一棵cJSON树。即时OK/NG响应及长度、错误偏移、堆和UART overflow诊断均保留。

完整编译：Sketch 637976字节，全局变量26816字节，应用BIN 638128字节；SHA-256：
`C061BC1485809BEE658B7DCF956BF0BDBAEA522D3A6A7B216C59488CE5517CF9`；源码输入
指纹：`BC8650DD281604D7110ED9A569E21DD3C055101E21A5DE23FF6FD9C34045CAC6`；esptool
识别为ESP32-S3且checksum、validation hash有效。构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.12-lcd-config-memory-safe`

状态为`compiled-not-hardware-verified`，Codex没有烧录MCU。首次上机先确认设备不再
启动重启，再测试完整14因子配置和重启后14个采集器。详细内容见
`CHANGELOG_2.0.12.md`。

## 17. 2026-08-20 Firmware 2.0.13 HJ212构包内存修复

用户清空旧pending后的日志`C:/Users/LK/Desktop/ESP-LOG/8月20日-1.log`确认：

- 启动后采集trace 1～163连续，无panic、watchdog、brownout或二次重启；
- 16个十分钟包中9个首次构包失败，3个小时包全部首次构包失败；
- 失败全部集中为约1210～1217字节的`stage=validation`；
- 相同长度和类型既有成功也有失败，恢复重建后能够发送，排除固定内容或旧pending；
- 14因子全量`set_config`仍在碎片堆下出现序列化/解析失败，属于独立后续问题。

经用户明确授权，2.0.13已修改`src/module/pack212/pack212.cpp/.h`：2017和2025协议
共用单一最终报文缓冲区，使用`##0000`占位并原地回填长度；CRC直接对该缓冲区正文
计算并在末尾追加；`isValidPacket()`改为下标校验，完全移除`substring/String`
副本。大对象峰值由`cp + content + result + substring`降为`cp + final packet`。

小时统计逻辑、HJ212逐包3000ms、SHT30、DEBUG、K-7S、LCD/Remote OTA和配置接口
均未修改。完整编译：Sketch 637604字节，全局变量26816字节，应用BIN 637760字节；
SHA-256：
`5B54511F84FB55E5A1EFDA024A3156AEFFB835EA86E9BAA3D0F740EE848E9CF2`；源码输入
指纹：`D628E81BE814EEE854A7EE5A06AD49B57AB9993CE64F5EC9ED1A027B9C55607C`。
esptool确认ESP32-S3、16MB、checksum和validation hash有效。构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.13-hj212-memory-safe`

状态：`compiled-not-hardware-verified`。Codex未烧录MCU、未操作SD卡。首次上机重点
观察至少3个十分钟边界和2个整点边界，确认不再出现`HJ_BUILD_FAIL`；随后再单独
实施LCD局部配置接口和全量配置兼容路径的内存优化。详见`CHANGELOG_2.0.13.md`。

## 18. 2026-08-21 Firmware 2.0.13实机验证通过

`v2.0.13版本日志.log`覆盖约16小时16分钟，Firmware 2.0.13完成976轮连续14因子
采集、97个十分钟包、16个小时包和1个日包。共1090个HJ212报文全部构建成功，
`HJ_BUILD_FAIL=0`；00:00四类事件重叠时的1211～1217字节聚合包也全部成功。
设备无重启、panic、watchdog、brownout、队列压力、事件丢失或UART overflow。

应用BIN继续为637760字节，SHA-256：
`5B54511F84FB55E5A1EFDA024A3156AEFFB835EA86E9BAA3D0F740EE848E9CF2`。
2.0.13的HJ212构包内存修复判定实机验证通过。

独立遗留：18:40十分钟包构建和SD源数据保存成功，但pending恢复占用串口导致
`HJ_TX_DEFER`，随后pending写后校验失败；当前没有自动创建重建标记。启动前遗留的
17:50十分钟重建标记也因源文件不可用持续保留。后续pending版本应为
`savePendingPacket()`失败增加`savePendingRebuildMarker()`兜底，并隔离不可重建的
旧标记。本轮未测试`set_config`。

用户已授权下一阶段实施`LCD_SENSOR_CONFIG_PROTOCOL.md`：必须保留旧远程端全量
`get_config/set_config`兼容，同时新增LCD的`view=selection`、`mode=selection`、
最多4因子的局部查询和`mode=merge`。未经明确授权仍不得烧录MCU。

## 19. 2026-08-21 Firmware 2.0.14传感器局部配置接口

经用户授权，当前开发源码已升级到Firmware 2.0.14，并完成
`LCD_SENSOR_CONFIG_PROTOCOL.md`的MCU侧实现：

- 旧Remote/LCD不带新字段的全量查询和全量修改继续兼容；
- LCD开关页使用`view=selection`和`mode=selection`；
- 详细页使用最多4个因子的`ids`局部查询和`mode=merge`局部修改；
- 版本根据最终启用因子动态计算；关闭全部四气体且仍有气象因子时成为扬尘版；
- TVOC与其他因子互斥；
- FFAT新增`/sensorCatalog.json`保留关闭因子的单位和报警设置；
- 旧全量保存改用2048字节任务栈缓冲，释放请求cJSON树后再构建兼容响应；
- LCD/Remote大JSON进入业务任务时使用移动传递，减少完整`String`副本。

完整编译：Sketch 648592字节，全局变量26816字节，应用BIN 648736字节；SHA-256：
`376DC82371E709350D779901C2D040883665A0C13E41D8EAA17A70FCBB04E9C6`；源码输入指纹：
`1BDAD06A40F12E29DFF01EBDCFB7456E3C625EAC37075E035DC5E1789D0156FF`。
esptool确认ESP32-S3、16MB、checksum和validation hash有效。构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.14-sensor-config-partial`

状态：`compiled-not-hardware-verified`。本次未烧录MCU、未操作SD卡；HJ212、小时统计、
3000ms逐包间隔、SHT30、DEBUG、K-7S校准策略和OTA协议未修改。首次实机必须同时
回归旧Remote全量接口、新LCD四类接口、重启后配置保留和至少一个整点HJ212运行。
详见`CHANGELOG_2.0.14.md`。

## 20. 2026-08-25 Firmware 2.0.15四气体两点校准

经用户明确授权，当前开发源码已升级到Firmware 2.0.15，并完成四个K-7S模组的
异步两点校准MCU实现：

- O3/NO2/CO/SO2日常采集改为只读`0x6001`数量1，通信成功时`0 ppb`也有效；
- 新增唯一活动`GasCalibrationManager`会话，零点写`0x1000`、量程点写`0x1001`
  和FFAT本地目标，并轮询`0x6006`，只有`0x0001`判定成功；
- 本地默认目标为O3/NO2/SO2 500 ppb、CO 5000 ppb；量程点使用最近20秒10项
  平均值进行±20%防误操作，稳定提示阈值为窗口极差不超过目标5%；
- 新`gas_calibration`的`start/status/zero/span/cancel/finish`全部要求管理员权限，
  旧`gal_data`四气体路径禁用，颗粒物校准继续兼容；
- 正常60秒采集优先使用485；校准维护期跳过四气体业务采集，页面高频值不进入
  DataManager、SD或HJ212，清洗后显式`finish`才恢复，不重启MCU；
- 量程目标保存在FFAT`/gasCalibration.json`，原子创建和校验；校准页面及状态接口
  不读取、不显示、不覆盖目标。

完整编译：Sketch 663212字节，全局变量26976字节，应用BIN 663360字节；SHA-256：
`F65E5C1686CDCA83FEC44EB33DF9D831F73F2B53FAF8B27725386C83860C0E9D`；源码输入
指纹：`2A38194BC7E08418DF7136F4854BEDE32E3D2293B2FB62B524EC1FB0065ED5ED`。
esptool确认ESP32-S3、16MB、checksum和validation hash有效。

状态：`compiled-not-hardware-verified`。本次未烧录MCU、未操作SD卡、未执行真实气体
校准。小时统计逻辑、HJ212逐包3000ms、SHT30、DEBUG、pending二级兜底、
`pending_bad`防堵、OTA和2.0.14传感器局部配置均保留。LCD实现及实机测试按根目录
`LCD_GAS_CALIBRATION_PROTOCOL.md`和`MCU_GAS_CALIBRATION_UPGRADE_CHECKLIST.md`
继续，详细代码变更见`CHANGELOG_2.0.15.md`。

## 21. 2026-08-27 Firmware 2.0.16 暂定通用正式版本

2.0.15保留为已验证正式基线；2.0.16定位为正常、未做特殊气体量程限制传感器的
通用正式版本。本版修复颗粒物原生零值判定、移除TSP专用分段压低算法、修复四个
颗粒物`gal()`全部出口的TTL互斥锁释放及写入回读校验；同时修复HJ212配置Flag、
超时、重试和协议版本的启动加载，并按Flag 4/8不等待应答、Flag 5/9等待应答。

2026-08-27实机日志确认：故意断开颗粒物板时，PM1和PM2.5写入失败均返回NG，
后续校准和普通采集仍可执行；连接正常时，PM1、PM2.5、TSP、PM10以
`increment=0, ratio=100`写入并回读一致，四项均返回`galRes=true`，随后颗粒物
采集、SD保存和HJ212发送正常。HJ212 Flag 4无应答分支及Flag 5应答重试分支也已
实机验证。相关日志：

- `C:\Users\LK\Desktop\ESP-LOG\测试1.log`
- `C:\Users\LK\Desktop\ESP-LOG\颗粒物校准操作.log`

当前判定：Firmware 2.0.16可暂定为通用正式版本。后续仍建议补做24～48小时连续
稳定性测试，并使用可控输入验证颗粒物真实0值及TSP大于900时不再执行旧分段压低。
O3、NO2、SO2的500 ppb显示封顶和250 ppb量程目标属于后续特定认证版本，不并入
2.0.16。完整变更和构建信息见根目录`CHANGELOG_2.0.16.md`。

## 22. 2026-08-27 Firmware 2.0.17 气体认证特定版

经用户授权，当前开发源码已从2.0.16升级为2.0.17气体认证特定版：

- O3、NO2、SO2普通采集及校准维护读数大于500 ppb时统一封顶为500 ppb；封顶在
  DataPacket和单位换算前完成，因此LCD、SD、统计、LED和HJ212保持一致；
- CO不封顶，继续保留实际0～10000 ppb数据；
- O3、NO2、SO2认证量程点目标改为250 ppb，CO保持5000 ppb；
- 2.0.17只读写独立`/gasCalibrationCertified.json`，首次缺失时原子创建并要求
  `profile=certified_250ppb_v1`；
- 原`/gasCalibration.json`不读取、不迁移、不覆盖、不删除，确保降级回2.0.16后
  通用版仍使用原目标，不被认证版250 ppb配置污染。

完整编译通过：Sketch 664688字节，全局变量26976字节，应用BIN 664832字节；
应用BIN SHA-256：
`C8F549BE80F666CBB25D0CA081B61E705FEDB7292C0586C4679632435358D9ED`；
源码输入指纹：
`85E9120377541CE865F49E2CCE8BF0F0C75E1946CEB79583680A2A8D613FDA04`。
esptool确认ESP32-S3、16 MB、checksum和validation hash有效。构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.17-certified-gas-20260827`

状态：`compiled-not-hardware-verified`。Codex未烧录MCU、未操作设备FFat。首次实机
重点验证三项气体500 ppb边界、CO不封顶、250 ppb量程命令和降级配置隔离。完整
说明见根目录`CHANGELOG_2.0.17.md`。

### 2026-08-28 systemInfo运行版本响应修复

用户确认LCD和远程端都通过公共`get_config/systemInfo`读取版本。日志已证明实际
运行2.0.17时，旧板FFat`/system.json`仍可能返回历史`version=2.0.5`。经用户授权，
保持版本号2.0.17不变，在ConfigManager公共查询响应中用编译常量`VERSION2`动态
覆盖内存响应的`version`；不写回FFat，其他systemInfo字段保持原文件值。新增INFO
诊断`SYSTEM_INFO_VERSION result=ok version=... source=runtime`，关闭DEBUG后仍保留。

2026-08-28经用户授权已完成干净编译，未烧录。当前构建确认DEBUG关闭并采用
`LOG_LEVEL_INFO`；Sketch 665276字节，全局变量26976字节，应用BIN 665424字节。
应用BIN SHA-256：
`BD23D44CC864E0A2260C9268C84BC7FDA2465F8D4473A36C04E4FC8FCCEE8393`；
merged BIN SHA-256：
`35E914C71F2E1B4E6935F06483A2C612625FB71071BDA075D2CFA2D5918121C5`；
源码输入指纹：
`7C07D14414AD1D41162B80678E97FD286F31338600728C8A7F12E0CB31C2B84A`。
esptool确认ESP32-S3、16 MB、checksum和validation hash有效，BIN字符串确认包含
`SYSTEM_INFO_VERSION ... source=runtime`和认证气体配置标识。构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.17-certified-gas-systeminfo-20260828`

状态为`compiled-not-hardware-verified`。旧应用BIN `C8F549...`开启DEBUG且不包含
systemInfo修复，仅作为2026-08-27历史构建保留。
