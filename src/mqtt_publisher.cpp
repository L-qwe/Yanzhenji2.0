/*
 * mqtt_publisher.cpp
 * ============================================================================
 * MQTT 模块实现 — 单 topic 通信
 *
 * 发布 topic (心跳+告警):  由 g_cfg_mqtt_pub_topic 配置, {MAC}替换为实际地址
 * 订阅 topic (OTA指令):    由 g_cfg_mqtt_sub_topic 配置, {MAC}替换为实际地址
 *
 * 心跳: {"<mac>_online":true}
 * 告警: {"<mac>_online":true,"<mac>_error":"<timestamp>"}
 * 指令: {"cmd":"ota","url":"http://..."}
 *
 * 报警期间心跳暂停, 报警清除后自动恢复
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "mqtt_publisher.h"
#include "wifi_manager.h"
#include "config.h"
#include "ota_handler.h"
#include "ntp_time.h"
#include "power_monitor.h"

// (PubSubClient mqtt 定义在 ESP32_Alarm_Monitor.ino 中, 避免初始化顺序问题)

// ============================================================================
//  基于 MAC 的动态 topic
// ============================================================================

String g_device_mac;     // e.g. "20:50:0D:E5:55:84"
String g_topic_pub;      // 发布 topic (心跳 + 告警)
String g_topic_sub;      // 订阅 topic (OTA 指令)
String g_ota_pending_url;  // 待执行的 OTA URL
String g_ota_pending_md5;  // 待校验的 MD5 (空=不校验)
long   g_ota_pending_size = 0; // 待校验的文件大小 (0=不校验)
bool   g_reboot_pending  = false; // 待处理的远程重启请求

// ============================================================================
//  读取 MAC 并生成 topic 字符串
// ============================================================================

void mqtt_setup_topics() {
  g_device_mac = WiFi.macAddress();

  // 替换模板中的 {MAC}
  String mac_no_colon = g_device_mac;
  mac_no_colon.replace(":", "");

  g_topic_pub = g_cfg_mqtt_pub_topic;
  g_topic_pub.replace("{MAC}", g_device_mac);
  g_topic_pub.replace("{MAC_NC}", mac_no_colon);  // 无冒号版

  g_topic_sub = g_cfg_mqtt_sub_topic;
  g_topic_sub.replace("{MAC}", g_device_mac);
  g_topic_sub.replace("{MAC_NC}", mac_no_colon);

  Serial.print(F("[MQTT] Device MAC:  "));
  Serial.println(g_device_mac);
  Serial.print(F("[MQTT] Publish to:  "));
  Serial.println(g_topic_pub);
  Serial.print(F("[MQTT] Subscribe:   "));
  Serial.println(g_topic_sub);
}

// ============================================================================
//  简易 JSON 提取 — 支持 "key":"val" 和 "key": "val" 两种格式
// ============================================================================

static String json_get_value(const String& json, const String& key) {
  // 先找到 "key"
  String ksearch = "\"" + key + "\"";
  int kpos = json.indexOf(ksearch);
  if (kpos < 0) return "";
  // 跳过 "key" 找到冒号
  int colon = json.indexOf(':', kpos + ksearch.length());
  if (colon < 0) return "";
  // 跳过冒号后的空格
  int vstart = colon + 1;
  while (vstart < (int)json.length() && json.charAt(vstart) == ' ') vstart++;
  // 读字符串值 (引号包裹) 或数字值
  if (json.charAt(vstart) == '"') {
    vstart++;  // 跳过前引号
    int vend = json.indexOf('"', vstart);
    if (vend < 0) return "";
    return json.substring(vstart, vend);
  } else {
    // 数字 — 读到非数字为止
    int vend = vstart;
    while (vend < (int)json.length() && (isdigit(json.charAt(vend)) || json.charAt(vend) == '-')) vend++;
    return json.substring(vstart, vend);
  }
}

// ============================================================================
//  MQTT 消息回调
//  支持指令:
//    {"cmd":"ota","url":"http://server/firmware.bin"}  → 触发 OTA
// ============================================================================

static void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  char buf[4096];
  unsigned int len = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
  memcpy(buf, payload, len);
  buf[len] = '\0';

  String msg(buf);

  // ★ 临时诊断
  Serial.print(F("[MQTT] << IN len="));
  Serial.print(length);
  Serial.print(F(" data="));
  Serial.println(buf);

  // 过滤回声
  bool has_cmd   = msg.indexOf("\"cmd\"") >= 0;
  bool has_msg   = msg.indexOf("\"msg_type\"") >= 0;

  if (!has_cmd && !has_msg) return;

  Serial.print(F("[MQTT] !! MATCHED: "));
  Serial.println(buf);

  // === 旧格式: {"cmd":"ota","url":"..."}  {"cmd":"reboot"} ===
  if (has_cmd) {
    String cmd = json_get_value(msg, "cmd");
    if (cmd == "ota") {
      String url = json_get_value(msg, "url");
      if (url.length() > 0) {
        Serial.println(F("[MQTT] → OTA queued (legacy format)"));
        g_ota_pending_url  = url;
        g_ota_pending_md5  = "";
        g_ota_pending_size = 0;
      }
    } else if (cmd == "reboot") {
      Serial.println(F("[MQTT] → Reboot requested"));
      g_reboot_pending = true;
    } else if (cmd == "config") {
      String key = json_get_value(msg, "key");
      String value = json_get_value(msg, "value");
      if (key.length() > 0) {
        Serial.printf("[MQTT] → Config: %s = %s\n", key.c_str(), value.c_str());
        Preferences p;
        p.begin("mqtt", false);
        if (key == "port") {
          p.putInt("port", value.toInt());
        } else if (key == "hb_interval") {
          p.putULong("hb_interval", value.toInt());
        } else {
          p.putString(key.c_str(), value.c_str());
        }
        p.end();
        Serial.println(F("[MQTT] Config saved, will reboot..."));
        g_reboot_pending = true;
      }
    }
    return;
  }

  // === 新格式: {"msg_type":"firmware_upgrade","firmware":{...}} ===
  if (has_msg) {
    String msg_type = json_get_value(msg, "msg_type");
    if (msg_type == "firmware_upgrade" || msg_type == "fw_upd" || msg_type == "upgrade") {
      String url  = json_get_value(msg, "file_url");
      String md5  = json_get_value(msg, "file_md5");
      long   fsize = json_get_value(msg, "file_size").toInt();

      if (url.length() > 0) {
        Serial.printf("[MQTT] → OTA queued (new format, md5=%s, size=%ld)\n",
                      md5.c_str(), fsize);
        g_ota_pending_url  = url;
        g_ota_pending_md5  = md5;
        g_ota_pending_size = fsize;
      }
    }
    return;
  }
}

// ============================================================================
//  初始化
// ============================================================================

void mqtt_init() {
  mqtt.setBufferSize(4096);
  mqtt.setServer(g_cfg_mqtt_broker.c_str(), g_cfg_mqtt_port);
  mqtt.setCallback(mqtt_callback);
  mqtt_connect();
}

// ============================================================================
//  连接
// ============================================================================

bool mqtt_connect() {
  if (mqtt.connected()) return true;

  Serial.printf("[MQTT] Connecting to %s:%d ... ", g_cfg_mqtt_broker.c_str(), g_cfg_mqtt_port);
  mqtt.setSocketTimeout(5);

  // Client ID 加 MAC 后缀, 保证全局唯一, 避免多客户端冲突
  String unique_id = g_cfg_mqtt_client_id + "_" + g_device_mac;
  unique_id.replace(":", "");  // 去掉冒号, 如 ESP32_Alarm_Monitor_20500DE55584

  if (mqtt.connect(unique_id.c_str(), g_cfg_mqtt_username.c_str(), g_cfg_mqtt_password.c_str())) {
    Serial.println(F("Connected!"));
    mqtt_subscribe();
    mqtt.setSocketTimeout(5);

    // ★ 一次性启动通知 (重启后 static 自动重置, publish 成功才标记)
    {
      static bool boot_notified = false;
      if (!boot_notified) {
        bool dhcp = (g_cfg_static_ip.length() == 0);
        const bool battery = is_battery_powered();
        String boot_msg = "{\"" + g_device_mac + "_status\":\"online\",\"" +
                          g_device_mac + "_version\":\"" + FIRMWARE_VERSION + "\",\"" +
                          g_device_mac + "_ip\":\"" + WiFi.localIP().toString() + "\",\"" +
                          g_device_mac + "_gw\":\"" + WiFi.gatewayIP().toString() + "\",\"" +
                          g_device_mac + "_dhcp\":" + (dhcp ? "true" : "false") +
                          ",\"mac_address\":\"" + g_device_mac +
                          "\",\"power_source\":\"" + (battery ? "battery" : "mains") +
                          "\",\"power_lost\":" + (battery ? "true" : "false") + "}";
        if (mqtt.publish(g_topic_pub.c_str(), boot_msg.c_str())) {
          boot_notified = true;
          Serial.print(F("[MQTT] Boot notification: "));
          Serial.println(boot_msg);
        } else {
          Serial.println(F("[MQTT] Boot notification publish FAILED, will retry"));
        }
      }
    }

    return true;
  }

  Serial.print(F("Failed, rc="));
  Serial.print(mqtt.state());
  Serial.print(F(" ("));
  switch (mqtt.state()) {
    case -4: Serial.print(F("CONNECTION_TIMEOUT")); break;
    case -3: Serial.print(F("CONNECTION_LOST"));    break;
    case -2: Serial.print(F("CONNECT_FAILED"));     break;
    case -1: Serial.print(F("DISCONNECTED"));       break;
    case  1: Serial.print(F("CONNECT_BAD_PROTOCOL")); break;
    case  2: Serial.print(F("CONNECT_BAD_CLIENT_ID")); break;
    case  3: Serial.print(F("CONNECT_UNAVAILABLE")); break;
    case  4: Serial.print(F("CONNECT_BAD_CREDENTIALS")); break;
    case  5: Serial.print(F("CONNECT_UNAUTHORIZED")); break;
    default: Serial.print(F("UNKNOWN"));
  }
  Serial.println(')');
  return false;
}

// ============================================================================
//  订阅设备控制主题
// ============================================================================

void mqtt_subscribe() {
  if (!mqtt.connected()) return;

  Serial.print(F("[MQTT] Subscribing to: "));
  Serial.println(g_topic_sub);

  if (mqtt.subscribe(g_topic_sub.c_str())) {
    Serial.println(F("[MQTT] Subscribe OK"));
  } else {
    Serial.println(F("[MQTT] Subscribe FAILED"));
  }
}

// ============================================================================
//  维护连接
// ============================================================================

void maintain_mqtt() {
  if (WiFi.isConnected() && !mqtt.connected()) {
    static unsigned long last_mqtt_retry = 0;
    unsigned long now = millis();
    if (now - last_mqtt_retry > MQTT_RETRY_INTERVAL_MS) {
      last_mqtt_retry = now;
      if (mqtt_connect()) {
        // mqtt_connect() 内部已调用 mqtt_subscribe(), 无需重复
      }
    }
  }
}

// ============================================================================
//  发布心跳 — 每秒 {"<mac>_online":true,"<mac>_error":"NO"}, 报警期间跳过
//  直接读 GPIO 状态, 不依赖跨模块变量, 避免符号链接问题
// ============================================================================

void publish_heartbeat() {
  if (!mqtt.connected()) return;

  // GPIO 为 LOW 表示报警中, 暂停心跳
  if (digitalRead(ALARM_PIN) == LOW) return;

  static unsigned long last_heartbeat_ms = 0;
  unsigned long now = millis();

  if (now - last_heartbeat_ms >= g_cfg_heartbeat_interval_ms) {
    last_heartbeat_ms = now;

    bool dhcp = (g_cfg_static_ip.length() == 0);
    const bool battery = is_battery_powered();
    String payload = "{\"" + g_device_mac + "_online\":true,\"" +
                     g_device_mac + "_version\":\"" + FIRMWARE_VERSION + "\",\"" +
                     g_device_mac + "_ip\":\"" + WiFi.localIP().toString() + "\",\"" +
                     g_device_mac + "_gw\":\"" + WiFi.gatewayIP().toString() + "\",\"" +
                     g_device_mac + "_dhcp\":" + (dhcp ? "true" : "false") +
                     ",\"mac_address\":\"" + g_device_mac +
                     "\",\"power_source\":\"" + (battery ? "battery" : "mains") +
                     "\",\"power_lost\":" + (battery ? "true" : "false") + "}";

    if (mqtt.publish(g_topic_pub.c_str(), payload.c_str())) {
      // 心跳正常, 不打印
    } else {
      Serial.println(F("[MQTT] Heartbeat publish FAILED"));
    }
  }
}

// ============================================================================
//  发布告警 — {"<mac>_online":true,"<mac>_error":"<timestamp>"}
// ============================================================================

bool publish_alarm(const String& timestamp, int count) {
  if (!mqtt.connected()) {
    Serial.println(F("[MQTT] Not connected, skipping alarm publish"));
    return false;
  }

  const bool battery = is_battery_powered();
  String payload = "{\"" + g_device_mac + "_online\":true,\"" +
                   g_device_mac + "_version\":\"" + FIRMWARE_VERSION + "\",\"" +
                   g_device_mac + "_error\":\"" + timestamp + "\",\"" +
                   g_device_mac + "_count\":" + String(count) +
                   ",\"mac_address\":\"" + g_device_mac +
                   "\",\"power_source\":\"" + (battery ? "battery" : "mains") +
                   "\",\"power_lost\":" + (battery ? "true" : "false") + "}";

  Serial.print(F("[MQTT] >> Publishing alarm: "));
  Serial.println(payload);

  if (mqtt.publish(g_topic_pub.c_str(), payload.c_str())) {
    Serial.println(F("[MQTT] Publish OK"));
    return true;
  }

  Serial.println(F("[MQTT] Publish FAILED"));
  return false;
}

// ============================================================================
//  发布供电状态事件
// ============================================================================

bool publish_power_state(bool battery_powered) {
  if (!mqtt.connected()) return false;

  String payload = F("{\"msg_type\":\"power_status\",\"mac_address\":\"");
  payload += g_device_mac;
  payload += F("\",\"firmware_version\":\"");
  payload += FIRMWARE_VERSION;
  payload += F("\",\"power_source\":\"");
  payload += battery_powered ? F("battery") : F("mains");
  payload += F("\",\"power_lost\":");
  payload += battery_powered ? F("true") : F("false");
  payload += F(",\"event_time\":\"");
  payload += get_time_string_ms();
  payload += F("\"}");

  Serial.print(F("[MQTT] >> Publishing power state: "));
  Serial.println(payload);
  if (mqtt.publish(g_topic_pub.c_str(), payload.c_str())) {
    Serial.println(F("[MQTT] Power state publish OK"));
    return true;
  }
  Serial.println(F("[MQTT] Power state publish FAILED"));
  return false;
}
