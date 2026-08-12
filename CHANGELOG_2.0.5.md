# Firmware 2.0.5

日期：2026-08-11

状态：已在当前唯一源码目录完成整合并通过 ESP32 Core 3.3.7 全量编译；尚未烧录、
启动或进行硬件运行验证。默认可烧录发布包仍为2026-08-10已验证的Firmware 2.0.4。

## 整合基线

本版本以2026-08-10已烧录并完成首轮验证的Firmware 2.0.4为底座，不使用
`D:\V5.0\TSP`覆盖当前源码。以下2.0.4修复完整保留：

- `vTaskDelayUntil()`采集周期漂移修复；
- RTC和DTU时间保留秒，内部使用14位`YYYYMMDDhhmmss`；
- RTC优先启动，首次有效CSQ后再尝试网络校时；
- 使用`millis()`调度每24小时网络校时，失败5分钟后重试；
- 网络校时避让实时和pending HJ212上传，并避开分钟边界；
- 时间偏差不超过3秒时只验证，不重设系统和RTC。

## FreeRTOS任务优先级

任务优先级集中定义为`TASK_PRIORITY_*`具名常量：

| 优先级 | 任务 | 核心 |
|---|---|---|
| 11 | 实际OTA写入`otaUpload` | 1 |
| 10 | `DataProcessTask` | 0 |
| 10 | `Controllcd`、`Controldtu` | 1 |
| 9 | `CollectTask` | 1 |
| 9 | `SaveFileTask` | 0 |
| 8 | HJ212发送任务 | 0 |
| 7 | 报警、温湿度控制 | 1 |
| 6 | CSQ、pending和校时维护 | 1 |
| 4 | 权限、配置、校准、OTA监听 | 0/1 |
| 3 | LED、MQTT | 0/1 |

LCD和Remote DTU解析任务继续保持当前已验证的优先级10，没有照搬早期V5实验目录
中的4/5，待后续专门完成屏幕和Remote DTU压力测试后再决定是否降低。

## 日志和串口

- USB调试串口由9600调整为115200，业务串口配置不变；
- `LogManager`增加FreeRTOS互斥锁，防止双核日志相互穿插；
- 已格式化文本使用`Serial.print()`，不再作为`printf`格式串；
- RTC状态和OTA进度统一进入日志接口；
- Remote DTU硬件Serial2 RX缓冲区调整为2048字节；
- `UPLOAD_RES`响应引用由回调正确释放。

## 远程OTA模块

- 新增`src/system/ota/remote_ota_manager.h/.cpp`，旧OTA代码已从`system.cpp`
  完整删除；
- OTA监听任务常驻，无效请求、初始化失败、断流或校验失败后仍可再次请求；
- 镜像大小不得超过下一OTA分区；只有`esp_ota_end()`验证成功后才切换启动分区；
- 失败使用`esp_ota_abort()`，释放Remote DTU互斥锁并恢复业务，不再一律重启；
- 无数据超时90秒，总会话上限20分钟；
- 收到数据后等待20ms空闲间隔再写Flash，承接800字节突发数据；
- 不再使用`vTaskDelete()`或`vTaskSuspend()`强制停止业务任务；
- 采集、处理、SD、HJ212、屏幕、串口和维护等操作进入活动计数保护区。OTA
  先阻止新业务进入，再等待全部在途操作计数归零，确认后才发送`Ready`。

## 当前OTA限制

当前线上格式仍为800字节原始二进制、分包间隔1000ms，尚未实现分包序号、分包
CRC、应用层ACK和丢包重传。当前实现可以在最终镜像验证失败时保留旧固件，但不能
自动补回网络中丢失的分包，因此尚不能作为可靠OTA正式上线。

固件来源认证、Secure Boot状态、升级后健康确认和自动回滚也尚未完成实机验证。

## 固定约束核对

- 小时统计算法未修改；`dataManager.cpp`只增加OTA活动边界；
- HJ212逐包间隔保持3000ms；
- SHT30读取代码未修改，继续独立处理；
- `DEBUG`保持开启；
- HJ212报文、ACK、pending格式和补传规则未修改。

## 2026-08-11完整编译

使用统一入口`firmware_workspace\build-current.cmd`和生产FQBN完成三次干净构建。
最终构建结果：

- ESP32 Core：3.3.7；
- 分区：16MB Flash，3M APP / 9M FATFS；
- 应用BIN：624016字节；
- 应用BIN SHA-256：
  `00BA20C9909A84FC6252FA3EE2D44B7219F3DCB3BB142D4E5AC2B844BFD24E44`；
- 源码输入指纹：
  `7959BBB402B544ABCF7BFFBB1CBC65D851E0BBA2EC7BFF60450CC4D08B58BA9C`；
- 构建状态：`compiled-not-hardware-verified`；
- 构建目录：
  `D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\current`。

本次没有执行烧录。该BIN不得标记为硬件验证版，也不得替换默认2.0.4发布包，直到
用户明确授权烧录并完成优先级、串口、采集、SD、HJ212和OTA专项测试。
