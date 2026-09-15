# Firmware 2.0.23 变更记录

日期：2026-09-14  
Variant：standard  
状态：`compiled-not-hardware-verified`

## 变更范围

本版本只处理pending完整HJ212报文写入后首个512字节读回为零的问题，不包含ACK、
CSQ仲裁、EventBus、气体状态、PSRAM或Logger架构调整。

## Pending写入加固

- 首次写入使用最终路径加`.tmp1`，执行stdio写入、`fflush`、`fsync`、关闭及重开校验。
- 首次校验失败时不立即删除`.tmp1`，避免第二次尝试直接复用相同临时路径及已释放簇。
- 第二次写入使用独立`.tmp2`，以POSIX `O_EXCL`新建，并按最多256字节分块写入。
- 第二次写入完成后执行`fsync`、关闭及重开校验。
- 只有CRC和逐字节校验通过的临时文件才会重命名为最终`.pkt`。
- 两次均失败时继续保留既有rebuild marker、raw重建和pending补传兜底。

## 失败诊断

诊断只在pending写入或校验失败时输出，并拆分为短日志：

- 两次尝试各自的临时路径和写入方法；
- open/write/flush/fsync/close结果及`errno`；
- memory/file前16字节十六进制；
- 全文CRC、前512字节CRC；
- 前128和前512字节零值数量；
- 文件长度、首次不一致偏移及字节值。

## 保持不变

- 小时统计逻辑；
- HJ212逐包3000 ms间隔；
- 默认ACK超时5000 ms及重试3次；
- 实时优先和pending空闲补传策略；
- CRC、逐字节验证、rebuild marker和raw重建；
- SHT30独立、DEBUG开启及现有气体读取策略。

## 构建结果

- 构建输入：88；
- 源码指纹：`A97AA91FD298EAA31D3ED2051C1A19FAEB210A98284533267508A947D644C3E9`；
- 构建目录：`firmware_workspace/build/firmware-2.0.23-pending-sector-write-hardening-20260914`；
- application BIN：680192字节；
- application SHA-256：`C10B8B98B052A11C90B1CB0E5DD1DCC9C0831E361590712444642B3A749A486A`；
- 分区：`app3M_fat9M_16MB`；
- bootloader、partitions和boot_app0与2.0.22正式包一致。

本次未烧录、未擦除MCU、未操作设备FFat，也未创建或发布正式Release。下一步需要在
目标硬件上进行pending写入故障注入和长时间运行验证。
