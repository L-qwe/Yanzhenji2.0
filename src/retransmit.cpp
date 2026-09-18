/*
 * retransmit.cpp
 * ============================================================================
 * 断网续传实现 — FIFO 从 SD /records.txt 逐条补传到 MQTT
 *
 * /records.txt — 仅存未发送的离线记录 (补传队列)
 * /history.txt  — 所有记录 (永久历史，不定时清理)
 * ============================================================================
 */

#include <Arduino.h>
#include "retransmit.h"
#include "config.h"
#include "sd_storage.h"
#include "mqtt_publisher.h"
#include "wifi_manager.h"
#include "alarm_processor.h"

static bool retransmit_active = false;

// 补传必须节流。同步 WebServer 依赖主 loop 高频调用 handleClient()；如果队首是
// 无法处理的记录（例如内网无 NTP 时保存的 1970 时间），无间隔重读 SD 和刷串口
// 会长期挤占主循环，最终表现为设备已联网但 80 端口网页访问超时。
static unsigned long retransmit_last_attempt_ms = 0;
static unsigned long retransmit_retry_delay_ms  = 0;

static void schedule_retransmit_retry(unsigned long delay_ms) {
  retransmit_last_attempt_ms = millis();
  retransmit_retry_delay_ms = delay_ms;
}

// ============================================================================
//  补传处理 (每次 loop 最多处理 1 条)
// ============================================================================

void process_retransmit() {
  // 前置条件: WiFi 和 MQTT 都必须在线
  if (!WiFi.isConnected() || !mqtt.connected()) {
    retransmit_active = false;
    return;
  }

  // 防止同一 loop 内多次进入
  if (retransmit_active) return;

  // 检查是否有补传队列
  if (!storage_file_exists(RECORDS_FILE)) return;

  // millis() 无符号减法天然兼容约 49 天回绕。
  const unsigned long now = millis();
  if (retransmit_retry_delay_ms > 0 &&
      (unsigned long)(now - retransmit_last_attempt_ms) < retransmit_retry_delay_ms) {
    return;
  }

  // 默认限制为每秒最多处理一条；下面的失败分支会设置更长退避。
  schedule_retransmit_retry(1000UL);

  // 打开文件, 读取第一条记录
  File f = open_storage_file(RECORDS_FILE, FILE_READ);
  if (!f) return;

  if (!f.available()) {
    f.close();
    remove_storage_file(RECORDS_FILE);
    return;
  }

  String first_line = f.readStringUntil('\n');
  f.close();

  first_line.trim();
  if (first_line.length() == 0) {
    remove_first_line(RECORDS_FILE);
    return;
  }

  Serial.print(F("[RETX] Processing: "));
  Serial.println(first_line);

  // 解析 — 格式: millis|timestamp
  String timestamp;
  unsigned long alarm_millis = 0;
  int sep = first_line.indexOf('|');
  if (sep > 0) {
    alarm_millis = strtoul(first_line.substring(0, sep).c_str(), NULL, 10);
    timestamp    = first_line.substring(sep + 1);
  } else {
    alarm_millis = strtoul(first_line.c_str(), NULL, 10);
    timestamp    = generate_alarm_message_for_millis(alarm_millis);
  }

  // 兼容旧格式: 提取 count / 去掉可能残留的 sent 标记
  int retx_count = 1;
  {
    int s2 = timestamp.indexOf('|');
    if (s2 > 0) {
      String extra = timestamp.substring(s2 + 1);
      retx_count = extra.toInt();
      if (retx_count < 1) retx_count = 1;
      timestamp = timestamp.substring(0, s2);
    }
  }

  // 只补传 5 秒前的旧记录, 避免在线报警的瞬时重复
  unsigned long age_ms = millis() - alarm_millis;
  if (age_ms < 5000) {
    Serial.printf("[RETX] Record too fresh (%lums < 5000ms), deferring\n", age_ms);
    schedule_retransmit_retry(5000UL - age_ms);
    return;
  }

  // 如果保存的时间戳是 epoch (NTP 从未同步过), 尝试用 millis 反推
  if (timestamp.startsWith("1970")) {
    Serial.println(F("[RETX] Timestamp is epoch, trying millis fallback..."));
    String fallback = generate_alarm_message_for_millis(alarm_millis);
    if (!fallback.startsWith("1970")) {
      timestamp = fallback;
    } else {
      Serial.println(F("[RETX] NTP not synced yet, deferring..."));
      // 无公网的企业内网可能长期无法同步 NTP，禁止在主循环中紧密重试。
      schedule_retransmit_retry(60000UL);
      return;
    }
  }

  Serial.print(F("[RETX] Cached alarm → "));
  Serial.println(timestamp);

  retransmit_active = true;

  if (publish_alarm(timestamp, retx_count)) {
    Serial.println(F("[RETX] Upload OK, removing from queue"));
    remove_first_line(RECORDS_FILE);
    // 队列仍有数据时逐条平滑排空，同时给 Web/MQTT/BLE 留出处理时间。
    schedule_retransmit_retry(250UL);
  } else {
    Serial.println(F("[RETX] Upload FAILED, keeping in queue for next retry"));
    schedule_retransmit_retry(5000UL);
  }

  retransmit_active = false;
}
