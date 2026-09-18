/*
 * alarm_processor.h
 * ============================================================================
 * 报警信号处理 — 消抖 + 下降沿检测 + 2.5秒分组合并
 * ============================================================================
 */

#ifndef ALARM_PROCESSOR_H
#define ALARM_PROCESSOR_H

#include <Arduino.h>

// 处理报警信号 (loop 中调用, 非阻塞)
void process_alarm_signal();
void process_alarm_group();   // 分组窗口过期检查 (loop 中调用, 非阻塞)

// 生成当前时间戳字符串 (毫秒级)
// 格式: "2026-07-27 12:00:00.123"
String generate_alarm_message();

// 根据 alarm 发生时的 millis() 反推时间戳 (补传专用)
String generate_alarm_message_for_millis(unsigned long alarm_millis);

#endif  // ALARM_PROCESSOR_H
