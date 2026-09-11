# Firmware 2.0.20.1 变更记录

## 版本定位

- 2.0.20继续作为通用原始气体数据版和后续公共功能开发基线。
- 2.0.20.1是以2.0.20为完整基线生成的特定气体认证版。
- 2.0.20中的HJ212-2017报文修正、补传公平性、MN配置刷新、pending写入校验、
  传感器读取优化、OTA及其余修复全部保留。

## 特定气体数据限制

- O3 `w34011`、NO2 `a21004`、SO2 `a21026`读取`0x6001`成功后，原始浓度大于
  500 ppb时，进入业务`DataPacket`的值固定为500 ppb；0～500 ppb保持原值。
- CO `a21005`不封顶，继续使用传感器实际ppb值。
- 封顶位于LCD实时数据、SD新数据、分钟/小时/日统计和HJ212组包之前，保证新采集
  数据的各业务出口使用同一基准值。
- DEBUG日志同时记录`raw_ppb`和`output/value_ppb`，用于验证封顶边界。

## 认证标定配置隔离

- 认证版只使用FFat文件`/gasCalibrationCertified.json`。
- 配置必须包含`profile=certified_250ppb_v1`。
- 默认量程点目标为O3=250 ppb、NO2=250 ppb、CO=5000 ppb、SO2=250 ppb。
- 量程点防误操作范围保持目标值±20%。
- 校准维护期间读取的O3、NO2、SO2浓度同样应用500 ppb封顶。
- 不读取、不迁移、不覆盖、不删除通用配置`/gasCalibration.json`。

## 独立源码和版本位置

- 认证源码：
  `D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\Firmware_2.0.20.1_certified\TSP`
- `TSP.ino`：`VERSION2=2.0.20.1`。
- `src/inc/sys_init.h`：`VERSION2=2.0.20.1`。
- `src/app/configManager/system_json.h`：内置版本为`2.0.20.1`。

## 明确保持不变

- 小时统计算法不改。
- HJ212完整报文逐包等待保持3000 ms。
- SHT30保持独立。
- DEBUG保持开启。
- HJ212默认Flag保持5。
- 不烧录、不擦除MCU，也不操作设备FFat。

## 升级边界

- 从通用2.0.20升级后，新采集数据按认证规则封顶。
- 升级前已经保存在SD中的历史数据和完整待补`.pkt`保留原始值，不做静默重写。
- 用于认证测试的设备应在升级前确认旧待补队列已经处理完成。

## 编译验证

2026-09-03已完成独立目录完整编译，未烧录：

- ESP32 Core：3.3.7；目标：ESP32-S3；Flash：16 MB。
- Sketch：668024字节（21%）。
- 全局变量：27272字节（8%），剩余300408字节。
- 应用BIN：668176字节。
- 应用BIN SHA-256：
  `1295C20ECE2D0683DF3FFC10DD8D3BDE87158D6B1A14C8A69DA0D398575B1E8E`。
- merged BIN：16777216字节。
- merged BIN SHA-256：
  `2B74E3D082F87F32DA4C69BF0595ACC4E78BCDBEFC3BFB038AB85943721E78B5`。
- 源码输入指纹：
  `163985E0EB8AF68B9668ABFCBFFC846AB3E0A761BCECB8C3D6EC3DBC18D97B15`。
- esptool识别为ESP32-S3、16 MB、6个段，checksum和validation hash均有效。
- 应用BIN确认包含`2.0.20.1`、`/gasCalibrationCertified.json`、
  `certified_250ppb_v1`及2.0.20补传诊断标记，不包含通用配置路径
  `/gasCalibration.json`。
- 三个采集器、认证策略及GasCalibrationManager的认证实现与已编译的2.0.17
  读取优化版逐文件一致；2.0.19/2.0.20公共改进由当前基线保留。
- 通用2.0.20源码指纹仍为
  `B5D0E0791CD52BABCD78A6B651BAAF5A7C84BB6AE883749C89D43247C862C0E1`，
  与其原构建基线完全一致。

构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.20.1-certified`

发布包目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\releases\2.0.20.1\2026-09-03_compiled-not-hardware-verified`

当前状态：`compiled-not-hardware-verified`。
