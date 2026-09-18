/*
 * config.cpp
 * ============================================================================
 * 全局配置常量定义 (初始配置)
 * ============================================================================
 */

#include <Arduino.h>
#include "config.h"

// --- MQTT (默认值, 配网页面可覆盖) ---
String  g_cfg_mqtt_broker    = "10.0.100.26";
int     g_cfg_mqtt_port      = 1883;
String  g_cfg_mqtt_client_id = "ESP32_Alarm_Monitor";
String  g_cfg_mqtt_username  = "test01";
String  g_cfg_mqtt_password  = "123456asd";
String  g_cfg_mqtt_pub_topic = "self_device/yanzhenji/{MAC}";   // {MAC} → 设备MAC
String  g_cfg_mqtt_sub_topic = "self_device/yanzhenji/{MAC}";   // OTA 指令订阅

// --- 界面语言 ---
String g_cfg_lang = "zh";   // 默认中文

// --- 设备热点 (空 = 自动生成/无密码) ---
String g_cfg_ap_ssid     = "";   // 空: 自动 YanZhenJi_{MAC_NC}
String g_cfg_ap_password = "";   // 空: 无密码

// --- NTP (默认值, 配网页面可覆盖) ---
String g_cfg_ntp_server1 = "ntp.aliyun.com";
String g_cfg_ntp_server2 = "ntp.ntsc.ac.cn";
String g_cfg_tz_info     = "CST-8";    // UTC+8 中国标准时间

// --- 静态 IP (默认空 = DHCP) ---
String g_cfg_static_ip      = "";
String g_cfg_static_gateway = "";
String g_cfg_static_subnet  = "";
String g_cfg_static_dns     = "";

// --- GPIO ---
const int   ALARM_PIN     = 34;            // GPIO34, 仅输入, 无内部上拉
const int   POWER_GOOD_PIN = 35;           // BQ24074 PGOOD#, 板载 10K 上拉

// --- SD 卡 (原理图 TF1, SPI 模式) ---
const int SD_SCK_PIN         = 18;
const int SD_MISO_PIN        = 19;
const int SD_MOSI_PIN        = 23;
const int SD_CS_PIN          = 27;
const int SD_CARD_DETECT_PIN = 32;         // 卡座 CD, 插卡时接地
const uint32_t SD_SPI_FREQUENCY_HZ = 10000000UL;

// --- 固件版本 ---
const char* FIRMWARE_VERSION = "v2.0.0";
const char* FIRMWARE_TYPE    = "验针机";

// --- 设备 HTTP 服务 ---
const uint16_t HTTP_SERVER_PORT = 80;

// --- IoT 平台设备接入上报 ---
const char* DEVICE_ACCESS_REPORT_URL =
    "http://10.0.100.65:8080/anton/device-access/report";
const unsigned long DEVICE_ACCESS_REPORT_INTERVAL_MS = 60000;
const unsigned long DEVICE_ACCESS_REPORT_RETRY_MS    = 15000;

// --- 时序参数 ---
const unsigned long DEBOUNCE_MS              = 20;    // 软件消抖时间
const unsigned long POWER_DEBOUNCE_MS        = 100;   // PGOOD# 状态消抖
unsigned long g_cfg_heartbeat_interval_ms    = 1000;  // 心跳间隔 ms (默认 1s)

// --- SD 卡文件（历史永久保留；仅补传成功后删除队列中的对应记录） ---
const char* RECORDS_FILE   = "/records.txt";  // 补传队列
const char* HISTORY_FILE   = "/history.txt";  // 仪表盘历史

// --- 重试间隔 ---
const unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
const unsigned long MQTT_RETRY_INTERVAL_MS = 5000;
