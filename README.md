# ESP32 验针机报警监控系统 2.0 — 用户手册

---

## 目录

1. [项目概述](#1-项目概述)
2. [硬件接线](#2-硬件接线)
3. [快速上手 SOP](#3-快速上手-sop)
4. [配网门户详解](#4-配网门户详解)
5. [MQTT 通信协议](#5-mqtt-通信协议)
6. [OTA 固件升级](#6-ota-固件升级)
7. [恢复出厂设置](#7-恢复出厂设置)
8. [多语言界面](#8-多语言界面)
9. [串口日志速查](#9-串口日志速查)
10. [常见故障排查](#10-常见故障排查)
11. [技术参数表](#11-技术参数表)

---

## 1. 项目概述

ESP32-WROOM-32 为核心，实时监控验针机（金属检测机）的报警信号。验针机检测到金属异物 → 蜂鸣器鸣响 → GPIO 采集电平变化 → 消抖处理 → MQTT 上报云端。

### 1.1 核心功能一览

| 功能 | 说明 | 默认值 |
|------|------|--------|
| 报警检测 | 下降沿触发（HIGH→LOW），20ms 软件消抖 | 引脚 GPIO34 |
| 实时上报 | 报警事件立即通过 MQTT 推送 | 毫秒级时间戳 |
| 永久历史 | 每次报警追加到 SD 卡，不再定时清理 | `/history.txt` |
| 断网续传 | WiFi 断开期间报警存 SD 卡，恢复后 FIFO 补传 | `/records.txt` |
| 掉电监控 | BQ24074 PGOOD# 检测市电掉电/恢复并通过 MQTT 上报 | GPIO35 |
| 心跳监控 | 每秒发送在线状态，报警期间自动暂停 | 可配 200-60000ms |
| 版本上报 | 心跳和报警均携带固件版本号 | `FIRMWARE_VERSION` 编译时设定 |
| 设备信息接口 | HTTP/设备 AP/BLE 读取 MAC、固件及 WiFi 状态 | 详见 `DEVICE_INFO_API.md` |
| NTP 对时 | 自动同步网络时间，断网期间系统时钟继续走 | 时区可配 |
| WiFi 配网 | 手机连设备热点 → 网页配置 | 热点名可自定义 |
| 系统配置 | 同一网页配置 MQTT/AP/NTP/心跳/时区 | 全部存入 NVS |
| OTA 升级 | MQTT 指令 / 网页粘贴 URL 两种方式 | MD5+大小校验 |
| 看门狗 | 10 秒任务看门狗 | 自动复位 |
| 多语言 | 中文/English/Tiếng Việt/Bahasa Indonesia | 网页切换 |

### 1.2 工作流程

```
ESP32 上电
  ├─ 加载 NVS 配置 (WiFi/MQTT/NTP/AP/语言)
  ├─ 挂载 SD 卡并检查历史/补传文件
  ├─ 读取 GPIO35 当前供电来源
  ├─ 启动热点 + 连接 WiFi
  ├─ NTP 时间同步
  ├─ MQTT 连接 + 订阅 OTA 指令主题
  └─ 进入主循环:
       ├─ 检测 GPIO34 报警信号
       ├─ 检测 GPIO35 市电/电池状态
       ├─ 检测 SD 卡热插拔
       ├─ 发布心跳 (每秒)
       ├─ 监听 MQTT OTA 指令
       ├─ 断网补传检查
       └─ 喂看门狗
```

---

## 2. 硬件接线

2.0 底板的 H1/H2 接线已经按嘉立创EDA原理图固化；卡座为 Micro SD：

| 功能 | ESP32 GPIO | 原理图连接 |
|---|---:|---|
| 报警输入 | 34 | H1-5，U1 PC817 集电极，低有效 |
| 市电正常检测 | 35 | H1-6，U2 BQ24074 `PGOOD#`，低=市电、 高=电池 |
| Micro SD 卡检测 | 32 | H1-7，Micro SD `CD`，低=已插卡 |
| Micro SD CS | 27 | H1-11，经 U6 电平缓冲连接 Micro SD `CD/DAT3` |
| Micro SD SCLK | 18 | H2-9，经 U6 连接 Micro SD `CLK` |
| Micro SD MISO | 19 | H2-8，经 U6 连接 Micro SD `DAT0` |
| Micro SD MOSI | 23 | H2-2，经 U6 连接 Micro SD `CMD` |

Micro SD 卡使用 FAT/FAT32。设备未插卡时仍可采集和在线 MQTT 上报，但无法保存永久历史或
离线补传记录；运行中插卡会自动挂载。

```
          ESP32-WROOM-32               验针机
       ┌──────────────────┐
       │                  │
       │  GPIO34 ◄────────┼──── 报警信号线 (输入)
       │                  │
       │  GND ◄───────────┼──── 验针机 GND (共地)
       │                  │
       │  3.3V ──┬────────┤
       │         │        │
       │       10KΩ (必需)│
       │         │        │
       │  GPIO34 ─────────┤  ← 上拉电阻接 3.3V
       │                  │
       └──────────────────┘
```

| 连接 | 说明 |
|------|------|
| GPIO34 ← 报警线 | 验针机蜂鸣器输出（未触发=高电平，触发=拉低到GND） |
| GND ← 验针机GND | 必须共地 |
| 3.3V ←10KΩ→ GPIO34 | **必需外接上拉电阻**，GPIO34 无内部上拉 |

> **不接上拉电阻的后果：** GPIO34 浮空，电平随机跳变，串口持续打印 DEBOUNCE 误报。

---

## 3. 快速上手 SOP

### 场景 A：全新设备首次使用

**步骤 1 — 硬件准备**
- [ ] 按上图接线，确认 10KΩ 上拉电阻已接
- [ ] USB 供电 ESP32

**步骤 2 — 配 WiFi**
- [ ] 手机打开 WiFi 设置，搜索热点 `YanZhenJi_` 开头
- [ ] 连接该热点（无密码）
- [ ] 手机浏览器打开 `http://192.168.4.1/`
- [ ] 页面顶部可看到设备 MAC 地址和固件版本
- [ ] 点击「开始扫描」→ 选择工厂 WiFi → 输入密码 → 连接
- [ ] 等待显示"已连接"+ IP 地址

**步骤 3 — 配 MQTT（如需修改）**
- [ ] 在首页点击「修改 MQTT 配置」
- [ ] 填入厂商自己的 Broker 地址、端口、用户名、密码
- [ ] 确认发布 Topic 和订阅 Topic 模板
- [ ] 点击「保存并重启」

**步骤 4 — 验证**
- [ ] 串口（115200 波特率）观察启动日志
- [ ] 看到 `MQTT Connected!` `Subscribe OK` `Initialization complete`
- [ ] 在 MQTTX 订阅 `self_device/yanzhenji/设备MAC`，应收到 `_online:true` 心跳
- [ ] 触发验针机报警（金属测试卡过机），MQTTX 应收到 `_error` 报警消息

### 场景 B：更换 WiFi

- [ ] 手机连接设备热点（`YanZhenJi_` 开头）
- [ ] 浏览器打开 `http://192.168.4.1/`
- [ ] 点击「断开 WiFi」
- [ ] 点击「开始扫描」→ 选新 WiFi → 输入密码 → 连接

### 场景 C：更换 MQTT Broker

- [ ] 手机连接设备热点
- [ ] `192.168.4.1` →「修改 MQTT 配置」
- [ ] 修改 Broker 参数 →「保存并重启」

### 场景 D：OTA 升级固件

- **短 URL（MQTT 方式）：** 向设备订阅 topic 发送 `{"cmd":"ota","url":"http://IP/firmware.bin"}`
- **长 URL（网页方式）：** 手机连热点 → `192.168.4.1` →「粘贴固件 URL 升级」→粘贴 URL → 点开始
- 下载过程约 1-2 分钟，串口显示百分比进度
- 成功后自动重启，新固件运行

---

## 4. 配网门户详解

### 4.1 访问方式

手机连接设备热点 → 浏览器打开 `http://192.168.4.1/`

> 提示：如果页面不自动弹出，手动输入地址。部分手机会弹出"登录 WiFi"提示，点击即可。

### 4.2 首页（`/`）

| 显示内容 | 说明 |
|----------|------|
| 语言选择器 | 顶部下拉：中文 / English / Tiếng Việt / Bahasa Indonesia |
| 设备 MAC 卡片 | 显示 WiFi MAC 地址（与 MQTT topic 中的 MAC 一致） |
| 固件版本 | 当前运行的固件版本号 |
| WiFi 状态 | 已连接（绿点+SSID+IP）/ 未连接（红点） |
| 断开 WiFi 按钮 | 仅已连接时出现，断开并清除保存的密码 |
| MQTT Broker 卡片 | 显示 Broker地址:端口 / Client / 用户 / 发布Topic / 订阅Topic |
| 修改 MQTT 配置 | 跳转到系统配置页 |
| OTA 升级卡片 | 跳转到粘贴 URL 升级页 |
| WiFi 扫描卡片 | 扫描附近 WiFi 并选择连接 |

### 4.3 系统配置页（`/mqtt`）

点击首页「修改 MQTT 配置」进入。所有参数保存后自动重启生效。

#### 📡 MQTT Broker 配置

| 参数 | 说明 | 默认值 | 示例 |
|------|------|--------|------|
| Broker 地址 | MQTT 服务器 IP 或域名 | `10.0.100.26` | `mqtt.example.com` |
| 端口 | MQTT 端口 | `1883` | `8883`(TLS) |
| Client ID | 设备 Client ID 前缀 | `ESP32_Alarm_Monitor` | `Factory3_Alarm` |
| 用户名 | MQTT 认证用户名 | `test01` | |
| 密码 | MQTT 认证密码 | `123456asd` | |
| 发布 Topic | 心跳和报警消息发往的主题模板 | `self_device/yanzhenji/{MAC}` | `factory/{MAC_NC}/status` |
| 订阅 Topic | 接收 OTA 指令的主题模板 | `self_device/yanzhenji/{MAC}` | `factory/{MAC_NC}/cmd` |

**Topic 模板变量：**

| 占位符 | 替换结果 | 示例 |
|--------|----------|------|
| `{MAC}` | WiFi MAC 地址（带冒号） | `20:50:0D:E5:55:84` |
| `{MAC_NC}` | MAC 地址（无冒号） | `20500DE55584` |

> **实际运行时的 Client ID：** `Client ID前缀_MAC无冒号`
> 例：`ESP32_Alarm_Monitor_20500DE55584` 保证多设备不冲突

#### 📱 设备热点配置

| 参数 | 说明 | 默认值 |
|------|------|--------|
| 热点名称 (SSID) | 设备 WiFi 热点名称 | 留空=自动 `YanZhenJi_设备MAC` |
| 热点密码 | 热点密码 | 留空=无密码（开放式） |

#### ❤ 设备心跳

| 参数 | 说明 | 范围 | 默认值 |
|------|------|------|--------|
| 心跳间隔 (ms) | 在线状态发送频率 | 200 - 60000 | 1000（1秒） |

#### 🕐 NTP 时间同步

| 参数 | 说明 | 默认值 |
|------|------|--------|
| NTP 服务器 1 | 主时间服务器 | `ntp.aliyun.com` |
| NTP 服务器 2 | 备用时间服务器 | `ntp.ntsc.ac.cn` |
| 时区 | POSIX 格式下拉选择 | `CST-8 (UTC+8)` |

> **纯内网环境：** NTP 服务器改为内网服务器 IP。
> **时区规则：** UTC 以东用减号。`CST-8` = UTC+8, `EST5` = UTC-5

#### ⚠ 恢复出厂设置

清除全部 NVS 配置（WiFi/MQTT/NTP/AP/语言），恢复到编译时的默认值。操作后自动重启。

---

## 5. MQTT 通信协议

### 5.1 默认连接参数

| 参数 | 值 |
|------|-----|
| Broker | `10.0.100.26:1883` |
| 认证 | `test01` / `123456asd` |
| Client ID | `ESP32_Alarm_Monitor_` + MAC（去冒号） |

### 5.2 Topic

默认发布和订阅同一 topic（可通过网页配置分开）：

```
self_device/yanzhenji/20:50:0D:E5:55:84
```

### 5.3 消息格式

#### 心跳（每秒，无报警时）

```json
{
  "20:50:0D:E5:55:84_online": true,
  "20:50:0D:E5:55:84_version": "v2.0.0"
}
```

#### 报警（下降沿触发）

```json
{
  "20:50:0D:E5:55:84_online": true,
  "20:50:0D:E5:55:84_version": "v2.0.0",
  "20:50:0D:E5:55:84_error": "2026-07-31 14:23:41.157"
}
```

所有启动、心跳和报警消息还会携带当前供电字段：

```json
{"mac_address":"20:50:0D:E5:55:84","power_source":"battery","power_lost":true}
```

#### 供电状态变化

外部电源掉电切换至电池，或外部电源恢复时立即发布：

```json
{
  "msg_type": "power_status",
  "mac_address": "20:50:0D:E5:55:84",
  "firmware_version": "v2.0.0",
  "power_source": "battery",
  "power_lost": true,
  "event_time": "2026-09-18 10:30:15.123"
}
```

`power_source` 为 `mains` 或 `battery`；`power_lost=true` 表示当前外部电源已经掉电。
MQTT 重连后设备会再次发送当前供电状态。

#### OTA 指令（旧格式，兼容）

```json
{"cmd":"ota","url":"http://10.0.101.52:8080/ESP32_Alarm_Monitor.ino.bin"}
```

#### OTA 指令（新格式，带校验）

```json
{"msg_type":"upgrade","version":"v0.0.8","file_url":"http://10.0.102.201:8080/f/abc123","file_md5":"b12232087b24426f613cb9a2cca8db03","file_size":1169584}
```

> **新格式注意事项：**
> - `msg_type` 支持 `upgrade` / `fw_upd` / `firmware_upgrade`
> - `file_md5` 非空时下载后自动校验，不匹配中止升级
> - `file_size` 非 0 时下载后自动校验大小
> - 消息总大小不能超过 MQTT Broker 限制（测试环境约 256 字节），建议压缩为单行无空格

### 5.4 监控端判断逻辑

```
收到消息中有 "_error" 字段 → 设备正在报警
收到消息中无 "_error" 字段 → 设备正常在线
超过 3 秒未收到任何消息   → 设备离线
```

---

## 6. OTA 固件升级

### 6.1 方式一：MQTT 指令（适合短 URL）

向设备订阅 topic 发送：

```json
{"cmd":"ota","url":"http://10.0.101.52:8080/firmware.bin"}
```

**限制：** URL 长度受 MQTT Broker 消息大小限制，长 URL 请用方式二。

### 6.2 方式二：网页粘贴（适合长 URL / S3 签名 URL）

1. 手机连接设备热点
2. `192.168.4.1` →「粘贴固件 URL 升级」
3. 粘贴完整固件下载 URL
4. 点击「开始升级」
5. 观察串口进度（百分比）
6. 下载完成 → MD5 校验 → 刷写 → 自动重启

### 6.3 方式三：后端新格式 OTA 指令

后端发：
```json
{"msg_type":"upgrade","file_url":"http://10.0.102.201:8080/f/abc","file_md5":"abc123...","file_size":1169584}
```

ESP32 自动下载、MD5 校验、大小校验、刷写、重启。

### 6.4 OTA 流程详解

```
收到 OTA 指令
  ├─ 排队到 loop() 中安全执行（避免 MQTT 回调栈溢出）
  ├─ HTTP GET 请求固件 URL (30 秒超时)
  ├─ 检查 Content-Length
  ├─ file_size 预校验（如提供）
  ├─ Update.begin() 分配 Flash 空间
  ├─ 分块下载 (每 4KB 一块，每块喂狗)
  │    ├─ Update.write() 写入 OTA 分区
  │    ├─ 同时计算 MD5
  │    └─ 每 10% 打印进度
  ├─ 下载完成后：
  │    ├─ file_size 最终校验
  │    ├─ MD5 对比 (如提供)
  │    └─ Update.end() + Update.isFinished()
  └─ ESP.restart() 重启
```

### 6.5 编译新固件

1. 修改 `config.cpp` 中 `FIRMWARE_VERSION` 为新版本号
2. Arduino IDE → 编译
3. 在 `build/esp32.esp32.esp32/` 找到 `ESP32_Alarm_Monitor.ino.bin`
4. 上传此 `.bin` 文件到 HTTP 服务器或后端平台
5. 通过 MQTT 或网页触发 OTA

---

## 7. 恢复出厂设置

### 方式：网页按钮

1. 手机连设备热点 → `192.168.4.1`
2. 进入「修改 MQTT 配置」
3. 滑到最底部 → 红色「恢复出厂设置」按钮
4. 确认 → ESP32 清除全部 NVS → 自动重启

**清除内容包括：** WiFi 密码、MQTT Broker 参数、Topic 配置、NTP 服务器、时区、热点名称/密码、心跳间隔、语言设置。

---

## 8. 多语言界面

配网页顶部有语言下拉菜单：

| 选项 | 语言 |
|------|------|
| 中文 | 简体中文（默认） |
| English | 英文 |
| Tiếng Việt | 越南文 |
| Bahasa Indonesia | 印尼文 |

切换后立即生效，选择保存在 NVS 中，断电不丢失。

---

## 9. 串口日志速查

波特率 **115200**。

| 日志内容 | 含义 | 级别 |
|----------|------|------|
| `ESP32 Alarm Monitor - Metal Detector` | 启动横幅 | INFO |
| `Device MAC: 84:55:E5:0D:50:20` | EFUSE MAC（仅参考） | INFO |
| `GPIO34 initialized, current state: HIGH` | 无报警 | INFO |
| `GPIO34 initialized, current state: LOW` | 报警中或上拉未接 | WARN |
| `[SD] Mounting... OK` | SD 卡挂载成功 | INFO |
| `[SD] No card inserted` | 未插 SD 卡，设备继续运行但不落盘 | WARN |
| `[POWER] External power lost; switched to battery` | 外部电源掉电，已切至电池 | WARN |
| `[POWER] External power restored; switched to mains` | 外部电源恢复 | INFO |
| `No NVS config found, using defaults` | 首次使用或已恢复出厂 | INFO |
| `System config loaded from NVS` | 从 NVS 读到了配置 | INFO |
| `AP 'YanZhenJi_XXXX' started` | 热点已启动 | INFO |
| `Connected! IP: 192.168.x.x` | WiFi 连接成功 | INFO |
| `Syncing via ... (TZ: CST-8)` | NTP 开始同步 | INFO |
| `Synchronized: 2026-07-31 14:23:41` | NTP 同步成功 | INFO |
| `Device MAC: 20:50:0D:E5:55:84` | WiFi MAC（用于 topic） | INFO |
| `Publish to: self_device/yanzhenji/...` | 发布 topic | INFO |
| `Subscribe: self_device/yanzhenji/...` | 订阅 topic | INFO |
| `Connecting to 10.0.100.26:1883 ... Connected!` | MQTT 连接成功 | INFO |
| `Subscribe OK` | 订阅确认 | INFO |
| `WDT enabled (10s timeout)` | 看门狗已启动 | INFO |
| `Initialization complete` | 进入主循环 | INFO |
| `Pin changed 1→0, starting 20ms timer` | 信号变化，开始消抖 | DEBUG |
| `Confirmed: 1 → 0` | 下降沿确认 | DEBUG |
| `Falling edge detected → ALARM TRIGGERED` | 报警触发 | INFO |
| `Event at: 2026-07-31 14:23:41.157` | 报警时间戳 | INFO |
| `Publishing alarm: {...}` | 发布报警消息 | INFO |
| `Publish OK` | 发布成功 | INFO |
| `Offline; SD history=saved, retransmit queue=saved` | 断网记录已写入 SD | WARN |
| `Found cached records, processing...` | 开始补传 | INFO |
| `Upload OK, removing from cache` | 补传成功 | INFO |
| `<< Received: {"cmd":"ota"...}` | 收到 MQTT OTA 指令 | INFO |
| `OTA command queued (will run in loop)` | OTA 已排队 | INFO |
| `Executing queued OTA from MQTT...` | 开始执行 OTA | INFO |
| `OTA: Starting OTA update...` | OTA 开始 | INFO |
| `OTA Progress: 50%` | OTA 下载进度 | INFO |
| `OTA SUCCESS! Firmware flashed.` | OTA 完成 | INFO |
| `Computed MD5: ...` | MD5 计算值 | INFO |
| `MD5 check: PASS` | MD5 校验通过 | INFO |
| `FACTORY RESET All NVS config cleared` | 恢复出厂 | INFO |
| `task_wdt: Task watchdog got triggered` | 看门狗复位 | ERROR |

---

## 10. 常见故障排查

| 现象 | 可能原因 | 排查步骤 |
|------|----------|----------|
| 串口不断打印 DEBOUNCE | 缺少上拉电阻 | 接 10KΩ 电阻到 3.3V |
| 串口显示 `state: LOW` | 报警信号线悬空 | 检查接线，确认上拉 |
| NTP 同步失败 | WiFi 未连接 / NTP 不可达 | 先确认 WiFi 连上；内网更换 NTP 服务器 |
| MQTT 连接失败 `rc=4` | 认证错误 | 检查用户名密码 |
| MQTT 连接失败 `rc=-2` | Broker 不可达 | 检查 IP 端口 |
| 网页 `192.168.4.1` 打不开 | 手机未连热点 | 确认 WiFi 连的是设备热点 |
| MQTT 长 URL 收不到 | Broker 消息大小限制 | 用压缩格式或网页粘贴替代 |
| 断网后数据没补传 | SD 卡未插入/挂载失败 | 检查 `[SD]` 日志和 FAT/FAT32 格式 |
| SD 卡无法挂载 | 卡未格式化或接触不良 | 重新插卡并格式化为 FAT/FAT32 |
| 一直显示电池供电 | GPIO35/PGOOD# 异常 | 测量 GPIO35：市电正常应为低电平 |
| 报警时间显示 1970 | NTP 从未同步 | 确保开机时 NTP 可达 |
| OTA 下载失败 `code=404` | URL 文件不存在 | 检查服务器文件路径 |
| OTA MD5 不匹配 | 固件传输损坏 | 重新上传固件或检查 URL |
| 热点名改了不生效 | NVS 配置加载顺序 | 已修复，保存后重启生效 |
| 时区改了不生效 | 同样的问题 | 已修复，`load_sys_config()` 在 NTP 同步前执行 |
| 语言切换不保留 | NVS 未写入 | 已修复，切换时写入 NVS |

---

## 11. 技术参数表

| 参数 | 值 |
|------|-----|
| **MCU** | ESP32-WROOM-32 |
| **框架** | Arduino |
| **报警输入** | GPIO34（低有效） |
| **供电状态输入** | GPIO35，BQ24074 PGOOD#（低=市电，高=电池） |
| **SD SPI** | SCLK=18、MISO=19、MOSI=23、CS=27、CD=32 |
| **消抖时间** | 20ms（编译时常量） |
| **触发方式** | 单次下降沿 HIGH→LOW |
| **心跳间隔** | 200-60000ms 可配（默认 1000ms） |
| **MQTT 库** | PubSubClient |
| **MQTT Buffer** | 4096 字节 |
| **报警历史** | SD `/history.txt`，只追加、不定时删除，格式 `millis|timestamp|count` |
| **补传队列** | SD `/records.txt`，MQTT 补传成功后 FIFO 删除 |
| **配置存储** | NVS（WiFi密码 + MQTT + NTP + AP + 语言 + 心跳） |
| **看门狗** | 任务看门狗 10 秒 |
| **NTP 服务器** | 两个服务器可选（默认 aliyun + ntsc） |
| **NTP 时区** | 23 个时区下拉可选 |
| **OTA 方式** | HTTP GET + Update 刷写 + MD5/SIZE 校验 |
| **OTA Buffer** | 4KB 分块，每块喂狗 |
| **配网方式** | AP+STA 双模，内置 WebServer + DNS Captive Portal |
| **多语言** | 中文/English/Tiếng Việt/Bahasa Indonesia |
| **分区方案** | 双 OTA 1.5MB；Flash SPIFFS 仅兼容预留，2.0 不写报警数据 |
| **固件编译大小** | 约 1.1 MB |
