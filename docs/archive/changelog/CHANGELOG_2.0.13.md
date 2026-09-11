# Firmware 2.0.13

日期：2026-08-20

状态：针对14因子HJ212十分钟包和小时包的瞬时堆峰值进行最小范围修复。已通过
ESP32 Core 3.3.7完整编译、ESP32-S3镜像校验和约16小时实机运行验证；HJ212构包
修复判定通过。仍存在独立的pending写入失败兜底问题。

## 问题依据

清空旧pending后的运行日志仍出现约1210～1217字节聚合包`stage=validation`：

- 16个十分钟包中9个首次构包失败；
- 3个小时包全部首次构包失败；
- 相同长度和类型的包既有成功也有失败，恢复发送后可重新构建；
- 无持续单调内存下降，问题符合瞬时堆峰值和连续堆碎片化，而非旧pending影响。

旧构包路径在校验时可能同时保留`cp`、`hj212_content`、`result`以及完整
`substring`副本。14因子聚合包因此需要多份约1.2KB连续堆块。

## 修改

- 2017和2025协议共用`finalizePacket()`最终封装函数；
- 一次性为最终报文申请容量，先写入`##0000`占位，再在同一个`String`中写入正文；
- 正文完成后原地回填四位长度，CRC直接对同一缓冲区中偏移6字节后的正文计算；
- 在同一缓冲区末尾追加CRC和`\r\n`，不再创建第二份完整`result`；
- `isValidPacket()`改为按下标解析长度、查找必要字段并解析CRC；
- 移除长度、正文和CRC的全部`substring/String`副本；
- 构包失败日志补充目标字节数、空闲堆和最大连续堆块。

预期大对象峰值由：

`cp + hj212_content + result + validation substring`

降为：

`cp + final packet`

## 保持不变

- HJ212字段顺序、长度含义、CRC算法和报文尾部不变；
- 小时统计逻辑未修改；
- HJ212逐包发送间隔保持3000ms；
- SHT30继续独立处理；
- DEBUG保持开启；
- K-7S采集、单位换算、LCD/Remote OTA和配置接口未修改；
- 未操作SD卡，未烧录MCU。

## 编译与镜像验证

- ESP32 Core：3.3.7；
- 分区：16MB Flash，3M APP / 9M FATFS；
- Sketch：637604字节，占程序空间20%；
- 全局变量：26816字节，占动态内存8%；
- 应用BIN：637760字节；
- 应用BIN SHA-256：
  `5B54511F84FB55E5A1EFDA024A3156AEFFB835EA86E9BAA3D0F740EE848E9CF2`；
- 源码输入指纹：
  `D628E81BE814EEE854A7EE5A06AD49B57AB9993CE64F5EC9ED1A027B9C55607C`；
- esptool 5.1.0识别为ESP32-S3、16MB，checksum与validation hash均有效；
- 构建目录：
  `D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.13-hj212-memory-safe`；
- 状态：`compiled-not-hardware-verified`。

## 首次实机验证

1. 启动日志确认Firmware 2.0.13，无重启、panic、watchdog或brownout。
2. 保持14因子运行，连续观察至少3个十分钟边界和2个整点边界。
3. 十分钟包和小时包不得再出现`HJ_BUILD_FAIL stage=validation`或
   `stage=packet_reserve`。
4. 对比平台或原始日志，确认HJ212包长、CN、CP、CRC及ACK正常。
5. 检查SD的raw/min/hour写入正常；若网络失败，pending仍可恢复并删除。
6. 记录构包失败诊断中的`free/largest`；若仍失败，保留完整日志继续定位。
7. 单独复测14因子全量`set_config`。该JSON路径是独立问题，本版本尚未修改。

## 2026-08-21实机验证结论

日志：`C:/Users/LK/Desktop/ESP-LOG/v2.0.13版本日志.log`；SHA-256：
`25661D25864014D315F1BE45203C3E76B142F9886262A39C9C83BAA385EFF218`。

- 运行范围：2026-08-20 18:11:19至2026-08-21 10:27:03，约16小时16分钟；
- trace 1～976连续，无重启、panic、watchdog或brownout；
- 实时976、十分钟97、小时16、日1，共1090个HJ212报文全部构建成功；
- `HJ_BUILD_FAIL`、`packet_reserve`和`validation`失败均为0；
- 1090个实时/统计文件全部成功保存到SD；
- 00:00同时产生实时、十分钟、小时和日包，1211～1217字节聚合包全部成功；
- 瞬时`free=848,largest=564`后下一分钟恢复到`free=6464,largest=6132`，未出现
  单调内存泄漏。

因此2.0.13的HJ212单缓冲区构包和无分配校验判定实机验证通过。

独立遗留问题：18:40十分钟包已构建并保存源数据，但发送时遇到`serial_busy`，随后
`/sdcard/pending/1/20260820184000.pkt`写后校验失败并被删除，当前代码没有自动降级
创建`REBUILD_FROM_SD`标记。另有启动前遗留的
`/sdcard/pending/1/20260820175000.pkt`标记因源文件不可用而重复扫描。上述问题不
影响HJ212构包修复结论，留待后续pending可靠性版本处理。

本次日志没有`get_config/set_config`测试，配置接口结论保持不变。
