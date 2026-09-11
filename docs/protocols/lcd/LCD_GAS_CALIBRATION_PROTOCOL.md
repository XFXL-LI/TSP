# TSP MCU 与 LCD 四气体两点校准交互协议

文档版本：1.0  
日期：2026-08-25  
适用版本：Firmware 2.0.15/2.0.16通用版；2.0.17差异见下方说明  
当前状态：MCU 侧已实现并完成全量编译和镜像校验；LCD 侧及实机联调待完成

> Firmware 2.0.17为气体认证特定版：O3、NO2、SO2量程点目标改为250 ppb，
> 并使用独立FFat`/gasCalibrationCertified.json`；CO仍为5000 ppb。2.0.17不读取
> 或修改通用版`/gasCalibration.json`，因此降级回2.0.16不会继承认证版目标。
> JSON交互动作和状态字段不变，LCD无需因配置文件隔离而修改协议。

## 1. 目标与边界

本协议用于 O3、NO2、CO、SO2 四个 K-7S 气体模组的独立两点校准：

1. 零点使用命令参数 `0x1000`。
2. 量程点使用命令参数 `0x1001`。
3. 写入标定命令后读取 `0x6006`，以附表 5 状态判断本次标定结果。
4. 四个气体分别使用独立 LCD 页面，页面结构可以共用同一模板。
5. LCD 只显示当前浓度和操作状态，不显示原始寄存器值、量程、小数位或 MCU 本地配置中的量程点目标值。
6. 量程点目标浓度保存在 MCU 本地管理员配置中，LCD 不查询、不显示、不修改，也不随校准请求下发目标浓度。
7. 校准期间不修改小时统计逻辑、HJ212 逐包 3000 ms、SHT30、DEBUG、OTA 和 pending 逻辑。

## 2. 当前确认的气体映射和 MCU 本地目标

以下数值以当前现场实际模组为准，不使用 K-7S 通用手册中的型号量程作为运行判断：

| 气体 | 因子 ID | Modbus 从站 | 现场实际量程 | MCU 本地量程点目标 |
| --- | --- | ---: | --- | ---: |
| O3 | `w34011` | 3 | 0～1 ppm | 500 ppb |
| NO2 | `a21004` | 4 | 0～1 ppm | 500 ppb |
| CO | `a21005` | 5 | 0～10 ppm | 5000 ppb |
| SO2 | `a21026` | 6 | 0～1 ppm | 500 ppb |

说明：

- 表中的目标值不出现在 LCD 页面和新接口响应中。
- 如果以后更换不同浓度的标准气，必须先由管理员修改 MCU 本地配置并保留配置变更记录；不能把 LCD 当前读数当作新的标定目标。
- 当前方案固定按 ppb 整数编码量程点参数。例如 500 ppb 写入 `0x01F4`，5000 ppb 写入 `0x1388`。
- MCU 配置文件为 FFAT `/gasCalibration.json`。2.0.15 首次启动缺失时原子创建默认文件，已有文件格式错误或目标越界时禁用校准；本版没有开放给校准页面的目标读写接口。

## 3. LCD 页面要求

## 3.1 四个独立页面

LCD 分别提供：

- O3 校准页；
- NO2 校准页；
- CO 校准页；
- SO2 校准页。

同一时间只允许存在一个 MCU 校准会话，不能同时校准两个气体。

## 3.2 页面显示内容

页面主要测量区域只显示：

```text
当前浓度：487 ppb
```

页面还需要显示操作状态，例如：

```text
等待通入零气
零点标定命令执行中
零点标定成功
等待通入标准气
量程点标定命令执行中
量程点标定成功
正在清洗，请确认浓度恢复后退出
校准失败：参数无效
```

页面不得显示：

- `0x6001` 的“原始寄存器值”标签；
- `0x202B` 量程；
- `0x2030` 单位代码；
- `0x2031` 小数位；
- MCU 本地配置中的量程点目标值；
- 预计修正倍率。

当前浓度无有效新数据时显示 `-- ppb`，不得把通信失败显示为 `0 ppb`。

## 3.3 页面按钮

建议按钮：

```text
[开始校准]
[零点校准]
[量程校准]
[取消校准]
[完成并退出]
```

按钮启用规则：

1. 未开始会话时只允许“开始校准”。
2. 会话开始后允许“零点校准”。
3. 零点返回成功后才允许“量程校准”。
4. 量程点返回成功后进入清洗阶段，允许“完成并退出”。
5. 执行零点或量程点命令期间禁用重复操作按钮。
6. 中途取消不直接恢复正常气体数据，先进入清洗提示，再由操作人员确认退出。

## 4. 串口公共格式

| 项目 | 规定 |
| --- | --- |
| 接口 | MCU LCD 专用串口 |
| 波特率 | 9600 |
| 数据格式 | 8N1 |
| 报文 | 单行紧凑 JSON |
| 结尾 | 沿用现有 LCD JSON 结束格式 |
| 权限 | 仅管理员；普通用户的校准请求由 MCU 拒绝 |

新操作名统一为：

```text
gas_calibration
```

## 5. 会话状态

`state` 使用以下值：

| 状态 | 含义 |
| --- | --- |
| `zero_wait` | 会话已建立，等待零气稳定和零点操作 |
| `zero_executing` | 正在执行零点命令并读取 `0x6006` |
| `span_wait` | 零点成功，等待标准气稳定和量程点操作 |
| `span_executing` | 正在执行量程点命令并读取 `0x6006` |
| `purging` | 量程点成功，等待清洗和退出 |
| `cancel_purging` | 中途取消，等待清洗和退出 |
| `completed` | 已正常退出校准模式 |
| `timed_out` | 会话超时，高频读取和写命令停止 |
| `failed` | 当前标定点执行失败，可按规则重试或取消 |

## 6. 接口定义

## 6.1 开始或恢复校准会话

请求：

```json
{"operation":"gas_calibration","action":"start","id":"a21026"}
```

新会话成功响应（字段以实际完整响应为准）：

```json
{"operation":"gas_calibration","code":"OK","action":"start","session":12345,"id":"a21026","state":"zero_wait","current_ppb":null,"fresh":false,"age_ms":0,"stable_hint":false,"sample_count":0,"point":"none","point_result":"none","module_status":null,"bus_waiting":false}
```

同一气体已有活动会话时，MCU 返回原会话，用于 LCD 断开后的页面恢复：

```json
{"operation":"gas_calibration","code":"OK","action":"resume","session":12345,"id":"a21026","state":"span_wait","current_ppb":493,"fresh":true,"age_ms":420,"stable_hint":true,"sample_count":10,"point":"zero","point_result":"success","module_status":1,"bus_waiting":false}
```

其他气体已有会话时：

```json
{"operation":"gas_calibration","code":"NG","action":"start","reason":"session_busy"}
```

## 6.2 查询当前浓度和会话状态

LCD 建议每 2 秒查询一次：

```json
{"operation":"gas_calibration","action":"status","session":12345}
```

响应：

```json
{"operation":"gas_calibration","code":"OK","action":"status","session":12345,"id":"a21026","state":"span_wait","current_ppb":487,"fresh":true,"age_ms":420,"stable_hint":true,"sample_count":10,"point":"zero","point_result":"success","module_status":1,"bus_waiting":false}
```

说明：

- `current_ppb` 是 LCD 唯一需要显示的浓度。
- `fresh=false` 或 `current_ppb=null` 时显示 `-- ppb`。
- `age_ms` 供 LCD 判断缓存新鲜度，可不直接显示。
- `stable_hint` 是 MCU 根据最近 20 秒的 10 个连续有效样本生成的提示；当前实现以窗口最大值与最小值之差不超过内部目标值的 5% 判定为稳定。它只用于提示，不代替人工确认，也不是执行量程点的必要条件。
- `sample_count` 是当前稳定窗口中的有效样本数，完整 20 秒窗口为 10 个样本。
- `module_status` 是最近一次 `0x6006` 状态值；未执行标定点时为 `null`。
- `bus_waiting=true` 表示正常 60 秒采集正在优先使用 485，LCD 保持最近一次有效浓度并显示“等待总线”，不得当作校准失败。
- 响应中不返回量程点目标值。
- `status` 同时作为 LCD 会话心跳。

## 6.3 执行零点校准

请求：

```json
{"operation":"gas_calibration","action":"zero","session":12345}
```

立即接受响应：

```json
{"operation":"gas_calibration","code":"OK","action":"zero","session":12345,"state":"zero_executing"}
```

MCU 后台执行：

```text
解除写保护
→ 向0x6006写入0x1000和0x0000
→ 每1秒读取一次0x6006
→ 得到终态后更新会话缓存
```

LCD 继续使用 `status` 查询最终结果，不在本次请求中同步等待数十秒。

注意：上面的 `code=OK` 只表示 MCU 已接受操作并进入异步执行态，不代表模组已经标定成功。解保护、写命令或后续状态读取失败时，`status` 会返回 `state=failed`，具体原因见 `point_result`；只有 `module_status=1` 且 `point_result=success` 才是本标定点成功。

## 6.4 执行量程点校准

请求：

```json
{"operation":"gas_calibration","action":"span","session":12345}
```

请求中不得携带当前浓度、目标浓度、DATA 或 RATIO。

立即接受响应：

```json
{"operation":"gas_calibration","code":"OK","action":"span","session":12345,"state":"span_executing"}
```

MCU 根据会话气体 ID 自动取得内部预设值：

```text
O3/NO2/SO2 → 500
CO          → 5000
```

MCU 后台执行：

```text
解除写保护
→ 向0x6006写入0x1001和MCU本地配置目标值
→ 每1秒读取一次0x6006
→ 得到终态后更新会话缓存
```

## 6.5 取消校准

请求：

```json
{"operation":"gas_calibration","action":"cancel","session":12345}
```

响应：

```json
{"operation":"gas_calibration","code":"OK","action":"cancel","session":12345,"state":"cancel_purging"}
```

取消后：

- 不再允许发送新的零点或量程点命令；
- 继续显示当前浓度；
- 四气体仍保持维护数据隔离；
- LCD 提示停止标气并通入洁净空气；
- 操作人员确认清洗完成后发送 `finish`。

## 6.6 清洗完成并退出

请求：

```json
{"operation":"gas_calibration","action":"finish","session":12345}
```

响应：

```json
{"operation":"gas_calibration","code":"OK","action":"finish","session":12345,"state":"completed"}
```

MCU 在成功响应前：

1. 停止目标气体高频读取；
2. 清除活动会话；
3. 解除四气体维护隔离；
4. 从下一轮正常 60 秒采集开始恢复四气体统计、存储和 HJ212。

## 7. `0x6006` 结果映射

| `module_status` | `point_result` | LCD 文案 |
| ---: | --- | --- |
| `0x0000` | `pending` | 标定命令执行中 |
| `0x0001` | `success` | 本标定点成功 |
| `0x0002` | `module_failed` | 模组执行失败 |
| `0x0003` | `unsupported` | 模组不支持该命令 |
| `0x0004` | `invalid_parameter` | 标定参数无效 |
| `0x0005` | `module_timeout` | 模组执行超时 |
| `0x0100`～`0x0103` | `pending` | 标定命令执行中 |

`0x0001` 是该标定点成功。只有零点和量程点都成功，才能显示“两点校准完成”。

## 8. 读取周期和会话时长

## 8.1 浓度读取

在以下状态，每 2 秒读取一次目标气体 `0x6001`：

- `zero_wait`；
- `span_wait`；
- `purging`；
- `cancel_purging`。

在 `zero_executing` 和 `span_executing` 状态暂停浓度读取，改为每 1 秒读取一次 `0x6006`。

## 8.2 心跳和超时

- LCD 连续 10 秒没有发送 `status`：MCU 暂停高频 485 浓度读取，但保留会话和维护隔离。
- LCD 恢复同一会话查询后：恢复对应阶段读取。
- 单个气体会话建议最长 30 分钟。
- 会话超时后停止高频读取和新标定命令，不自动恢复四气体业务数据；LCD 重新连接后应引导操作人员清洗并执行 `finish`。
- MCU 重启后活动会话不保留，必须重新开始校准；操作人员应先确认气路已经恢复洁净空气。

## 9. 485 总线调度

## 9.1 基本原则

校准任务不得从进入页面到退出页面持续持有 `SERIAL_485` 互斥锁。每次只在一笔完整 Modbus 事务中持锁：

```text
申请锁 → 发送请求 → 接收并校验响应 → 释放锁
```

通气等待、稳定等待、LCD 心跳等待和两次状态查询之间的 1～2 秒间隔均必须在释放锁后进行。

## 9.2 正常 60 秒采集优先

MCU 增加 `normal_485_pending` 或等价调度状态：

```text
正常60秒轮询准备开始
→ 标记 normal_485_pending
→ 校准任务不再发起新的485事务
→ 已经开始的校准事务完成并释放锁
→ 正常风速、风向、气象等485采集完成
→ 清除 normal_485_pending
→ 校准读取恢复
```

如果 LCD 点击校准时正常采集正在使用 485，MCU 应保持动作待执行并返回“等待总线”，不能立即判定标定失败。

优先级：

```text
正常60秒485采集
  > 已经进入解保护+写标定的不可分割短事务
  > 0x6006状态查询
  > 校准页面0x6001浓度刷新
```

已经进入“解保护+写标定”的短临界区必须完整结束，之后才交给正常采集。

## 10. 校准期间的数据隔离

任一气体会话开始后，直到 `finish`：

- 四个气体的常规 60 秒读取暂停；
- 校准任务只为 LCD 单独读取目标气体；
- 四个气体不进入正常 raw/min/hour/day 业务统计；
- 四个气体不进入 HJ212 实时、十分钟、小时和日数据；
- 颗粒物、风速、风向、气象、噪声、SHT30 正常运行；
- 不修改现有小时统计算法，只在采集入口隔离维护数据。

这样可以避免标准气以及 O3/NO2 交叉响应污染正常业务数据。

## 11. 错误响应

通用格式：

```json
{"operation":"gas_calibration","code":"NG","action":"span","reason":"invalid_state"}
```

错误码：

| `reason` | 含义 |
| --- | --- |
| `invalid_sensor_id` | 不是四个支持的气体 ID |
| `permission_denied` | 未登录或权限不足 |
| `session_busy` | 已有其他气体活动会话 |
| `invalid_session` | 会话不存在或编号错误 |
| `invalid_state` | 当前阶段不允许该操作 |
| `calibration_config_invalid` | MCU 本地校准配置缺失、格式错误或目标越界 |
| `concentration_unavailable` | 目标气体浓度通信失败 |
| `insufficient_samples` | 最近 20 秒有效样本不足 10 个，不能执行量程点 |
| `concentration_not_ready` | 最近 20 秒平均浓度与 MCU 目标偏差超过 ±20% |
| `request_parameter_not_allowed` | `span` 请求携带了目标浓度、当前浓度、`DATA` 或 `RATIO`，MCU 拒绝覆盖内部目标 |
| `unlock_failed` | 解保护命令无有效应答 |
| `calibration_write_failed` | 标定写命令无有效应答 |
| `status_read_failed` | `0x6006` 连续读取失败 |
| `module_failed` | 模组返回 `0x0002` |
| `unsupported` | 模组返回 `0x0003` |
| `invalid_parameter` | 模组返回 `0x0004` |
| `module_timeout` | 模组返回 `0x0005` |
| `session_timeout` | 单气体会话超过允许时长 |

LCD 收到任何 `code=NG` 时不得重启 MCU，不得本地伪造成功状态。

`unlock_failed`、`calibration_write_failed`、`status_read_failed` 和模组终态属于异步结果，实际通过 `status.point_result` 返回，不会回填到已经完成的 `zero/span` 立即应答中。

## 12. LCD 状态机流程

```text
选择气体
→ 开始校准会话
→ 通入零气并观察当前浓度
→ 点击零点校准
→ 等待0x6006返回成功
→ 通入同种标准气并观察当前浓度
→ 当前浓度接近现场已知标气浓度后点击量程校准
→ MCU使用内部预设目标写0x1001
→ 等待0x6006返回成功
→ 切换洁净空气清洗
→ 观察当前浓度恢复
→ 点击完成并退出
```

参考时长：

- 单个气体通常约 10～20 分钟；
- 现场保守按每个气体 20～30 分钟准备；
- 四气体顺利时约 40～80 分钟，考虑换气与清洗建议预留 1～2 小时；
- 设备预热时间不计入上述校准时长。

## 13. LCD 验收清单

1. 四个气体分别进入独立页面，不能同时建立两个会话。
2. 页面只显示当前浓度 `ppb` 和操作状态，不显示 MCU 目标值。
3. 浓度无效时显示 `--`，不显示 0。
4. 零点成功前不能点击量程点。
5. 标定操作为异步，不阻塞 LCD 串口接收和页面刷新。
6. `0x6006=0x0001` 才显示本标定点成功。
7. 量程点请求不携带目标浓度或当前显示值。
8. LCD 临时断开后能够恢复同一气体会话。
9. 量程点成功后必须经过清洗提示和显式 `finish`。
10. 错误状态不重启、不伪造成功、不自动跳过清洗。
11. 正常 60 秒采集期间允许当前浓度短暂停止刷新，LCD 保持最后一次有效值。
12. 校准结束后重新进入正常页面，四气体恢复正常数据。

## 14. 权限与重启规则

### 14.1 管理员权限

- 新 `gas_calibration` 接口属于管理员操作，`start/status/zero/span/cancel/finish` 均要求当前会话具有管理员权限。
- 普通用户请求由 MCU 拒绝，不能仅依赖 LCD 页面隐藏按钮实现权限控制。
- LCD 断线或重新登录后，应重新校验管理员会话，不能沿用失效的本地页面状态。

### 14.2 校准完成后不重启 MCU

- 零点或量程点标定成功后，不需要重启 MCU，也不建议主动重启。
- 完成清洗后由 LCD 发送 `finish`，MCU 清除校准会话和维护数据标记；从下一轮正常 60 秒采集周期恢复常规采集、统计、存储和上传。
- 校准和清洗过程中禁止把“重启 MCU”作为正常流程步骤，否则 RAM 中的校准会话、计时和稳定性样本会丢失。
- 若校准过程中意外掉电或重启，MCU 开机后不得自动续做未完成步骤；现场应停止标气、完成清洗，并重新开始该气体的校准流程。

## 15. 已确认实施决策

1. MCU 启用量程点目标偏差 ±20% 的防误操作限制。判断使用最近 20 秒有效样本的平均值，不使用点击瞬间的单点值。
2. MCU 保存最近 20 秒浓度样本并向 LCD 提供稳定性提示；按 2 秒一次采样时，完整窗口为 10 个样本。稳定性提示不代替现场人工确认。
3. 旧 `gal_data` 的四气体校准调用禁用，返回 `legacy_gas_calibration_disabled`；颗粒物原有校准功能不受影响。
4. 四气体量程目标保存在 MCU 本地管理员配置中，不写死为编译常量；LCD 校准页面及校准状态接口不显示目标值。
5. 实施版本号确定为 `2.0.15`。
