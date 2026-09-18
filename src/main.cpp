#include <Arduino.h>

/*
 * ESP32_Alarm_Monitor (原 ESP32_Alarm_Monitor.ino → PlatformIO main.cpp)
 * ============================================================================
 * 硬件: ESP32-WROOM-32  |  框架: Arduino
 * 功能: 验针机报警监控 — 报警判定、SD永久存储、MQTT补传、电池状态监控
 *
 * 引脚: GPIO34 (输入, 高电平=无报警, 低电平=蜂鸣器鸣响)
 *       ⚠ GPIO34 是输入专用引脚，无内部上拉电阻，硬件需外接上拉。
 *
 * 项目结构:
 *   config.h / config.cpp            — 全局配置常量
 *   wifi_manager.h / wifi_manager.cpp    — WiFi 连接管理
 *   ntp_time.h / ntp_time.cpp           — NTP 时间同步
 *   sd_storage.h / sd_storage.cpp         — SD 卡永久存储
 *   power_monitor.h / power_monitor.cpp   — 市电/电池状态监控
 *   mqtt_publisher.h / mqtt_publisher.cpp — MQTT 发布/订阅/心跳
 *   alarm_processor.h / alarm_processor.cpp — 报警信号处理
 *   retransmit.h / retransmit.cpp         — 断网续传
 *   ota_handler.h / ota_handler.cpp       — OTA 远程固件升级
 * ============================================================================
 */

// 必须在 PubSubClient 之前定义, 默认 128 字节不够装 OTA URL
#define MQTT_MAX_PACKET_SIZE 4096

#include "config.h"
#include "wifi_manager.h"
#include "ntp_time.h"
#include "sd_storage.h"
#include "mqtt_publisher.h"
#include "power_monitor.h"
#include "alarm_processor.h"
#include "retransmit.h"
#include "ota_handler.h"
#include "ble_provisioning.h"
#include "device_access_reporter.h"

#include "esp_task_wdt.h"

// ============================================================================
//  全局对象 (必须在 .ino 定义, 保证 wifiClient 先于 mqtt 构造)
// ============================================================================

WiFiClient    wifiClient;
PubSubClient  mqtt(wifiClient);

// ============================================================================
//  初始化
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  ESP32 Alarm Monitor - Metal Detector"));
  Serial.println(F("========================================"));

  // --- MAC 地址 (开机即打印，不依赖 WiFi) ---
  {
    uint64_t chipid = ESP.getEfuseMac();
    Serial.printf("[SYS] Device MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
      (uint8_t)(chipid >> 40), (uint8_t)(chipid >> 32),
      (uint8_t)(chipid >> 24), (uint8_t)(chipid >> 16),
      (uint8_t)(chipid >> 8),  (uint8_t)(chipid));
  }

  // --- GPIO ---
  pinMode(ALARM_PIN, INPUT);
  int initial_state = digitalRead(ALARM_PIN);
  Serial.print(F("[GPIO] Alarm pin GPIO34 initialized, current state: "));
  Serial.println(initial_state == HIGH ? F("HIGH (no alarm)") : F("LOW (alarm!)"));

  // --- 市电/电池状态输入 ---
  power_monitor_init();

  // --- SD 卡（无卡时继续运行，插卡后自动挂载） ---
  init_sd_storage();

  // --- WiFi ---
  connect_wifi();

  // --- BLE 配网服务 (AP+STA 模式已启用, 与 WiFi 共存) ---
  init_ble_provisioning();

  // --- NTP (load_sys_config 已在 connect_wifi 中调用) ---
  sync_time();

  // --- MQTT ---
  mqtt_setup_topics();
  mqtt_init();

  // --- 看门狗 (在所有阻塞操作完成后启动, 避免超时复位) ---
  {
    esp_task_wdt_deinit();  // 先反初始化 (框架可能已启用)
    esp_task_wdt_init(10000, true);  // 10s 超时, panic=true
    esp_task_wdt_add(NULL);
    Serial.println(F("[WDT] Task watchdog enabled (10s timeout)"));
  }

  Serial.println(F("[SETUP] Initialization complete, entering main loop..."));
  Serial.println();
}

// ============================================================================
//  主循环 (完全非阻塞)
// ============================================================================

void loop() {
  esp_task_wdt_reset();

  // 1. 维护网络连接
  maintain_wifi();
  ble_provisioning_loop();   // BLE 配网状态变化推送 (在 maintain_wifi 之后)
  device_access_report_loop(); // 联网后向 IoT 平台登记/刷新设备信息
  maintain_mqtt();
  power_monitor_loop();        // GPIO35 PGOOD# 消抖 + MQTT 状态上报
  sd_storage_loop();           // SD 卡热插拔与自动重挂载

  // 2. 报警信号采集与状态机
  process_alarm_signal();
  process_alarm_group();   // 分组窗口过期检查 (无新触发 >2.5s 则关闭分组)

  // 3. 断网续传 (每次 loop 最多处理 1 条)
  process_retransmit();

  // 4. NTP 定时重试 (未同步时每60秒重试, WiFi连上后立刻试一次)
  {
    static unsigned long last_ntp_retry_ms = 0;
    static bool last_wifi_state = false;
    unsigned long now = millis();
    bool wifi_ok = WiFi.isConnected();

    bool just_connected = (wifi_ok && !last_wifi_state);
    bool interval_ok    = (now - last_ntp_retry_ms >= 60000UL);

    if (!is_ntp_synced() && wifi_ok && (just_connected || interval_ok)) {
      last_ntp_retry_ms = now;
      Serial.printf("[NTP] Retrying sync (%s)...\n",
                    just_connected ? "WiFi just connected" : "60s interval");
      configTzTime(g_cfg_tz_info.c_str(),
                   g_cfg_ntp_server1.c_str(), g_cfg_ntp_server2.c_str());
    }
    last_wifi_state = wifi_ok;
  }

  // 5. 心跳发布
  publish_heartbeat();

  // 6. 串口命令 (OTA 备用触发方式)
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("ota ")) {
      String url = cmd.substring(4);
      url.trim();
      Serial.printf("[SERIAL] OTA command, URL: %s\n", url.c_str());
      ota_from_url(url);
    } else if (cmd.length() > 0) {
      Serial.printf("[SERIAL] Unknown command: %s\n", cmd.c_str());
    }
  }

  // 7. 处理 MQTT 回调排队下来的 OTA 请求
  if (g_ota_pending_url.length() > 0) {
    String url  = g_ota_pending_url;
    String md5  = g_ota_pending_md5;
    long   size = g_ota_pending_size;
    g_ota_pending_url  = "";
    g_ota_pending_md5  = "";
    g_ota_pending_size = 0;
    Serial.println(F("[OTA] Executing queued OTA from MQTT..."));
    ota_from_url(url, md5, size);
  }

  // 8. 远程重启指令
  if (g_reboot_pending) {
    g_reboot_pending = false;
    Serial.println(F("[REBOOT] Remote reboot triggered, restarting in 2s..."));
    delay(2000);
    ESP.restart();
  }

  // 9. MQTT 保活
  mqtt.loop();

  // 10. 让小任务得以调度
  yield();
}
