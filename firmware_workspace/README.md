# TSP ESP32-S3 固件工作区

最后更新：2026-10-20

本目录是固件状态查看、构建、Release验证和受保护烧录入口。正式业务源码已迁移到
仓库根目录的 `firmware/`，不再从 `tmp/` 或根目录旧 `TSP/` 构建。

## 当前正式源码

| Variant | 版本 | 源码路径 | 构建输入 | 源码指纹 |
|---|---:|---|---:|---|
| standard | 2.1.5 | `../firmware/standard/TSP` | 104 | `A8CE98F1F83A084691BCFB2B4311B513B6FAC1844A7201AE2C9AE9AD66317F95` |
| certified | 2.1.5.1 | `../firmware/certified/TSP` | 105 | `7414A1CCDB3B84B384FAA6E75F99D9AE3038935B92B2AB5A391EAF764FCBDDA3` |

两版2.1.5系列SD对照诊断构建均已通过，状态为
`compiled-not-hardware-verified`。当前构建目录分别为：

- `build/standard-2.1.5-sd-aba-20261020`
- `build/certified-2.1.5.1-sd-aba-20261020`

继承2.1.4系列的HJ212软件串口写字节期间临时提高优先级、失败报文有界异步存储，
以及存储事件队列满时不阻塞上传投递。臭氧HJ212出口统一使用平台指定的w34011，
有效旧缓存报文在RAM中改名并重算CRC，内部因子与单位不变。
120秒采集、70秒气泵、9600波特率、ACK策略及认证气体策略不变。
2.1.4系列两版静态RAM为28032字节；pending队列元数据与最多2048报文字节占动态堆，
存储事件队列由20减为4，其他任务／队列沿用旧版。未烧录、未实机验收。
保留2.1.3系列LED／加热及此前可靠性修改和旧构建。SD SDK卡住不能由重试计时器取消；
UART波形／中断时延仍须现场对照原始TCP验证。详细行为与验证入口见
[项目背景](../docs/PROJECT_CONTEXT.md)和[版本记录](../FIRMWARE_VERSION_LOCATIONS.md)。

本次新增每次启动一次的SD 30／60／30分钟对照；等待在途I/O结束后才开始60分钟
暂停，之后保持正常。暂停业务覆盖历史和pending读写／查询／扫描及挂载重试，跳过的
数据及时释放，不积压或恢复后补写；统计与上传继续。历史查询返回原有NG响应。
`priority_tx`改为实际测量值，并记录发送开始／结束时SD在途计数和前8字节。
两版静态RAM均为28080字节，比2.1.4系列增加48字节；没有新增任务、队列或持久化配置。
这是诊断固件，卡内会出现预期的历史空档；重新启动会重新执行对照，尚未现场验证。

## 目录职责

- `build/`：可再生构建产物，不是发布来源，也不应长期作为Git资产保存。
- `releases/`：Git跟踪正式发布的manifest、SHA256SUMS、flash_args等追溯元数据；四段BIN不进入Git工作树。
- `scripts/`：正式构建、状态检查、Release打包/验证和受保护烧录逻辑。
- `toolchain/`：工作区使用的便携工具。
- `sources/`：需要长期保留的旧版本独立源码快照，不用于当前开发。
- `archive/`：历史构建和旧副本，只用于追溯。
- `logs/`：运行、串口和验证日志。

## 构建和状态入口

```text
build-standard.cmd       构建 ../firmware/standard/TSP
build-certified.cmd      构建 ../firmware/certified/TSP
build-current.cmd        兼容入口，等价于 standard
status-standard.cmd      查看 standard 状态
status-certified.cmd     查看 certified 状态
status.cmd               默认查看 standard 状态
```

构建脚本会在编译前检查三个版本定义一致性，记录variant、源码路径、输入数量、源码指纹、
构建参数和应用BIN SHA-256。编译本身不会烧录。

## 最新已发布 Release

- standard 2.0.22：`releases/2.0.22/20260907-gas-pacing-diagnostics`
- certified 2.0.22.1：`releases/2.0.22.1/20260907-gas-pacing-diagnostics-certified`

两者状态均为 `packaged-not-hardware-verified`。它们已完成文件级和静态验证，但未取得
现场硬件验证状态。

当前已发布的四段BIN由GitHub Releases分发；历史BIN由离线Release归档保存。验证或
烧录前，应从对应GitHub Release或离线归档恢复四段BIN到同一Release metadata目录，
再按 `SHA256SUMS.txt`、`manifest.json`和 `flash_args.txt`校验。恢复的BIN由精确
`.gitignore`规则排除，不作为Git源码或Release metadata提交。

2026-08-10的Firmware 2.0.4四段包继续作为历史硬件验证回退基线保留：

`releases/2.0.4/2026-08-10_hw-verified`

## Release验证与受保护烧录

`validate-release.cmd`保留兼容默认值。验证当前Release时，应向底层验证脚本显式传入
目标Release路径。

只有得到明确烧录授权后才可使用：

```text
flash-verified.cmd COM9 FLASH
```

正式四段地址为：

```text
0x000000  bootloader.bin
0x008000  partitions.bin
0x00E000  boot_app0.bin
0x010000  firmware.bin
```

受保护烧录不执行全片擦除，不写FFat分区。

## 当前固定约束

- 小时统计逻辑不修改。
- HJ212完整报文逐包间隔保持3000 ms。
- 默认ACK超时保持5000 ms、重试3次，除非明确要求修改。
- SHT30独立处理。
- DEBUG保持开启。
- standard和certified保持独立；公共优化同步，认证气体封顶只存在于certified。
- 未获得明确授权时不得烧录、擦除MCU或操作设备FFat。

## 文件判定规则

不要从源码目录、旧 `TSP/build`、历史 `.build_*`、OTA工具缓存或普通 `build/` 中直接
挑选BIN。只有 `releases/` 下具有 `manifest.json`、`SHA256SUMS.txt`和明确烧录地址的
目录才是正式可追溯发布包；其硬件验证状态仍以manifest和现场记录为准。
