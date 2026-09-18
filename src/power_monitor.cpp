/*
 * power_monitor.cpp
 * ============================================================================
 * GPIO35 <- BQ24074 PGOOD#，板载 10K 上拉至 3.3V。
 * 状态变化消抖后立即发布 MQTT；MQTT 重连时再次发布当前状态。
 * ============================================================================
 */

#include <Arduino.h>

#include "config.h"
#include "mqtt_publisher.h"
#include "power_monitor.h"

namespace {

int g_stable_state = LOW;
int g_last_raw_state = LOW;
unsigned long g_raw_changed_ms = 0;
bool g_publish_pending = true;
bool g_initialized = false;
bool g_mqtt_was_connected = false;
unsigned long g_last_publish_attempt_ms = 0;

}  // namespace

void power_monitor_init() {
  // GPIO35 无内部上下拉；底板 R23 提供 10K 上拉。
  pinMode(POWER_GOOD_PIN, INPUT);
  g_stable_state = digitalRead(POWER_GOOD_PIN);
  g_last_raw_state = g_stable_state;
  g_raw_changed_ms = millis();
  g_publish_pending = true;
  g_initialized = true;

  Serial.printf("[POWER] Initial source: %s (PGOOD#=%s)\n",
                current_power_source(),
                g_stable_state == LOW ? "LOW" : "HIGH");
}

bool is_battery_powered() {
  return g_initialized && g_stable_state == HIGH;
}

const char* current_power_source() {
  return is_battery_powered() ? "battery" : "mains";
}

void power_monitor_loop() {
  if (!g_initialized) return;

  const unsigned long now = millis();
  const int raw = digitalRead(POWER_GOOD_PIN);
  if (raw != g_last_raw_state) {
    g_last_raw_state = raw;
    g_raw_changed_ms = now;
  }

  if (raw != g_stable_state &&
      (unsigned long)(now - g_raw_changed_ms) >= POWER_DEBOUNCE_MS) {
    g_stable_state = raw;
    g_publish_pending = true;
    if (is_battery_powered()) {
      Serial.println(F("[POWER] External power lost; switched to battery"));
    } else {
      Serial.println(F("[POWER] External power restored; switched to mains"));
    }
  }

  const bool mqtt_connected = mqtt.connected();
  if (mqtt_connected && !g_mqtt_was_connected) g_publish_pending = true;
  g_mqtt_was_connected = mqtt_connected;

  if (mqtt_connected && g_publish_pending &&
      (g_last_publish_attempt_ms == 0 ||
       (unsigned long)(now - g_last_publish_attempt_ms) >= 2000UL)) {
    g_last_publish_attempt_ms = now;
    if (publish_power_state(is_battery_powered())) {
      g_publish_pending = false;
    }
  }
}
