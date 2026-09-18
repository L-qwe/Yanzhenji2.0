/*
 * ble_provisioning.h
 * ============================================================================
 * BLE 配网服务 — APP 通过蓝牙写入 WiFi 凭证, 设备自动连接并回报状态
 *
 * GATT 结构:
 *   Service  : 8e7f1a01-2b3c-4d5e-9f01-aabbccddeeff
 *   Char #1 : 8e7f1a02-...  WRITE   接收 "SSID|PASSWORD"
 *   Char #2 : 8e7f1a03-...  READ+NOTIFY  配网状态字符串
 *   Char #3 : 8e7f1a04-...  READ   设备信息 JSON (每次读取实时刷新)
 *
 * 状态机:
 *   IDLE                  BLE 服务已就绪, 等待凭证
 *   CONNECTING            收到凭证, 正在连接
 *   CONNECTED,<ip>        连接成功, 附带局域网 IP (APP 可切换到 HTTP 通道)
 *   FAILED,<reason>       连接失败 (EMPTY_SSID/SSID_NOT_FOUND/CONNECT_FAILED/TIMEOUT)
 * ============================================================================
 */

#ifndef BLE_PROVISIONING_H
#define BLE_PROVISIONING_H

#include <Arduino.h>

// 初始化 BLE 配网服务 (setup 中调用一次, WiFi 初始化之后)
void init_ble_provisioning();

// 主循环维护 (loop 中调用, 非阻塞)
// - 推送 onWrite 期间积压的状态通知
// - 检测 WiFi 状态变化并 Notify
// - 超时检测 (30 秒未连上视为失败)
void ble_provisioning_loop();

// 当前配网状态字符串 (供调试 / 串口打印)
String ble_provisioning_status();

// ★ 配网进行中标志 — onWrite 设 true, CONNECTED/FAILED 设 false
//   wifi_manager.cpp 的 10 秒重连逻辑会检查此标志, 配网期间禁止 WiFi.reconnect 干扰
extern bool g_provisioning_active;

#endif  // BLE_PROVISIONING_H
