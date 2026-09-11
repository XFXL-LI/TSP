# TSP Firmware 2.0.21.1 变更记录

日期：2026-09-04  
版本类型：特定认证限制版  
状态：发送热修复已编译、已校验镜像，尚未进行真机验证

## 2026-09-04 HJ212发送热修复（版本号不变）

现场运行的2.0.21.1在`C:\Users\LK\Desktop\ESP-LOG\9月4日-最新日志.log`中共出现
18次`request_identity_missing`，同时没有`TX_ATTEMPT`和`ACK_ACCEPT`。报文在写入
HJ212串口前就被拦截，因此平台当时确实收不到数据。

原因是完整HJ212帧的QN从位置6开始，前一字符是四位长度字段的最后一位，而原字段边界
判断没有识别该合法位置。本次与通用2.0.21同步增加完整帧正文起点识别；只有`##`、四位
数字长度和位置6同时成立才放行。本修复不降低9014 ACK的长度、CRC、QN、MN、
`QnRtn=1`等校验要求，也不改变认证气体限制。

日志中的13个完整pending包和2个分钟数据重建标记将在连接及有效ACK条件满足后按原公平
调度补传。2026-09-03发布包已标记禁止使用，应改用本日志“构建结果”所列2026-09-04包。

## 版本关系

Firmware 2.0.21.1 从通用版 Firmware 2.0.21 同步产生，完整包含 2.0.21 的因子真实状态、9014 应答校验、QN 唯一性、timeout/retry_times 生效和 pending 写入保护。

与 2.0.21 的业务差异仅保留认证气体策略：

- O3、NO2、SO2 对外采集值最高限制为 500 ppb；
- CO 不封顶；
- 使用独立校准文件 `/gasCalibrationCertified.json`；
- 校准配置必须带 `profile=certified_250ppb_v1`；
- 默认跨度目标为 O3=250 ppb、NO2=250 ppb、CO=5000 ppb、SO2=250 ppb；
- 校准读取对 O3、NO2、SO2 同样应用 500 ppb 认证限制。

通用版源码中没有 `GasSpecificPolicy`，因此两条版本线不会互相污染。后续公共优化应先进入通用版，再同步到 `.1` 认证版，并保留上述差异清单。

## 冻结约束确认

- 小时统计的有效值取样逻辑未改；
- HJ212 逐包间隔保持 3000 ms；
- SHT30 保持独立；
- DEBUG 保持开启；
- 未烧录、未擦除 MCU、未操作设备 FFat。

## 构建结果

- 源码：`D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\Firmware_2.0.21.1_certified\TSP`
- 构建目录：`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.21.1-certified-hj212-hardening`
- 发布包：`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\releases\2.0.21.1\2026-09-04_hj212-send-hotfix_compiled-not-hardware-verified`
- `firmware.bin`：675,488 字节
- SHA-256：`57C61954F820F160C819295160F17C00D865FF3C82EE79E0631F369C4C1BA048`
- 源码指纹：`0996E72479DD48B2E0FC04638D4B447B466AE579E0DCB8A2B10E5BEF7872E11A`
- ESP32-S3 镜像校验：checksum 与 validation hash 均有效
