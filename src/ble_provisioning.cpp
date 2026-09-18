/*
 * ble_provisioning.cpp
 * ============================================================================
 * BLE 配网服务实现 (基于 NimBLE, 比 ESP32 BLE Arduino 轻 ~300KB)
 *
 * 协议:
 *   APP 写入 "SSID|PASSWORD" 到 Char #1
 *   → 设备调 set_wifi_credentials_from_ble() (与 web /connect 同路径)
 *   → maintain_wifi() 切换到新网络
 *   → 本模块 loop 中监测 WiFi 状态变化并 Notify Char #2
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include "ble_provisioning.h"
#include "wifi_manager.h"
#include "device_info.h"

// ============================================================================
//  UUID (自造, 避免与 BLE SIG 标准 UUID 冲突)
// ============================================================================

static const char* SERVICE_UUID        = "8e7f1a01-2b3c-4d5e-9f01-aabbccddeeff";
static const char* CHAR_WIFI_CRED_UUID = "8e7f1a02-2b3c-4d5e-9f01-aabbccddeeff";  // WRITE
static const char* CHAR_STATUS_UUID    = "8e7f1a03-2b3c-4d5e-9f01-aabbccddeeff";  // READ + NOTIFY
static const char* CHAR_DEVICE_INFO_UUID = "8e7f1a04-2b3c-4d5e-9f01-aabbccddeeff"; // READ 完整设备信息 JSON

// ============================================================================
//  状态字符串 (保持短小, 减少 BLE 包负载)
// ============================================================================

static const char* ST_IDLE       = "IDLE";
static const char* ST_CONNECTING = "CONNECTING";

// 连接超时 (毫秒) — WiFi.begin 后 20 秒未连上视为失败
static const unsigned long CONNECT_TIMEOUT_MS = 30000UL;

// ============================================================================
//  模块内部状态
// ============================================================================

static NimBLEServer*          g_server      = nullptr;
static NimBLECharacteristic*   g_cred_char   = nullptr;
static NimBLECharacteristic*   g_status_char = nullptr;
static NimBLECharacteristic*   g_info_char   = nullptr;  // ★ 设备信息特征 (只读)

static String                g_current_status      = "IDLE";   // 当前对外状态
static volatile wl_status_t  g_last_wifi_status    = WL_IDLE_STATUS;
static volatile unsigned long g_connecting_start_ms = 0;       // 0 = 不在连接中

// ★ 配网进行中标志 — onWrite 设 true, CONNECTED/FAILED 设 false
//   wifi_manager.cpp 的 10 秒重连逻辑会检查此标志, 配网期间禁止 WiFi.reconnect 干扰
bool g_provisioning_active = false;

// onWrite 回调上下文不能直接调用 notify (BLE 任务栈), 用标志位延后到 loop
static String  g_pending_notify;
static volatile bool g_notify_pending = false;

// ============================================================================
//  实际推送状态 (在 loop 上下文调用)
// ============================================================================

static void do_notify(const String& status) {
  g_current_status = status;
  if (g_status_char) {
    // ★ 重复发送 5 次, 每次间隔 200ms
    //    WiFi.begin() 导致射频共存短暂中断, 首次 notify 可能丢包,
    //    重发确保 APP 至少收到一次
    g_status_char->setValue(std::string(status.c_str()));
    for (int i = 0; i < 5; i++) {
      g_status_char->notify(true);
      delay(200);
    }
    Serial.printf("[BLE] status -> %s (sent x5)\n", status.c_str());
  }
}

// ============================================================================
//  WiFi 凭证写入回调 (NimBLE 1.x onWrite 签名)
// ============================================================================

class WifiCredCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    std::string val = pChar->getValue();
    if (val.empty()) {
      Serial.println(F("[BLE] onWrite: empty value, ignored"));
      return;
    }

    // 解析 "SSID|PASSWORD" 或 "SSID,PASSWORD" (兼容 APP 端用逗号分隔)
    //   优先用 | 分隔 (协议规范), 找不到 | 再尝试 , (兼容性)
    //   ⚠ 密码含逗号时必须用 | 分隔, 否则解析错乱
    String s(val.c_str());
    int sep = s.indexOf('|');
    if (sep < 0) sep = s.indexOf(',');
    String ssid = (sep < 0) ? s : s.substring(0, sep);
    String pass = (sep < 0) ? String("") : s.substring(sep + 1);
    ssid.trim();
    pass.trim();  // 去掉 APP 可能拼入的前后空格

    Serial.printf("[BLE] onWrite: SSID=%s, passLen=%d\n",
                  ssid.c_str(), (int)pass.length());

    if (set_wifi_credentials_from_ble(ssid, pass)) {
      // 凭证已接受, 进入 CONNECTING (实际切换在 maintain_wifi 中执行)
      g_pending_notify       = ST_CONNECTING;
      g_notify_pending       = true;
      g_connecting_start_ms  = millis();
      g_provisioning_active  = true;   // ★ 配网开始, 禁止 maintain_wifi 的 10 秒重连干扰
      // ★ 重置状态变化检测基准 — 重复配网同 SSID 时, WiFi 重连后 status 仍是 WL_CONNECTED,
      //   不重置会导致 cur == g_last_wifi_status, 检测不到状态变化, 永远不发 CONNECTED notify
      g_last_wifi_status     = WL_IDLE_STATUS;
    } else {
      g_pending_notify       = String("FAILED,EMPTY_SSID");
      g_notify_pending       = true;
      g_connecting_start_ms  = 0;
    }
  }
};

// 每次读取时动态生成，确保 WiFi IP、SSID 与连接状态都是当前值。
class DeviceInfoCallback : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    String json = build_device_info_json();
    pChar->setValue(std::string(json.c_str()));
    Serial.printf("[BLE] device info read (%u bytes)\n", (unsigned)json.length());
  }
};

// ============================================================================
//  初始化 BLE 配网服务
// ============================================================================

void init_ble_provisioning() {
  // 广播名与 HTTP/AP 设备信息共用同一生成逻辑，避免两种通道结果不一致。
  String dev_name = device_ble_name();

  Serial.print(F("[BLE] Starting provisioning service, name="));
  Serial.println(dev_name);

  NimBLEDevice::init(dev_name.c_str());
  // 配网服务每次连接后重新启动广播, 便于 APP 反复配网
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // 最大发射功率

  g_server = NimBLEDevice::createServer();

  NimBLEService* service = g_server->createService(SERVICE_UUID);

  // WiFi 凭证特征 (Write)
  g_cred_char = service->createCharacteristic(
      CHAR_WIFI_CRED_UUID,
      NIMBLE_PROPERTY::WRITE);
  g_cred_char->setCallbacks(new WifiCredCallback());

  // 状态特征 (Read + Notify)
  g_status_char = service->createCharacteristic(
      CHAR_STATUS_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  g_status_char->setValue(ST_IDLE);

  // 设备信息特征 (Read only)：字段与 GET /api/device-info 完全一致。
  g_info_char = service->createCharacteristic(
      CHAR_DEVICE_INFO_UUID,
      NIMBLE_PROPERTY::READ);
  g_info_char->setCallbacks(new DeviceInfoCallback());
  g_info_char->setValue(std::string(build_device_info_json().c_str()));

  // NimBLE 2.x: 服务在 server 启动时一并启动; 配置广播后 start
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  // ★ 显式把设备名注入广播/扫描响应 (NimBLE 2.x 不会自动从 Device 拷过来)
  //    否则手机 nRF Connect 和 APP 都会显示 N/A
  adv->setName(dev_name.c_str());
  adv->addServiceUUID(SERVICE_UUID);
  adv->enableScanResponse(true);
  // ★ 缩短广播间隔 (默认约 100ms, 改成 ~50ms), 提高被手机扫到的概率
  //    间隔越短扫描越快但功耗越高, 配网场景优先可用性
  adv->setMinInterval(80);   // 单位: 0.625ms → 50ms
  adv->setMaxInterval(160);  // 单位: 0.625ms → 100ms
  adv->setPreferredParams(0x06, 0x40);  // 连接参数建议
  bool ok = adv->start();
  Serial.printf("[BLE] adv->start() returned: %s\n", ok ? "true" : "false");
  Serial.printf("[BLE] isAdvertising: %s\n",
                NimBLEDevice::getAdvertising()->isAdvertising() ? "true" : "false");
  Serial.printf("[BLE] Device address: %s\n",
                NimBLEDevice::getAddress().toString().c_str());

  Serial.println(F("[BLE] Advertising started, waiting for APP..."));
}

// ============================================================================
//  主循环维护
// ============================================================================

void ble_provisioning_loop() {
  // 1. 推送 onWrite 回调期间积压的状态通知
  if (g_notify_pending) {
    do_notify(g_pending_notify);
    g_notify_pending = false;
  }

  // 2. WiFi 状态变化检测
  wl_status_t cur = WiFi.status();
  if (cur != g_last_wifi_status) {
    if (cur == WL_CONNECTED) {
      do_notify(String("CONNECTED,") + WiFi.localIP().toString());
      g_connecting_start_ms = 0;
      g_provisioning_active = false;   // ★ 配网结束, 恢复 10 秒重连逻辑
    } else if (cur == WL_NO_SSID_AVAIL) {
      do_notify("FAILED,SSID_NOT_FOUND");
      g_connecting_start_ms = 0;
      g_provisioning_active = false;
    } else if (cur == WL_CONNECT_FAILED) {
      do_notify("FAILED,CONNECT_FAILED");
      g_connecting_start_ms = 0;
      g_provisioning_active = false;
    }
    g_last_wifi_status = cur;
  }

  // 3. 超时检测: CONNECTING 超过 30 秒视为失败
  if (g_connecting_start_ms > 0 &&
      millis() - g_connecting_start_ms > CONNECT_TIMEOUT_MS) {
    do_notify("FAILED,TIMEOUT");
    g_connecting_start_ms = 0;
    g_provisioning_active = false;   // ★ 配网超时结束
  }

  // 4. 广播自愈: WiFi 切换/连接可能让 NimBLE 广播停掉, 检测并重启
  static unsigned long last_adv_check_ms = 0;
  unsigned long now = millis();
  if (now - last_adv_check_ms > 5000) {
    last_adv_check_ms = now;
    bool advertising = NimBLEDevice::getAdvertising()->isAdvertising();
    static bool last_advertising = advertising;
    if (!advertising) {
      Serial.println(F("[BLE] Adv stopped, restarting..."));
      NimBLEDevice::getAdvertising()->start();
    }
    if (advertising != last_advertising) {
      Serial.printf("[BLE] Adv state changed: %s -> %s\n",
                    last_advertising ? "true" : "false",
                    advertising ? "true" : "false");
      last_advertising = advertising;
    }
    // 周期性心跳 (调试用, 每 30 秒打印一次状态)
    static unsigned long last_heartbeat_ms = 0;
    if (now - last_heartbeat_ms > 30000) {
      last_heartbeat_ms = now;
      Serial.printf("[BLE] heartbeat: adv=%s, status=%s\n",
                    advertising ? "on" : "off",
                    g_current_status.c_str());
    }
  }
}

// ============================================================================
//  当前状态查询 (供调试)
// ============================================================================

String ble_provisioning_status() {
  return g_current_status;
}
