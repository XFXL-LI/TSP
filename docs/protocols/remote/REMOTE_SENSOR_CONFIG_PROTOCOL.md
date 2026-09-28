# TSP 云端因子配置读取与修改接口

文档版本：1.0；核对日期：2026-09-28。

适用固件：standard 2.1.1、certified 2.1.1.1；沿用2.1.0系列的因子配置接口。

核对状态：已按两套正式源码核对；云端程序和实际链路仍需联调验收。

本文供云端开发人员实现因子开关、因子详情读取和修改，以及当前采集/上传周期的配置显示。
MCU 已具备本文配置接口，2.1.1系列不改变本文因子选择、详情查询和保存语义。
实时数据的可选设备状态扩展另见
[LCD/云端实时数据与设备状态接口](../lcd/LCD_GET_DATA_STATUS_PROTOCOL.md)。

新云端使用 `selection` 管理开关，使用 `ids` 查询详情，使用 `merge` 修改详情。
旧全量接口继续兼容，但不应作为新页面的局部保存接口。

## 1. 适用范围与旧文档的关系

正式源码仅为：

- standard：`firmware/standard/TSP`，固件版本 `2.1.1`。
- certified：`firmware/certified/TSP`，固件版本 `2.1.1.1`。

两套正式固件的本文配置接口逻辑一致；不适用于 experimental 实验固件。
`certified` 名称不代表本文接口已经完成云端或硬件验收。

`开发文档/接口文档/远程接口文档.docx` 和同目录 PDF 中更早的配置报文不能直接作为
当前配置开发依据。本文覆盖的配置业务以本文为准，其他业务需另行按当前实现核对。

特别注意：

- 当前 `get_config` 必须明确指定字符串 `config`，例如 `"sensors"`。
- 当前配置响应使用 `config/content`，不是旧文档中的 `params/values`。
- 当前配置保存把对象放在 `content` 中；`config` 不是配置对象。
- 本文不重新定义 MQTT Topic、DTU 网络参数或云端转发服务。需要确认网关能将本文 JSON
  完整透传到 Remote DTU 串口，并将 MCU 响应原样回传；不能直接假设旧 Topic 仍适用。
- 本文不修改 HJ212 ACK、气体校准、OTA 或继电器控制协议。

## 2. 链路与报文约束

| 项目 | 当前实现或接入要求 |
| --- | --- |
| MCU 接入通道 | Remote DTU 专用串口 `SERIAL_DTU`，不是 HJ212 上传串口 |
| MCU 串口参数 | 115200，8N1；不要套用 LCD 的 9600 波特率 |
| 载荷 | 单行紧凑 JSON，结尾 `\r\n`；示例代码块本身不显示结尾字节 |
| 编码 | UTF-8；操作名、因子 ID 和提交的单位使用 ASCII |
| 请求长度 | 建议紧凑编码后不超过 1800 字节；当前接收器在超过 2000 字节时丢弃未完成报文 |
| 接收停顿 | MCU 对未完成 JSON 检查 500 ms 无后续字节的超时；网络服务应缓冲完整请求后连续转发 |
| 请求关联 | 当前响应不回显业务请求 ID；同一设备配置请求应串行执行，不并发发送 |
| MCU 内部等待 | `get_config` 最长等待配置任务 5000 ms；`set_config` 为 2000 ms，不含云端网络耗时 |

云端应使用增量接收缓冲，不假设一次接收恰好对应一个 JSON。
不得附加 BOM、串口助手时间戳、注释或 Markdown 标记。
不要在字符串中嵌入原始花括号或换行；当前串口接收器按花括号计数，不能视为通用 JSON 流解析器。

匹配响应至少检查 `operation`、`code`，配置业务还应检查 `config`。
`content` 是 JSON 对象，不是需要再次解析的 JSON 字符串。
`reason` 和 `message` 均可能出现，不能只支持其中一种。

## 3. 登录与权限

读取配置要求已登录且至少为 USER；修改配置要求 ADMIN。云端应使用现场授权账户，
不要把源码或旧文档里的默认账户当作生产凭据。

登录请求模板，其中占位字符串必须替换为已授权凭据：

```json
{"operation":"login","user":"YOUR_ADMIN_USER","pass":"YOUR_AUTHORIZED_PASSWORD"}
```

管理员登录成功响应：

```json
{"operation":"login","code":"OK","message":"admin"}
```

未登录读取配置示例：

```json
{"operation":"get_config","code":"NG","message":"Account not logged in"}
```

非管理员保存配置示例：

```json
{"operation":"set_config","code":"NG","message":"Permission denied: ADMIN required"}
```

MCU 当前使用公共 `PermissionSystem`，LCD 和 Remote 并非两个独立登录会话。
有效业务命令更新活动时间，空闲超时阈值为 15 分钟；重启后需要重新登录。
云端不得只凭网络连接仍在线就假设 MCU 登录态仍有效。

## 4. 因子 ID 与单位

配置键大小写敏感。下表的单位是建议工程单位，不表示设备现有配置必然相同，
实际值应查询后显示，不要用模板默认值覆盖现场配置。

| 因子 | 配置 ID | 建议工程单位 |
| --- | --- | --- |
| TSP | `a34001` | `ug/m3` |
| PM1 | `a34005` | `ug/m3` |
| PM2.5 | `a34004` | `ug/m3` |
| PM10 | `a34002` | `ug/m3` |
| 风速 | `a01007` | `m/s` |
| 风向 | `a01008` | `degree` |
| 气压 | `a01006` | `kPa` |
| 温度 | `a01001` | `celsius` |
| 湿度 | `a01002` | `%` |
| 噪声 | `L90` | `dB` |
| O3 | `w34011` | `ug/m3` |
| NO2 | `a21004` | `ug/m3` |
| CO | `a21005` | `mg/m3` |
| SO2 | `a21026` | `ug/m3` |
| TVOC | `a24035` | `ug/m3` |

O3 配置键是 `w34011`，不是 HJ212 输出代码 `a05024`；噪声配置键是 `L90`，不是 HJ212
输出代码 `LA`。新配置接口不接受这些输出代码作为替代 ID，也不接受旧文档的 O3 `a21008`。

四气体 `unit` 仅支持 `ppb`、`ppm`、`ug/m3`、`mg/m3`；MCU 会去除首尾空白并转为小写。
气体单位更改会影响重启后的 `get_data`、显示和 HJ212 浓度转换，云端接收/显示口径必须同步，
不能再次把已经换算的数值当作原始 ppb 换算。

其他因子单位在 `merge` 路径中仅验证为 1～16 字节的可打印 ASCII，不允许双引号和反斜杠。
通过字符串校验不代表 MCU 会做相应物理单位换算；非气体单位应按实际工程意义限制选项。

`alarmLimit` 是因子详情的整数阈值，单位应与该因子 `unit` 对应，当前用于 LED 越限显示。
它不是气体量程设置，也不是 GPIO40 的 `alarmConfig` 继电器上下限。
certified 的 O3/NO2/SO2 500 ppb 上限策略不能通过本接口关闭或改变，CO 不受该上限策略限制。

## 5. 建议的初始化顺序

登录成功后，串行执行：

1. 查询 `systemInfo`，读取 `content.version`，确认是本文适用固件。
2. 查询 `systemConfig`，读取实际周期；当前两字段均为 120 秒。
3. 查询 `sensors` 的 `view=selection`，恢复所有开关与实际产品类型。
4. 进入详情页面后，按需要查询 1～4 个 ID。

版本查询：

```json
{"operation":"get_config","config":"systemInfo"}
```

运行固件的版本由 MCU 编译常量覆盖配置文件中的历史版本值；云端读取 `content.version`，
不要根据升级文件夹名称或旧缓存判断设备当前固件。

## 6. 查询因子开关

请求：

```json
{"operation":"get_config","config":"sensors","view":"selection"}
```

成功响应示例：

```json
{"operation":"get_config","code":"OK","config":"sensors","profile":"particulate","enabled":["a34001","a34004","a34002"]}
```

`enabled` 是完整启用列表；不在列表中的已知因子为关闭。不要依赖数组顺序。
`profile` 根据启用列表计算，不是固件 variant，也不是固件版本号：

| 优先级 | 条件 | `profile` |
| --- | --- | --- |
| 1 | 仅启用 TVOC | `tvoc` |
| 2 | 启用任意 O3、NO2、CO、SO2 | `air_station` |
| 3 | 无四气体，但启用任意风速、风向、气压、温度、湿度、噪声 | `dust` |
| 4 | 仅启用颗粒物因子 | `particulate` |

## 7. 保存因子开关

请求中的 `enabled` 必须是保存后的完整启用列表，不是本次新增或删除的差异列表：

```json
{"operation":"set_config","config":"sensors","mode":"selection","enabled":["a34001","a34004","a34002"]}
```

成功响应：

```json
{"operation":"set_config","code":"OK","config":"sensors","mode":"selection","profile":"particulate","enabled_count":3,"restart_required":true}
```

保存规则：

- 不允许空列表、重复 ID 或未知 ID。
- 非 TVOC 因子最多启用 14 个。
- TVOC 与其他因子互斥，启用 TVOC 时列表必须只有 `a24035`。
- 不提交 `profile` 或每个因子的详情来控制开关。
- 关闭因子时保留其目录中的单位和阈值；重启后重新启用可恢复先前详情。
- 成功后必须按第 10 节重启并回读，不能把 `enabled_count` 当作已经生效的运行因子数量。

## 8. 查询因子详情

每次提交 1～4 个已知、不重复的 ID。不得同时携带 `view:"selection"`，因为 MCU 优先处理 `view`。

颗粒物请求：

```json
{"operation":"get_config","config":"sensors","ids":["a34001","a34005","a34004","a34002"]}
```

假设 PM1 已关闭，成功响应示例：

```json
{"operation":"get_config","code":"OK","config":"sensors","content":{"a34001":{"name":"TSP","alarmLimit":500,"unit":"ug/m3"},"a34004":{"name":"PM2.5","alarmLimit":500,"unit":"ug/m3"},"a34002":{"name":"PM10","alarmLimit":500,"unit":"ug/m3"}}}
```

其他分组请求：

```json
{"operation":"get_config","config":"sensors","ids":["a01006","L90","a01001","a01002"]}
```

```json
{"operation":"get_config","config":"sensors","ids":["w34011","a21004","a21005","a21026"]}
```

```json
{"operation":"get_config","config":"sensors","ids":["a24035"]}
```

响应只返回当前运行中已启用的因子。查询已知但关闭的因子不会自动启用它，
全部请求因子均关闭时可以成功返回空对象 `content:{}`。
云端依据开关列表隐藏或禁用其详情，不应把缺少一个键解释为通信失败。

| 详情字段 | 类型 | 含义 |
| --- | --- | --- |
| `name` | 字符串 | 当前名称；新局部接口中只读 |
| `unit` | 字符串 | 当前配置单位；可按第 9 节修改 |
| `alarmLimit` | 整数 | 当前因子阈值；可按第 9 节修改 |

## 9. 局部修改因子详情

只修改 PM2.5 阈值，不改变其单位或其他因子：

```json
{"operation":"set_config","config":"sensors","mode":"merge","content":{"a34004":{"alarmLimit":350}}}
```

成功响应：

```json
{"operation":"set_config","code":"OK","config":"sensors","mode":"merge","changed":["a34004"],"restart_required":true}
```

若 SO2、CO 均已启用，修改单位时同时提交对应单位下的阈值，例如：

```json
{"operation":"set_config","config":"sensors","mode":"merge","content":{"a21026":{"unit":"ppb","alarmLimit":250},"a21005":{"unit":"ppm","alarmLimit":5}}}
```

数值仅为接口示例，不是现场阈值建议。返回 `changed` 表示接受的因子 ID，
不保证该因子的新值与旧值实际不同。

验证与保存规则：

- `content` 为非空对象，单次最多 4 个因子，且这些因子当前必须已启用。
- 每个因子只允许 `unit`、`alarmLimit`，至少提供其中一个字段。
- 不允许提交 `name`、`enabled`、`range`、`address` 等其他字段。
- `alarmLimit` 必须是 JSON 数字，数学意义上为整数，范围 0～1000000000；字符串、小数、负数均拒绝。
- 未提供的字段和未提交的因子保持当前运行配置值；`merge` 不增加、删除或启停因子。
- 先验证所有补丁，再保存；验证失败不会进入保存步骤。写盘失败不等同于具备断电原子回滚保证。
- MCU 不自动换算单位改变前的阈值，云端必须同时提交与新单位对应的数值。

## 10. 保存后的重启与回读

`selection/merge` 的 `code=OK` 表示文件保存成功，不表示运行中的采集器、配置映射和上传单位已更新。
`restart_required:true` 必须处理。二者查询使用运行配置，保存后立即查询仍可能得到旧值。

云端执行顺序：

1. 一次提交一组变更并等待响应；收到 `NG` 不自动重启。
2. `OK` 且 `restart_required:true` 时，停止后续配置写入，并提示/安排设备重启。
3. 用户确认后通过已有重启方式重启；JSON 请求见下方。网关应保留响应后停止后续业务发送。
4. 等设备重新上线，再登录、查询版本和周期、查询开关、查询本次修改的详情。
5. 回读与目标一致才显示“配置已生效”；仅收到保存响应应显示“已保存，待重启生效”。

重启请求与当前确认响应：

```json
{"operation":"restart"}
```

```json
{"operation":"restart","code":"OK","message":"Device restarting..."}
```

该响应只表示 MCU 准备重启，不能证明设备已重新启动。当前重启请求在入口直接处理；
云端必须自行限制管理员操作和设备授权，不能把本接口当作额外的 MCU 鉴权保障。

不得先保存 A 分组、再保存 B 分组、最后仅重启一次：`merge` 从运行内存生成完整模型，
后一次保存可能覆盖前一次未重启的改动。需修改超过 4 个因子时，每组保存后重启并重新查询，
再保存下一组。也不要在 `selection` 保存后未重启就继续修改刚启用的因子。

保存超时或链路中断时，可能已经写入但响应没有到达。状态应标记为“待确认”，
停止连续自动重发；恢复链路后重新核对，若文件与运行状态不一致，应在用户确认的重启后回读。
存储失败时也不能承诺旧文件完整保留，需进一步核对，不能以 `NG` 为由自动切换到旧全量接口。

## 11. 旧全量接口兼容

旧全量查询仍有效：

```json
{"operation":"get_config","config":"sensors"}
```

返回 `operation/code/config/content`，其中 `content` 是已启用因子的完整配置。
该兼容查询读取保存文件；与第 6、8 节的运行配置查询来源不同，未重启时可能不一致。

不带 `mode` 的全量保存同样保留。以下示例是一个最终只启用 PM2.5 和 PM10 的完整配置，
不是日常详情修改示例：

```json
{"operation":"set_config","config":"sensors","content":{"a34004":{"name":"PM2.5","alarmLimit":500,"unit":"ug/m3"},"a34002":{"name":"PM10","alarmLimit":500,"unit":"ug/m3"}}}
```

成功响应示例：

```json
{"operation":"set_config","code":"OK","config":"sensors","content":{"a34004":{"name":"PM2.5","alarmLimit":500,"unit":"ug/m3"},"a34002":{"name":"PM10","alarmLimit":500,"unit":"ug/m3"}}}
```

兼容路径的关键差别：

- `content` 中缺少的因子在重启后关闭；不能只提交待修改的一项。
- 成功响应没有 `mode/changed/restart_required`，但仍需要重启生效；云端须按兼容路径自行处理。
- 旧全量路径对不合法气体单位会回退到默认单位；新的 `merge` 明确返回 `invalid_unit`。
  不要用旧路径绕过新路径的验证。
- 新页面不得自动去掉 `mode` 重试，也不得把详情查询得到的一个分组当作完整启用列表提交。
- 更早文档中不带配置名的 `get_config`、对象类型的 `config`、`collect_ids` 和旧
  `params/values` 配置响应，不属于本文承诺兼容的报文格式。

## 12. 当前采集与上传周期

查询：

```json
{"operation":"get_config","config":"systemConfig"}
```

成功响应为 `operation=get_config`、`code=OK`、`config=systemConfig`，完整配置位于 `content`。
云端重点读取以下字段，其他字段保留其原值：

| 字段 | 类型/单位 | 当前正式固件值 |
| --- | --- | --- |
| `content.collect_time` | 整数，秒 | 120，固定 |
| `content.upload_interval` | 整数，秒 | 120，固定 |

当前 MCU 在查询和保存 `systemConfig` 时均把两字段规范化为 120。
旧设备文件可能存着 60，但本版本查询仍返回 120；启动时不会仅为此自动改写历史文件。
如果云端提交 60/180/300，不能期待 `NG` 表示不可用，也不能把 `OK` 理解为请求值被采用：
当前实现会将两字段改为 120 后保存并回传。

因此当前云端页面应显示“120 秒，固件固定”，不要提供可自由修改周期的控件。
修改 `systemConfig` 的其他字段时，必须先取得最新完整 `content`，保留所有其他字段，再提交
`{"operation":"set_config","config":"systemConfig","content":完整对象}`。
这里只是结构说明，不是可直接发送的 JSON。
该路径是整体文件替换，不是自动合并，禁止只提交两个周期字段而丢失其他配置。

不要把 LCD 可列出 60/120/180/300 的兼容显示能力理解为当前 MCU 接受自由周期设置；
也不要套用 experimental 的 5 秒实验节拍。

## 13. 失败响应与处理

因子接口失败示例：

```json
{"operation":"set_config","code":"NG","config":"sensors","reason":"sensor_not_enabled"}
```

| `reason` | 含义/处理 |
| --- | --- |
| `invalid_mode` | `view/mode` 类型或值不支持；检查是否用了本文准确字段 |
| `invalid_sensor_id` | 未知 ID、ID 类型错误或无效/空详情查询；检查第 4 节 |
| `duplicate_sensor_id` | 数组或补丁含重复 ID；不要生成重复 JSON 键 |
| `empty_selection` | 开关列表为空；至少保留一个有效因子 |
| `too_many_ids` | 详情查询超过 4 个 ID，或开关选择超过允许数量 |
| `too_many_patch_items` | 单次修改超过 4 个因子 |
| `tvoc_exclusive` | TVOC 与其他因子同时启用 |
| `sensor_not_enabled` | 试图修改未启用因子；先保存开关、重启并回读 |
| `unsupported_field` | 空补丁、详情不是对象或含不允许字段；只发送允许字段 |
| `invalid_unit` | 单位类型/内容不合法；按第 4 节处理 |
| `invalid_alarm_limit` | 阈值类型、整数性或范围不合法 |
| `config_save_failed` | 详情保存/序列化失败，部分查询分支内存不足也使用此码；需核对设备状态 |
| `state_save_failed` | 开关目录/模型保存失败；需核对存储和设备状态 |

其他错误使用 `message`，例如 `Account not logged in`、权限不足、`Response timeout`、
`Config serialization failed`、`Config save failed`、`Config reload failed`。
云端必须兼容缺少 `config` 的通用失败响应和 `content:null` 的保存失败响应。
不要仅按 HTTP/MQTT 投递成功判断 MCU 配置成功。

## 14. 云端改动清单与联调验收

实现要点：

1. 改为本文 `config/content` 结构，不继续使用旧文档的配置 `params/values`。
2. 因子开关与详情保存分开；开关提交最终完整列表，详情明确使用 `mode=merge`。
3. 大页面按最多 4 个 ID 分组查询；保存按第 10 节逐组重启和回读，不连续覆盖。
4. 正确处理因子缺项、空详情对象、`NG/reason/message`、保存超时与待重启状态。
5. 当前周期只读显示 120 秒，不擅自把 120 改为 60。
6. 管理员授权、设备识别、版本识别和请求串行化由云端落实；现场参数不使用模板覆盖。

以下测试需在获准修改配置的测试设备上执行，先记录原配置，完成后按相同流程恢复；
本文编写期间没有向设备发送这些命令。

| 场景 | 预期结果 |
| --- | --- |
| 管理员登录，读取版本/周期/开关 | 实际版本正确；周期两字段均 120；完整开关列表可恢复 |
| 查询启用的 1～4 个因子详情 | `OK`，返回单位和整数阈值 |
| 查询已知但关闭的因子 | `OK`，对应键省略；全关闭可返回 `content:{}` |
| 查询 5 个 ID、未知 ID、重复 ID | 分别返回限制/ID错误，不自动降为全量读写 |
| 修改一个已启用因子的阈值 | `OK` 和 `restart_required:true`；重启回读仅该阈值变化 |
| 修改四气体单位及配套阈值 | 重启后详情、数据显示和上传单位口径一致，不二次换算 |
| 修改关闭因子、`name` 字段、字符串/小数/负阈值 | `NG`，不得自动重启或走全量保存绕过验证 |
| 关闭再启用一个因子，每次保存后重启 | 开关准确，原单位和阈值保留 |
| TVOC 与其他 ID 混合、启用列表为空 | `NG`，分别为 `tvoc_exclusive/empty_selection` |
| 受控旧客户端全量读写 | 兼容响应可解析；未提交因子按全量语义关闭；保存后重启回读 |
| 保存后响应丢失或中断 | 页面标为待确认，不并发重发；重新上线并回读后判定 |
| standard 与 certified 各重复上述核心测试 | 接口格式一致，certified 专用气体策略未被配置接口绕过 |

## 15. 源码与相关协议

- [standard 配置分发及因子实现](../../../firmware/standard/TSP/src/app/configManager/config.cpp)
- [certified 配置分发及因子实现](../../../firmware/certified/TSP/src/app/configManager/config.cpp)
- [权限、响应封装与串口发送](../../../firmware/certified/TSP/src/app/permissionManager/permissionManager.cpp)
- [权限声明与登录超时](../../../firmware/certified/TSP/src/app/permissionManager/permissionManager.h)
- [Remote DTU JSON 入口](../../../firmware/certified/TSP/src/system/system/system.cpp)
- [串口参数](../../../firmware/certified/TSP/src/module/Serial/SerialManager.h)
- [气体单位转换](../../../firmware/certified/TSP/src/module/gas/GasUnitConverter.h)
- [HJ212 代码映射及配置单位使用](../../../firmware/certified/TSP/src/module/pack212/pack212.cpp)
- [LCD 因子配置协议](../lcd/LCD_SENSOR_CONFIG_PROTOCOL.md)
- [Remote DTU OTA 协议](REMOTE_DTU_OTA_SENDER_PROTOCOL.md)

对应函数：`PermissionSystem::processLine/executeCommand/onConfigRes/onSetConfigRes`；
`ConfigManager::getConfigRes/setConfigRes/getSensorSelectionRes/getSensorSubsetRes/`
`setSensorSelectionRes/setSensorMergeRes`；`normalizeSystemTimingObject`。
