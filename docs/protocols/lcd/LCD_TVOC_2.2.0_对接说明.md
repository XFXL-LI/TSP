# LCD 屏幕 TVOC 对接说明

- 文档版本：1.0
- 日期：2026-10-28
- 适用 MCU：standard 2.2.0、certified 2.2.0.1
- 适用范围：TVOC 日常浓度、批次状态、因子选择、单位、数量显示及现有历史查询。
- 实施状态：MCU 已完成主机回归和 ESP32 编译；LCD 配合升级及实机联调待完成。

本文件按当前 MCU 实现编写。示例数据是接口演示，不是现场测量记录。
示例中的字段顺序、JSON 数字尾随零不作为匹配依据。代码块中的报文实际应单行发送。

## 1. LCD 本次需要完成的修改

| 项目 | LCD 要求 |
| --- | --- |
| TVOC 因子 | 使用 `a99054`，显示名称 TVOC；更新选择开关、卡片、配置、趋势和历史查询映射 |
| 设备模式 | 根据 `profile="tvoc"` 和 `enabled=["a99054"]` 进入单 TVOC 模式，其他监测因子互斥 |
| 主页面浓度 | 显示 MCU 返回的最近完成批次平均值，不显示内部每 5 秒的单点值 |
| 查询方式 | 保持原查询应答机制及现有查询节奏，不增加 2 秒/5 秒快速查询或主动推送 |
| 新批次识别 | 使用 `ds.seq` 去重；重复查询同一批次不重复累计或插入趋势 |
| 有效性 | 使用 `ds.valid_mask`，有效零值正常显示；无效占位零不当成正常零值 |
| 数量与状态 | 识别 `ds.tvoc`，建议在现有状态栏/详情页显示“有效点 11/12”和批次状态 |
| 单位 | 提供 ppb、ppm、μg/m³、mg/m³；按 MCU 配置显示，不能再次换算返回数值 |
| 配置生效 | 保存成功且要求重启时，沿用现有重启流程，重连后重新查询配置 |
| 标定 | 本次不开放 TVOC 零点、跨度、取消、维护会话等控制入口 |
| 其他功能 | 空气微站、登录、配置、OTA 交互沿用现有协议 |

同一套 MCU 固件支持空气微站与 TVOC 配置；运行时选择其中一种配置。
TVOC 单因子不限制 MCU 自身温湿度等运行状态字段的显示。

## 2. 串口、拼包与请求队列

- LCD 连接 MCU 的 LCD 专用串口：9600 bit/s、8N1。
- 业务报文为单行紧凑 JSON，实际帧末尾发送 CR LF 两字节（0x0D、0x0A）。
- 字段名、因子 ID、单位编码使用本文 ASCII 字符串；发送时不附加 BOM 或注释。
- MCU 的新版状态回复包含 CR LF 最多 800 字节。LCD 接收容量还需要实机确认。
- 接收器须能处理分包和多帧粘包：完整 JSON 及时解析，保留剩余半帧。
- 串口业务同一时刻只保留一个在途普通请求；配置、数据及历史查询共用原有串行队列。
- 按 `operation` 分发消息；OTA 通知沿用现有独立处理，不能当成 `get_data` 回复。
- 不新增 TVOC 专用串口、Modbus 透传或状态轮询任务。

LCD 查询读取 MCU 缓存，不触发传感器采样，不触发写 SD，也不改变采集周期。

## 3. 启动与配置交互

### 3.1 启动顺序

1. 沿用现有登录与初始化流程。
2. 获取 MCU 固件版本和当前因子选择。
3. 根据选择结果决定当前显示空气微站还是 TVOC。
4. TVOC 模式下读取 a99054 的单位及报警设置。
5. 恢复原有 `get_data` 查询，携带 `with_status:true`。
6. 尚无首批时显示“等待首批数据”；配置、SD或时间状态分别显示。

MCU 的 `systemInfo.content.version` 是字符串，可能为 `2.2.0` 或四段 `2.2.0.1`，
不能转成小数比较；`profile` 和 `ds.v` 都不是固件版本号。

版本查询请求：

```json
{"operation":"get_config","config":"systemInfo"}
```

读取成功回复的 `content.version` 即可；其他 systemInfo 字段沿用原处理。

### 3.2 查询当前启用因子

```json
{"operation":"get_config","config":"sensors","view":"selection"}
```

TVOC 模式成功回复：

```json
{"operation":"get_config","code":"OK","config":"sensors","profile":"tvoc","enabled":["a99054"]}
```

LCD 不只凭本地上次保存结果判断模式，重启后要重新查询。
启用 TVOC 时，界面关闭并禁用其他监测因子开关。

### 3.3 选择 TVOC 模式

```json
{"operation":"set_config","config":"sensors","mode":"selection","enabled":["a99054"]}
```

成功回复：

```json
{"operation":"set_config","code":"OK","config":"sensors","mode":"selection","profile":"tvoc","enabled_count":1,"restart_required":true}
```

收到 OK 后按现有流程重启；收到 NG 时显示失败原因，不重启。
不能把 TVOC 和其他因子放在同一个 enabled 列表中。

### 3.4 查询 TVOC 详细参数

```json
{"operation":"get_config","config":"sensors","ids":["a99054"]}
```

新启用、使用默认参数时的回复示例：

```json
{"operation":"get_config","code":"OK","config":"sensors","content":{"a99054":{"name":"TVOC","alarmLimit":500,"unit":"ppb"}}}
```

已有配置可能保留其他合法单位，不应在 LCD 上强制改回默认 ppb。
如果 content 没有 a99054，先重新查询选择状态，不自行补造一个已启用因子。

### 3.5 保存单位与报警设置

示例：把单位设为 ppm，用户选择报警阈值 2 ppm。

```json
{"operation":"set_config","config":"sensors","mode":"merge","content":{"a99054":{"unit":"ppm","alarmLimit":2}}}
```

成功回复：

```json
{"operation":"set_config","code":"OK","config":"sensors","mode":"merge","changed":["a99054"],"restart_required":true}
```

- merge 只修改已启用因子的 unit、alarmLimit，不能修改名称、启用状态或底板量程。
- 单位变化时，LCD 应同时提交用户确认后的报警值；MCU 不自动换算原报警阈值。
- 当前 alarmLimit 只接受 0～1000000000 的整数，不支持小数阈值。
  例如 0.5 ppm 不能直接提交；不要截断成 0。需要小数阈值时须另行扩展 MCU 接口。
- alarmLimit 与底板量程 R 是不同设置；修改报警值不改 R，也不产生或清除 T 标志。
- 保存成功后完成重启、重新读取配置，再更新单位标签，避免旧数值配上新单位。
- 本次 unit 是共享因子配置，同时影响 LCD/LED、历史查询及 HJ212 输出；
  没有新增“仅 LCD 显示单位”和“平台上传单位”两个独立字段。

## 4. 采样节奏与显示语义

| 层级 | MCU 当前处理 | LCD 处理 |
| --- | --- | --- |
| 内部单点 | 抽气窗口内每 5 秒读取浓度和状态 | 不获取或显示该单点序列 |
| 实时批次 | 每 120 秒一轮，完成采样后发布一次有效点均值 | 主界面、实时趋势使用这批值 |
| 分钟数据 | 沿用现有 10 分钟统计窗口 | 保持现有分钟历史功能 |
| 小时、天数据 | 沿用原统计与保存方式 | 保持现有小时、天历史功能 |

当前工程候选时序为开泵70秒、休息50秒，第10、15……65秒共12个计划槽。
前10秒是否足够通气，留待标定联调确认；LCD 不把12写成永远固定的样本数量。

首批通常在采集任务启动后的约70秒形成，初始化、重启、OTA恢复时间另计。
后续约120秒更新一批。LCD 不要求每5秒出现新 seq，也不新增固定70秒阻塞等待。

**屏幕上的“实时值”是最新完成批次均值，不是瞬时浓度，也不是完整120秒连续平均。**
LCD 不对重复查询的缓存值再次求平均、平滑或固定去最大/最小值。

## 5. get_data 请求与成功回复

请求：

```json
{"operation":"get_data","ids":["a99054"],"with_status":true}
```

with_status 必须是 JSON 布尔值，不能发字符串 "true" 或数字 1。
状态已随该回复提供，不增加独立状态查询。

### 5.1 正常批次完整示例

```json
{"operation":"get_data","code":"OK","message":"get data success","timestamp":20261028120000,"csq":20,"temp":25.3,"mete":48.0,"params":["a99054"],"values":[200],"ds":{"v":1,"init":1,"sd":2,"write":1,"time":2,"seq":7,"age_ms":3000,"valid_mask":1,"tvoc":{"samples":12,"valid":12,"flags":1,"flag":"N"}}}
```

| 字段 | 含义与 LCD 用法 |
| --- | --- |
| operation / code | get_data / OK 表示缓存查询成功，不代表设备所有部分正常 |
| timestamp | 本批日期时间，YYYYMMDDhhmmss 数字；没有有效日期时为0，不是 Unix 时间戳 |
| params / values | 同位置一一对应；Lua bit0 对应数组下标1 |
| values[i] | 已按当前 unit 换算、格式化的批次均值；不能再次除1000或乘质量系数 |
| csq | 通信信号字段，不能只凭该值认定报文已到平台 |
| temp / mete | 既有 MCU 温度/湿度字段；mete 不是 SD 容量 |

### 5.2 ds 通用设备字段

| 字段 | 定义 |
| --- | --- |
| v | 扩展结构版本，当前1；未知版本不按版本1强行解释 |
| init | 0初始化中；1必要检查结束；2关键初始化失败 |
| sd | 0未检查；1挂载中；2已挂载；3挂载失败 |
| write | 0本次启动尚无正式写入尝试；1最近写入返回成功；2最近写入返回失败 |
| time | 0系统时间无效；1当前有效时间来自RTC；2已经网络校时 |
| seq | 本次启动的批次序号，0表示尚无批次；查询不会增加序号 |
| age_ms | 最近批次缓存更新后的单调计时年龄；没有批次为-1，不是单点采样时长 |
| valid_mask | 按本次 params 顺序排列的有效位图 |
| sd_error | sd=3且有底层错误码时可附加；缺失时不默认成正常 |

SD 挂载成功与最近写入返回成功，都不等于全部历史已通过读回验证。
SD/时间异常不能阻止有效的本地浓度显示；时间无效时暂停建立正式历史日期基准。
time 描述当前系统时钟；timestamp 描述本批时间，不能用当前时间给旧批次补造日期。

### 5.3 ds.tvoc 专用批次字段

仅请求列表包含 TVOC 且已有 TVOC 批次时提供。空列表只查状态、首批之前，
或其他设备/旧固件没有此字段时，不应据此认定通信失败。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| samples | 整数 | 计划槽数，当前12；包含未读、错过或失败的槽，不是成功采样数 |
| valid | 整数 | 实际有效点数，范围0～samples |
| flags | 整数 | 最后一个槽读取到的模块标志；最后槽状态未读到时为0 |
| flag | 单字符字符串 | 整批状态 N、T、B、D、C |

建议显示“有效点11/12”。只要 MCU 判定均值有效，就允许显示：
**valid=1 也可有效，不在 LCD 额外设置至少9点或必须12点的门槛。**

flags 是最后一个槽的标志，flag 是整批状态，二者不是同一含义。
中间有效点可以形成均值，而最后槽读取失败；因此 flags=0 不能单独推翻 valid_mask。
flags 也不提供该槽是否成功读取的独立布尔字段，LCD 不据其单独下故障定论。

## 6. 有效性与状态显示

TVOC 单因子请求时，valid_mask=1表示均值可用，0表示本批不可用。
对任意因子排列，第 i 项（Lua下标从1开始）的有效性可按：

    math.floor(valid_mask / (2 ^ (i - 1))) % 2 == 1

处理顺序：

1. 检查 JSON、operation、code、数组对应关系及 ds.v。
2. seq=0：显示“等待首批数据”，不把占位0当浓度。
3. seq非0且对应有效位为1：显示 values[i]，包括真实0。
4. 有效位为0：显示“--/数据无效”，按 flag 提示原因；保留上次值时必须标明为旧值。
5. 无论 seq 是否变化，都更新设备状态、数据年龄和通信提示。
6. 只有收到新批次才处理新的趋势点；数值相同但 seq 变化仍是新批次。
7. 新批次有效时追加有效数值，无效时保留缺测标志，不向正常曲线插入占位零。

| flag | 含义 | LCD 处理 |
| --- | --- | --- |
| N | 本批有可用均值 | 正常显示，按返回值/配置单位处理现有报警 |
| T | 有效样本中出现超量程 | 保留均值，提示超量程；不封顶、不归零 |
| B | 本批无有效均值，通信/读取失败类 | 显示数据无效/读取异常 |
| D | 本批无有效均值，参数/测量故障类 | 显示测量无效；不宣称一定是硬件损坏 |
| C | 本批无有效均值，标定忙等状态 | 显示标定中/本批无有效数据，不开放写入控制 |

若有效位与状态字段出现不符合预期的组合，保留诊断信息并以有效位决定是否使用值，
不要把占位0画成正常测量。未知 flag 保留为“状态未知”，不自行套四气体规则。

### 6.1 首批尚未形成

```json
{"operation":"get_data","code":"OK","message":"get data success","timestamp":0,"csq":20,"temp":25.3,"mete":48.0,"params":["a99054"],"values":[0],"ds":{"v":1,"init":1,"sd":2,"write":0,"time":2,"seq":0,"age_ms":-1,"valid_mask":0}}
```

显示等待首批；缺少 ds.tvoc 正常，不自动重启主板。

### 6.2 只有一个有效点，且浓度确实为零

```json
{"operation":"get_data","code":"OK","message":"get data success","timestamp":20261028120200,"csq":20,"temp":25.3,"mete":48.0,"params":["a99054"],"values":[0],"ds":{"v":1,"init":1,"sd":2,"write":1,"time":2,"seq":8,"age_ms":3000,"valid_mask":1,"tvoc":{"samples":12,"valid":1,"flags":1,"flag":"N"}}}
```

显示0 ppb、有效点1/12。不能显示成故障，也不使用另外11个占位零参与平均。

### 6.3 本批读取失败

```json
{"operation":"get_data","code":"OK","message":"get data success","timestamp":20261028120400,"csq":20,"temp":25.3,"mete":48.0,"params":["a99054"],"values":[0],"ds":{"v":1,"init":1,"sd":2,"write":1,"time":2,"seq":9,"age_ms":3000,"valid_mask":0,"tvoc":{"samples":12,"valid":0,"flags":0,"flag":"B"}}}
```

显示“--/读取异常”、有效点0/12。values里的0是兼容占位。

### 6.4 超量程

```json
{"operation":"get_data","code":"OK","message":"get data success","timestamp":20261028120600,"csq":20,"temp":25.3,"mete":48.0,"params":["a99054"],"values":[6000],"ds":{"v":1,"init":1,"sd":2,"write":1,"time":2,"seq":10,"age_ms":3000,"valid_mask":1,"tvoc":{"samples":12,"valid":12,"flags":33,"flag":"T"}}}
```

显示6000 ppb并提示超量程。认证版也不能套用O3/NO2/SO2的500ppb封顶。

### 6.5 没有有效日期，但本地浓度有效

```json
{"operation":"get_data","code":"OK","message":"get data success","timestamp":0,"csq":20,"temp":25.3,"mete":48.0,"params":["a99054"],"values":[200],"ds":{"v":1,"init":1,"sd":2,"write":0,"time":0,"seq":11,"age_ms":3000,"valid_mask":1,"tvoc":{"samples":12,"valid":12,"flags":1,"flag":"N"}}}
```

显示200 ppb并提示“时间未校准”。不以 timestamp=0 拒绝显示，
也不把它保存到LCD的1970年或伪造为当前日期的正式历史。

### 6.6 模块 flags 位表（只作诊断）

| 位 | 数值 | TVOC 含义 |
| --- | ---: | --- |
| bit0 | 1 | 存在完整有效标定参数 |
| bit1 | 2 | 首次零点暂存，等待跨度配对 |
| bit2 | 4 | 标定采样忙 |
| bit3 | 8 | 最近ADC失败 |
| bit4 | 16 | 最近采样饱和 |
| bit5 | 32 | 最近有效浓度超过配置量程R |

**不得套用原四气体的 bit15 就绪、低四位报警或0x0010超量程规则。**
TVOC 的 bit4 表示饱和，超量程是 bit5。
LCD 主判断采用 MCU 的有效位和批次状态；模块历史标定失败不必然代表当前测量无效。

## 7. 单位、精度和本地缓存

| MCU unit 编码 | LCD 建议显示 | 小数位 |
| --- | --- | ---: |
| ppb | ppb | 0 |
| ppm | ppm | 3 |
| ug/m3 | μg/m³ | 2 |
| mg/m3 | mg/m³ | 2 |

协议发送 ASCII 的 ug/m3、mg/m3，不发送 μg/m³ 等显示字符串。
当前数值和历史查询数值均已由 MCU 按配置换算，LCD 不再次换算。
质量单位定义为异丁烯当量、25℃、101.325kPa。内部与新SD原始记录统一ppb，
换算系数约2.29329 μg/m³/ppb，由MCU统一处理。

单位改变并完成重启确认后，清理/重建 LCD 内存中的旧单位缓存、曲线轴与日期基准。
如果 LCD 自己持久化了已转换数值，必须保留单位标识，不能与新单位数值混排。
本地缓存键建议至少包含因子、单位、批次时间/本次连接的批次序号。

不根据值是否变化判断新批次，也不以“120秒之内每次查询”创建多条记录。
重启、实际重连或首次 OTA 恢复后清空旧序号基准；序号可能回绕，不假定永远递增。
数据新鲜度沿用与120秒周期匹配的策略，不以5秒无新值判故障。

## 8. 历史数据：沿用原查询，按单时间点串行

新 TVOC 实时、分钟、小时、天数据仍由 MCU 保存。
本次没有新增历史主动推送、连续流、分页游标或请求ID协议。
当前路由每次等待一次历史回复；LCD 不把一次范围请求当成可靠的多帧流接口。

| param | 含义 | 时间选择 |
| --- | --- | --- |
| raw（real也兼容） | 每120秒一批的实时均值 | 优先使用 MCU 返回的真实批次日期时间 |
| min | 现有10分钟统计 | 按10分钟窗口起始时刻 |
| hour | 小时统计 | 按小时起始时刻 |
| day | 天统计 | 当天00:00:00 |

### 8.1 请求示例

```json
{"operation":"get_records","param":"raw","time":"20261028120000","ids":["a99054"]}
```

```json
{"operation":"get_records","param":"min","time":"20261028120000","ids":["a99054"]}
```

```json
{"operation":"get_records","param":"hour","time":"20261028120000","ids":["a99054"]}
```

```json
{"operation":"get_records","param":"day","time":"20261028000000","ids":["a99054"]}
```

以上日期时间仅演示格式，不保证设备上存在相应记录。
收到当前查询的回复或超时后，再发送下一条；沿用现有有限重试。
当前 MCU 历史路由等待上限为15秒，LCD不能因此每秒反复发同一条请求。

raw文件按日期/小时/分钟定位，回包 timestamp 以文件实际记录为准；
可能与请求中的秒不同。LCD 不应强制把返回秒改成00，也不能假定设备批次严格对齐偶数整分钟。
分钟、小时、天记录只在统计窗口结束并处理到后续批次时生成，未生成时查不到正常。

### 8.2 成功回复示例

以 ppb 配置查询一条实时均值：

```json
{"operation":"get_records","code":"OK","message":"get records success","timestamp":20261028120000,"data":{"a99054":{"value":200,"cou":200,"max":200,"min":200}}}
```

- 从 data.a99054 读取值，单位采用本次已确认的 MCU 因子配置。
- 不在 LCD 从 get_data 轮询次数重新计算 MCU 的分钟/小时/天统计。
- cou 沿用原统计字段，不直接解释为排放质量。
- 历史响应没有 ds、valid_mask、完整模块状态或采样数量。
- 当前历史接口对请求中缺少的因子会兼容填0；仅看到 code=OK和值0，
  不能证明这是一条有效的真实零值。
- 空记录/读取异常可返回 NG；LCD保留缺测或错误提示，不能补造正常零值。
- 当前接口不能独立证明每条历史的测量有效性；需要完整质量字段时要另扩MCU接口。

### 8.3 旧TVOC历史隔离

旧 a24035 历史来自旧电压估算方式，本次没有迁移为新 a99054 的ppb数据。
历史查询不把旧ID自动别名化为新ID，也不自动换算旧数据。
新TVOC历史页只查询a99054，不把旧文件中的a24035值改名混入新曲线。
如要显示旧历史，应独立标注旧算法、旧单位与来源，不能按新协议宣称为同口径数据。

## 9. 标定与其他功能

本次只接日常浓度/状态，不实现 TVOC 零点、跨度、取消或结果查询界面。
TVOC 模式下不要自动进入原四气体维护会话，也不要自动发送校准心跳/zero/span。
MCU会对TVOC模式的 gas_calibration 返回 tvoc_calibration_not_supported，
旧 gal_data 对TVOC也拒绝。LCD应隐藏或禁用相应TVOC控制入口。

原空气微站、四气体标定、登录、加热、LED及其他设置按原协议运行，
不要将TVOC的标志位、换算系数或采样逻辑用于其他因子。

## 10. OTA 与重启恢复

原 ota_status / ota_status_ack 协议保持不变：

- preparing 时停止普通查询、历史补查与配置，按原会话回复 preparing ACK。
- transferring、verifying、restarting 等状态保持原升级页面。
- normal 按原规则回复 ACK并恢复；重复normal补ACK，但不反复重启恢复定时器。
- 原约定的恢复等待完成后，重新读取选择状态与因子配置，再恢复数据查询。
- 清理旧批次/时间/单位基准，等待新批次；不在MCU尚未恢复时高频补查。

完整流程见[原OTA交互协议](LCD_OTA_INTERACTION_PROTOCOL.md)。

## 11. 失败回复和兼容

典型配置失败：

```json
{"operation":"set_config","code":"NG","config":"sensors","reason":"tvoc_exclusive"}
```

| reason / 情况 | LCD 处理 |
| --- | --- |
| tvoc_exclusive | 提示 TVOC 必须单独启用 |
| duplicate_sensor_id | 去除重复ID；旧a24035与新a99054同时出现也算同一因子 |
| sensor_not_enabled | 重新查询选择状态，先启用再改参数 |
| invalid_unit | 按四种ASCII单位检查 |
| invalid_alarm_limit | 检查非负整数和范围，不能提交小数 |
| state_save_failed / config_save_failed | 保留页面和用户输入，不重启，不假定部分成功 |
| get_data返回NG或超时 | 沿用有限重试及通信提示，不能用旧值冒充新批次 |
| ds或tvoc对象缺失 | 进入原有兼容处理/状态未提供，不伪造有效位或采样数量 |
| 未知扩展字段 | 忽略未知字段，保留已识别字段，不能导致整条有效JSON被丢弃 |

新LCD统一发送a99054。MCU的a24035输入兼容只用于配置/实时请求过渡，
回复params及配置输出均采用a99054；LCD不能要求回包ID必须原样回显旧别名。
历史不采用该别名迁移。

## 12. 联调验收清单

| 用例 | 预期结果 |
| --- | --- |
| 初次选择TVOC | enabled只有a99054，其他开关互斥，按restart_required完成重启 |
| 配置回读 | 正确显示profile、单位和报警值；四段认证版本字符串可正常识别 |
| 等待首批 | seq=0时显示等待，缺少tvoc对象不报通信故障 |
| 正常均值 | 主页面和趋势采用MCU批次均值，120秒约更新一批 |
| 重复查询 | 同一seq只更新状态/年龄，不重复入图、不重复平均 |
| 新批次同值 | seq变化即识别新批次，即使均值和上一批相同 |
| 有效零值 | valid_mask=1且value=0，正常显示0 |
| 少量有效点 | valid=1仍显示均值及1/12，不自行加有效数量门槛 |
| 全部无效 | valid_mask=0显示--/异常，不将占位0画成正常值 |
| 超量程 | T状态保留数值，无500ppb截断 |
| 最后槽失败 | 不因flags=0单独丢弃MCU已经判定有效的本批均值 |
| 无有效时间 | timestamp=0时有效浓度仍显示，正式历史日期基准暂停 |
| 单位切换 | 重启回读确认后更新标签、精度、阈值和缓存，不二次换算 |
| 历史查询 | 单点串行；处理缺测/超时，不把旧a24035数据当新数据 |
| 空气微站回归 | 原显示、配置、状态及历史功能保持可用 |
| OTA/重启 | 维持原ACK/恢复流程，不残留旧单位、旧因子或旧批次基准 |

建议保留LCD版本、MCU版本、收发时间、原始JSON、显示单位、seq和有效数量，
对照同一批MCU的TVOC_BATCH日志确认。本文不把候选通气窗口视为已经实气验证通过。

## 13. 配套文档

- [TVOC MCU日常读取与采样口径](../TVOC_DAILY_READ_PROTOCOL.md)
- [因子选择与参数设置协议](LCD_SENSOR_CONFIG_PROTOCOL.md)
- [get_data设备状态协议](LCD_GET_DATA_STATUS_PROTOCOL.md)
- [OTA交互协议](LCD_OTA_INTERACTION_PROTOCOL.md)
- [标准版2.2.0变更记录](../../changelog/CHANGELOG_2.2.0.md)
- [认证版2.2.0.1变更记录](../../changelog/CHANGELOG_2.2.0.1.md)
