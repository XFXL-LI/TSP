# TSP MCU 与 LCD 传感器配置交互协议

文档版本：1.1  
日期：2026-08-21  
适用对象：TSP ESP32-S3 MCU、LCD 屏幕程序、现有远程配置程序  
当前状态：MCU 已在 Firmware 2.0.14 实现并通过完整编译，尚待烧录和 LCD/Remote 实机联调

## 1. 修改目标

现有 LCD 和远程端使用全量 `get_config/sensors` 与全量
`set_config/sensors`。完整 14 因子 JSON 会增加串口流量和 MCU 动态内存峰值。

本次修改目标：

1. 现有远程端不修改程序，旧全量查询和全量更改继续兼容。
2. 新 LCD 开关页面只传当前启用的因子 ID，不再发送完整因子属性。
3. 新 LCD 详细设置页面每次最多查询和修改 4 个因子。
4. 当前产品版本由最终启用的特征因子自动计算，不单独固定。
5. 关闭因子时保留其单位和报警配置，以后重新打开时恢复原设置。
6. TVOC 与其他因子互斥。
7. 不修改小时统计、HJ212 逐包 3000ms、SHT30、DEBUG 和 OTA 协议。

## 2. 串口和公共格式

| 项目 | 规定 |
| --- | --- |
| 接口 | MCU LCD 专用串口 |
| 波特率 | 9600 |
| 数据格式 | 8N1 |
| 报文 | 单行紧凑 JSON |
| 结尾 | `\r\n` |
| 协议单位 | 使用 ASCII，例如 `ug/m3`；LCD 可显示为 `μg/m³` |

新增接口继续使用已有登录和管理员权限。未登录或权限不足时沿用现有拒绝规则。

## 3. 因子分组和代码

### 3.1 颗粒物详细设置页

| 因子 | 代码 |
| --- | --- |
| TSP | `a34001` |
| PM1 | `a34005` |
| PM2.5 | `a34004` |
| PM10 | `a34002` |

### 3.2 气象详细设置页

| 因子 | 代码 |
| --- | --- |
| 气压 | `a01006` |
| 噪声 | `L90` |
| 温度 | `a01001` |
| 湿度 | `a01002` |

### 3.3 只在开关页控制

| 因子 | 代码 | 说明 |
| --- | --- | --- |
| 风速 | `a01007` | LCD 不修改单位和报警值 |
| 风向 | `a01008` | LCD 不修改单位和报警值 |

### 3.4 四气体详细设置页

| 因子 | 代码 |
| --- | --- |
| O3 | `w34011` |
| NO2 | `a21004` |
| CO | `a21005` |
| SO2 | `a21026` |

### 3.5 TVOC 详细设置页

| 因子 | 代码 |
| --- | --- |
| TVOC | `a24035` |

## 4. 产品版本自动判断

产品版本根据最终启用因子按以下优先级计算：

1. 启用 `a24035`：版本为 `tvoc`，并且启用列表只能包含 `a24035`。
2. 启用任意四气体代码：版本为 `air_station`。
3. 未启用四气体，但启用任意风速、风向、气压、温度、湿度或噪声：版本为 `dust`。
4. 只要仍启用任意颗粒物代码：版本为 `particulate`。
5. 未启用任何因子：拒绝保存。

示例：

- 空气微站关闭全部四气体后，仍有气象因子，自动变为扬尘版。
- 扬尘版关闭全部扬尘特征因子，只剩颗粒物，自动变为颗粒物版。
- 颗粒物版关闭 PM1，但仍有其他颗粒物，仍是颗粒物版。
- TVOC 不允许与其他因子同时启用。

## 5. LCD 页面流程

### 5.1 开关页的版本下拉框

版本下拉框仅用于快速设置开关预设，不作为独立保存的固定版本：

| LCD 选择 | 默认打开 |
| --- | --- |
| 颗粒物 | 4 个颗粒物因子 |
| 扬尘 | 颗粒物 4 项 + 风速、风向、气压、温度、湿度、噪声 |
| 空气微站 | 扬尘 10 项 + O3、NO2、CO、SO2 |
| TVOC | 仅 TVOC |

用户选择预设后仍可关闭单个因子。保存时只发送最终开启的 ID 列表，MCU 根据该列表
重新计算实际版本。

### 5.2 进入详细设置总页

LCD 每次进入详细设置总页时先查询当前版本和启用列表。LCD 根据启用列表显示子页面
按钮：

- 任意颗粒物启用：显示“颗粒物设置”。
- 气压、噪声、温度、湿度中任意一项启用：显示“气象参数设置”。
- 四气体中任意一项启用：显示“气态污染物设置”。
- TVOC 启用：显示“TVOC 设置”。
- 风速和风向没有详细设置项，不单独触发详细设置按钮。

### 5.3 进入详细子页面

进入子页面后，LCD 只查询该页面当前需要显示的 1～4 个因子。保存时可发送该页面
全部已显示因子，不要求 LCD 逐字段记录是否变更，但单次最多 4 个因子。

## 6. 新接口：查询当前版本和开关

请求：

```json
{"operation":"get_config","config":"sensors","view":"selection"}
```

响应示例：

```json
{"operation":"get_config","code":"OK","config":"sensors","profile":"dust","enabled":["a34001","a34004","a34002","a01007","a01008","a01006","a01001","a01002"]}
```

LCD 使用 `enabled` 设置开关状态，使用 `profile` 设置版本下拉框和页面显示。

## 7. 新接口：保存开关

请求示例：

```json
{"operation":"set_config","config":"sensors","mode":"selection","enabled":["a34001","a34004","a34002","a01007","a01008","a01006","a01001","a01002"]}
```

成功响应：

```json
{"operation":"set_config","code":"OK","config":"sensors","mode":"selection","profile":"dust","enabled_count":8,"restart_required":true}
```

LCD 收到 `code=OK` 且 `restart_required=true` 后，沿用现有安全重启流程。重启完成后
重新查询 `view=selection`，不得只使用重启前的本地开关状态。

保存规则：

1. 禁止空列表。
2. 禁止重复和未知 ID。
3. 非 TVOC 因子同时启用数量最多为 14。
4. `a24035` 存在时列表中不得出现其他 ID。
5. 保存失败时保持原配置，不执行部分修改。

## 8. 新接口：详细参数局部查询

### 8.1 颗粒物

```json
{"operation":"get_config","config":"sensors","ids":["a34001","a34005","a34004","a34002"]}
```

### 8.2 气象参数

```json
{"operation":"get_config","config":"sensors","ids":["a01006","L90","a01001","a01002"]}
```

### 8.3 四气体

```json
{"operation":"get_config","config":"sensors","ids":["w34011","a21004","a21005","a21026"]}
```

### 8.4 TVOC

```json
{"operation":"get_config","config":"sensors","ids":["a24035"]}
```

响应只包含请求 ID 中当前已经启用的因子。例如 PM1 已关闭：

```json
{"operation":"get_config","code":"OK","config":"sensors","content":{"a34001":{"name":"TSP","alarmLimit":500,"unit":"ug/m3"},"a34004":{"name":"PM2.5","alarmLimit":500,"unit":"ug/m3"},"a34002":{"name":"PM10","alarmLimit":500,"unit":"ug/m3"}}}
```

LCD 已从 `view=selection` 获得启用状态，可隐藏未启用的卡片或将其置灰。单次 `ids`
最多 4 个，超过时 MCU 返回 `too_many_ids`。

## 9. 新接口：详细参数局部修改

请求示例：

```json
{"operation":"set_config","config":"sensors","mode":"merge","content":{"a34001":{"unit":"ug/m3","alarmLimit":500},"a34004":{"unit":"ug/m3","alarmLimit":500},"a34002":{"unit":"ug/m3","alarmLimit":500}}}
```

成功响应：

```json
{"operation":"set_config","code":"OK","config":"sensors","mode":"merge","changed":["a34001","a34004","a34002"],"restart_required":true}
```

局部修改规则：

1. 单次最多 4 个因子。
2. 只能修改当前已启用的因子。
3. 新接口只允许修改 `unit` 和 `alarmLimit`。
4. 未提供的字段保持原值。
5. 不改变因子开关，不增加或删除因子。
6. 所有字段验证通过后一次性保存；任一字段错误则全部不保存。
7. 气体单位继续使用 MCU 已有合法单位规则。
8. LCD 保存气体时应同时提交单位和报警值，避免单位改变后继续使用旧单位下的报警数值。

## 10. 旧全量接口兼容

### 10.1 旧全量查询

现有远程端和旧 LCD 请求保持不变：

```json
{"operation":"get_config","config":"sensors"}
```

MCU 继续返回当前启用因子的完整配置，字段结构保持现有格式。

### 10.2 旧全量更改

不带 `mode` 的请求继续按旧全量方式处理：

```json
{"operation":"set_config","config":"sensors","content":{"a34001":{"name":"TSP","alarmLimit":500,"unit":"ug/m3"}}}
```

规则：

1. `content` 中出现的因子设为启用。
2. 未出现的因子设为关闭。
3. 更新出现因子的详细参数。
4. 根据最终因子重新计算产品版本。
5. 旧响应字段和结构保持不变，避免现有远程端修改程序。

旧全量接口作为兼容路径保留；新 LCD 不再使用它执行开关和日常详细参数修改。

## 11. 错误响应

通用格式：

```json
{"operation":"set_config","code":"NG","config":"sensors","reason":"invalid_sensor_id"}
```

建议错误码：

| `reason` | 含义 |
| --- | --- |
| `invalid_mode` | 不支持的 `mode` |
| `invalid_sensor_id` | 未知因子代码 |
| `duplicate_sensor_id` | 启用列表包含重复 ID |
| `empty_selection` | 未启用任何因子 |
| `too_many_ids` | 局部查询超过 4 项 |
| `too_many_patch_items` | 局部修改超过 4 项 |
| `tvoc_exclusive` | TVOC 与其他因子同时启用 |
| `sensor_not_enabled` | 尝试修改当前未启用因子 |
| `unsupported_field` | 局部修改包含不允许字段 |
| `invalid_unit` | 单位不合法 |
| `invalid_alarm_limit` | 报警上限不合法 |
| `config_save_failed` | 详细配置保存失败 |
| `state_save_failed` | 启用状态保存失败 |

LCD 收到任何 `code=NG` 时不得重启，应保留当前页面内容并显示失败原因。

## 12. LCD 修改清单

1. 开关页版本下拉框改为开关预设，不直接作为固定版本保存。
2. 开关页进入时发送 `view=selection`。
3. 开关页保存时发送 `mode=selection` 和最终 `enabled` 数组。
4. 详细设置总页进入时再次发送 `view=selection`，根据启用因子显示子页面按钮。
5. 颗粒物、气象、气体和 TVOC 子页面进入时分别按第 8 节发送局部查询。
6. 每个详细页面保存时使用 `mode=merge`，单次最多 4 个因子。
7. 风速和风向只保留开关，不进入单位/报警详细修改。
8. TVOC 被选择时关闭并禁用其他开关；其他因子开启时不得同时开启 TVOC。
9. `selection` 保存成功后按 `restart_required` 执行现有重启流程。
10. `merge` 保存成功后按 `restart_required` 执行现有重启流程。
11. 新 LCD 不再用全量 `set_config/sensors` 执行开关或详细页面保存。
12. 保留旧全量报文解析能力，便于兼容和调试。

## 13. LCD/MCU 联调验收

1. 旧远程端全量查询和全量更改无需升级即可继续工作。
2. 开关页正确恢复版本下拉框和每个开关状态。
3. 颗粒物关闭 PM1 后版本仍为颗粒物，颗粒物详细页不再编辑 PM1。
4. 扬尘关闭噪声后版本仍为扬尘，气象详细页不再编辑噪声。
5. 空气微站关闭全部四气体后自动变为扬尘。
6. 扬尘关闭全部扬尘特征因子后自动变为颗粒物。
7. TVOC 与其他因子不能同时保存。
8. 每次局部查询和局部修改最多 4 个因子。
9. 保存失败时不重启、不产生部分配置。
10. 重启后启用状态、单位和报警值保持一致。
11. LCD 串口无 `UART_OVERFLOW`，MCU 无大 JSON 解析失败。

## 14. MCU 2.0.14 实现说明

1. 已实现第 6～10 节全部新旧接口；旧 Remote 端无需修改程序。
2. MCU 使用 FFAT `/sensorCatalog.json` 保存所有 15 个已知因子的详细配置；
   `/model.json` 仍只保存当前启用因子。
3. 第一次建立目录时，当前已经启用的因子以设备现有配置为准，未启用因子使用内置
   默认值；以后关闭再打开时恢复目录中的值。
4. TVOC 首次默认值为 `name=TVOC, unit=ug/m3, alarmLimit=500`。
5. `selection` 或 `merge` 成功后 MCU 文件已保存，但运行内存中的采集器列表要在重启
   后更新。因此 LCD 必须执行 `restart_required`，不得连续保存后跳过重启继续操作。
6. Firmware 2.0.14 构建状态为 `compiled-not-hardware-verified`；首次联调按第 13 节
   执行并保留 MCU、LCD 和 Remote 完整日志。
