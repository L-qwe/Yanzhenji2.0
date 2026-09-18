/*
 * device_info.h
 * ============================================================================
 * 设备信息公共接口 — HTTP/AP 与 BLE 共用同一份 JSON 数据定义
 * ============================================================================
 */

#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <Arduino.h>

// 返回设备当前信息。WiFi 状态和 IP 在每次调用时实时读取。
String build_device_info_json();

// IoT 平台设备接入接口要求的 7 个字段。未连接 WiFi 时返回空字符串。
String build_device_access_report_json();

// BLE 广播名称，与设备信息接口中的 ble_name 保持一致。
String device_ble_name();

#endif  // DEVICE_INFO_H
