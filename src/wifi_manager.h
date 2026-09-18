/*
 * wifi_manager.h
 * ============================================================================
 * WiFi 连接管理 — WiFiManager 配网 + 断线重连
 *
 * 首次使用: 手机连热点 "YanZhenJi_<MAC>" → 网页选 WiFi 输密码
 * 日常使用: 自动连接已保存的 WiFi
 * ============================================================================
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>

// 全局 WiFi 客户端对象 (供 MQTT 使用)
extern WiFiClient wifiClient;

// 配网 + 连接 (setup 阶段调用, 阻塞; 配网超时则自动重启)
void connect_wifi();

// 维护 WiFi 连接 (loop 中调用, 非阻塞)
void maintain_wifi();

// 由 BLE 配网模块调用: 写入新的 WiFi 凭证并触发连接
// (与 web /connect 路径等价, 同时持久化到 NVS)
// 返回值: true=已接受凭证, false=空 SSID 被拒绝
bool set_wifi_credentials_from_ble(const String& ssid, const String& pass);

#endif  // WIFI_MANAGER_H
