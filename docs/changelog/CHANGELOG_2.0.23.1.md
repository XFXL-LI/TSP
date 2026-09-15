# Firmware 2.0.23.1 变更记录

日期：2026-09-14  
Variant：certified  
状态：`compiled-not-hardware-verified`

## 变更范围

本版本将[Firmware 2.0.23](CHANGELOG_2.0.23.md)的pending完整HJ212报文SD写入加固和
失败诊断同步到certified源码。除此之外未混入ACK、CSQ仲裁、EventBus、气体状态、
PSRAM或Logger架构调整。

## Certified策略

认证专用`GasSpecificPolicy.h`及既有策略保持不变：

- O3、NO2和SO2最高500 ppb；
- CO不封顶；
- 使用独立`/gasCalibrationCertified.json`；
- profile为`certified_250ppb_v1`；
- 校准目标为250/250/5000/250 ppb。

## 构建结果

- 构建输入：89；
- 源码指纹：`090B6AD564ED66941D9DA60EF1ABB1626113E5837A9A77C736A23CAC4A3CAA6B`；
- 构建目录：`firmware_workspace/build/firmware-2.0.23.1-certified-pending-sector-write-hardening-20260914`；
- application BIN：680672字节；
- application SHA-256：`58F2506A295B2FB3853321BC80E63028C911BE3AA7E29B9884CB0747425A51C5`；
- 分区：`app3M_fat9M_16MB`；
- bootloader、partitions和boot_app0与2.0.22.1正式包一致。

本次未烧录、未擦除MCU、未操作设备FFat，也未创建或发布正式Release。下一步需要在
目标硬件上进行pending写入故障注入和长时间运行验证。
