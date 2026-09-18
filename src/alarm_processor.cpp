/*
 * alarm_processor.cpp
 * ============================================================================
 * 报警信号处理实现
 *
 * 流程: GPIO 读取 → 20ms 消抖 → 下降沿检测 → 2秒分组合并 → 发出报警事件
 * ============================================================================
 */

#include <Arduino.h>
#include "alarm_processor.h"
#include "config.h"
#include "ntp_time.h"
#include "sd_storage.h"
#include "mqtt_publisher.h"
#include "wifi_manager.h"
#include "esp_task_wdt.h"

// ============================================================================
//  消抖状态 (模块内部静态变量)
// ============================================================================

static int  last_stable_state  = HIGH;
static bool debouncing         = false;
static unsigned long debounce_start_ms = 0;

// ============================================================================
//  分组状态 (2.5 秒窗口内连续触发合并为 1 次报警)
// ============================================================================

static bool          alarm_group_active    = false;
static unsigned long alarm_group_start_ms  = 0;
static int           alarm_group_count     = 0;

// ============================================================================
//  内部函数声明
// ============================================================================

static void emit_grouped_alarm(unsigned long first_millis, int count);

// ============================================================================
//  主入口: 报警信号采集与状态机
// ============================================================================

void process_alarm_signal() {
  int raw = digitalRead(ALARM_PIN);

  // ---- 阶段1: 20ms 软件消抖 ----
  if (raw != last_stable_state) {
    if (!debouncing) {
      debouncing = true;
      debounce_start_ms = millis();
      Serial.printf("[DEBOUNCE] Pin changed %d→%d, starting %lums timer\n",
                    last_stable_state, raw, DEBOUNCE_MS);
    } else if (millis() - debounce_start_ms >= DEBOUNCE_MS) {
      int prev_state = last_stable_state;
      last_stable_state = raw;
      debouncing = false;

      Serial.printf("[DEBOUNCE] Confirmed: %d → %d\n", prev_state, last_stable_state);

      // ---- 阶段2: 下降沿 (HIGH→LOW) → 触发报警 ----
      if (prev_state == HIGH && last_stable_state == LOW) {
        unsigned long now_ms = millis();
        Serial.printf("[EDGE] Falling edge detected @ %lums → ALARM TRIGGERED\n", now_ms);

        // 分组逻辑: 2.5 秒窗口内合并
        if (!alarm_group_active) {
          // 新分组
          alarm_group_active   = true;
          alarm_group_start_ms = now_ms;
          alarm_group_count    = 1;
          Serial.printf("[ALARM] Group started @ %lums\n", now_ms);
        } else if (now_ms - alarm_group_start_ms <= 2500UL) {
          // 2.5 秒窗口内, 合并到当前分组
          alarm_group_count++;
          Serial.printf("[ALARM] Group count=%d (within window, +%lums)\n",
                        alarm_group_count, now_ms - alarm_group_start_ms);
        } else {
          // 超出 2.5 秒窗口, 关闭旧分组, 开始新分组
          emit_grouped_alarm(alarm_group_start_ms, alarm_group_count);
          alarm_group_start_ms = now_ms;
          alarm_group_count    = 1;
          Serial.printf("[ALARM] New group started @ %lums\n", now_ms);
        }
      }

      // ---- 阶段3: 上升沿 (LOW→HIGH) → 报警清除 ----
      if (prev_state == LOW && last_stable_state == HIGH) {
        Serial.printf("[EDGE] Rising edge detected @ %lums → ALARM CLEARED\n", millis());
      }
    }
  } else {
    if (debouncing) {
      debouncing = false;
      Serial.println(F("[DEBOUNCE] Glitch ignored (signal returned to stable state)"));
    }
  }
}

// ============================================================================
//  分组窗口过期检查 (loop 中持续调用)
//  当 2.5 秒窗口到期且没有新触发时, 关闭分组并发出报警事件
// ============================================================================

void process_alarm_group() {
  if (alarm_group_active && (millis() - alarm_group_start_ms > 2500UL)) {
    emit_grouped_alarm(alarm_group_start_ms, alarm_group_count);
    alarm_group_active = false;
  }
}

// ============================================================================
//  发出分组后的报警事件: 写入历史 + MQTT/补传
//  记录格式: millis|timestamp|count
// ============================================================================

static void emit_grouped_alarm(unsigned long first_millis, int count) {
  String timestamp = generate_alarm_message_for_millis(first_millis);
  Serial.printf("[ALARM] Emitting group: count=%d, time=%s\n", count, timestamp.c_str());

  esp_task_wdt_reset();

  // 记录格式: millis|timestamp|count (始终写入 count, 方便解析)
  String record = String(first_millis) + "|" + timestamp + "|" + String(count);

  // ★ 始终写入历史文件 (仪表盘用)
  const bool history_saved = append_record_to_sd(record, HISTORY_FILE);

  bool published = false;
  if (WiFi.isConnected() && mqtt.connected()) {
    published = publish_alarm(timestamp, count);  // MQTT 含 count 字段
  }

  if (published) {
    Serial.printf("[ALARM] Published; SD history=%s\n",
                  history_saved ? "saved" : "FAILED");
  } else {
    // 离线或 MQTT 发布失败：写入补传队列，防止瞬时发布失败丢报警。
    const bool queued = append_record_to_sd(record, RECORDS_FILE);
    Serial.printf("[ALARM] Not published; SD history=%s, retransmit queue=%s\n",
                  history_saved ? "saved" : "FAILED",
                  queued ? "saved" : "FAILED");
  }

  esp_task_wdt_reset();
}

// ============================================================================
//  生成时间戳 (毫秒级, NTP 未同步时返回 epoch)
// ============================================================================

String generate_alarm_message() {
  return get_time_string_ms();
}

// ============================================================================
//  根据毫秒值反推真实时间 (补传专用)
//  原理: 当前 NTP 时间 - (当前 uptime - alarm 时的 uptime) = alarm 真实时间
// ============================================================================

String generate_alarm_message_for_millis(unsigned long alarm_millis) {
  return get_time_for_millis(alarm_millis);
}
