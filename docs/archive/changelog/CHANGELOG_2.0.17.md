# Firmware 2.0.17 变更记录

## 版本定位

- 2.0.16继续作为正常、未做特殊量程限制传感器的通用正式版本，不回改。
- 2.0.17是气体认证项目专用版本，只在2.0.16基线上增加特定气体数据封顶和
  认证量程点目标。
- 颗粒物、小时统计、HJ212逐包3000 ms、SHT30、DEBUG、OTA及2.0.16其余行为
  均保持不变。

## 特定气体数据限制

- O3 `w34011`、NO2 `a21004`、SO2 `a21026`读取`0x6001`成功后，原始浓度大于
  500 ppb时，进入业务`DataPacket`的数值固定为500 ppb；0～500 ppb原值保留。
- 封顶发生在单位换算、LCD实时数据、SD、分钟/小时/日统计和HJ212之前，因此
  所有业务出口使用一致的封顶后基准值。
- 气体校准会话直接读取`0x6001`时也应用同一规则，`current_ppb`、稳定性样本和
  LCD校准页不会显示超过500 ppb的O3、NO2或SO2值。
- CO `a21005`完全不封顶，继续保留传感器实际0～10000 ppb数据。
- 调试日志同时记录`raw_ppb`和最终`value_ppb/output`，便于实机验证封顶前后数值。

## 认证量程点目标与配置隔离

2.0.17使用独立FFat文件：

`/gasCalibrationCertified.json`

首次缺失时原子创建：

```json
{"profile":"certified_250ppb_v1","span_targets_ppb":{"w34011":250,"a21004":250,"a21005":5000,"a21026":250},"max_deviation_percent":20,"stability_window_seconds":20,"min_stability_samples":10,"stability_tolerance_percent":5}
```

- O3、NO2、SO2量程点目标为250 ppb，±20%防误操作允许区间为200～300 ppb。
- CO目标继续为5000 ppb，允许区间为4000～6000 ppb。
- 文件必须携带`profile=certified_250ppb_v1`；配置损坏、profile不匹配或参数越界时
  禁用气体校准，不回退到通用文件。
- 2.0.17不读取、不迁移、不覆盖、不删除原`/gasCalibration.json`。
- 降级回2.0.16时，2.0.16仍只读取原`/gasCalibration.json`，不会继承认证版250 ppb
  目标；以后再次升级2.0.17时可继续使用独立认证配置。

## 版本位置

- `TSP.ino`：`VERSION2=2.0.17`。
- `src/inc/sys_init.h`：`VERSION2=2.0.17`。
- `src/app/configManager/system_json.h`：系统信息版本为`2.0.17`。

## 编译与镜像验证

- Arduino CLI完整编译：通过。
- ESP32 Core：3.3.7；目标：ESP32-S3；Flash：16 MB；分区：3 MB APP / 9 MB FFat。
- Sketch：664688字节（21%）。
- 全局变量：26976字节（8%），剩余300704字节。
- 应用BIN：664832字节。
- 应用BIN SHA-256：`C8F549BE80F666CBB25D0CA081B61E705FEDB7292C0586C4679632435358D9ED`。
- merged BIN：16777216字节。
- merged BIN SHA-256：`E758AC69D38846EA88C47EE76C2D3EDAF9B5F6090298A7E43F770B108A327C24`。
- 源码输入指纹：`85E9120377541CE865F49E2CCE8BF0F0C75E1946CEB79583680A2A8D613FDA04`。
- esptool识别为ESP32-S3、16 MB，checksum和validation hash有效。
- 构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.17-certified-gas-20260827`

当前状态：`compiled-not-hardware-verified`。本次未烧录MCU、未改动设备FFat。

## 2026-08-28 systemInfo运行版本响应修复（源码记录）

- 版本号保持Firmware 2.0.17，不提升版本号。
- LCD和远程端共用的`get_config/systemInfo`响应不再直接相信FFat历史
  `/system.json`中的`version`，而是在响应内存中使用当前编译常量`VERSION2`
  覆盖该字段。
- 产品名称、型号、生产日期和厂家等其他字段仍从`/system.json`读取。
- 不写回、不迁移、不删除`/system.json`，因此升级或降级后查询会自动反映实际
  运行固件版本，不产生FFat版本污染。
- INFO日志新增：
  `[DIAG] SYSTEM_INFO_VERSION result=ok version=2.0.17 source=runtime`；关闭DEBUG后
  LCD或远程端查询时仍可核对版本来源。
- 启动主动发送的`ota_status state=normal version=VERSION2`保持不变。

2026-08-28已按用户授权完成干净编译，版本号仍为2.0.17，未烧录：

- DEBUG已关闭，运行日志等级为`LOG_LEVEL_INFO`；构建清单已按源码实际状态记录。
- Sketch：665276字节（21%）。
- 全局变量：26976字节（8%），剩余300704字节。
- 应用BIN：665424字节。
- 应用BIN SHA-256：`BD23D44CC864E0A2260C9268C84BC7FDA2465F8D4473A36C04E4FC8FCCEE8393`。
- merged BIN：16777216字节。
- merged BIN SHA-256：`35E914C71F2E1B4E6935F06483A2C612625FB71071BDA075D2CFA2D5918121C5`。
- 源码输入指纹：`7C07D14414AD1D41162B80678E97FD286F31338600728C8A7F12E0CB31C2B84A`。
- esptool确认应用镜像为ESP32-S3、16 MB，checksum和validation hash有效。
- BIN字符串核对已确认包含`2.0.17`、`SYSTEM_INFO_VERSION ... source=runtime`、
  `/gasCalibrationCertified.json`和`certified_250ppb_v1`。
- 构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.17-certified-gas-systeminfo-20260828`

当前状态：`compiled-not-hardware-verified`。本次没有连接或烧录MCU，也没有操作设备
FFat。上文SHA-256为`C8F549...`的2026-08-27构建保留为历史产物：它开启DEBUG且
不包含本节systemInfo响应修复，不应再作为当前源码对应固件使用。

## 2026-08-31 三类传感器读取节奏优化（源码记录）

- 版本号保持Firmware 2.0.17，不提升版本号；本次DEBUG保持开启。
- 优化范围仅限SDS069颗粒物、XM8189风速风向和从机2气象百叶箱的正常分钟采集。
- 新增统一读取策略：响应超时500 ms、最多3次尝试、失败重试间隔300 ms、一次事务
  结束后继续保留300 ms总线间隔。
- 每个已启用因子正常成功时只发起一次查询；只有通信失败时才重试，最多3次。未启用
  因子仍由原有因子选择逻辑跳过。
- SDS069的PM1、PM2.5、PM10、TSP取消同一分钟内连读两次取平均。传感器数据约一分钟
  更新一次，重复读取不会形成两个独立样本；颗粒物`gal()`校准写入和回读流程未改。
- XM8189风速和风向取消外层2次采样及旧单寄存器函数内部3次采样，寄存器、从机地址、
  波特率和除以100的换算均未改。
- 从机2气象百叶箱的湿度、温度、噪声和气压均改为一次正常读取；寄存器500、501、502、
  505及除以10的换算保持不变。气压取消同一秒内5次采样中值，但原有30～120 kPa物理
  范围检查和跨采集周期突变确认继续保留。
- 湿度0%按说明书视为有效值；温度依据有符号补码解析，读取成功时`0xFFFF`可表示
  -0.1摄氏度，不再与通信失败混淆。
- 三类传感器全部启用且首次读取成功时，理论Modbus命令数由每分钟56条降为10条；若
  所有读取都连续失败，最多30条，不会无限重试。
- 未修改小时统计、HJ212逐包3000 ms、SHT30独立采集、四气体校准和认证版气体限制。

2026-08-31已完成干净编译，未烧录：

- Sketch：664044字节（21%）。
- 全局变量：26976字节（8%），剩余300704字节。
- 应用BIN：664192字节。
- 应用BIN SHA-256：`3769154A8ED5529957D2A237F2A9E6A7B2589A141325ABA5723EF6F764A666A7`。
- 源码输入指纹：`E4D20263969BAB0A32690B5DB71C77A55BAB6D82481DEEFBD56BD5A685ED0DD1`。
- 构建目录：

`D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.17-sensor-read-optimization-20260831`

当前状态：`compiled-not-hardware-verified`。需要通过带DEBUG实机日志确认从机2超时明显
下降、14因子采集/存储/HJ212连续正常，并完成24～48小时稳定性观察。

## 首次实机验证重点

1. 启动日志应显示Firmware 2.0.17以及
   `profile=certified_250ppb_v1 path=/gasCalibrationCertified.json`，目标顺序为
   `250,250,5000,250`。
2. 分别模拟O3、NO2、SO2为499、500、501和高于500 ppb，确认业务输出依次为
   499、500、500和500；CO高于500 ppb时必须保持原值。
3. 对照LCD、SD原始记录、分钟统计及HJ212，确认三项气体都使用同一个封顶结果。
4. 实测O3、NO2、SO2量程校准时，250 ppb标气应通过±20%检查，并确认`0x1001`
   参数为250；CO仍为5000。
5. 如需验证降级隔离，烧回2.0.16后启动日志应继续显示通用
   `/gasCalibration.json`中的目标，而不是认证版250 ppb。
