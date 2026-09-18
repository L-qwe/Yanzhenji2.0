/*
 * ntp_time.h
 * ============================================================================
 * NTP 时间同步 — 对时、获取格式化时间字符串
 * ============================================================================
 */

#ifndef NTP_TIME_H
#define NTP_TIME_H

#include <Arduino.h>
#include <time.h>

// 同步 NTP 时间 (setup 阶段调用, 允许有限阻塞)
void sync_time();

// NTP 是否已同步 (time > 2020-01-01 即认为已同步)
bool is_ntp_synced();

// 获取当前时间字符串 "YYYY-MM-DD HH:MM:SS"
// NTP 未同步时返回 "1970-01-01 00:00:00"
String get_time_string();

// 获取当前时间字符串 "YYYY-MM-DD HH:MM:SS.000" (毫秒级)
// NTP 未同步时返回 "1970-01-01 00:00:00.000"
String get_time_string_ms();

// 根据 alarm 发生时的 millis() 反推真实时间字符串
// 原理: 当前系统时间 - (当前 uptime - alarm 时的 uptime) = alarm 真实时间
// NTP 未同步时返回 "1970-01-01 00:00:00"
String get_time_for_millis(unsigned long alarm_millis);

#endif  // NTP_TIME_H
