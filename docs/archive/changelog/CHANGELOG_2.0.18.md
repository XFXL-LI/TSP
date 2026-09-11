# Firmware 2.0.18 变更记录

## 版本定位

- 2.0.16继续保留为未包含后续读取优化的通用正式基线，不回改。
- 2.0.17继续保留为气体认证项目定制版：O3、NO2、SO2封顶500 ppb，认证量程目标250 ppb。
- 2.0.18是通用优化版：保留当前后续修复和传感器读取优化，但气体业务规则恢复为2.0.16通用规则。
- 2.0.18与2.0.17认证版的预期业务差异仅限认证气体策略，不修改小时统计、HJ212、SHT30及其他业务链。

## 通用气体规则

- O3 `w34011`、NO2 `a21004`、SO2 `a21026`日常读取`0x6001`成功后直接使用传感器实际ppb值，不再在500 ppb封顶。
- 气体校准维护读取同样保留实际ppb值，LCD校准状态和稳定性样本不再应用认证封顶。
- CO `a21005`继续保留实际值，不封顶。
- 气体校准恢复使用通用FFat文件`/gasCalibration.json`。
- 默认量程目标恢复为O3=500 ppb、NO2=500 ppb、CO=5000 ppb、SO2=500 ppb。
- 不读取、不迁移、不覆盖、不删除2.0.17认证文件`/gasCalibrationCertified.json`，升级或降级时两套配置继续隔离。
- 已删除认证专用`GasSpecificPolicy`，源码和应用BIN均不再包含`certified_250ppb_v1`或认证配置路径。

## 保留的传感器读取优化

- SDS069颗粒物PM1、PM2.5、PM10、TSP正常采集保持单次读取，取消同一分钟重复读取取平均。
- XM8189风速、风向保持单次正常读取，取消原外层和底层叠加重复采样。
- 从机2气象百叶箱湿度、温度、噪声、气压保持单次正常读取。
- 统一通信策略保持响应超时500 ms、最多3次尝试、失败重试间隔300 ms、一次事务结束后300 ms总线间隔。
- 未启用因子继续由原有因子选择逻辑跳过。

## 明确保持不变

- 小时统计逻辑不改。
- HJ212完整报文逐包等待保持3000 ms。
- SHT30保持独立。
- DEBUG保持开启，运行日志等级为`LOG_LEVEL_DEBUG`。
- HJ212默认Flag保持5；FFat中已保存配置仍按启动加载规则优先应用。
- pending可靠性、OTA、LCD配置和`systemInfo`运行版本覆盖等现有修复全部保留。

## 版本位置

- `TSP.ino`：`VERSION2=2.0.18`。
- `src/inc/sys_init.h`：`VERSION2=2.0.18`。
- `src/app/configManager/system_json.h`：内置系统信息版本为`2.0.18`。
- LCD和远程端查询`systemInfo`时继续由编译常量覆盖FFat历史版本字段。

## 编译与镜像验证

2026-09-01已完成独立目录干净编译，未烧录：

- ESP32 Core：3.3.7；目标：ESP32-S3；Flash：16 MB；分区：3 MB APP / 9.9 MB FFat。
- Sketch：663552字节（21%）。
- 全局变量：26976字节（8%），剩余300704字节。
- 应用BIN：663696字节。
- 应用BIN SHA-256：`7BCE11150DB0CC06F7FF41636C6EDA8FBE763C6972DCC03733A7626B2B3289CA`。
- merged BIN：16777216字节。
- merged BIN SHA-256：`DC8F3A4007D2B2D96E24676E249E02D0EDB367937E26C59851212345B8E6B6B6`。
- 源码输入指纹：`A9FD364B5D330944BE76334050A6B88E869B7FA501B287665945B5D0585E00DD`。
- esptool识别为ESP32-S3、16 MB，checksum和validation hash均有效。
- BIN字符串核对包含`2.0.18`和`gasCalibration.json`，不包含`2.0.17`、`gasCalibrationCertified.json`或`certified_250ppb_v1`。
- 构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.18-general-sensor-read-optimization-20260901`

当前状态：`compiled-not-hardware-verified`。本次没有烧录MCU、没有擦除Flash，也没有操作设备FFat。

## 首次实机验证重点

1. 启动日志确认Firmware 2.0.18、DEBUG日志开启，气体配置路径为`/gasCalibration.json`，目标顺序为`500,500,5000,500`。
2. 分别模拟O3、NO2、SO2为499、500、501和高于500 ppb，确认LCD、SD、统计和HJ212均保留实际值，不再封顶。
3. 实测O3、NO2、SO2量程校准按500 ppb目标执行，±20%允许范围为400～600 ppb；CO仍为5000 ppb。
4. 通过带DEBUG串口日志确认三类传感器读取命令数和超时次数符合优化设计。
5. 完成24～48小时稳定性观察，确认无异常复位、看门狗、SD失败、持续内存下降或HJ212长时间中断。
