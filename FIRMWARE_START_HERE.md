# TSP Firmware 从这里开始

最后更新：2026-10-23

本文件是当前固件源码、构建和发布入口。详细版本关系见
[固件版本与目录说明](FIRMWARE_VERSION_LOCATIONS.md)，仓库整体说明见
[项目 README](README.md)。

AI辅助开发应同时读取：

- [仓库级AI规则](AGENTS.md)
- [项目长期技术背景](docs/PROJECT_CONTEXT.md)
- [当前未解决问题](docs/OPEN_ISSUES.md)
- [AI长期变更记录](docs/AI_CHANGELOG.md)

## 当前正式源码

| Variant | 版本 | 正式源码 | 构建输入 | 源码指纹 |
|---|---:|---|---:|---|
| standard | 2.1.7 | `firmware/standard/TSP` | 104 | `2B481FE81D0ECEA02FEA1F81DA523DF422157CCC622A267113D71A2FE32D321A` |
| certified | 2.1.7.1 | `firmware/certified/TSP` | 105 | `4894E25D48D29E62BE4D30EEDF1A393F9AEC563B3BD36926F5CB4D952F59F5B2` |

两套2.1.7系列恢复常规流程的源码均已编译通过，状态为
`compiled-not-hardware-verified`。实时数据固定每120秒形成一批；有空气通道因子时气泵
固定运行70秒，其他传感器从第45秒采集，颗粒物与气体在通气60秒后且泵仍运行时采集。
颗粒物改为一次读取四通道，小时统计改为纳入每个有效实时批次。
当前版保留无有效时间时的本地快照、可选状态查询、最多3次气体状态读取及
SD启动挂载失败后的约60秒重试；不支持运行中热拔卡。
继承2.1.4系列的HJ212软件串口写字节优先级保护，失败报文异步交给存储任务；存储队列满时
明确拒收新存储项，防止其阻塞上传投递。臭氧出口使用平台指定的w34011，旧有效pending
在内存迁移因子名并更新CRC。保留2.1.3的LED／加热行为；不改变采集周期、波特率或认证策略。
入队不等于落盘；底层SD卡住和中断造成的UART时延仍需实机验证，多通道适配尚未实现。

2.1.6系列修正已发生优先级继承时HJ212发送保护未达到实际优先级11的问题。
写字节前核验实际等级，写完恢复进入前的有效等级，串口事务释放互斥锁后再恢复原基础
等级；未达到保护等级时明确返回失败。2.1.7继续沿用此保护，完整送达仍需现场验证。
`HJ_UART_CONTEXT`增加`guard_ok`，`HJ_PRIORITY_RESTORE`记录事务结束后的基础／有效等级。

本版取消30／60／30分钟SD对照。启动输出`SD_MODE mode=normal comparison=off`，
历史保存／查询、pending和挂载重试持续按原业务规则执行，不再人为跳过一小时。
`SdIoDiagnostics`只统计操作在途数，不执行定时阻断；旧诊断版已经跳过的历史不补写。

默认`LOG_LEVEL_INFO`，保留`COLLECT`、`TX_RESULT`、`STORE`和待传结果及告警；LED逐帧、
JSON正常回显、采集阶段和正常复读详情不再默认输出。`HJ_UART_TX/CONTEXT`、
`HJ_PRIORITY_RESTORE`及`SD_IO_BEGIN/END`改为DEBUG，保护校验继续执行。短写、保护／
恢复失败、LED写入失败、最终无效传感器及SD读写异常保留提示；操作返回时耗时至少3秒
记录`SD_IO_SLOW`，不强行取消SDK调用。小时／日历史读取异常和现场残帧仍未宣称解决。

当前构建产物位于：

- standard：`firmware_workspace/build/standard-2.1.7-normal-sd-20261023`
- certified：`firmware_workspace/build/certified-2.1.7.1-normal-sd-20261023`

迁移前的两个 `tmp` 历史源码副本已退出主工作树；需要追溯时使用仓库外历史归档和
迁移安全备份。当前开发与构建只使用上表中的 `firmware/` 正式源码。

## 构建与状态入口

从 `firmware_workspace/` 运行：

```text
build-standard.cmd       构建 standard
build-certified.cmd      构建 certified
build-current.cmd        兼容入口，等价于 standard
status-standard.cmd      查看 standard 状态
status-certified.cmd     查看 certified 状态
status.cmd               默认查看 standard 状态
validate-release.cmd     Release 静态验证入口
flash-verified.cmd       受保护烧录入口，必须明确授权后使用
```

`firmware_workspace/build/`是可再生构建产物；
`firmware_workspace/releases/`是正式发布资产；
`firmware_workspace/scripts/`保存正式构建、验证和烧录逻辑。

## 最新已发布 Release

| Variant | Release | 状态 |
|---|---|---|
| standard 2.0.22 | `firmware_workspace/releases/2.0.22/20260907-gas-pacing-diagnostics` | `packaged-not-hardware-verified` |
| certified 2.0.22.1 | `firmware_workspace/releases/2.0.22.1/20260907-gas-pacing-diagnostics-certified` | `packaged-not-hardware-verified` |

两包均包含2026-09-07的气体200 ms统一节流和轻量运行诊断，但尚未取得现场硬件验证
状态。不要把“打包并静态验证通过”写成“现场验证通过”。

2026-08-10的Firmware 2.0.4固定四段包仍作为历史硬件验证回退基线保留：

`firmware_workspace/releases/2.0.4/2026-08-10_hw-verified`

## 烧录布局

正式四段烧录地址统一为：

```text
0x000000  bootloader.bin
0x008000  partitions.bin
0x00E000  boot_app0.bin
0x010000  firmware.bin
```

不使用merged BIN替代四段正式包，不执行全片擦除，不写FFat分区。未经明确授权不得
烧录、擦除MCU或操作设备FFat。

## 当前版本文档

- [当前2.1.7/2.1.7.1构建与版本记录](FIRMWARE_VERSION_LOCATIONS.md)
- [LCD/远程端开关配置协议](docs/protocols/lcd/LCD_SWITCH_CONFIG_PROTOCOL.md)
- [Firmware 2.1.7 变更记录](docs/changelog/CHANGELOG_2.1.7.md)
- [Firmware 2.1.7.1 变更记录](docs/changelog/CHANGELOG_2.1.7.1.md)
- [Firmware 2.1.0 变更记录](docs/changelog/CHANGELOG_2.1.0.md)
- [Firmware 2.1.0.1 变更记录](docs/changelog/CHANGELOG_2.1.0.1.md)
- [Firmware 2.0.23 变更记录](docs/changelog/CHANGELOG_2.0.23.md)
- [Firmware 2.0.23.1 变更记录](docs/changelog/CHANGELOG_2.0.23.1.md)
- [Firmware 2.0.22 变更记录](docs/changelog/CHANGELOG_2.0.22.md)
- [Firmware 2.0.22.1 变更记录](docs/changelog/CHANGELOG_2.0.22.1.md)
- [LCD传感器配置协议](docs/protocols/lcd/LCD_SENSOR_CONFIG_PROTOCOL.md)
- [LCD四气体校准协议](docs/protocols/lcd/LCD_GAS_CALIBRATION_PROTOCOL.md)
- [LCD OTA交互协议](docs/protocols/lcd/LCD_OTA_INTERACTION_PROTOCOL.md)
- [Remote DTU OTA协议](docs/protocols/remote/REMOTE_DTU_OTA_SENDER_PROTOCOL.md)
- [继电器远程控制需求](docs/development/requirements/RELAY_REMOTE_CONTROL_REQUIREMENTS.md)

历史Arduino设置、任务交接和会话交接已归入 `docs/archive/`，仅用于追溯，不作为当前
权威入口。

历史CHANGELOG、交接资料、开发日志和旧接口README统一保存在 `docs/archive/`，不得把
其中的旧源码路径、旧版本状态或旧烧录说明当作当前操作依据。
