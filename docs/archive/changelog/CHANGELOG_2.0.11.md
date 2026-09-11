# Firmware 2.0.11

> **禁止使用：** 用户烧录后设备启动即重启。LCD RX由1024提高到2048时，
> EspSoftwareSerial默认ISR缓冲也从约40KB扩大到约80KB，单端口额外占用约41984
> 字节动态堆并增加大块连续分配风险。该修改已在2.0.12撤销；不得烧录本版本BIN。

日期：2026-08-19

状态：`rejected-hardware-boot-loop`。虽然完整编译和镜像结构校验通过，但用户实机
烧录后启动即重启，已禁止使用。后继修正版为Firmware 2.0.12。

## 实机问题与判断

用户烧录 Firmware 2.0.10 后，LCD 下发包含 14 个因子的完整 sensors JSON。日志显示：

- LCD 接收层已经识别到完整花括号报文；
- 权限层第一次 cJSON 解析成功，输出 `json is valid`；
- 同一个请求转交 ConfigManager 后，第二次 cJSON 解析失败；
- 旧失败分支直接释放响应对象并返回，没有发布 `CONFIG_SET_RES`；
- 权限层等待 2 秒后只能向 LCD 返回 `Response timeout`；
- 因此 `/model.json` 没有更新，设备仍保留原有 10 因子配置。

日志正文缓冲只有约 256 字节，串口日志中显示到一半的 JSON 只是日志截断，不能据此
认定 LCD 原始报文本身被截断。源代码默认 14 因子内容生成的紧凑请求为 849 字节。

## 修改内容

- LCD EspSoftwareSerial RX 缓冲由 1024 字节提高到 2048 字节；其他软件串口仍保持
  1024 字节，不扩大无关内存占用；
- 权限层确认 operation 后，在同步转交异步业务路由前主动释放第一次 cJSON 解析树，
  避免 ConfigManager 二次解析时同时保留两棵完整 JSON 树；
- LCD、DTU及 ConfigManager 接收日志新增实际请求长度，避免仅看被截断的正文；
- ConfigManager 对 JSON 非法、config 名称不支持、content 缺失、序列化失败、保存失败
  和保存后读取失败均立即发布 `CONFIG_SET_RES`；
- `set_config` 响应根据结果返回 `code=OK/NG`，失败时附带明确 `message` 和
  `content:null`，不再统一伪装为 OK，也不再无响应等待超时；
- JSON 二次解析失败诊断新增请求长度、错误偏移、空闲堆和最大连续堆块；
- 成功响应中的 `config` 改为准确的 `sensors`，去除旧代码附带的前导空格。

## 保持不变

- 2.0.10 的 K-7S 采集、ppb 基准值、四种单位换算和默认单位保持不变；
- 校准逻辑未修改；CO 不钳制到 1ppm；
- 小时统计算法未修改；HJ212 完整报文逐包间隔仍为 3000ms；
- SHT30 独立处理；`DEBUG` 保持开启；LCD OTA 和 Remote DTU OTA 协议未修改；
- sensors 仍使用原有 `unit` 字段，没有新增 `displayUnit`。

## 编译与镜像验证

- ESP32 Core：3.3.7；
- 分区：16MB Flash，3M APP / 9M FATFS；
- Sketch：637996 字节，占程序空间 20%；
- 全局变量：26816 字节，占动态内存 8%，启动前静态余量 300864 字节；
- 应用 BIN：638144 字节；
- 应用 BIN SHA-256：
  `7C423DF2A4287185FDD2D2CD80D02431EE7E674EC91B3BAECF8926B05DE5C3C7`；
- 源码输入指纹：
  `10E4DE462513E7CFD02F15E8B1F52BF33FE911AB89B443A71B06D59FFCF4C24C`；
- esptool 5.1.0：识别为 ESP32-S3、16MB，checksum 和 validation hash 均有效；
- 构建状态：`compiled-not-hardware-verified`；
- 构建目录：
  `D:\ChatGPT-Pro\TSP-ESP32-S3\firmware_workspace\build\firmware-2.0.11-lcd-config`。

Codex 未执行 MCU 烧录。

## 首次实机验证

1. 启动日志确认 `Firmware version: 2.0.11`。
2. LCD 下发完整 14 因子 sensors JSON，确认日志长度约为实际报文长度，无
   `UART_OVERFLOW`，且 LCD 收到 `operation=set_config,code=OK,config=sensors`。
3. 立即执行 `get_config/sensors`，确认返回 14 个因子及四种气体的 `unit`。
4. 重启后确认配置加载日志显示 14 个传感器，并实际运行 14 个采集器。现有架构在
   启动时创建采集器，因此从 10 因子切换到 14 因子后仍需重启。
5. 若仍失败，保留 `[DIAG] Invalid set_config JSON` 的 `len/errorOffset/freeHeap/
   largestBlock` 和 `[DIAG] UART_OVERFLOW` 原始日志，不要只截取被缩短的 JSON 正文。
