# TSP Firmware 2.0.22.1 变更记录

日期：2026-09-04  
版本类型：特定认证限制版  
当前状态：2026-09-07构建已正式打包，`packaged-not-hardware-verified`

## 版本关系

Firmware 2.0.22.1从通用版Firmware 2.0.22同步产生，完整包含实时HJ212报文优先、
pending空闲调度、低优先级串口保护以及`DEFERRED`不计发送失败的改动。

详细公共逻辑和实机验证清单见
[Firmware 2.0.22变更记录](CHANGELOG_2.0.22.md)。

## 认证版独立策略

与2.0.22的业务差异仍然只有认证气体策略：

- O3、NO2、SO2对外采集值最高限制为500 ppb；
- CO不封顶；
- 使用独立校准文件`/gasCalibrationCertified.json`；
- 校准配置必须带`profile=certified_250ppb_v1`；
- 默认跨度目标为O3=250 ppb、NO2=250 ppb、CO=5000 ppb、SO2=250 ppb；
- 校准读取对O3、NO2、SO2同样应用500 ppb认证限制。

本次没有修改上述策略。通用版源码中仍不存在`GasSpecificPolicy`。

## 冻结约束确认

- 小时统计逻辑未改；
- HJ212逐包间隔保持3000 ms；
- ACK超时、重试和9014校验规则未改；
- SHT30保持独立；
- DEBUG保持开启；
- 未烧录、未擦除MCU、未操作设备FFat。

## 构建结果

- 当时源码（historical source location）：迁移前 `tmp` 副本，现由迁移安全备份保存
- 构建目录：`firmware_workspace/build/firmware-2.0.22.1-certified-hj212-live-priority`
- 发布包：`firmware_workspace/releases/2.0.22.1/2026-09-04_hj212-live-priority-final_compiled-not-hardware-verified`
- Sketch：676,460字节；
- 全局变量：27,320字节；
- 应用BIN：676,608字节；
- 应用BIN SHA-256：`65509A722E8F4194B128F8EF16C88A28F777918C1EA3F2D79B01B275A1D5EFC0`；
- 源码指纹：`35717D8485A4CC3539CF941BB7EBDE777012A6157B07E82865C0E461F9740367`；
- ESP32-S3镜像checksum和validation hash均有效；
- 状态：`compiled-not-hardware-verified`。

同版本目录中名称不带`final`的包是最终竞态检查加入前的中间构建，已放置
`DO_NOT_USE.md`，不得烧录或OTA。

## 2026-09-07同版本同步

保持版本号2.0.22.1不变，完整同步`CHANGELOG_2.0.22.md`第7节的气体200 ms统一节流、
失败重试节流、校准首次状态查询延后、HJ212串口占用者诊断、pending全零累计计数和
内存低水位诊断。认证版独立的O3/NO2/SO2封顶、CO不封顶、认证校准文件与250 ppb
目标保持不变。

构建结果：

- 构建目录：`firmware_workspace/build/firmware-2.0.22.1-certified-gas-pacing-diagnostics-20260907`；
- Sketch：678,988字节；
- 全局变量：27,360字节；
- 应用BIN：679,136字节；
- SHA-256：`D9300FCCB31B5A0C44BEFEC3A37E0F6411C97F8C870429E184BECFE3A52E9EFE`；
- 源码指纹：`540EE286955E50B552882A8F7639E858DBC724DFECFC9B0571E0AC5D0A128779`；
- ESP32-S3镜像checksum和validation hash有效；
- 状态：`compiled-not-hardware-verified`，未烧录、未操作设备FFat。

## 2026-09-10正式源码迁移与Release索引

- 当前variant：`certified`；
- 当前正式源码：`firmware/certified/TSP`；
- 构建输入：89，包含`GasSpecificPolicy.h`；
- 当前源码指纹：`540EE286955E50B552882A8F7639E858DBC724DFECFC9B0571E0AC5D0A128779`；
- 迁移测试构建：
  `firmware_workspace/build/firmware-2.0.22.1-certified-repo-migration-test-20260910`；
- 迁移验证结论：
  `MIGRATION BUILD VERIFIED - EXPECTED NONDETERMINISTIC METADATA ONLY`；
- 正式Release：
  `firmware_workspace/releases/2.0.22.1/20260907-gas-pacing-diagnostics-certified`；
- Release状态：`packaged-not-hardware-verified`。

迁移测试BIN与2026-09-07 BIN不是bit-identical。全部不同字节已归因于ESP32 Core 3.3.7
编译日期/时间、ELF SHA、image checksum和validation hash等非确定性构建元数据，
未发现无法解释的业务payload差异。认证版O3/NO2/SO2 500 ppb封顶、CO不封顶、
`gasCalibrationCertified.json`、`certified_250ppb_v1`及250/250/5000/250 ppb目标
均保持不变。Release尚未取得现场硬件验证状态。
