# Firmware 2.1.0.1 变更记录

最后更新：2026-09-20
Variant：certified
状态：`compiled-not-hardware-verified`

本版本同步[Firmware 2.1.0](CHANGELOG_2.1.0.md)的120秒分段采集、70秒气泵保护、
颗粒物四通道批量读取、配置时间归一和逐实时批次小时统计。

认证版专用行为保持独立：O3、NO2、SO2最高500 ppb，CO不封顶；独立
`/gasCalibrationCertified.json`、`certified_250ppb_v1`及250/250/5000/250 ppb
校准目标均未改为standard策略。

## 构建结果

- 构建输入：89；
- 源码指纹：`7612C96BE98E74AC6182F8F9D96E3C4BE836DDACC9F87001418DDBA5A06777F0`；
- 应用BIN：684208字节；
- 应用BIN SHA-256：`D959F8C5CF179C0090BFD4BC002ADF082E30FFD585717730F6136780D9605A39`；
- 构建目录：`firmware_workspace/build/current-certified`。

本次只完成编译和静态检查，未烧录、未擦除Flash、未操作设备FFat，尚未取得硬件验证状态。
