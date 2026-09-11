# Firmware 2.0.22 / 2.0.22.1 气体200 ms与轻量诊断

日期：2026-09-07

## 修改范围

- 通用源码：`D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\Firmware_2.0.3_original\TSP`
- 认证源码：`D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\Firmware_2.0.21.1_certified\TSP`
- 保持版本号2.0.22和2.0.22.1。

## 气体通信

新增公共`GasModbusAccess`。调用方在持有`SERIAL_485`互斥量期间进入该访问层，每一笔
正常读取、失败重试和校准读写均从上一笔气体事务完成时计时，实际间隔不足200 ms时
使用FreeRTOS延时补足并再次检查，达到要求后才开始下一笔事务。

四气体读取保持`0x6000`起始、一次读取状态和浓度两个寄存器、500 ms超时、最多3次
尝试。校准解锁到命令的1000 ms间隔不变；命令完成后的首次状态查询改为按1000 ms
状态周期触发。

## 诊断

- `GAS_QUERY`：气体事务间隔和耗时；
- `HJ_SERIAL_BUSY`：HJ212串口请求方、占用者与持锁时间；
- `PENDING_WRITE_HEALTH`：pending保存失败及全零前缀事件累计；
- `min_free/min_largest`：开机后跨任务累计内存低水位。

## 编译结果

通用2.0.22：

- Sketch 678492字节；全局变量27360字节；
- BIN 678640字节；
- SHA-256 `A8ECE4A080D82D944468C601513B15D26FBB835A7EC554B4F235081BC1DA6A60`；
- checksum `0x35`有效，validation hash有效。

认证2.0.22.1：

- Sketch 678988字节；全局变量27360字节；
- BIN 679136字节；
- SHA-256 `D9300FCCB31B5A0C44BEFEC3A37E0F6411C97F8C870429E184BECFE3A52E9EFE`；
- checksum `0x7c`有效，validation hash有效。

两版状态均为`compiled-not-hardware-verified`。未烧录、未擦除MCU、未操作设备FFat，
也未创建新发布包。

## 实机验证

1. 正常和重试`GAS_QUERY`中，除开机首笔`first=1`外，必须始终`gap_ms>=200`；
2. 正常一分钟周期仍应连续生成14因子采集、存储和HJ212包；
3. 校准命令后首次`reg=0x6006`读取不应立即发生；
4. 若出现HJ212串口忙，保留同一时刻的`HJ_SERIAL_BUSY owner/held_ms`；
5. 若pending写入再次出现全零，核对`PENDING_WRITE_HEALTH`累计值及后续
   `PENDING_REBUILT/ACK_ACCEPT/ack_and_delete_ok`闭环；
6. 长时间观察`min_free/min_largest`、重启、WDT、panic和OOM。
