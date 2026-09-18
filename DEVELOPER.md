# 验针机报警监控系统 — 开发者参考手册

> **2.0 说明：** 本文件主体保留 1.x 模块细节供迁移参考。2.0 已将 SPIFFS
> 报警存储替换为 `sd_storage`，取消定时清理，并新增 `power_monitor`。硬件引脚、
> SD 故障恢复和 MQTT 供电字段以 [V2_ARCHITECTURE.md](V2_ARCHITECTURE.md) 与
> [README.md](README.md) 为准。

> **适用对象：** 需要理解、修改或扩展本项目的开发者
> **前置知识：** C/C++、Arduino 框架、ESP32 基础知识

---

## 目录

1. [项目架构](#1-项目架构)
2. [完整数据流](#2-完整数据流)
3. [启动流程（setup）](#3-启动流程setup)
4. [主循环（loop）](#4-主循环loop)
5. [模块详解](#5-模块详解)
   - [config — 全局配置与运行时变量](#51-config--全局配置与运行时变量)
   - [wifi_manager — WiFi 配网 + 系统配置门户](#52-wifi_manager--wifi-配网--系统配置门户)
   - [ntp_time — NTP 时间同步](#53-ntp_time--ntp-时间同步)
   - [spiffs_storage — SPIFFS 存储](#54-spiffs_storage--spiffs-存储)
   - [mqtt_publisher — MQTT 通信](#55-mqtt_publisher--mqtt-通信)
   - [alarm_processor — 报警信号处理](#56-alarm_processor--报警信号处理)
   - [retransmit — 断网续传](#57-retransmit--断网续传)
   - [ota_handler — OTA 远程升级](#58-ota_handler--ota-远程升级)
6. [全局对象与变量](#6-全局对象与变量)
7. [NVS 存储结构](#7-nvs-存储结构)
8. [编译与烧录](#8-编译与烧录)
9. [扩展指南](#9-扩展指南)

---

## 1. 项目架构

```
ESP32_Alarm_Monitor.ino        ← 主程序入口 (setup/loop)
    │
    ├── config.h/.cpp           ← 全局常量 + 运行时配置变量 + load/save 声明
    ├── wifi_manager.h/.cpp     ← WiFi + 系统配置门户 (WebServer + DNS + NVS)
    ├── ntp_time.h/.cpp         ← NTP 对时 + 时间戳生成
    ├── spiffs_storage.h/.cpp   ← SPIFFS 文件读写
    ├── mqtt_publisher.h/.cpp   ← MQTT 发布/订阅/心跳/回调
    ├── alarm_processor.h/.cpp  ← GPIO 消抖 + 下降沿检测 + 报警触发
    ├── retransmit.h/.cpp       ← 断网报警 FIFO 补传
    └── ota_handler.h/.cpp      ← HTTP 下载 + Update 刷写 OTA
```

### 依赖关系

```
ESP32_Alarm_Monitor.ino
 ├── config.h
 ├── wifi_manager.h
 ├── ntp_time.h
 ├── spiffs_storage.h
 ├── mqtt_publisher.h ──────→ wifi_manager.h, config.h, ota_handler.h
 ├── alarm_processor.h ─────→ mqtt_publisher.h, ntp_time.h, spiffs_storage.h, wifi_manager.h
 ├── retransmit.h ──────────→ mqtt_publisher.h, spiffs_storage.h, alarm_processor.h, wifi_manager.h
 └── ota_handler.h

wifi_manager.cpp 内部依赖:
 ├── WiFi.h, WebServer.h, DNSServer.h, Preferences.h (均 ESP32 内置)
 └── config.h (访问 g_cfg_* 运行时变量)
```

### 外部库依赖

| 库名 | 用途 | 安装方式 |
|------|------|----------|
| `PubSubClient` | MQTT 客户端 | Arduino 库管理器 (作者 Nick O'Leary) |
| `WiFi.h` / `WebServer.h` / `DNSServer.h` / `Preferences.h` | 配网门户 + NVS | ESP32 Arduino 核心内置 |
| `HTTPClient.h` / `Update.h` | OTA 固件升级 | ESP32 Arduino 核心内置 |
| `SPIFFS.h` | Flash 文件系统 | ESP32 Arduino 核心内置 |

---

## 2. 完整数据流

### 2.1 报警检测 → 发布流程

```
验针机蜂鸣器鸣响
    ↓ (GPIO34 被拉低)
digitalRead(ALARM_PIN) → LOW
    ↓
process_alarm_signal()
    ├─ 20ms 消抖 (DEBOUNCE_MS)
    ├─ 确认下降沿 (HIGH→LOW)
    └─ trigger_alarm(millis())
        ├─ generate_alarm_message() → "2026-07-28 11:06:23.662"
        ├─ WiFi.isConnected() && mqtt.connected() ?
        │   ├─ YES → publish_alarm(timestamp) → MQTT g_topic_pub
        │   └─ NO  → append_record_to_spiffs("167288|2026-07-28 11:06:23.662")
        └─ 串口输出日志
```

### 2.2 断网续传流程

```
WiFi 断开 → 报警触发
    ↓
trigger_alarm() → append_record_to_spiffs()
    ↓ (SPIFFS /records.txt)
保存: "167288|2026-07-28 11:06:23.662"
保存: "172500|2026-07-28 11:07:15.831"
    ↓ (WiFi 恢复, MQTT 重连)
process_retransmit()
    ├─ 读第一条: "167288|2026-07-28 11:06:23.662"
    ├─ 检查 age > 5 秒? ✓
    ├─ 时间戳不是 epoch? ✓ (直接使用保存的时间戳)
    ├─ publish_alarm() → MQTT g_topic_pub
    └─ remove_first_line() → 删除该条, 下条待处理
```

### 2.3 心跳流程

```
loop() 每次迭代
    ↓
publish_heartbeat()
    ├─ mqtt.connected() ?  ──NO──→ return
    ├─ digitalRead(ALARM_PIN) == LOW ? ──YES──→ return (报警中暂停)
    ├─ 距上次 > 1000ms ?  ──NO──→ return
    └─ mqtt.publish(g_topic_pub, "{\"20:50:0D:E5:55:84_online\":true}")
```

### 2.4 配置存储与加载

```
ESP32 上电
    ↓
connect_wifi()
    ├─ load_sys_config() → 从 NVS 读全部配置
    │   ├─ MQTT: broker, port, clientid, user, pass, pub_topic, sub_topic
    │   ├─ NTP:  ntp_srv1, ntp_srv2, tz
    │   └─ AP:   ap_ssid, ap_pass
    ├─ 读 WiFi 凭证 (NVS "wifi" namespace)
    └─ 启动 AP + STA
    ↓
mqtt_setup_topics()
    ├─ {MAC} → WiFi.macAddress()
    ├─ {MAC_NC} → 去掉冒号的 MAC
    └─ g_topic_pub / g_topic_sub 生成
    ↓
mqtt_init() → 使用 g_cfg_mqtt_* 连接 Broker
```

---

## 3. 启动流程（setup）

```
setup()
│
├─ 1. Serial.begin(115200)
├─ 2. 打印横幅 + eFuse MAC 地址
├─ 3. pinMode(ALARM_PIN, INPUT)
├─ 4. init_spiffs()
│      └─ SPIFFS.begin(true) → 失败则 while(1) 死循环
├─ 5. connect_wifi()
│      ├─ load_sys_config() → NVS 读取所有配置
│      ├─ WiFi.mode(WIFI_AP_STA)
│      ├─ 构建 AP SSID (用户配置或自动 YanZhenJi_{MAC_NC})
│      ├─ WiFi.softAP(ap_name, ap_pass) → 启动热点
│      ├─ 从 NVS "wifi" 读凭证 → WiFi.begin()
│      ├─ 等待 10 秒看能否连上
│      ├─ dns.start() + web.begin() → 配网门户
│      └─ 打印 IP / AP 信息
├─ 6. load_sys_config() 已在 connect_wifi 中调用
├─ 7. sync_time()
│      ├─ configTzTime(g_cfg_tz_info, g_cfg_ntp_server1, g_cfg_ntp_server2)
│      └─ getLocalTime() 等待 10 秒 → 打印结果
├─ 8. mqtt_setup_topics()
│      ├─ g_device_mac = WiFi.macAddress()
│      ├─ g_topic_pub = g_cfg_mqtt_pub_topic.replace({MAC}, {MAC_NC})
│      └─ g_topic_sub = g_cfg_mqtt_sub_topic.replace({MAC}, {MAC_NC})
├─ 9. mqtt_init()
│      ├─ mqtt.setServer(g_cfg_mqtt_broker, g_cfg_mqtt_port)
│      ├─ mqtt.setCallback(mqtt_callback)
│      └─ mqtt_connect() → connect + subscribe(g_topic_sub)
├─ 10. esp_task_wdt_init() → 10 秒看门狗
└─ 11. 打印 "Initialization complete"
```

---

## 4. 主循环（loop）

```cpp
void loop() {
  esp_task_wdt_reset();           // 喂狗

  maintain_wifi();                // DNS + WebServer + WiFi 重连
  maintain_mqtt();                // 断线重连 Broker
  process_alarm_signal();         // GPIO 消抖 + 下降沿检测
  process_retransmit();           // 断网补传 (每次 1 条)
  publish_heartbeat();            // 每秒心跳 (报警时跳过)

  // 串口 OTA 命令
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    if (cmd.startsWith("ota ")) {
      ota_from_url(cmd.substring(4));
    }
  }

  mqtt.loop();                    // MQTT 保活 + 接收消息
  yield();                        // 系统调度
}
```

**执行频率：** 完全非阻塞，每次迭代约几毫秒。

---

## 5. 模块详解

---

### 5.1 config — 全局配置与运行时变量

#### 文件
- `config.h` — 常量声明 + 运行时变量 + 存取函数声明
- `config.cpp` — 默认值定义

#### 设计原则

MQTT、NTP、AP 配置从编译时常量改为**运行时 String 变量**。默认值在 `config.cpp` 中定义，系统启动时通过 `load_sys_config()` 从 NVS 读取，有保存值则覆盖默认值。

#### 全部变量

| 变量名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `g_cfg_mqtt_broker` | `String` | `"10.0.100.26"` | MQTT Broker IP/域名 |
| `g_cfg_mqtt_port` | `int` | `1883` | MQTT 端口 |
| `g_cfg_mqtt_client_id` | `String` | `"ESP32_Alarm_Monitor"` | Client ID 前缀（实际追加 `_MAC`） |
| `g_cfg_mqtt_username` | `String` | `"test01"` | MQTT 用户名 |
| `g_cfg_mqtt_password` | `String` | `"123456asd"` | MQTT 密码 |
| `g_cfg_mqtt_pub_topic` | `String` | `"self_device/yanzhenji/{MAC}"` | 发布 Topic 模板（心跳+告警） |
| `g_cfg_mqtt_sub_topic` | `String` | `"self_device/yanzhenji/{MAC}"` | 订阅 Topic 模板（OTA 指令） |
| `g_cfg_ntp_server1` | `String` | `"ntp.aliyun.com"` | 主 NTP 服务器 |
| `g_cfg_ntp_server2` | `String` | `"ntp.ntsc.ac.cn"` | 备 NTP 服务器 |
| `g_cfg_tz_info` | `String` | `"CST-8"` | 时区（POSIX 格式） |
| `g_cfg_ap_ssid` | `String` | `""` | 热点名称（空=自动生成） |
| `g_cfg_ap_password` | `String` | `""` | 热点密码（空=无密码） |

#### 保留的编译时常量

| 常量名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `ALARM_PIN` | `const int` | `34` | 报警信号输入 GPIO |
| `DEBOUNCE_MS` | `const unsigned long` | `20` | 消抖时间（毫秒） |
| `HEARTBEAT_INTERVAL_MS` | `const unsigned long` | `1000` | 心跳间隔（毫秒） |
| `RECORDS_FILE` | `const char*` | `"/records.txt"` | SPIFFS 缓存文件路径 |
| `WIFI_RETRY_INTERVAL_MS` | `const unsigned long` | `10000` | WiFi 重连检查间隔 |
| `MQTT_RETRY_INTERVAL_MS` | `const unsigned long` | `5000` | MQTT 重连间隔 |

#### 全部函数

---

##### `void load_sys_config()`

**调用位置：** `connect_wifi()` 内部（WiFi 初始化时）

**功能：** 从 NVS namespace `"mqtt"` 读取全部系统配置（MQTT + NTP + AP）。如果 NVS 中无记录（`isKey("broker")` 返回 false），保留 `config.cpp` 中的默认值。

**参数：** 无

**返回值：** 无

---

##### `void save_sys_config(broker, port, client_id, user, pass, pub_topic, sub_topic, ntp_srv1, ntp_srv2, tz, ap_ssid, ap_pass)`

**功能：** 将所有系统配置写入 NVS，同时更新运行时变量。

**参数：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `broker` | `const String&` | MQTT Broker 地址 |
| `port` | `int` | MQTT 端口 |
| `client_id` | `const String&` | MQTT Client ID |
| `user` | `const String&` | MQTT 用户名 |
| `pass` | `const String&` | MQTT 密码 |
| `pub_topic` | `const String&` | 发布 Topic 模板 |
| `sub_topic` | `const String&` | 订阅 Topic 模板 |
| `ntp_srv1` | `const String&` | 主 NTP 服务器 |
| `ntp_srv2` | `const String&` | 备 NTP 服务器 |
| `tz` | `const String&` | 时区 (POSIX 格式) |
| `ap_ssid` | `const String&` | 热点名称（空=自动生成） |
| `ap_pass` | `const String&` | 热点密码（空=开放式） |

**返回值：** 无

---

### 5.2 wifi_manager — WiFi 配网 + 系统配置门户

#### 文件
- `wifi_manager.h`
- `wifi_manager.cpp`

#### 依赖
- `WiFi.h`, `WebServer.h`, `DNSServer.h`, `Preferences.h`（均为 ESP32 内置）

#### NVS 存储结构

**namespace `"wifi"`：**
| Key | 类型 | 说明 |
|-----|------|------|
| `ssid` | String | WiFi SSID |
| `pass` | String | WiFi 密码 |

**namespace `"mqtt"`：**
| Key | 类型 | 说明 |
|-----|------|------|
| `broker` | String | MQTT Broker |
| `port` | Int | MQTT 端口 |
| `clientid` | String | Client ID |
| `user` | String | 用户名 |
| `pass` | String | 密码 |
| `pub_topic` | String | 发布 Topic 模板 |
| `sub_topic` | String | 订阅 Topic 模板 |
| `ntp_srv1` | String | 主 NTP 服务器 |
| `ntp_srv2` | String | 备 NTP 服务器 |
| `tz` | String | 时区 |
| `ap_ssid` | String | 热点名 |
| `ap_pass` | String | 热点密码 |

#### 全部函数

---

##### `void connect_wifi()`

**调用位置：** `setup()` 中调用一次

**功能：**
1. 调用 `load_sys_config()` 从 NVS 加载全部配置
2. 设置 WiFi 模式为 `WIFI_AP_STA`
3. 构建 AP SSID：
   - 如果 `g_cfg_ap_ssid` 非空 → 使用用户配置（支持 `{MAC}` `{MAC_NC}` 占位符）
   - 如果为空 → 自动生成 `YanZhenJi_{MAC_NC}`
4. 启动热点（有密码则加密，无密码则开放）
5. 从 NVS `"wifi"` namespace 读取 WiFi 凭证尝试连接
6. 启动 DNS + Web 服务器

**参数：** 无

**返回值：** 无

**阻塞行为：** 最多阻塞约 10 秒（WiFi 连接等待）

**Web 路由表：**

| 路由 | 处理函数 | 说明 |
|------|----------|------|
| `GET /` | `handle_root()` | 首页（MAC + WiFi 状态 + MQTT 状态 + 导航） |
| `GET /scan` | `handle_scan()` | WiFi 扫描结果列表 |
| `GET /pw?ssid=XXX` | `handle_pw()` | 输入密码页面 |
| `GET /connect?ssid=XXX&pass=YYY` | `handle_connect()` | 执行 WiFi 连接 |
| `GET /disconnect` | `handle_disconnect()` | 断开 WiFi，清除凭证 |
| `GET /mqtt` | `handle_mqtt()` | 系统配置表单（MQTT + AP + NTP） |
| `GET /mqtt_save` | `handle_mqtt_save()` | 保存配置 → ESP.restart() |
| 其他所有 | `handle_root()` | 强制门户回退到首页 |

---

##### `void maintain_wifi()`

**调用位置：** `loop()` 中每次迭代调用

**功能：**
1. `dns.processNextRequest()` — DNS 劫持
2. `web.handleClient()` — HTTP 请求处理
3. 处理网页提交的 WiFi 切换
4. 每 10 秒检查断线重连

**参数：** 无

**返回值：** 无

**阻塞行为：** 完全非阻塞

---

##### `static String url_encode(const String& str)`

**功能：** URL 编码（SSID 中特殊字符处理）

**参数：** `str` — 原始字符串

**返回值：** `String` — 编码后字符串

---

### 5.3 ntp_time — NTP 时间同步

#### 全部函数

---

##### `void sync_time()`

**调用位置：** `setup()` 中调用一次

**功能：** 使用 `g_cfg_tz_info`、`g_cfg_ntp_server1`、`g_cfg_ntp_server2` 配置 NTP 并等待同步。

**参数：** 无

**返回值：** 无

**阻塞行为：** 最多阻塞 10 秒

---

##### `bool is_ntp_synced()`

**功能：** 判断系统时间是否 > 2020-01-01（即 NTP 已同步过）。

**参数：** 无

**返回值：** `true` 已同步 / `false` 未同步

---

##### `String get_time_string_ms()`

**功能：** 获取当前毫秒级时间戳。使用 `gettimeofday()` 获取微秒精度。

**参数：** 无

**返回值：** `"YYYY-MM-DD HH:MM:SS.000"`，NTP 未同步返回 epoch

---

##### `String get_time_for_millis(unsigned long alarm_millis)`

**功能：** 反推计算——根据报警时刻 `millis()` 值推算 NTP 时间。

**公式：** `alarm_real_time = current_ntp_time - (current_millis - alarm_millis) / 1000`

**参数：** `alarm_millis` — 报警发生时的 `millis()` 值

**返回值：** `"YYYY-MM-DD HH:MM:SS.000"`

**⚠ 限制：** 不能跨设备重启使用（`millis()` 归零后推算失效）。

---

### 5.4 spiffs_storage — SPIFFS 存储

#### 全部函数

| 函数 | 参数 | 返回值 | 说明 |
|------|------|--------|------|
| `init_spiffs()` | 无 | 无 | 挂载 SPIFFS，失败则 `while(1)` |
| `append_record_to_spiffs(record)` | `const String&` | 无 | 追加一行到 `/records.txt` |
| `remove_first_line(path)` | `const char*` | 无 | FIFO 删除第一行，空则删文件 |

---

### 5.5 mqtt_publisher — MQTT 通信

#### 全局变量

| 变量名 | 类型 | 说明 |
|--------|------|------|
| `mqtt` | `PubSubClient` | MQTT 客户端对象（`.ino` 中定义） |
| `g_device_mac` | `String` | WiFi MAC 如 `"20:50:0D:E5:55:84"` |
| `g_topic_pub` | `String` | 发布 Topic（由模板 + MAC 生成） |
| `g_topic_sub` | `String` | 订阅 Topic（由模板 + MAC 生成） |

#### Topic 模板变量

| 占位符 | 替换函数 | 结果示例 |
|--------|----------|----------|
| `{MAC}` | `WiFi.macAddress()` | `20:50:0D:E5:55:84` |
| `{MAC_NC}` | `WiFi.macAddress()` 去冒号 | `20500DE55584` |

#### Client ID 规则

```
g_cfg_mqtt_client_id + "_" + g_device_mac (去冒号)
```

例如：`ESP32_Alarm_Monitor_20500DE55584`

#### 全部函数

---

##### `void mqtt_setup_topics()`

**功能：** 读取 MAC，用模板生成 `g_topic_pub` 和 `g_topic_sub`。

**参数：** 无 | **返回值：** 无

---

##### `void mqtt_init()`

**功能：** 设置 Broker、回调、首次连接。

**参数：** 无 | **返回值：** 无

---

##### `bool mqtt_connect()`

**功能：** 连接 Broker + 订阅 `g_topic_sub`。使用 `g_cfg_mqtt_*` 变量。

**参数：** 无

**返回值：** `true` 成功 / `false` 失败（打印错误码）

---

##### `void maintain_mqtt()`

**功能：** WiFi 在线但 MQTT 断开时按 5 秒间隔重连。

**参数：** 无 | **返回值：** 无 | **阻塞：** 非阻塞

---

##### `bool publish_alarm(const String& timestamp)`

**功能：** 发布报警到 `g_topic_pub`。

**参数：** `timestamp` — 报警时间戳

**返回值：** `true` 成功 / `false` 失败

**Payload：** `{"<mac>_online":true,"<mac>_error":"<timestamp>"}`

---

##### `void publish_heartbeat()`

**功能：** 每秒发心跳到 `g_topic_pub`。GPIO LOW 时暂停。

**参数：** 无 | **返回值：** 无 | **阻塞：** 非阻塞

**Payload：** `{"<mac>_online":true}`

---

##### `static void mqtt_callback(char* topic, byte* payload, unsigned int length)`

**功能：** MQTT 消息回调。

**过滤逻辑：**
- 不含 `"cmd"` → 视为回声/心跳，忽略
- 含 `"cmd":"ota"` → 调用 `ota_from_url(url)`
- 其他 cmd → 打印 "Unknown command"

---

##### `static String json_get_value(const String& json, const String& key)`

**功能：** 简易 JSON 解析（搜索 `"key":"` 模式）。不支持嵌套。

**参数：** `json` — JSON 字符串，`key` — 键名

**返回值：** String — 找到的值，未找到返回空串

---

### 5.6 alarm_processor — 报警信号处理

#### 全部函数

---

##### `void process_alarm_signal()`

**功能：** 三阶段状态机。

**状态机流程：**
```
阶段1: 20ms 软件消抖
  raw != last_stable_state
    ├─ 首次变化 → 记录 millis(), 开始计时
    ├─ 计时满 20ms → 确认, 进入阶段2
    └─ 计时未满但信号恢复 → Glitch, 丢弃

阶段2: 下降沿 (HIGH → LOW)
  └─ trigger_alarm(millis())

阶段3: 上升沿 (LOW → HIGH)
  └─ 串口日志: ALARM CLEARED
```

**参数：** 无 | **返回值：** 无 | **阻塞：** 完全非阻塞

---

##### `static void trigger_alarm(unsigned long alarm_millis)`

**功能：** 生成时间戳 → 在线发布 / 离线存 SPIFFS。

**参数：** `alarm_millis` — 下降沿发生时的 `millis()` 值

**SPIFFS 记录格式：** `<millis值>|<NTP时间戳>`

---

##### `String generate_alarm_message()`

**功能：** 获取当前毫秒级时间戳。

**返回值：** 等同于 `get_time_string_ms()`

---

##### `String generate_alarm_message_for_millis(unsigned long alarm_millis)`

**功能：** 反推报警时刻的时间戳（补传专用）。

**返回值：** 等同于 `get_time_for_millis(alarm_millis)`

---

### 5.7 retransmit — 断网续传

#### 模块内部状态

| 变量 | 类型 | 说明 |
|------|------|------|
| `retransmit_active` | `static bool` | 防重入锁 |

#### 全部函数

---

##### `void process_retransmit()`

**功能：** FIFO 从 SPIFFS 读一条 → 补传到 MQTT。每次调用最多处理 1 条。

**完整流程：**
```
Step 1: WiFi + MQTT 在线? ─NO→ return
Step 2: retransmit_active? ─YES→ return
Step 3: SPIFFS 文件存在? ─NO→ return
Step 4: 读第一行
Step 5: 解析格式:
   ├─ 含 '|' → 新格式 millis|timestamp
   └─ 无 '|' → 旧格式 millis (兼容)
Step 6: age < 5000ms? ─YES→ defer
Step 7: 时间戳 epoch? → 反推, 仍 epoch → defer
Step 8: publish_alarm() → 成功删行 / 失败保留
```

**参数：** 无 | **返回值：** 无 | **阻塞：** 非阻塞

---

### 5.8 ota_handler — OTA 远程升级

#### 全部函数

---

##### `void ota_from_url(const String& url)`

**功能：** HTTP GET 下载固件 → Update 分块刷写 → ESP.restart()。

**参数：** `url` — 固件 `.bin` 文件的完整 HTTP URL

**返回值：** 无（成功自动重启，失败打印错误后返回）

**流程：**
```
Step 1: HTTP GET (30s 超时, 注意单位是毫秒!)
Step 2: 检查 Content-Length
Step 3: Update.begin(contentLength)
Step 4: 分块下载 4KB → Update.write → esp_task_wdt_reset
Step 5: 校验写入大小
Step 6: Update.end() + Update.isFinished()
Step 7: delay(2000) → ESP.restart()
```

**触发方式：**

| 方式 | 格式 |
|------|------|
| 串口命令 | `ota http://192.168.31.129:8080/firmware.bin` |
| MQTT 消息 | `{"cmd":"ota","url":"http://192.168.31.129:8080/firmware.bin"}` |

---

## 6. 全局对象与变量

### 定义在 `ESP32_Alarm_Monitor.ino`

| 变量 | 类型 | 说明 |
|------|------|------|
| `wifiClient` | `WiFiClient` | WiFi TCP 客户端 |
| `mqtt` | `PubSubClient` | MQTT 客户端 |

### 定义在 `config.cpp`（运行时可变）

| 变量 | 类型 | 说明 |
|------|------|------|
| `g_cfg_mqtt_broker` | `String` | MQTT Broker |
| `g_cfg_mqtt_port` | `int` | MQTT 端口 |
| `g_cfg_mqtt_client_id` | `String` | Client ID 前缀 |
| `g_cfg_mqtt_username` | `String` | MQTT 用户 |
| `g_cfg_mqtt_password` | `String` | MQTT 密码 |
| `g_cfg_mqtt_pub_topic` | `String` | 发布 Topic 模板 |
| `g_cfg_mqtt_sub_topic` | `String` | 订阅 Topic 模板 |
| `g_cfg_ntp_server1` | `String` | 主 NTP |
| `g_cfg_ntp_server2` | `String` | 备 NTP |
| `g_cfg_tz_info` | `String` | 时区 |
| `g_cfg_ap_ssid` | `String` | 热点名 |
| `g_cfg_ap_password` | `String` | 热点密码 |

### 定义在 `mqtt_publisher.cpp`

| 变量 | 类型 | 说明 |
|------|------|------|
| `g_device_mac` | `String` | MAC 地址 |
| `g_topic_pub` | `String` | 发布 Topic（模板替换后） |
| `g_topic_sub` | `String` | 订阅 Topic（模板替换后） |

---

## 7. NVS 存储结构

```
ESP32 NVS Flash
│
├── namespace "wifi"
│   ├── "ssid"  (String)  — WiFi SSID
│   └── "pass"  (String)  — WiFi 密码
│
└── namespace "mqtt"
    ├── "broker"    (String) — MQTT Broker
    ├── "port"      (Int)    — MQTT 端口
    ├── "clientid"  (String) — Client ID 前缀
    ├── "user"      (String) — MQTT 用户名
    ├── "pass"      (String) — MQTT 密码
    ├── "pub_topic" (String) — 发布 Topic 模板
    ├── "sub_topic" (String) — 订阅 Topic 模板
    ├── "ntp_srv1"  (String) — 主 NTP 服务器
    ├── "ntp_srv2"  (String) — 备 NTP 服务器
    ├── "tz"        (String) — 时区
    ├── "ap_ssid"   (String) — 热点名
    └── "ap_pass"   (String) — 热点密码
```

---

## 8. 编译与烧录

### 编译环境

| 项目 | 要求 |
|------|------|
| IDE | Arduino IDE 1.8+ 或 PlatformIO |
| 开发板 | ESP32 Dev Module (WROOM-32) |
| 分区方案 | **Default 4MB with spiffs**（必须！） |
| 上传速率 | 115200 |

### 依赖库

仅需安装 `PubSubClient` by Nick O'Leary，其余均为 ESP32 核心内置。

### 编译产物

| 文件 | 用途 |
|------|------|
| `ESP32_Alarm_Monitor.ino.bin` | **OTA 固件** |
| `ESP32_Alarm_Monitor.ino.bootloader.bin` | 含 bootloader |
| `ESP32_Alarm_Monitor.ino.bootloader_flashed.bin` | 烧录用完整固件 |

---

## 9. 扩展指南

### 添加新的网页配置项

1. 在 `config.h` / `config.cpp` 中声明和定义新的 `g_cfg_*` 变量
2. 在 `load_sys_config()` 中添加 `p.getString()`
3. 在 `save_sys_config()` 中添加 `p.putString()`
4. 在 `handle_mqtt()` 表单中添加 HTML 输入字段
5. 在 `handle_mqtt_save()` 中读取参数并传给 `save_sys_config()`

### 添加新的 MQTT 指令

在 `mqtt_callback()` 中添加分支：
```cpp
if (cmd == "reboot") {
  ESP.restart();
}
```

### 修改心跳间隔

编辑 `config.cpp`：
```cpp
const unsigned long HEARTBEAT_INTERVAL_MS = 5000;  // 5 秒
```

### 修改消抖时间

编辑 `config.cpp`：
```cpp
const unsigned long DEBOUNCE_MS = 50;  // 50ms
```

### MAC 地址来源

本项目统一使用 `WiFi.macAddress()`（WiFi 驱动 MAC，正序），不使用 `ESP.getEfuseMac()`（EFUSE 出厂 MAC，反序）。二者值不同是正常的——例如 EFUSE 存 `84:55:E5:0D:50:20`，WiFi 驱动解析为 `20:50:0D:E5:55:84`。

`WiFi.macAddress()` 在 `WiFi.mode()` 之前可能返回 `00:00:00:00:00:00`，需要先初始化 WiFi 栈并加 `delay(200)` 等待初始化完成。`connect_wifi()` 已处理此问题。

### 修改 GPIO 引脚

编辑 `config.cpp` 中 `ALARM_PIN`。注意 GPIO34-39 仅输入无上拉。

### 默认时区改为其他地区

编辑 `config.cpp`：
```cpp
String g_cfg_tz_info = "EST5";  // 美国东部
```

### Topic 模板支持更多变量

在 `mqtt_setup_topics()` 中添加 `replace()` 调用即可。
