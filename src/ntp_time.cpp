/*
 * ntp_time.cpp
 * ============================================================================
 * NTP 时间同步实现
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include "ntp_time.h"
#include "config.h"

// ============================================================================
//  时间同步
// ============================================================================

void sync_time() {
  // ★ WiFi 未连接时跳过同步, 避免阻塞 10 秒拖垮 BLE 启动
  //    (loop 中已有重试逻辑, WiFi 连上后会自动重新触发)
  if (!WiFi.isConnected()) {
    Serial.println(F("[NTP] WiFi not connected, skipping initial sync (will retry in loop)"));
    // 仍然设置时区, 让 SNTP 配置准备好, 一旦 WiFi 连上就能同步
    setenv("TZ", g_cfg_tz_info.c_str(), 1);
    tzset();
    configTzTime(g_cfg_tz_info.c_str(),
                 g_cfg_ntp_server1.c_str(), g_cfg_ntp_server2.c_str());
    return;
  }

  Serial.printf("[NTP] Syncing via %s / %s (TZ: %s)...\n",
                g_cfg_ntp_server1.c_str(), g_cfg_ntp_server2.c_str(), g_cfg_tz_info.c_str());

  // 显式设置时区 (确保覆盖 SNTP 缓存的旧时区)
  setenv("TZ", g_cfg_tz_info.c_str(), 1);
  tzset();

  // 启动 SNTP
  configTzTime(g_cfg_tz_info.c_str(), g_cfg_ntp_server1.c_str(), g_cfg_ntp_server2.c_str());

  // 等待同步 (缩短到 3 秒, 避免长时间阻塞 setup)
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 3000)) {
    Serial.print(F("[NTP] Synchronized: "));
    Serial.print(&timeinfo, "%Y-%m-%d %H:%M:%S");
    Serial.print(F(" (TZ: "));
    Serial.print(g_cfg_tz_info);
    Serial.println(F(")"));
  } else {
    Serial.println(F("[NTP] WARNING: Sync failed! Will retry in loop."));
  }
}

// ============================================================================
//  获取格式化时间字符串
// ============================================================================

// ============================================================================
//  判断 NTP 是否已同步
// ============================================================================

bool is_ntp_synced() {
  time_t now;
  time(&now);
  return now > 1577836800;  // 2020-01-01 00:00:00 UTC
}

// ============================================================================
//  获取格式化时间字符串
// ============================================================================

String get_time_string() {
  tzset();  // 确保时区生效
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buf);
  }
  return String("1970-01-01 00:00:00");
}

// ============================================================================
//  获取毫秒级格式化时间字符串 "YYYY-MM-DD HH:MM:SS.000"
// ============================================================================

String get_time_string_ms() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  if (tv.tv_sec > 1577836800) {  // 2020-01-01
    tzset();  // 确保时区生效
    struct tm* t = localtime(&tv.tv_sec);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    sprintf(buf + 19, ".%03d", (int)(tv.tv_usec / 1000));
    return String(buf);
  }
  return String("1970-01-01 00:00:00.000");
}

// ============================================================================
//  根据 millis 反推真实时间
//  公式: alarm_time = 当前系统时间 - (当前 millis - alarm_millis) / 1000
// ============================================================================

String get_time_for_millis(unsigned long alarm_millis) {
  if (!is_ntp_synced()) {
    return String("1970-01-01 00:00:00.000");
  }

  time_t now;
  time(&now);
  unsigned long elapsed_ms = millis() - alarm_millis;
  time_t alarm_time = now - (elapsed_ms / 1000);

  tzset();  // 确保时区生效
  struct tm* t = localtime(&alarm_time);
  if (t) {
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    sprintf(buf + 19, ".%03d", (int)(elapsed_ms % 1000));
    return String(buf);
  }
  return String("1970-01-01 00:00:00.000");
}
