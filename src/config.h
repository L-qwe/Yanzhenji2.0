/*
 * config.h
 * ============================================================================
 * 全局配置常量 — 所有可调参数集中在此文件
 * ============================================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

// --- MQTT (运行时可变, 配网页面可覆盖 NVS 中的值) ---
extern String  g_cfg_mqtt_broker;
extern int     g_cfg_mqtt_port;
extern String  g_cfg_mqtt_client_id;
extern String  g_cfg_mqtt_username;
extern String  g_cfg_mqtt_password;
extern String  g_cfg_mqtt_pub_topic;    // 发布 topic 模板, {MAC} 会被替换
extern String  g_cfg_mqtt_sub_topic;    // 订阅 topic 模板, {MAC} 会被替换

// 从 NVS 加载系统配置 (MQTT + NTP, 有则覆盖默认值)
void load_sys_config();

// 保存系统配置到 NVS
void save_sys_config(const String& broker, int port,
                     const String& client_id,
                     const String& user, const String& pass,
                     const String& pub_topic, const String& sub_topic,
                     const String& ntp_srv1, const String& ntp_srv2,
                     const String& tz,
                     const String& ap_ssid, const String& ap_pass,
                     unsigned long hb_interval,
                     const String& static_ip, const String& static_gw,
                     const String& static_sn, const String& static_dns);

// --- 界面语言 ---
extern String g_cfg_lang;     // "zh"=中文 "en"=English "vi"=Tiếng Việt "id"=Indonesia

// --- 设备热点 (AP) ---
extern String g_cfg_ap_ssid;      // 空字符串 = 自动生成 YanZhenJi_{MAC_NC}
extern String g_cfg_ap_password;  // 空字符串 = 无密码

// --- NTP (运行时可变, 配网页面可覆盖) ---
extern String g_cfg_ntp_server1;
extern String g_cfg_ntp_server2;
extern String g_cfg_tz_info;

// --- 静态 IP (留空=DHCP) ---
extern String g_cfg_static_ip;
extern String g_cfg_static_gateway;
extern String g_cfg_static_subnet;
extern String g_cfg_static_dns;

// --- GPIO ---
extern const int ALARM_PIN;
extern const int POWER_GOOD_PIN;       // BQ24074 PGOOD#, LOW=市电正常, HIGH=电池供电

// --- SD 卡 (SPI) ---
extern const int SD_SCK_PIN;
extern const int SD_MISO_PIN;
extern const int SD_MOSI_PIN;
extern const int SD_CS_PIN;
extern const int SD_CARD_DETECT_PIN;   // LOW=已插卡
extern const uint32_t SD_SPI_FREQUENCY_HZ;

// --- 固件版本 ---
extern const char* FIRMWARE_VERSION;
extern const char* FIRMWARE_TYPE;

// --- 设备 HTTP 服务 ---
extern const uint16_t HTTP_SERVER_PORT;

// --- IoT 平台设备接入上报 ---
extern const char* DEVICE_ACCESS_REPORT_URL;
extern const unsigned long DEVICE_ACCESS_REPORT_INTERVAL_MS;
extern const unsigned long DEVICE_ACCESS_REPORT_RETRY_MS;

// --- 时序参数 (ms) ---
extern const unsigned long DEBOUNCE_MS;
extern const unsigned long POWER_DEBOUNCE_MS;
extern unsigned long g_cfg_heartbeat_interval_ms;  // 心跳间隔 (运行时可变)

// --- SD 卡文件 ---
extern const char* RECORDS_FILE;   // 补传队列
extern const char* HISTORY_FILE;   // 仪表盘历史

// --- WiFi/MQTT 重试间隔 (ms) ---
extern const unsigned long WIFI_RETRY_INTERVAL_MS;
extern const unsigned long MQTT_RETRY_INTERVAL_MS;

#endif  // CONFIG_H
