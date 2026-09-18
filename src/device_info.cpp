/*
 * device_info.cpp
 * ============================================================================
 * 统一生成设备信息 JSON，避免 HTTP/AP 与 BLE 返回不同字段或不同状态。
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "device_info.h"

static const char* wifi_status_name(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:     return "idle";
    case WL_NO_SSID_AVAIL:   return "no_ssid_available";
    case WL_SCAN_COMPLETED:  return "scan_completed";
    case WL_CONNECTED:       return "connected";
    case WL_CONNECT_FAILED:  return "connect_failed";
    case WL_CONNECTION_LOST: return "connection_lost";
    case WL_DISCONNECTED:    return "disconnected";
    default:                 return "unknown";
  }
}

static String json_escape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    switch (c) {
      case '\"': escaped += F("\\\""); break;
      case '\\': escaped += F("\\\\"); break;
      case '\b': escaped += F("\\b"); break;
      case '\f': escaped += F("\\f"); break;
      case '\n': escaped += F("\\n"); break;
      case '\r': escaped += F("\\r"); break;
      case '\t': escaped += F("\\t"); break;
      default:
        if (c < 0x20) {
          char unicode_escape[7];
          snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04X", c);
          escaped += unicode_escape;
        } else {
          escaped += static_cast<char>(c);
        }
    }
  }
  return escaped;
}

String device_ble_name() {
  String mac_no_colon = WiFi.macAddress();
  mac_no_colon.replace(":", "");
  return String("YanZhenJi_") + mac_no_colon;
}

String build_device_info_json() {
  const wl_status_t status = WiFi.status();
  const bool connected = status == WL_CONNECTED;

  String json;
  json.reserve(320);
  json += F("{\"mac_address\":\"");
  json += WiFi.macAddress();
  json += F("\",\"firmware_version\":\"");
  json += FIRMWARE_VERSION;
  json += F("\",\"firmware_type\":\"");
  json += FIRMWARE_TYPE;
  json += F("\",\"ble_name\":\"");
  json += device_ble_name();
  json += F("\",\"wifi_ssid\":\"");
  if (connected) json += json_escape(WiFi.SSID());
  json += F("\",\"wifi_ip\":\"");
  if (connected) json += WiFi.localIP().toString();
  json += F("\",\"wifi_port\":");
  json += String(HTTP_SERVER_PORT);
  json += F(",\"wifi_connected\":");
  json += connected ? F("true") : F("false");
  json += F(",\"wifi_status\":\"");
  json += wifi_status_name(status);
  json += F("\"}");
  return json;
}

String build_device_access_report_json() {
  if (WiFi.status() != WL_CONNECTED) return String();

  String json;
  json.reserve(256);
  json += F("{\"mac_address\":\"");
  json += WiFi.macAddress();
  json += F("\",\"firmware_version\":\"");
  json += json_escape(FIRMWARE_VERSION);
  json += F("\",\"firmware_type\":\"");
  json += json_escape(FIRMWARE_TYPE);
  json += F("\",\"wifi_ip\":\"");
  json += WiFi.localIP().toString();
  json += F("\",\"wifi_port\":");
  json += String(HTTP_SERVER_PORT);
  json += F(",\"wifi_connected\":true,\"wifi_status\":\"connected\"}");
  return json;
}
