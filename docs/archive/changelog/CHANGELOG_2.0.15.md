# Firmware 2.0.15

日期：2026-08-25  
状态：`compiled-not-hardware-verified`  
源码：`D:\ChatGPT-Pro\TSP-ESP32-S3\tmp\Firmware_2.0.3_original\TSP`

## 1. 四气体日常读取

- O3 `w34011/slave3`、NO2 `a21004/slave4`、CO `a21005/slave5`、SO2 `a21026/slave6` 的日常采集改为 FC03 只读 `0x6001` 数量 1。
- 不再读取或使用 `0x6000` 判断日常浓度是否有效。
- Modbus 通信成功即为有效浓度，`0 ppb` 是有效值；通信失败才使本轮数据无效。

## 2. 异步两点校准

新增 `src/module/gas/GasCalibrationManager.cpp/.h`，复用常驻 `CollectGalTask`，不新增临时任务：

- 同时只允许一个 O3、NO2、CO 或 SO2 校准会话；
- 零点写 `0x6006=[0x1000,0x0000]`；
- 量程点写 `0x6006=[0x1001,本地目标ppb]`；
- 写命令前写 `0x4FFF=0x55AA` 解保护；
- 写应答成功后每 1 秒读取 `0x6006`；
- 仅 `0x0001` 判定当前标定点成功；`0x0000` 和 `0x0100～0x0103` 保持执行态；`0x0002～0x0005` 映射为明确失败；
- 单点执行上限 120 秒，会话上限 30 分钟；
- LCD 10 秒无 `status` 心跳时只暂停 2 秒浓度刷新，不解除维护隔离；
- 量程点成功后进入清洗态，收到 `finish` 才退出；不重启 MCU。

新管理员接口使用：

```json
{"operation":"gas_calibration","action":"start","id":"a21026"}
{"operation":"gas_calibration","action":"status","session":1}
{"operation":"gas_calibration","action":"zero","session":1}
{"operation":"gas_calibration","action":"span","session":1}
{"operation":"gas_calibration","action":"cancel","session":1}
{"operation":"gas_calibration","action":"finish","session":1}
```

`zero/span` 立即响应只表示异步动作被接受，最终结果必须查询 `status`。完整 LCD 对接格式见根目录 `LCD_GAS_CALIBRATION_PROTOCOL.md`。

## 3. 本地目标和防误操作

首次启动在 FFAT 原子创建 `/gasCalibration.json`：

```json
{"span_targets_ppb":{"w34011":500,"a21004":500,"a21005":5000,"a21026":500},"max_deviation_percent":20,"stability_window_seconds":20,"min_stability_samples":10,"stability_tolerance_percent":5}
```

- O3/NO2/SO2 目标上限按现场实际 0～1 ppm 限制为 1000 ppb；CO 按现场实际 0～10 ppm 限制为 10000 ppb。
- 已有配置格式错误、目标缺失或越界时禁止开始校准，不静默替换。
- 每 2 秒读取一次当前 `0x6001`；固定 10 项环形数组保存最近约 20 秒样本。
- 量程点使用 10 项平均值做目标 ±20% 防误操作判断。
- `stable_hint` 使用 10 项最大最小差不超过目标的 5% 作为提示，不代替人工稳定判断。
- LCD 校准请求和状态响应均不携带、不返回内部目标。

## 4. 485 调度和数据隔离

- 正常 60 秒采集开始前置位优先标志；校准读写遇到该标志时延后，不记作模组失败。
- 通气、稳定、LCD 等待及状态轮询间隔均不持有 485 锁。
- 解保护、厂家规定的 1 秒窗口和紧随的标定写命令构成单个有界临界区，完成后立即释放锁。
- 会话开始到 `finish` 期间跳过四个气体的正常采集；若会话在一轮采集中途开始，会在发布前移除本轮已经采到的气体值。
- 校准页面的高频浓度只进入管理器缓存，不进入 DataManager、SD 或 HJ212；其他因子继续工作。

## 5. 兼容和可靠性

- 旧 `gal_data` 中任何四气体请求统一拒绝并返回 `legacy_gas_calibration_disabled`。
- 原颗粒物 `gal_data` 的 DATA/RATIO 路径保留。
- Modbus 批量写增加明确响应超时和中止处理，避免写事务无应答时永久阻塞。
- FFAT 增加临时文件写入、回读校验、备份替换和失败恢复的原子写辅助函数。

## 6. 版本和冻结项

- `TSP.ino`、`src/inc/sys_init.h`、`src/app/configManager/system_json.h` 统一为 `2.0.15`。
- 小时统计算法未修改。
- HJ212 完整报文逐包间隔仍为 3000 ms。
- SHT30 仍独立处理。
- `DEBUG` 保持开启。
- pending 二级兜底和 `pending_bad` 防堵改动保留。
- LCD/Remote OTA 及 2.0.14 传感器局部配置协议未修改。

## 7. 编译和镜像校验

使用 ESP32 Core 3.3.7、ESP32S3 Dev Module、16MB、3MB APP/9.9MB FATFS、PSRAM disabled 完整编译：

- Sketch：663212 字节（21%）；
- 全局变量：26976 字节（8%），剩余 300704 字节；
- 应用 BIN：663360 字节；
- SHA-256：`F65E5C1686CDCA83FEC44EB33DF9D831F73F2B53FAF8B27725386C83860C0E9D`；
- 源码输入指纹：`2A38194BC7E08418DF7136F4854BEDE32E3D2293B2FB62B524EC1FB0065ED5ED`。

esptool 5.1.0 `image-info` 已确认：ESP32-S3、16MB、镜像 checksum 有效、validation hash 有效。

本次没有烧录 MCU，没有修改 SD 卡，也没有执行真实零气或标准气校准。首次实机测试必须先完成 LCD 接口联调和无标准气功能测试，再逐个气体进行带抓包的标准气验收。

## 8. 2026-08-27 运行日志复核和会话收尾竞态修复

复核 `C:\Users\LK\Desktop\ESP-LOG\8月27日日志.log`：

- 日志覆盖约 16 小时 8 分钟，完整分钟采集 trace 2～969 连续；正常阶段为 14 因子，三次校准维护期间为 10 因子；
- 实时、十分钟、小时和日数据共 1081 次存储全部成功，未发现重启、看门狗、崩溃、SD 保存失败或持续内存下降；
- 一次 HJ212 无 ACK 及 pending 写入校验失败由重建标记兜底，随后从 SD 原始数据重建并补发成功；
- 末尾 SO2 会话中，MCU 只收到一条 `cancel`，该请求首次即返回 `OK/cancel_purging`；用户此前的重复点击未到达 MCU，后续需在 LCD 侧检查按钮事件、发送队列及界面状态处理；
- `finish` 成功后出现一条错误的 `GAS_CAL_TIMEOUT session=0 id=w34011`。原因是 `tick()` 通过锁外维护状态检查后，`finish` 抢先取得状态锁并清空会话，随后 `tick()` 使用已清零的会话时间执行了超时判断。

源码修复：

- `GasCalibrationManager::tick()` 取得 `_stateMutex` 后重新检查 `_maintenanceActive` 和 `_session`；会话已经结束时释放互斥锁并立即返回；
- 仅修复会话收尾的并发时间窗，不修改 JSON 协议、校准状态机、485 调度、普通采集、小时统计、SD、HJ212 或版本号；
- Firmware 版本继续保持 `2.0.15`；未修改三个版本号位置。

编译验证：

- 2026-08-27 使用 Arduino CLI 1.5.1、ESP32 Core 3.3.7 和既定 ESP32S3/16MB/3MB APP/PSRAM disabled 配置全量编译通过；
- Sketch：663224 字节（21%）；全局变量：26976 字节（8%），剩余 300704 字节；
- 应用 BIN：663376 字节；SHA-256：`8D7D9A4199F1557D409BC3C58E3157011603A4DB884A7E70FC535FCB28877672`；
- esptool 5.1.0 `image-info` 确认芯片为 ESP32-S3、Flash 为 16MB、镜像 checksum 和 validation hash 均有效；
- 验证输出：`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\verify-2.0.15-finish-race-20260827`；
- 本次没有烧录，竞态修复仍需下一次实机校准取消/finish 流程确认。
