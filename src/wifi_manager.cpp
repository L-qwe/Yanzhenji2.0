/*
 * wifi_manager.cpp — WiFi/MQTT config portal (zh/en/vi/id 4 languages)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "wifi_manager.h"
#include "device_info.h"
#include "config.h"
#include "mqtt_publisher.h"
#include "ntp_time.h"
#include "sd_storage.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "ble_provisioning.h"   // ★ g_provisioning_active
#include <vector>

// ============================================================================
//  多语言字符串表
// ============================================================================

enum {
  K_TITLE, K_MAC, K_FW, K_CONNECTED, K_NOT_CONN, K_DISCONN,
  K_MQTT, K_ADDR, K_CLIENT, K_USER, K_PUB, K_SUB, K_MQTT_CFG,
  K_OTA, K_OTA_PASTE, K_WIFI_SCAN, K_SCAN_START,
  K_NETWORKS, K_SIGNAL, K_CONNECT_BTN, K_BACK,
  K_ENTER_PW, K_PW_HINT,
  K_CONNECTING, K_WAIT_REDIR,
  K_DISCONN_OK, K_DISCONN_MSG, K_HOME,
  K_SAVED, K_SAVE_MSG,
  K_BROKER_L, K_PORT_L, K_CLIENTID_L, K_CLIENT_HINT, K_USER_L, K_PASS_L,
  K_PUB_L, K_PUB_HINT1, K_PUB_HINT2, K_SUB_L,
  K_AP, K_AP_SSID_L, K_AP_SSID_PH, K_AP_HINT, K_AP_PASS_L, K_AP_PASS_PH,
  K_HB, K_HB_L, K_HB_HINT,
  K_NTP, K_NTP1_L, K_NTP1_HINT, K_NTP2_L, K_TZ_L,
  K_SAVE_BTN,
  K_FACTORY, K_FACTORY_MSG, K_FACTORY_BTN, K_FACTORY_CONF,
  K_OTA_TITLE, K_OTA_URL, K_OTA_START, K_OTA_TRIGGERED, K_OTA_CHECK_SERIAL,
  K_RESET_DOING, K_RESET_MSG,
  K_ERR_URL_EMPTY,
  K_DASHBOARD, K_DASH_TITLE, K_DASH_NO_DATA, K_DASH_TIME, K_DASH_STATUS,
  K_DASH_ALARM, K_DASH_VIEW, K_DASH_REFRESH, K_DASH_NTP_WARN, K_DASH_TOTAL,
  K_STATIC_IP, K_STATIC_HINT, K_IP_ADDR, K_GATEWAY, K_SUBNET, K_DNS,
  K_DASH_FILTER, K_DASH_DATE, K_DASH_FROM, K_DASH_TO, K_DASH_FILTER_BTN, K_DASH_CLEAR,
  K_MANUAL_ADD, K_OR,
};

static const char* ZH[] = {
  "验针机配置", "设备 MAC", "固件版本", "已连接", "未连接", "断开 WiFi",
  "MQTT Broker", "地址", "Client", "用户", "发布", "订阅", "修改 MQTT 配置",
  "OTA 升级", "粘贴固件 URL 升级", "扫描可用 WiFi", "开始扫描",
  "附近网络", "信号", "连接", "返回",
  "输入密码", "密码将保存在设备中",
  "正在连接...", "请稍候, 连接成功后自动跳转",
  "已断开", "WiFi 已断开, 密码已清除", "返回首页",
  "已保存", "配置已更新，设备即将重启...",
  "Broker 地址", "端口", "Client ID", "实际运行时会追加 _MAC 后缀",
  "用户名", "密码",
  "发布 Topic (心跳+告警)", "{MAC} = 设备MAC地址", "{MAC_NC} = 无冒号MAC",
  "订阅 Topic (OTA指令)",
  "设备热点", "热点名称 (SSID)", "留空=自动生成 YanZhenJi_设备MAC",
  "{MAC} = 带冒号MAC, {MAC_NC} = 无冒号MAC",
  "热点密码", "留空=无密码 (开放式热点)",
  "设备心跳", "心跳间隔 (毫秒)", "默认 1000ms = 1秒, 范围 200-60000ms",
  "NTP 时间同步", "NTP 服务器 1", "内网环境可用本地 NTP 服务器",
  "NTP 服务器 2 (备用)", "时区",
  "保存并重启",
  "恢复出厂设置", "清除所有配置, 恢复默认值", "恢复出厂设置",
  "确定清除所有配置?",
  "OTA 固件升级", "固件下载 URL", "开始升级",
  "OTA 已触发", "查看串口日志确认进度，成功后自动重启。",
  "正在恢复出厂设置...", "所有配置已清除，设备即将重启",
  "URL 不能为空",
  "报警记录", "报警记录面板", "近一周无报警记录", "报警时间", "状态",
  "报警", "查看报警记录", "(每30秒自动刷新)", "⚠ NTP未同步，时间可能不准确", "共",
  "静态IP设置", "留空则使用DHCP自动获取IP", "IP 地址", "网关", "子网掩码", "DNS 服务器",
  "时间筛选", "日期", "从", "至", "筛选", "清除",
  "手动添加网络", "或",
};

static const char* EN[] = {
  "Needle Detector Setup", "Device MAC", "Firmware", "Connected", "Not Connected", "Disconnect WiFi",
  "MQTT Broker", "Address", "Client", "User", "Publish", "Subscribe", "Modify MQTT Config",
  "OTA Update", "Paste Firmware URL", "WiFi Scan", "Start Scan",
  "networks", "Signal", "Connect", "Back",
  "Enter Password", "Password will be saved",
  "Connecting...", "Please wait, will redirect after connected",
  "Disconnected", "WiFi disconnected, password cleared", "Home",
  "Saved", "Config updated, restarting...",
  "Broker Address", "Port", "Client ID", "_MAC suffix will be appended at runtime",
  "Username", "Password",
  "Publish Topic (heartbeat+alarm)", "{MAC} = device MAC address",
  "{MAC_NC} = MAC without colons",
  "Subscribe Topic (OTA commands)",
  "Device Hotspot", "Hotspot Name (SSID)", "Empty = auto YanZhenJi_<MAC>",
  "{MAC} = MAC with colons, {MAC_NC} = MAC without",
  "Hotspot Password", "Empty = open (no password)",
  "Heartbeat", "Heartbeat Interval (ms)", "Default 1000ms, range 200-60000ms",
  "NTP Time Sync", "NTP Server 1", "Local NTP server for intranet",
  "NTP Server 2 (backup)", "Timezone",
  "Save & Restart",
  "Factory Reset", "Clear all settings, restore defaults", "Factory Reset",
  "Are you sure you want to clear all settings?",
  "OTA Firmware Update", "Firmware Download URL", "Start Update",
  "OTA Triggered", "Check serial log for progress. Device will restart after success.",
  "Resetting to factory defaults...", "All settings cleared, rebooting",
  "URL cannot be empty",
  "Alarm Records", "Alarm Dashboard", "No alarm records in the past week", "Time", "Status",
  "Alarm", "View Alarm Records", "(Auto-refresh every 30s)", "⚠ NTP not synced, times may be inaccurate", "Total",
  "Static IP", "Leave empty to use DHCP", "IP Address", "Gateway", "Subnet Mask", "DNS Server",
  "Time Filter", "Date", "From", "To", "Filter", "Clear",
  "Add Network Manually", "or",
};

static const char* VI[] = {
  "Cai dat may do kim", "MAC thiet bi", "Phien ban", "Da ket noi", "Chua ket noi", "Ngat WiFi",
  "MQTT Broker", "Dia chi", "Client", "Nguoi dung", "Gui", "Nhan", "Sua cau hinh MQTT",
  "Cap nhat OTA", "Dan URL firmware", "Quet WiFi", "Bat dau quet",
  "mang", "Tin hieu", "Ket noi", "Quay lai",
  "Nhap mat khau", "Mat khau se duoc luu",
  "Dang ket noi...", "Vui long doi, se tu chuyen sau khi ket noi",
  "Da ngat ket noi", "WiFi da ngat, da xoa mat khau", "Trang chu",
  "Da luu", "Cau hinh da cap nhat, dang khoi dong lai...",
  "Dia chi Broker", "Cong", "Client ID", "Hau to _MAC se duoc them khi chay",
  "Ten dang nhap", "Mat khau",
  "Topic gui (heartbeat+bao dong)", "{MAC} = dia chi MAC thiet bi",
  "{MAC_NC} = MAC khong dau hai cham",
  "Topic nhan (lenh OTA)",
  "Diem phat WiFi", "Ten diem phat (SSID)", "Trong = tu dong YanZhenJi_<MAC>",
  "{MAC} = MAC co dau hai cham, {MAC_NC} = MAC khong dau",
  "Mat khau diem phat", "Trong = mo (khong mat khau)",
  "Heartbeat", "Chu ky heartbeat (ms)", "Mac dinh 1000ms, pham vi 200-60000ms",
  "Dong bo NTP", "May chu NTP 1", "Dung NTP noi bo cho mang LAN",
  "May chu NTP 2 (du phong)", "Mui gio",
  "Luu & Khoi dong lai",
  "Khoi phuc cai dat goc", "Xoa tat ca cai dat, khoi phuc mac dinh", "Khoi phuc cai dat goc",
  "Ban co chac muon xoa tat ca cai dat?",
  "Cap nhat firmware OTA", "URL tai firmware", "Bat dau cap nhat",
  "OTA da kich hoat", "Kiem tra log serial de theo doi. Thiet bi se khoi dong lai.",
  "Dang khoi phuc cai dat goc...", "Da xoa tat ca cai dat, dang khoi dong lai",
  "URL khong duoc de trong",
  "Bao Dong", "Bang Dieu Khien Bao Dong", "Khong co bao dong trong tuan qua", "Thoi gian", "Trang thai",
  "Bao dong", "Xem Bao Dong", "(Tu dong lam moi moi 30 giay)", "⚠ NTP chua dong bo, thoi gian co the khong chinh xac", "Tong",
  "IP Tinh", "De trong de dung DHCP", "Dia chi IP", "Gateway", "Subnet Mask", "May chu DNS",
  "Loc thoi gian", "Ngay", "Tu", "Den", "Loc", "Xoa",
  "Them mang thu cong", "hoac",
};

static const char* ID[] = {
  "Pengaturan Detektor", "MAC Perangkat", "Firmware", "Terhubung", "Tidak Terhubung", "Putus WiFi",
  "MQTT Broker", "Alamat", "Client", "Pengguna", "Kirim", "Terima", "Ubah Konfigurasi MQTT",
  "Pembaruan OTA", "Tempel URL Firmware", "Pindai WiFi", "Mulai Pindai",
  "jaringan", "Sinyal", "Hubungkan", "Kembali",
  "Masukkan Kata Sandi", "Kata sandi akan disimpan",
  "Menghubungkan...", "Tunggu, akan dialihkan setelah terhubung",
  "Terputus", "WiFi terputus, kata sandi dihapus", "Beranda",
  "Tersimpan", "Konfigurasi diperbarui, memulai ulang...",
  "Alamat Broker", "Port", "Client ID", "Akhiran _MAC akan ditambahkan saat runtime",
  "Nama Pengguna", "Kata Sandi",
  "Topic Kirim (heartbeat+alarm)", "{MAC} = alamat MAC perangkat",
  "{MAC_NC} = MAC tanpa titik dua",
  "Topic Terima (perintah OTA)",
  "Hotspot Perangkat", "Nama Hotspot (SSID)", "Kosong = otomatis YanZhenJi_<MAC>",
  "{MAC} = MAC dengan titik dua, {MAC_NC} = MAC tanpa",
  "Kata Sandi Hotspot", "Kosong = terbuka (tanpa sandi)",
  "Heartbeat", "Interval Heartbeat (ms)", "Default 1000ms, rentang 200-60000ms",
  "Sinkronisasi NTP", "Server NTP 1", "Gunakan NTP lokal untuk intranet",
  "Server NTP 2 (cadangan)", "Zona Waktu",
  "Simpan & Mulai Ulang",
  "Reset Pabrik", "Hapus semua pengaturan, kembalikan default", "Reset Pabrik",
  "Yakin ingin menghapus semua pengaturan?",
  "Pembaruan Firmware OTA", "URL Unduh Firmware", "Mulai Pembaruan",
  "OTA Diaktifkan", "Periksa log serial untuk kemajuan. Perangkat akan restart setelah berhasil.",
  "Mengembalikan ke pengaturan pabrik...", "Semua pengaturan dihapus, memulai ulang",
  "URL tidak boleh kosong",
  "Rekaman Alarm", "Dasbor Alarm", "Tidak ada alarm dalam seminggu terakhir", "Waktu", "Status",
  "Alarm", "Lihat Rekaman Alarm", "(Refresh otomatis setiap 30 detik)", "⚠ NTP belum sinkron, waktu mungkin tidak akurat", "Total",
  "IP Statis", "Kosongkan untuk DHCP", "Alamat IP", "Gateway", "Subnet Mask", "Server DNS",
  "Filter Waktu", "Tanggal", "Dari", "Ke", "Filter", "Hapus",
  "Tambah Jaringan Manual", "atau",
};

// ============================================================================
//  模块级全局对象 (必须在函数使用前定义)
// ============================================================================

static WebServer  web(HTTP_SERVER_PORT);
static DNSServer   dns;
static Preferences prefs;

static bool   wifi_configured = false;
static String pending_ssid;
static String pending_pass;

// ★ WiFi 凭证内存缓存: 启动时读一次 NVS, 之后所有读/写改用全局变量,
//   避免 maintain_wifi 每 10 秒调 prefs.begin 刷屏
//   (Preferences 库的 begin 在命名空间不存在时即使返回 false 也会 log_e)
static String g_saved_ssid;
static String g_saved_pass;

static const char* const* TBL = ZH;
static int LANG_IDX = 0;

// ============================================================================
//  翻译 / 语言函数
// ============================================================================

static const char* T(int key) { return TBL[key]; }

static void set_lang(const String& lang) {
  g_cfg_lang = lang;
  if      (lang == "en") { TBL = EN; LANG_IDX = 1; }
  else if (lang == "vi") { TBL = VI; LANG_IDX = 2; }
  else if (lang == "id") { TBL = ID; LANG_IDX = 3; }
  else                   { TBL = ZH; LANG_IDX = 0; g_cfg_lang = "zh"; }
  Preferences p; p.begin("mqtt", false); p.putString("lang", g_cfg_lang); p.end();
}

static void init_lang() { set_lang(g_cfg_lang); }

// ============================================================================
//  语言选择器 + HTML 头部
// ============================================================================

static void add_lang_selector(String& html) {
  html += F("<div class='card' style='background:#f6f8fa;padding:8px 16px'>");
  html += F("<span style='font-size:13px;color:#888;margin-right:8px'>🌐</span>");
  html += F("<select onchange=\"location='/setlang?lang='+this.value\" style='padding:4px 8px;border:1px solid #d9d9d9;border-radius:6px;font-size:13px'>");
  static const char* opts[] = {"中文","English","Tiếng Việt","Bahasa Indonesia",nullptr};
  static const char* vals[] = {"zh","en","vi","id",nullptr};
  for (int i=0; opts[i]; i++) {
    html += F("<option value='"); html += vals[i]; html += F("'");
    if (LANG_IDX == i) html += F(" selected");
    html += F(">"); html += opts[i]; html += F("</option>");
  }
  html += F("</select></div>");
}

static void html_head(String& html) {
  html = F("<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "*{margin:0;padding:0;box-sizing:border-box}"
    "body{font-family:-apple-system,system-ui,sans-serif;background:#f0f2f5;color:#333;padding:16px}"
    "h2{font-size:18px;margin-bottom:8px;color:#1a1a2e}"
    ".card{background:#fff;border-radius:12px;padding:16px;margin-bottom:12px;box-shadow:0 1px 3px rgba(0,0,0,.1)}"
    ".status{display:flex;align-items:center;gap:8px;font-size:15px}"
    ".dot{width:10px;height:10px;border-radius:50%;flex-shrink:0}"
    ".dot-ok{background:#4caf50}.dot-no{background:#f44336}"
    ".ip{color:#888;font-size:13px;margin-top:4px}"
    ".btn{display:block;width:100%;padding:12px;border:none;border-radius:8px;font-size:15px;cursor:pointer;margin:6px 0}"
    ".btn-blue{background:#1890ff;color:#fff}.btn-red{background:#ff4d4f;color:#fff}.btn-gray{background:#e8e8e8;color:#333}"
    ".network{display:flex;justify-content:space-between;align-items:center;padding:12px 0;border-bottom:1px solid #f0f0f0}"
    ".signal{font-size:14px;font-weight:500}.rssi{color:#999;font-size:12px}"
    "input[type=password],input[type=text],input[type=number]{width:100%;padding:10px;border:1px solid #d9d9d9;border-radius:8px;font-size:14px;margin:6px 0}"
    ".hint{color:#999;font-size:12px;margin:4px 0}"
    "label{font-size:13px;color:#666;display:block;margin-top:8px}"
    "select{width:100%;padding:10px;border:1px solid #d9d9d9;border-radius:8px;font-size:14px}"
    "textarea{width:100%;font-size:12px;padding:8px;border:1px solid #d9d9d9;border-radius:8px}"
    "</style></head><body>");
  html += F("<h2>🔧 "); html += T(K_TITLE); html += F("</h2>");
  add_lang_selector(html);
}

static const char* const HTML_FOOT = "</body></html>";

// ============================================================================
//  前向声明所有路由处理函数
// ============================================================================

static void handle_setlang();
static void handle_device_info();
static void handle_root();
static void handle_scan();
static void handle_pw();
static void handle_connect();
static void handle_disconnect();
static void handle_mqtt();
static void handle_mqtt_save();
static void handle_factory_reset();
static void handle_ota_web();
static void handle_ota_web_go();
static void handle_dashboard();
static String url_encode(const String& str);

// ============================================================================
//  路由实现
// ============================================================================

static void handle_setlang() {
  set_lang(web.arg("lang"));
  web.sendHeader("Location", "/", true);
  web.send(302, "text/plain", "");
}

static void handle_device_info() {
  web.sendHeader("Cache-Control", "no-store");
  web.sendHeader("Access-Control-Allow-Origin", "*");
  web.send(200, "application/json; charset=utf-8", build_device_info_json());
}

static void handle_root() {
  String html; html_head(html);

  html += F("<div class='card' style='background:#f6f8fa'>");
  html += F("<div style='font-size:13px;color:#888;margin-bottom:4px'>");
  html += T(K_MAC); html += F("</div>");
  html += F("<div style='font-size:18px;font-weight:600;letter-spacing:1px'>");
  html += WiFi.macAddress(); html += F("</div>");
  html += F("<div style='font-size:12px;color:#888;margin-top:6px'>");
  html += T(K_FW); html += F(": "); html += FIRMWARE_VERSION; html += F("</div>");
  html += F("</div>");

  // ★ 报警记录面板入口
  html += F("<button class='btn btn-blue' onclick='location=\"/dashboard\"' style='margin-bottom:8px'>📋 ");
  html += T(K_DASH_VIEW); html += F("</button>");

  html += F("<div class='card'><div class='status'>");
  if (WiFi.status() == WL_CONNECTED) {
    html += F("<span class='dot dot-ok'></span><b>"); html += T(K_CONNECTED); html += F("</b>");
    html += F("<div class='ip'>SSID: "); html += WiFi.SSID();
    html += F("<br>STA IP: "); html += WiFi.localIP().toString(); html += F("</div>");
  } else {
    html += F("<span class='dot dot-no'></span><b>"); html += T(K_NOT_CONN); html += F("</b>");
  }
  html += F("<div class='ip' style='margin-top:6px'>📱 AP: "); html += WiFi.softAPIP().toString(); html += F("</div>");
  html += F("</div></div>");

  if (WiFi.status() == WL_CONNECTED) {
    html += F("<button class='btn btn-red' onclick=\"if(confirm('"); html += T(K_FACTORY_CONF);
    html += F("?'))location='/disconnect'\">"); html += T(K_DISCONN); html += F("</button>");
  }

  html += F("<div class='card'><h3>📡 "); html += T(K_MQTT); html += F("</h3>");
  html += F("<div style='font-size:14px'>");
  html += T(K_ADDR); html += F(": <b>"); html += g_cfg_mqtt_broker; html += F(":"); html += String(g_cfg_mqtt_port); html += F("</b><br>");
  html += T(K_CLIENT); html += F(": <b>"); html += g_cfg_mqtt_client_id; html += F("</b><br>");
  html += T(K_USER); html += F(": <b>"); html += g_cfg_mqtt_username; html += F("</b><br>");
  html += T(K_PUB); html += F(": <b style='font-size:12px'>"); html += g_cfg_mqtt_pub_topic; html += F("</b><br>");
  html += T(K_SUB); html += F(": <b style='font-size:12px'>"); html += g_cfg_mqtt_sub_topic; html += F("</b>");
  html += F("</div>");
  html += F("<button class='btn btn-blue' style='margin-top:8px' onclick='location=\"/mqtt\"'>⚙ "); html += T(K_MQTT_CFG); html += F("</button></div>");

  html += F("<div class='card'><h3>🔄 "); html += T(K_OTA); html += F("</h3>");
  html += F("<button class='btn btn-blue' onclick='location=\"/ota_web\"'>"); html += T(K_OTA_PASTE); html += F("</button></div>");

  html += F("<div class='card'>");
  html += F("<h3>📶 "); html += T(K_WIFI_SCAN); html += F("</h3>");
  html += F("<button class='btn btn-blue' onclick='location=\"/scan\"' style='margin-top:8px'>"); html += T(K_SCAN_START); html += F("</button></div>");

  html += HTML_FOOT;
  web.send(200, "text/html; charset=utf-8", html);
}

static void handle_scan() {
  // ★ 判断客户端来源: 通过 AP 热点连进来的不能关 AP, 否则页面就断了
  IPAddress client_ip = web.client().remoteIP();
  bool client_on_ap = (client_ip.toString().startsWith("192.168.4."));
  Serial.printf("[WiFi] Scan request from %s (on_AP=%d)\n",
                client_ip.toString().c_str(), client_on_ap);

  String ap_name; const char* ap_pass = NULL;
  if (!client_on_ap) {
    // 客户端在 STA 网络 → 可以安全关闭 AP 再扫描 (AP 信号干扰 STA 扫描)
    ap_name = g_cfg_ap_ssid;
    if (ap_name.length() == 0) {
      String nc = WiFi.macAddress(); nc.replace(":", "");
      ap_name = "YanZhenJi_" + nc;
    } else {
      String m = WiFi.macAddress(), nc = m; nc.replace(":", "");
      ap_name.replace("{MAC_NC}", nc); ap_name.replace("{MAC}", m);
    }
    ap_pass = g_cfg_ap_password.length() > 0 ? g_cfg_ap_password.c_str() : NULL;

    Serial.println(F("[WiFi] Stopping AP to scan..."));
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    delay(200);
  }

  // 扫描 (先清驱动层缓存, 再主动扫描)
  WiFi.scanDelete();
  esp_wifi_clear_ap_list();
  delay(100);
  int n = WIFI_SCAN_FAILED;
  for (int retry = 0; retry < 3 && n < 0; retry++) {
    if (retry > 0) { Serial.printf("[WiFi] Scan retry %d/3...\n", retry + 1); delay(300); }
    n = WiFi.scanNetworks(false, true);  // sync + show hidden
    Serial.printf("[WiFi] Scan result: n=%d\n", n);
  }

  // 恢复 AP (仅在之前关闭了的情况下)
  if (!client_on_ap) {
    Serial.println(F("[WiFi] Restarting AP..."));
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_name.c_str(), ap_pass);
    delay(100);
  }

  String html; html_head(html);

  if (n < 0) {
    html += F("<div class='card'><div style='display:flex;justify-content:space-between;align-items:center'>");
    html += F("<h3 style='margin:0'>⚠ "); html += T(K_WIFI_SCAN); html += F("</h3>");
    html += F("<a href='/scan' style='font-size:20px;text-decoration:none;padding:4px 8px'>🔄</a>");
    html += F("</div>");
    html += F("<p style='color:#ff4d4f;margin:12px 0'>Scan failed (code: ");
    html += String(n); html += F(")</p>");
    html += F("<p class='hint'>Please try again. If problem persists, check device antenna.</p>");
    html += F("<button class='btn btn-blue' onclick='location=\"/scan\"'>🔄 Retry</button>");
  } else if (n == 0) {
    html += F("<div class='card'><div style='display:flex;justify-content:space-between;align-items:center'>");
    html += F("<h3 style='margin:0'>📶 "); html += T(K_WIFI_SCAN); html += F("</h3>");
    html += F("<a href='/scan' style='font-size:20px;text-decoration:none;padding:4px 8px'>🔄</a>");
    html += F("</div>");
    html += F("<p style='color:#999;padding:12px'>No WiFi networks found nearby.</p>");
    html += F("<button class='btn btn-blue' onclick='location=\"/scan\"'>🔄 Rescan</button>");
  } else {
    html += F("<div class='card'><div style='display:flex;justify-content:space-between;align-items:center'>");
    html += F("<h3 style='margin:0'>"); html += T(K_NETWORKS); html += F(": "); html += n; html += F("</h3>");
    html += F("<a href='/scan' style='font-size:20px;text-decoration:none;padding:4px 8px'>🔄</a>");
    html += F("</div>");
    for (int i=0; i<n; i++) {
      String ssid = WiFi.SSID(i); int rssi = WiFi.RSSI(i);
      bool open = WiFi.encryptionType(i)==WIFI_AUTH_OPEN;
      html += F("<div class='network'><div><span class='signal'>"); html += ssid; html += F("</span>");
      if (open) html += F(" 🔓");
      html += F("<br><span class='rssi'>"); html += T(K_SIGNAL); html += F(": "); html += rssi; html += F(" dBm</span></div>");
      html += F("<button class='btn btn-blue' style='width:auto;padding:8px 14px' onclick='");
      if (open) { html += F("location=\"/connect?ssid="); html += url_encode(ssid); html += F("\"'"); }
      else      { html += F("location=\"/pw?ssid="); html += url_encode(ssid); html += F("\"'"); }
      html += F(">"); html += T(K_CONNECT_BTN); html += F("</button></div>");
    }
  }
  html += F("<button class='btn btn-gray' onclick='location=\"/\"'>← "); html += T(K_BACK); html += F("</button></div>");

  // ★ 手动添加网络 (扫描不到时的备用方案)
  html += F("<div class='card'>");
  html += F("<h3>✏ "); html += T(K_MANUAL_ADD); html += F("</h3>");
  html += F("<p style='color:#999;font-size:12px;margin-bottom:8px'>");
  html += T(K_OR); html += F("</p>");
  html += F("<form action='/connect' method='get'>");
  html += F("<input type='text' name='ssid' placeholder='WiFi SSID' style='margin-bottom:8px' required>");
  html += F("<input type='password' name='pass' placeholder='Password (empty if open)'>");
  html += F("<button type='submit' class='btn btn-blue'>🔗 "); html += T(K_CONNECT_BTN); html += F("</button>");
  html += F("</form></div>");

  html += HTML_FOOT;
  web.send(200, "text/html; charset=utf-8", html);
  WiFi.scanDelete();
}

static void handle_pw() {
  String ssid = web.arg("ssid");
  String html; html_head(html);
  html += F("<div class='card'><h3>🔐 "); html += T(K_ENTER_PW); html += F("</h3>");
  html += F("<p>SSID: <b>"); html += ssid; html += F("</b></p>");
  html += F("<form action='/connect' method='get'><input type='hidden' name='ssid' value='");
  html += ssid; html += F("'><input type='password' name='pass' placeholder='WiFi password' autofocus>");
  html += F("<p class='hint'>"); html += T(K_PW_HINT); html += F("</p>");
  html += F("<button type='submit' class='btn btn-blue'>"); html += T(K_CONNECT_BTN); html += F("</button></form>");
  html += F("<button class='btn btn-gray' onclick='location=\"/\"'>← "); html += T(K_BACK); html += F("</button></div>");
  html += HTML_FOOT;
  web.send(200, "text/html; charset=utf-8", html);
}

static void handle_connect() {
  String ssid = web.arg("ssid"), pass = web.arg("pass");
  prefs.begin("wifi", false); prefs.putString("ssid",ssid); prefs.putString("pass",pass); prefs.end();
  g_saved_ssid = ssid; g_saved_pass = pass;
  String html; html_head(html);
  html += F("<div class='card'><h3>⏳ "); html += T(K_CONNECTING); html += F("</h3>");
  html += F("<p>SSID: <b>"); html += ssid; html += F("</b></p><p>"); html += T(K_WAIT_REDIR); html += F("</p></div>");
  html += F("<script>setTimeout(function(){location='/'},4000)</script>"); html += HTML_FOOT;
  web.send(200, "text/html; charset=utf-8", html);
  pending_ssid=ssid; pending_pass=pass; wifi_configured=true;
}

static void handle_disconnect() {
  WiFi.disconnect();
  prefs.begin("wifi", false); prefs.putString("ssid",""); prefs.putString("pass",""); prefs.end();
  g_saved_ssid = ""; g_saved_pass = "";
  String html; html_head(html);
  html += F("<div class='card'><h3>✅ "); html += T(K_DISCONN_OK); html += F("</h3>");
  html += F("<p>"); html += T(K_DISCONN_MSG); html += F("</p>");
  html += F("<button class='btn btn-blue' onclick='location=\"/\"'>"); html += T(K_HOME); html += F("</button></div>");
  html += F("<script>setTimeout(function(){location='/'},3000)</script>"); html += HTML_FOOT;
  web.send(200, "text/html; charset=utf-8", html);
}

// ============================================================================
//  WiFi 初始化
// ============================================================================

void connect_wifi() {
  load_sys_config();
  init_lang();

  WiFi.mode(WIFI_AP_STA); delay(200);

  String ap_name = g_cfg_ap_ssid;
  if (ap_name.length() == 0) { String nc=WiFi.macAddress(); nc.replace(":",""); ap_name="YanZhenJi_"+nc; }
  else { String m=WiFi.macAddress(),nc=m; nc.replace(":",""); ap_name.replace("{MAC_NC}",nc); ap_name.replace("{MAC}",m); }
  const char* ap_pass = g_cfg_ap_password.length()>0 ? g_cfg_ap_password.c_str() : NULL;

  WiFi.softAP(ap_name.c_str(), ap_pass);
  Serial.print(F("[WiFi] AP '")); Serial.print(ap_name);
  Serial.println(ap_pass ? F("' started (with password)") : F("' started (no password)"));

  // ★ 启动时读一次 NVS 缓存到全局变量 (之后 maintain_wifi 不再碰 NVS)
  //   命名空间不存在时 begin 返回 false (会打一次日志, 启动期一次性, 可接受)
  {
    bool opened = prefs.begin("wifi", true);
    if (opened) {
      g_saved_ssid = prefs.getString("ssid", "");
      g_saved_pass = prefs.getString("pass", "");
      prefs.end();
    } else {
      g_saved_ssid = ""; g_saved_pass = "";
    }
  }
  String ssid = g_saved_ssid, pass = g_saved_pass;
  if (ssid.length()>0) {
    // 静态 IP (WiFi.config 必须在 WiFi.begin 之前调用)
    if (g_cfg_static_ip.length() > 0) {
      IPAddress ip;
      if (ip.fromString(g_cfg_static_ip)) {
        // 网关: 留空则自动推断 (IP 末位改为 1)
        String gw_str = g_cfg_static_gateway;
        if (gw_str.length() == 0) {
          gw_str = g_cfg_static_ip;
          int last_dot = gw_str.lastIndexOf('.');
          if (last_dot > 0) gw_str = gw_str.substring(0, last_dot) + ".1";
        }
        // 子网掩码: 留空则默认 255.255.255.0
        String sn_str = g_cfg_static_subnet;
        if (sn_str.length() == 0) sn_str = "255.255.255.0";

        IPAddress gw, sn, dns;
        if (gw.fromString(gw_str) && sn.fromString(sn_str)) {
          if (g_cfg_static_dns.length() > 0 && dns.fromString(g_cfg_static_dns)) {
            WiFi.config(ip, gw, sn, dns);
          } else {
            WiFi.config(ip, gw, sn);
          }
          Serial.printf("[WiFi] Static IP: %s / GW: %s / SN: %s\n",
                        g_cfg_static_ip.c_str(), gw_str.c_str(), sn_str.c_str());
        } else {
          Serial.printf("[WiFi] WARNING: Bad gateway(%s) or subnet(%s), using DHCP\n",
                        gw_str.c_str(), sn_str.c_str());
        }
      } else {
        Serial.printf("[WiFi] WARNING: Invalid IP '%s', using DHCP\n", g_cfg_static_ip.c_str());
      }
    } else {
      // ★ 显式重置为 DHCP, 清除 WiFi 驱动层缓存的旧静态 IP
      esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
      if (netif) {
        esp_netif_dhcpc_start(netif);
        Serial.println(F("[WiFi] DHCP client started"));
      }
    }
    Serial.printf("[WiFi] Connecting to saved: %s\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int w=0; WiFi.status()!=WL_CONNECTED && w<20; w++) { delay(500); Serial.print('.'); }
  }
  if (WiFi.status()==WL_CONNECTED) { Serial.println(); Serial.print(F("[WiFi] Connected! IP: ")); Serial.println(WiFi.localIP()); }
  else { Serial.println(); Serial.println(F("[WiFi] Not connected — use http://192.168.4.1/")); }

  dns.start(53, "*", WiFi.softAPIP());
  web.on("/",handle_root); web.on("/scan",handle_scan); web.on("/pw",handle_pw);
  web.on("/api/device-info", HTTP_GET, handle_device_info);
  web.on("/connect",handle_connect); web.on("/disconnect",handle_disconnect);
  web.on("/mqtt",handle_mqtt); web.on("/mqtt_save",handle_mqtt_save);
  web.on("/factory_reset",handle_factory_reset);
  web.on("/ota_web",handle_ota_web); web.on("/ota_web_go",handle_ota_web_go);
  web.on("/dashboard",handle_dashboard);
  web.on("/setlang",handle_setlang); web.onNotFound(handle_root);
  web.begin();
  Serial.println(F("[WiFi] Captive portal ready at http://192.168.4.1/"));
}

// BLE 配网入口: 接受新凭证 → 持久化 → 触发 maintain_wifi 切换网络
bool set_wifi_credentials_from_ble(const String& ssid, const String& pass) {
  if (ssid.length() == 0) {
    Serial.println(F("[WiFi] BLE: rejected empty SSID"));
    return false;
  }
  pending_ssid     = ssid;
  pending_pass     = pass;
  wifi_configured  = true;
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  g_saved_ssid = ssid; g_saved_pass = pass;
  Serial.printf("[WiFi] BLE: credentials accepted, SSID=%s\n", ssid.c_str());
  return true;
}

void maintain_wifi() {
  dns.processNextRequest(); web.handleClient();
  if (wifi_configured) {
    wifi_configured=false;
    Serial.printf("[WiFi] Switching to: %s\n", pending_ssid.c_str());
    // ★ 强制断开旧连接 + 清除配置, 确保 WiFi.begin 走完整重连流程
    //    解决设备已连上 WiFi 时, WiFi.begin 因 SSID 相同跳过重连导致配网超时
    //    (WiFi.begin() 内部有 "已连接同 SSID 就跳过" 的优化, 重复配网场景必须绕过)
    if (WiFi.status() == WL_CONNECTED || WiFi.status() == WL_DISCONNECTED) {
      WiFi.disconnect(true);   // true = 同时擦除当前连接配置
      delay(100);              // 给 radio 一点时间稳定
    }
    WiFi.begin(pending_ssid.c_str(), pending_pass.c_str());
  }
  static unsigned long lc=0;
  if (millis()-lc>10000) { lc=millis();
    if (WiFi.status()!=WL_CONNECTED && !wifi_configured) {
      // ★ 用全局缓存, 不再调 prefs.begin (避免每 10 秒刷屏报错)
      if (g_saved_ssid.length()>0 && WiFi.status()!=WL_CONNECTED) {
        // ★ 配网期间 (g_provisioning_active) 禁止 WiFi.reconnect 干扰
        //    WiFi.reconnect 会打断正在进行的 WiFi.begin 流程, 导致配网超时
        if (g_provisioning_active) {
          Serial.printf("[WiFi] Provisioning in progress, skip reconnect\n");
        } else {
          Serial.printf("[WiFi] Reconnecting: %s\n", g_saved_ssid.c_str());
          WiFi.reconnect();
        }
      }
    }
  }
}

// ============================================================================
//  NVS 存取
// ============================================================================

void load_sys_config() {
  Preferences p; p.begin("mqtt", true);
  if (p.isKey("broker")) {
    g_cfg_mqtt_broker    = p.getString("broker",    g_cfg_mqtt_broker);
    g_cfg_mqtt_port      = p.getInt("port",          g_cfg_mqtt_port);
    g_cfg_mqtt_client_id = p.getString("clientid",   g_cfg_mqtt_client_id);
    g_cfg_mqtt_username  = p.getString("user",       g_cfg_mqtt_username);
    g_cfg_mqtt_password  = p.getString("pass",       g_cfg_mqtt_password);
    g_cfg_mqtt_pub_topic = p.getString("pub_topic",  g_cfg_mqtt_pub_topic);
    g_cfg_mqtt_sub_topic = p.getString("sub_topic",  g_cfg_mqtt_sub_topic);
    g_cfg_ntp_server1    = p.getString("ntp_srv1",   g_cfg_ntp_server1);
    g_cfg_ntp_server2    = p.getString("ntp_srv2",   g_cfg_ntp_server2);
    g_cfg_tz_info        = p.getString("tz",         g_cfg_tz_info);
    g_cfg_ap_ssid        = p.getString("ap_ssid",    g_cfg_ap_ssid);
    g_cfg_ap_password    = p.getString("ap_pass",    g_cfg_ap_password);
    g_cfg_lang           = p.getString("lang",       g_cfg_lang);
    g_cfg_heartbeat_interval_ms = p.getULong("hb_interval", g_cfg_heartbeat_interval_ms);
    g_cfg_static_ip      = p.getString("static_ip",   "");
    g_cfg_static_gateway = p.getString("static_gw",   "");
    g_cfg_static_subnet  = p.getString("static_sn",   "");
    g_cfg_static_dns     = p.getString("static_dns",  "");
    p.end();
    Serial.println(F("[CFG] System config loaded from NVS"));
    if (g_cfg_static_ip.length() > 0) {
      Serial.printf("[CFG] Static IP: %s / GW: %s / SN: %s\n",
                    g_cfg_static_ip.c_str(), g_cfg_static_gateway.c_str(), g_cfg_static_subnet.c_str());
    }
  } else { p.end(); Serial.println(F("[CFG] No NVS config found, using defaults")); }
}

void save_sys_config(const String& broker, int port,
                     const String& client_id, const String& user, const String& pass,
                     const String& pub_topic, const String& sub_topic,
                     const String& ntp_srv1, const String& ntp_srv2, const String& tz,
                     const String& ap_ssid, const String& ap_pass, unsigned long hb_interval,
                     const String& static_ip, const String& static_gw,
                     const String& static_sn, const String& static_dns) {
  Preferences p; p.begin("mqtt", false);
  p.putString("broker",broker); p.putInt("port",port);
  p.putString("clientid",client_id); p.putString("user",user); p.putString("pass",pass);
  p.putString("pub_topic",pub_topic); p.putString("sub_topic",sub_topic);
  p.putString("ntp_srv1",ntp_srv1); p.putString("ntp_srv2",ntp_srv2); p.putString("tz",tz);
  p.putString("ap_ssid",ap_ssid); p.putString("ap_pass",ap_pass);
  p.putULong("hb_interval",hb_interval);
  p.putString("static_ip", static_ip); p.putString("static_gw", static_gw);
  p.putString("static_sn", static_sn); p.putString("static_dns", static_dns);
  p.end();
  g_cfg_mqtt_broker=broker; g_cfg_mqtt_port=port;
  g_cfg_mqtt_client_id=client_id; g_cfg_mqtt_username=user; g_cfg_mqtt_password=pass;
  g_cfg_mqtt_pub_topic=pub_topic; g_cfg_mqtt_sub_topic=sub_topic;
  g_cfg_ntp_server1=ntp_srv1; g_cfg_ntp_server2=ntp_srv2; g_cfg_tz_info=tz;
  g_cfg_ap_ssid=ap_ssid; g_cfg_ap_password=ap_pass;
  g_cfg_heartbeat_interval_ms=hb_interval;
  g_cfg_static_ip=static_ip; g_cfg_static_gateway=static_gw;
  g_cfg_static_subnet=static_sn; g_cfg_static_dns=static_dns;
  Serial.println(F("[CFG] System config saved to NVS"));
}

// ============================================================================
//  MQTT 配置页
// ============================================================================

static void handle_mqtt() {
  String html; html_head(html);
  html += F("<div class='card'><h3>⚙ "); html += T(K_MQTT_CFG); html += F("</h3><form action='/mqtt_save' method='get'>");

  html += F("<label>"); html += T(K_BROKER_L); html += F("</label>");
  html += F("<input type='text' name='broker' value='"); html += g_cfg_mqtt_broker; html += F("'>");
  html += F("<label>"); html += T(K_PORT_L); html += F("</label>");
  html += F("<input type='text' name='port' value='"); html += String(g_cfg_mqtt_port); html += F("'>");
  html += F("<label>"); html += T(K_CLIENTID_L); html += F("</label>");
  html += F("<input type='text' name='clientid' value='"); html += g_cfg_mqtt_client_id; html += F("'>");
  html += F("<p class='hint'>"); html += T(K_CLIENT_HINT); html += F("</p>");
  html += F("<label>"); html += T(K_USER_L); html += F("</label>");
  html += F("<input type='text' name='user' value='"); html += g_cfg_mqtt_username; html += F("'>");
  html += F("<label>"); html += T(K_PASS_L); html += F("</label>");
  html += F("<input type='password' name='pass' value='"); html += g_cfg_mqtt_password; html += F("'>");

  html += F("<label>"); html += T(K_PUB_L); html += F("</label>");
  html += F("<input type='text' name='pub_topic' value='"); html += g_cfg_mqtt_pub_topic; html += F("'>");
  html += F("<p class='hint'>"); html += T(K_PUB_HINT1); html += F("</p><p class='hint'>"); html += T(K_PUB_HINT2); html += F("</p>");
  html += F("<label>"); html += T(K_SUB_L); html += F("</label>");
  html += F("<input type='text' name='sub_topic' value='"); html += g_cfg_mqtt_sub_topic; html += F("'>");

  html += F("</div><div class='card'><h3>📱 "); html += T(K_AP); html += F("</h3>");
  html += F("<label>"); html += T(K_AP_SSID_L); html += F("</label>");
  html += F("<input type='text' name='ap_ssid' value='"); html += g_cfg_ap_ssid;
  html += F("' placeholder='"); html += T(K_AP_SSID_PH); html += F("'>");
  html += F("<p class='hint'>"); html += T(K_AP_HINT); html += F("</p>");
  html += F("<label>"); html += T(K_AP_PASS_L); html += F("</label>");
  html += F("<input type='password' name='ap_pass' value='"); html += g_cfg_ap_password;
  html += F("' placeholder='"); html += T(K_AP_PASS_PH); html += F("'>");

  html += F("</div><div class='card'><h3>❤ "); html += T(K_HB); html += F("</h3>");
  html += F("<label>"); html += T(K_HB_L); html += F("</label>");
  html += F("<input type='number' name='hb_interval' value='"); html += String(g_cfg_heartbeat_interval_ms);
  html += F("' min='200' max='60000'><p class='hint'>"); html += T(K_HB_HINT); html += F("</p>");

  html += F("</div><div class='card'><h3>🌐 "); html += T(K_STATIC_IP); html += F("</h3>");
  html += F("<p class='hint'>"); html += T(K_STATIC_HINT); html += F("</p>");
  html += F("<label>"); html += T(K_IP_ADDR); html += F("</label>");
  html += F("<input type='text' name='static_ip' value='"); html += g_cfg_static_ip;
  html += F("' placeholder='192.168.1.100'>");
  html += F("<label>"); html += T(K_GATEWAY); html += F("</label>");
  html += F("<input type='text' name='static_gw' value='"); html += g_cfg_static_gateway;
  html += F("' placeholder='192.168.1.1'>");
  html += F("<label>"); html += T(K_SUBNET); html += F("</label>");
  html += F("<input type='text' name='static_sn' value='"); html += g_cfg_static_subnet;
  html += F("' placeholder='255.255.255.0'>");
  html += F("<label>"); html += T(K_DNS); html += F("</label>");
  html += F("<input type='text' name='static_dns' value='"); html += g_cfg_static_dns;
  html += F("' placeholder='192.168.1.1'>");

  html += F("</div><div class='card'><h3>🕐 "); html += T(K_NTP); html += F("</h3>");
  html += F("<label>"); html += T(K_NTP1_L); html += F("</label>");
  html += F("<input type='text' name='ntp_srv1' value='"); html += g_cfg_ntp_server1; html += F("'>");
  html += F("<p class='hint'>"); html += T(K_NTP1_HINT); html += F("</p>");
  html += F("<label>"); html += T(K_NTP2_L); html += F("</label>");
  html += F("<input type='text' name='ntp_srv2' value='"); html += g_cfg_ntp_server2; html += F("'>");
  html += F("<label>"); html += T(K_TZ_L); html += F("</label><select name='tz'>");
  struct { const char* tz; const char* name; } zones[] = {
    {"UTC0","UTC±0"},{"GMT0","GMT±0"},{"WET0","WET±0"},{"CET-1","UTC+1"},
    {"EET-2","UTC+2"},{"MSK-3","UTC+3"},{"GST-4","UTC+4"},{"PKT-5","UTC+5"},
    {"BST-6","UTC+6"},{"ICT-7","UTC+7"},{"CST-8","UTC+8"},{"JST-9","UTC+9"},
    {"AEST-10","UTC+10"},{"NZST-12","UTC+12"},{"EST5","UTC-5"},{"CST6","UTC-6"},
    {"MST7","UTC-7"},{"PST8","UTC-8"},{"AKST9","UTC-9"},{"HST10","UTC-10"},
    {"BRT3","UTC-3"},{"ART2","UTC-2"},{"IST-5:30","UTC+5:30"},
  };
  int nz=sizeof(zones)/sizeof(zones[0]);
  for (int i=0;i<nz;i++) {
    html += F("<option value='"); html += zones[i].tz; html += F("'");
    if (g_cfg_tz_info==zones[i].tz) html += F(" selected");
    html += F(">"); html += zones[i].name; html += F(" ("); html += zones[i].tz; html += F(")</option>");
  }
  html += F("</select>");
  html += F("<button type='submit' class='btn btn-blue'>💾 "); html += T(K_SAVE_BTN); html += F("</button></form>");
  html += F("<button class='btn btn-gray' onclick='location=\"/\"'>← "); html += T(K_BACK); html += F("</button></div>");

  html += F("<div class='card' style='border:1px solid #ff4d4f'><h3 style='color:#ff4d4f'>⚠ "); html += T(K_FACTORY); html += F("</h3>");
  html += F("<p class='hint'>"); html += T(K_FACTORY_MSG); html += F("</p>");
  html += F("<button class='btn btn-red' onclick=\"if(confirm('"); html += T(K_FACTORY_CONF);
  html += F("?'))location='/factory_reset'\">🔄 "); html += T(K_FACTORY_BTN); html += F("</button></div>");

  html += HTML_FOOT;
  web.send(200, "text/html; charset=utf-8", html);
}

static void handle_mqtt_save() {
  String broker=web.arg("broker"); int port=web.arg("port").toInt();
  String clientid=web.arg("clientid"),user=web.arg("user"),pass=web.arg("pass");
  String pub_topic=web.arg("pub_topic"),sub_topic=web.arg("sub_topic");
  String ntp_srv1=web.arg("ntp_srv1"),ntp_srv2=web.arg("ntp_srv2"),tz=web.arg("tz");
  String ap_ssid=web.arg("ap_ssid"),ap_pass=web.arg("ap_pass");
  String static_ip=web.arg("static_ip"),static_gw=web.arg("static_gw");
  String static_sn=web.arg("static_sn"),static_dns=web.arg("static_dns");
  unsigned long hb=web.arg("hb_interval").toInt(); if(hb<200)hb=1000;
  if(broker.length()==0)broker="10.0.100.26";
  if(port==0)port=1883;
  if(clientid.length()==0)clientid="ESP32_Alarm_Monitor";
  if(pub_topic.length()==0)pub_topic="self_device/yanzhenji/{MAC}";
  if(sub_topic.length()==0)sub_topic="self_device/yanzhenji/{MAC}";
  if(ntp_srv1.length()==0)ntp_srv1="ntp.aliyun.com";
  if(ntp_srv2.length()==0)ntp_srv2="ntp.ntsc.ac.cn";
  if(tz.length()==0)tz="CST-8";
  save_sys_config(broker,port,clientid,user,pass,pub_topic,sub_topic,
                  ntp_srv1,ntp_srv2,tz,ap_ssid,ap_pass,hb,
                  static_ip,static_gw,static_sn,static_dns);

  String html; html_head(html);
  html += F("<div class='card'><h3>✅ "); html += T(K_SAVED); html += F("</h3><p>"); html += T(K_SAVE_MSG); html += F("</p></div>");
  html += F("<script>setTimeout(function(){location='/'},5000)</script>"); html += HTML_FOOT;
  web.send(200, "text/html; charset=utf-8", html);
  delay(1000); ESP.restart();
}

// ============================================================================
//  OTA 网页
// ============================================================================

static void handle_ota_web() {
  String html; html_head(html);
  html += F("<div class='card'><h3>🔄 "); html += T(K_OTA_TITLE); html += F("</h3>");
  html += F("<form action='/ota_web_go' method='get'>");
  html += F("<label>"); html += T(K_OTA_URL); html += F("</label>");
  html += F("<textarea name='url' rows='4' placeholder='http://...'></textarea>");
  html += F("<button type='submit' class='btn btn-blue'>"); html += T(K_OTA_START); html += F("</button></form>");
  html += F("<button class='btn btn-gray' onclick='location=\"/\"'>← "); html += T(K_BACK); html += F("</button></div>");
  html += HTML_FOOT;
  web.send(200, "text/html; charset=utf-8", html);
}

static void handle_ota_web_go() {
  String url=web.arg("url");
  String html; html_head(html);
  if (url.length()==0) {
    html += F("<div class='card'><h3>❌ "); html += T(K_ERR_URL_EMPTY); html += F("</h3>");
    html += F("<button class='btn btn-gray' onclick='location=\"/ota_web\"'>← "); html += T(K_BACK); html += F("</button></div>");
  } else {
    html += F("<div class='card'><h3>⏳ "); html += T(K_OTA_TRIGGERED); html += F("</h3><p>"); html += T(K_OTA_CHECK_SERIAL); html += F("</p></div>");
    html += HTML_FOOT;
    web.send(200, "text/html; charset=utf-8", html);
    g_ota_pending_url=url; g_ota_pending_md5=""; g_ota_pending_size=0;
    return;
  }
  html += HTML_FOOT;
  web.send(200, "text/html; charset=utf-8", html);
}

// ============================================================================
//  时间戳解析辅助 — "YYYY-MM-DD HH:MM:SS.mmm" → time_t
// ============================================================================

static time_t parse_ts(const String& ts) {
  struct tm t = {};
  int y, mo, d, h, mi, s;
  if (sscanf(ts.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) {
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = s;
    return mktime(&t);
  }
  return 0;
}

// 构造报警面板分页链接，并保留当前时间筛选条件。
static String dashboard_page_url(int page, const String& date,
                                 const String& from, const String& to) {
  String url = F("/dashboard?page=");
  url += String(page);
  if (date.length() > 0) {
    url += F("&date="); url += date;
    if (from.length() > 0) { url += F("&from="); url += from; }
    if (to.length() > 0)   { url += F("&to=");   url += to; }
  }
  return url;
}

// ============================================================================
//  报警记录面板 — 近一周告警 + 版本/MAC
// ============================================================================

static void handle_dashboard() {
  String html; html_head(html);

  static const int DASH_PAGE_SIZE = 25;
  int requested_page = web.arg("page").toInt();
  if (requested_page < 1) requested_page = 1;

  // --- 设备信息卡片 ---
  html += F("<div class='card' style='background:#f6f8fa'>");
  html += F("<div style='font-size:13px;color:#888;margin-bottom:4px'>");
  html += T(K_MAC); html += F("</div>");
  html += F("<div style='font-size:18px;font-weight:600;letter-spacing:1px'>");
  html += WiFi.macAddress(); html += F("</div>");
  html += F("<div style='font-size:12px;color:#888;margin-top:6px'>");
  html += T(K_FW); html += F(": <b>"); html += FIRMWARE_VERSION; html += F("</b></div>");
  html += F("</div>");

  // --- 解析筛选参数 ---
  String f_date = web.arg("date");
  String f_from = web.arg("from");
  String f_to   = web.arg("to");
  bool has_filter = (f_date.length() > 0);

  time_t filter_from = 0, filter_to = 0;
  if (has_filter) {
    struct tm t = {};
    int y, mo, d;
    if (sscanf(f_date.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) {
      t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
      int fh = 0, fm = 0, th = 23, tm = 59;
      if (f_from.length() > 0) sscanf(f_from.c_str(), "%d:%d", &fh, &fm);
      if (f_to.length() > 0)   sscanf(f_to.c_str(), "%d:%d", &th, &tm);
      t.tm_hour = fh; t.tm_min = fm; t.tm_sec = 0;
      filter_from = mktime(&t);
      t.tm_hour = th; t.tm_min = tm; t.tm_sec = 59;
      filter_to = mktime(&t);
      Serial.printf("[DASH] Filter: %s %02d:%02d ~ %02d:%02d (epoch %ld ~ %ld)\n",
                    f_date.c_str(), fh, fm, th, tm, (long)filter_from, (long)filter_to);
    } else {
      has_filter = false;
    }
  }

  // --- 生成今天的日期字符串 (供 date picker 默认值) ---
  char today_str[11] = "";
  {
    time_t now = time(nullptr);
    struct tm* tn = localtime(&now);
    if (tn) strftime(today_str, sizeof(today_str), "%Y-%m-%d", tn);
  }

  // --- 面板标题 + 筛选表单 ---
  html += F("<div class='card'><h3>📋 "); html += T(K_DASH_TITLE); html += F("</h3>");

  // 筛选表单
  html += F("<form action='/dashboard' method='get' style='margin:8px 0'>");
  html += F("<div style='display:flex;flex-wrap:wrap;gap:6px;align-items:flex-end'>");

  html += F("<div style='flex:1;min-width:100px'><label style='font-size:11px;color:#888'>");
  html += T(K_DASH_DATE); html += F("</label>");
  html += F("<input type='date' name='date' value='");
  html += has_filter ? f_date : today_str;
  html += F("' style='width:100%;padding:6px;border:1px solid #d9d9d9;border-radius:6px;font-size:13px'></div>");

  html += F("<div style='flex:1;min-width:70px'><label style='font-size:11px;color:#888'>");
  html += T(K_DASH_FROM); html += F("</label>");
  html += F("<input type='time' name='from' value='"); html += f_from;
  html += F("' style='width:100%;padding:6px;border:1px solid #d9d9d9;border-radius:6px;font-size:13px'></div>");

  html += F("<div style='flex:1;min-width:70px'><label style='font-size:11px;color:#888'>");
  html += T(K_DASH_TO); html += F("</label>");
  html += F("<input type='time' name='to' value='"); html += f_to;
  html += F("' style='width:100%;padding:6px;border:1px solid #d9d9d9;border-radius:6px;font-size:13px'></div>");

  html += F("<div style='display:flex;gap:4px;align-items:flex-end'>");
  html += F("<button type='submit' class='btn btn-blue' style='width:auto;padding:6px 14px;font-size:13px'>🔍 ");
  html += T(K_DASH_FILTER_BTN); html += F("</button>");
  if (has_filter) {
    html += F("<a href='/dashboard' style='padding:6px 10px;font-size:12px;color:#ff4d4f;text-decoration:none;white-space:nowrap'>✕ ");
    html += T(K_DASH_CLEAR); html += F("</a>");
  }
  html += F("</div></div></form>");

  // NTP 警告
  if (!is_ntp_synced()) {
    html += F("<p style='color:#ff9800;font-size:13px;margin-bottom:8px'>");
    html += T(K_DASH_NTP_WARN); html += F("</p>");
  } else if (!has_filter) {
    html += F("<p style='color:#999;font-size:11px;margin-bottom:4px'>"); html += T(K_DASH_REFRESH); html += F("</p>");
  }

  // --- 读取并过滤历史记录 ---
  if (!storage_file_exists(HISTORY_FILE)) {
    html += F("<p style='text-align:center;color:#999;padding:32px'>📭 ");
    html += T(K_DASH_NO_DATA); html += F("</p>");
    html += F("</div>");
  } else {
    File f = open_storage_file(HISTORY_FILE, FILE_READ);
    if (!f || !f.available()) {
      if (f) f.close();
      html += F("<p style='text-align:center;color:#999;padding:32px'>📭 ");
      html += T(K_DASH_NO_DATA); html += F("</p>");
      html += F("</div>");
    } else {
      time_t now_epoch = time(nullptr);
      time_t week_ago  = now_epoch - 7 * 24 * 3600;
      bool   do_filter = is_ntp_synced();

      std::vector<String> dash_ts;
      std::vector<int>    dash_counts;

      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int sep = line.indexOf('|');
        if (sep < 0) continue;
        String ts = line.substring(sep + 1);
        int count = 1;
        int sep2 = ts.indexOf('|');
        if (sep2 > 0) {
          count = ts.substring(sep2 + 1).toInt();
          if (count < 1) count = 1;
          ts = ts.substring(0, sep2);
        }

        if (do_filter && ts.startsWith("1970")) continue;

        time_t rec_time = parse_ts(ts);

        if (has_filter) {
          // 用户筛选模式: 必须在 filter_from ~ filter_to 范围内
          if (rec_time < filter_from || rec_time > filter_to) continue;
        } else {
          // 默认模式: 近 7 天
          if (do_filter && rec_time > 0 && rec_time < week_ago) continue;
        }

        dash_ts.push_back(ts);
        dash_counts.push_back(count);
      }
      f.close();
      int count = (int)dash_ts.size();
      int total_pages = count > 0 ? (count + DASH_PAGE_SIZE - 1) / DASH_PAGE_SIZE : 1;
      int current_page = requested_page;
      if (current_page > total_pages) current_page = total_pages;

      // --- 筛选结果标题 ---
      if (has_filter) {
        html += F("<div style='background:#e6f7ff;border:1px solid #91d5ff;border-radius:8px;padding:10px 14px;margin-bottom:12px'>");
        html += F("<span style='font-size:14px;font-weight:600;color:#1890ff'>");
        html += T(K_DASH_TOTAL); html += F(" <b style='font-size:18px'>"); html += String(count);
        html += F("</b> "); html += T(K_DASHBOARD); html += F("</span>");
        html += F("<br><span style='font-size:12px;color:#666'>");
        html += f_date; html += F(" ");
        html += (f_from.length() > 0 ? f_from : F("00:00"));
        html += F(" ~ ");
        html += (f_to.length() > 0 ? f_to : F("23:59"));
        html += F("</span></div>");
      }

      if (count == 0) {
        html += F("<p style='text-align:center;color:#999;padding:32px'>📭 ");
        html += T(K_DASH_NO_DATA); html += F("</p>");
        html += F("</div>");
      } else {
        if (!has_filter) {
          html += F("<p style='margin-bottom:8px;font-size:14px;color:#666'>");
          html += T(K_DASH_TOTAL); html += F(" <b>"); html += String(count);
          html += F("</b> "); html += T(K_DASHBOARD); html += F("</p>");
        }

        html += F("<div style='overflow-x:auto'><table style='width:100%;border-collapse:collapse;font-size:13px'>");
        html += F("<thead><tr style='background:#f6f8fa'>");
        html += F("<th style='padding:10px 8px;text-align:left;border-bottom:2px solid #e0e0e0;width:40px'>#</th>");
        html += F("<th style='padding:10px 8px;text-align:left;border-bottom:2px solid #e0e0e0'>"); html += T(K_DASH_TIME); html += F("</th>");
        html += F("<th style='padding:10px 8px;text-align:center;border-bottom:2px solid #e0e0e0;width:80px'>"); html += T(K_DASH_STATUS); html += F("</th>");
        html += F("</tr></thead><tbody>");

        // 倒序分页输出 (最新在前，每页 25 条)
        int page_first = (current_page - 1) * DASH_PAGE_SIZE;
        int page_last  = page_first + DASH_PAGE_SIZE;
        if (page_last > count) page_last = count;
        for (int row = page_first; row < page_last; row++) {
          int i = count - 1 - row;
          const char* bg = ((row - page_first) % 2 == 0) ? "#fff" : "#fafbfc";
          html += F("<tr style='background:"); html += bg; html += F(";border-bottom:1px solid #f0f0f0'>");
          html += F("<td style='padding:8px;color:#999;font-size:11px'>"); html += String(row + 1); html += F("</td>");
          html += F("<td style='padding:8px;font-family:SF Mono,Consolas,monospace;font-size:12px'>");
          html += dash_ts[i]; html += F("</td>");
          html += F("<td style='padding:8px;text-align:center'>");
          html += F("<span style='background:#ff4d4f;color:#fff;padding:3px 10px;border-radius:12px;font-size:11px;white-space:nowrap'>");
          html += T(K_DASH_ALARM);
          int alarm_count = dash_counts[i];
          if (alarm_count > 1) {
            html += F(" ×"); html += String(alarm_count);
          }
          html += F("</span></td></tr>");
        }
        html += F("</tbody></table></div>");

        // --- 分页导航 ---
        html += F("<div style='display:flex;align-items:center;justify-content:center;gap:10px;margin-top:14px'>");
        if (current_page > 1) {
          html += F("<a class='btn btn-gray' style='width:auto;min-width:70px;padding:8px 14px;text-align:center;text-decoration:none' href='");
          html += dashboard_page_url(current_page - 1, has_filter ? f_date : String(), f_from, f_to);
          html += F("'>‹</a>");
        } else {
          html += F("<span class='btn btn-gray' style='width:auto;min-width:70px;padding:8px 14px;text-align:center;opacity:.45'>‹</span>");
        }
        html += F("<span style='min-width:72px;text-align:center;font-size:13px;color:#666'>");
        html += String(current_page); html += F(" / "); html += String(total_pages); html += F("</span>");
        if (current_page < total_pages) {
          html += F("<a class='btn btn-blue' style='width:auto;min-width:70px;padding:8px 14px;text-align:center;text-decoration:none' href='");
          html += dashboard_page_url(current_page + 1, has_filter ? f_date : String(), f_from, f_to);
          html += F("'>›</a>");
        } else {
          html += F("<span class='btn btn-gray' style='width:auto;min-width:70px;padding:8px 14px;text-align:center;opacity:.45'>›</span>");
        }
        html += F("</div>");
      }
      html += F("</div>");
    }
  }

  // --- 导航按钮 ---
  html += F("<button class='btn btn-gray' onclick='location=\"/\"'>← "); html += T(K_BACK); html += F("</button>");
  html += F("<button class='btn btn-blue' onclick='location=\"/dashboard\"' style='margin-top:4px'>🔄 ");
  html += T(K_DASHBOARD); html += F("</button>");

  html += HTML_FOOT;
  web.send(200, "text/html; charset=utf-8", html);
}

// ============================================================================
//  恢复出厂
// ============================================================================

static void handle_factory_reset() {
  String html; html_head(html);
  html += F("<div class='card'><h3>🔄 "); html += T(K_RESET_DOING); html += F("</h3><p>"); html += T(K_RESET_MSG); html += F("</p></div>");
  html += F("<script>setTimeout(function(){location='/'},8000)</script>"); html += HTML_FOOT;
  web.send(200, "text/html; charset=utf-8", html);
  delay(500);
  Preferences p; p.begin("wifi",false); p.clear(); p.end();
  p.begin("mqtt",false); p.clear(); p.end();
  Serial.println(F("[FACTORY RESET] All NVS config cleared, rebooting..."));
  delay(1000); ESP.restart();
}

// ============================================================================
//  URL 编码
// ============================================================================

static String url_encode(const String& str) {
  String enc;
  for (int i=0;i<(int)str.length();i++) {
    char c=str.charAt(i);
    if (isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~') enc+=c;
    else { char b[4]; sprintf(b,"%%%02X",(unsigned char)c); enc+=b; }
  }
  return enc;
}
