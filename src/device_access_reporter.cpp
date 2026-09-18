/*
 * device_access_reporter.cpp
 * ============================================================================
 * POST /anton/device-access/report
 * - WiFi 联网后立即上报
 * - 成功后固定周期刷新
 * - 失败后短周期重试
 * ============================================================================
 */

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config.h"
#include "device_access_reporter.h"
#include "device_info.h"

static bool g_was_connected = false;
static volatile bool g_last_report_succeeded = false;
static volatile bool g_report_in_progress = false;
static unsigned long g_last_attempt_ms = 0;

static bool post_device_access_report() {
  const String payload = build_device_access_report_json();
  if (payload.length() == 0) return false;

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.setReuse(false);

  if (!http.begin(DEVICE_ACCESS_REPORT_URL)) {
    Serial.println(F("[DEVICE-ACCESS] HTTP init failed"));
    return false;
  }

  http.addHeader("Content-Type", "application/json; charset=utf-8");
  http.addHeader("Accept", "application/json");
  const int status_code = http.POST(payload);
  String response;
  if (status_code > 0) response = http.getString();
  http.end();

  const bool succeeded = status_code >= 200 && status_code < 300;
  if (succeeded) {
    Serial.printf("[DEVICE-ACCESS] report OK, HTTP %d\n", status_code);
  } else if (status_code > 0) {
    // 响应体只用于现场排查；限制日志长度，避免异常响应占用过多内存。
    if (response.length() > 256) response = response.substring(0, 256);
    Serial.printf("[DEVICE-ACCESS] report rejected, HTTP %d: %s\n",
                  status_code, response.c_str());
  } else {
    Serial.printf("[DEVICE-ACCESS] report failed: %s\n",
                  HTTPClient::errorToString(status_code).c_str());
  }
  return succeeded;
}

// HTTP 请求放到独立任务，避免平台不可达时阻塞验针报警采集与 BLE/MQTT 主循环。
static void device_access_report_task(void* parameter) {
  (void)parameter;
  g_last_report_succeeded = post_device_access_report();
  g_report_in_progress = false;
  vTaskDelete(nullptr);
}

void device_access_report_loop() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (!connected) {
    g_was_connected = false;
    g_last_report_succeeded = false;
    return;
  }

  const bool just_connected = !g_was_connected;
  g_was_connected = true;

  const unsigned long now = millis();
  const unsigned long interval = g_last_report_succeeded
      ? DEVICE_ACCESS_REPORT_INTERVAL_MS
      : DEVICE_ACCESS_REPORT_RETRY_MS;
  if (g_report_in_progress) return;
  if (!just_connected && (unsigned long)(now - g_last_attempt_ms) < interval) return;

  // 在启动任务前记录时间，避免任务创建/请求失败时在紧密 loop 中连续重试。
  g_last_attempt_ms = now;
  g_report_in_progress = true;
  const BaseType_t started = xTaskCreate(
      device_access_report_task,
      "device_access",
      8192,
      nullptr,
      1,
      nullptr);
  if (started != pdPASS) {
    g_report_in_progress = false;
    g_last_report_succeeded = false;
    Serial.println(F("[DEVICE-ACCESS] failed to create report task"));
  }
}
