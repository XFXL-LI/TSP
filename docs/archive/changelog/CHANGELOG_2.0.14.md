# Firmware 2.0.14

日期：2026-08-21

状态：已实现 LCD 传感器选择、局部查询和局部合并配置接口，并保留现有 Remote/LCD
全量接口兼容。已通过 ESP32 Core 3.3.7 完整编译和 ESP32-S3 镜像校验，尚未烧录及
实机联调。

## 修改目标

Firmware 2.0.13 已通过约 16 小时实机验证，14 因子 HJ212 十分钟、小时和日包均不再
构建失败。2.0.14 在此基线上单独处理 `get_config/set_config sensors` 的大 JSON 与
LCD 页面交互，不修改 HJ212 和统计逻辑。

## 新增 LCD 接口

- `get_config` 携带 `view:"selection"`：返回动态计算的 `profile` 和当前启用 ID；
- `set_config` 携带 `mode:"selection"`：只提交最终启用 ID，拒绝空列表、重复/未知
  ID，TVOC 与其他因子互斥；
- `get_config` 携带 `ids`：一次查询 1～4 个因子，只返回其中当前已启用的因子；
- `set_config` 携带 `mode:"merge"`：一次修改最多 4 个当前已启用因子的 `unit` 和
  `alarmLimit`，全部验证通过后才保存；
- `profile` 不单独固化，根据最终启用因子按 TVOC、空气微站、扬尘、颗粒物优先级
  动态计算；关闭全部四气体且仍有气象因子时自动成为扬尘版；
- 新增 FFAT `/sensorCatalog.json`，保存 15 个已知因子的详细配置。因子关闭时配置
  保留，以后重新启用时恢复；首次创建时用当前启用配置覆盖内置默认值；
- 新接口成功保存后返回 `restart_required:true`，LCD 应按协议执行安全重启并重新
  查询 MCU。

完整报文和 LCD 修改清单见：

`D:\ChatGPT-Pro\TSP-ESP32-S3\LCD_SENSOR_CONFIG_PROTOCOL.md`

## 旧接口兼容与内存优化

- 不带 `view/ids` 的旧全量 `get_config/sensors` 保持原有返回结构；
- 不带 `mode` 的旧全量 `set_config/sensors` 保持“出现即启用、遗漏即关闭”的语义，
  现有 Remote 端无需升级；
- 全量保存改用 2048 字节任务栈缓冲进行紧凑序列化，避免在 cJSON 树存活时再申请
  一份完整堆字符串；保存后先释放请求 cJSON 树，再读取并生成兼容响应；
- LCD/Remote 收到的完整 JSON 使用移动传递进入业务任务，减少一次完整 `String`
  副本；
- FFAT 写入增加 `const char *` 路径，避免把栈缓冲再复制成完整 `String`。

## 因子和验证规则

- 已知因子共 15 个：4 个颗粒物、风速、风向、4 个气象详情因子、4 个气体和 TVOC；
- 非 TVOC 最多启用 14 个；TVOC 只能单独启用；
- 局部查询和局部修改均拒绝未知或重复 ID；
- 局部修改只能操作当前已启用因子，且只允许 `unit`、`alarmLimit`；
- 报警值必须为 0～1000000000 的整数；
- 气体单位继续使用现有 `ppb/ppm/ug/m3/mg/m3` 规则；其他单位为 1～16 字节可打印
  ASCII；
- TVOC 首次默认配置为 `name=TVOC, unit=ug/m3, alarmLimit=500`，以后以 FFAT 保存值
  为准。

## 保持不变

- Firmware 2.0.13 的 `pack212` 单缓冲区构包和原地校验未修改；
- 小时统计逻辑未修改；
- HJ212 逐包发送间隔保持 3000ms；
- SHT30 继续独立处理；
- DEBUG 保持开启；
- K-7S 采集、气体校准策略、LCD/Remote OTA 协议未修改；
- pending 可靠性遗留问题未在本版本处理；
- Codex 未烧录 MCU、未操作 SD 卡。

## 编译与镜像校验

- ESP32 Core：3.3.7；
- 分区：16MB Flash，3M APP / 9M FATFS；
- Sketch：648592 字节，占程序空间 20%；
- 全局变量：26816 字节，占动态内存 8%；
- 应用 BIN：648736 字节；
- 应用 BIN SHA-256：
  `376DC82371E709350D779901C2D040883665A0C13E41D8EAA17A70FCBB04E9C6`；
- 源码输入指纹：
  `1BDAD06A40F12E29DFF01EBDCFB7456E3C625EAC37075E035DC5E1789D0156FF`；
- esptool 5.1.0 识别为 ESP32-S3、16MB，checksum 与 validation hash 均有效；
- 构建目录：
  `D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.14-sensor-config-partial`；
- 状态：`compiled-not-hardware-verified`。

## 首次实机联调清单

1. 启动确认 Firmware 2.0.14，无重启、panic、watchdog、brownout 或 UART overflow。
2. 先用旧 Remote 端执行全量查询，再执行一次全量更改，确认响应结构和启用语义不变。
3. 按协议依次验证 `view=selection`、4 个分组的局部查询、`mode=merge` 和
   `mode=selection`。
4. 验证 PM1 单独关闭、噪声单独关闭、四气体全部关闭、扬尘特征全部关闭时的
   `profile` 自动变化。
5. 验证 TVOC 与其他因子同时启用会返回 `tvoc_exclusive`，失败时不重启。
6. 修改单位/报警值后重启，关闭再重新启用该因子，确认详细配置恢复。
7. 用 5 个 ID 和 5 个 patch 验证数量限制；用未启用因子、未知 ID、非法单位和报警值
   验证明确 NG 原因且配置不发生部分变化。
8. 再运行至少 3 个十分钟边界和 1 个整点，确认 HJ212 继续无 `HJ_BUILD_FAIL`。
9. 保存 MCU、LCD 和 Remote 原始日志后，再更新硬件验证状态。
