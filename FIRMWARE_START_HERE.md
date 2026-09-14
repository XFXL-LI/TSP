# TSP Firmware 从这里开始

最后更新：2026-09-10

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
| standard | 2.0.22 | `firmware/standard/TSP` | 88 | `84CD14958E077AE1FE4294C4C566F8CF39FE89952C5CB929301AF2CF4C553213` |
| certified | 2.0.22.1 | `firmware/certified/TSP` | 89 | `540EE286955E50B552882A8F7639E858DBC724DFECFC9B0571E0AC5D0A128779` |

两套源码均已完成迁移构建验证，结论为：

`MIGRATION BUILD VERIFIED - EXPECTED NONDETERMINISTIC METADATA ONLY`

重新构建的应用BIN与2026-09-07历史BIN不是bit-identical；已确认差异仅来自ESP32
Core 3.3.7编译日期/时间、ELF摘要、镜像checksum和validation hash等非确定性元数据，
不存在无法解释的业务payload差异。

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

## 当前正式 Release

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
