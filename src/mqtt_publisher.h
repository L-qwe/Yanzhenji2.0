/*
 * mqtt_publisher.h
 * ============================================================================
 * MQTT 模块 — 单 topic 通信
 *
 * Topic: self_device/yanzhenji/<mac> (收发共用)
 *
 * 无报警: {"<mac>_online":true,"<mac>_error":"NO"}
 * 报警中: {"<mac>_online":true,"<mac>_error":"<timestamp>"}
 *         报警期间心跳暂停, 报警清除后恢复
 *
 * 订阅指令: {"cmd":"ota","url":"http://server/firmware.bin"}
 * ============================================================================
 */

#ifndef MQTT_PUBLISHER_H
#define MQTT_PUBLISHER_H

#include <Arduino.h>
#include <PubSubClient.h>

// 全局 MQTT 客户端对象
extern PubSubClient mqtt;

// 设备 MAC 地址和 topic (mqtt_setup_topics() 中初始化)
extern String g_device_mac;
extern String g_topic_pub;   // 发布 topic (心跳 + 告警)
extern String g_topic_sub;   // 订阅 topic (OTA 指令)
extern String g_ota_pending_url;   // 待处理的 OTA URL
extern String g_ota_pending_md5;   // 待校验的 MD5 (空=不校验)
extern long   g_ota_pending_size;  // 待校验的文件大小 (0=不校验)
extern bool   g_reboot_pending;     // 待处理的远程重启请求


// 读取 MAC 并生成 topic 字符串 (setup 中调用)
// 将 {MAC} 替换为实际 MAC 地址
void mqtt_setup_topics();

// 设置 Broker、回调、订阅, 并首次连接
void mqtt_init();

// 尝试连接 Broker; 返回是否成功
bool mqtt_connect();

// 维护 MQTT 连接 (loop 中调用, 非阻塞)
void maintain_mqtt();

// 发布告警: {"<mac>_online":true,"<mac>_error":"<timestamp>"} → g_topic_sub
bool publish_alarm(const String& timestamp, int count = 1);

// 发布市电/电池状态变化事件。
bool publish_power_state(bool battery_powered);

// 发布心跳: {"<mac>_online":true,"<mac>_error":"NO"}, 报警期间跳过
void publish_heartbeat();

// 订阅设备控制主题
void mqtt_subscribe();

#endif  // MQTT_PUBLISHER_H
