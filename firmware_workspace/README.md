# TSP ESP32-S3 固件工作区

最后更新：2026-10-09

本目录是固件状态查看、构建、Release验证和受保护烧录入口。正式业务源码已迁移到
仓库根目录的 `firmware/`，不再从 `tmp/` 或根目录旧 `TSP/` 构建。

## 当前正式源码

| Variant | 版本 | 源码路径 | 构建输入 | 源码指纹 |
|---|---:|---|---:|---|
| standard | 2.1.3 | `../firmware/standard/TSP` | 99 | `D490FACFB83CC839F004B30781B882F70E236EF7484A2891D497804A847C13BF` |
| certified | 2.1.3.1 | `../firmware/certified/TSP` | 100 | `5F50F4FBF848D450EE7E90842DF6E1D18EADC38889F4103420357FB150DB2700` |

两版2.1.3系列构建均已通过，状态为
`compiled-not-hardware-verified`。当前构建目录分别为：

- `build/standard-2.1.3-atmospheric-pressure-20261009`
- `build/certified-2.1.3.1-atmospheric-pressure-20261009`

本次公共改动为LED兼容开关映射、两／四区域连续翻页，以及加热总开关即时生效；
保留上一版的气体读取／SD挂载可靠性修改。120秒采集、70秒气泵及认证气体策略不变。
未烧录、未实机验收；不宣称支持SD热拔卡。
2026-10-09在同版本下修复LED帧间隔：发送完成后至少等待50ms，再发下一区域，
清屏／模式切换同样分帧；每版静态RAM比2026-10-08构建增加80字节。
旧`build/standard-2.1.3`及`build/certified-2.1.3.1`保留，不是本次修复产物。
接口与验收要求见[开关配置协议](../docs/protocols/lcd/LCD_SWITCH_CONFIG_PROTOCOL.md)。

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
