/*
 * power_monitor.h
 * ============================================================================
 * BQ24074 PGOOD# 市电/电池供电状态监控。
 * ============================================================================
 */

#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

void power_monitor_init();
void power_monitor_loop();

// PGOOD# 为开漏低有效：LOW=外部电源正常，HIGH=已切换电池。
bool is_battery_powered();
const char* current_power_source();

#endif  // POWER_MONITOR_H
