/*
 * ota_handler.cpp
 * ============================================================================
 * OTA 远程固件升级实现 — HTTP GET 下载 + Update 刷写 + MD5/大小校验
 * 分块下载, 每块喂狗, 避免看门狗超时复位
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/md5.h>
#include "ota_handler.h"
#include "esp_task_wdt.h"

// ============================================================================
//  从 HTTP URL 下载并刷写固件 (可选 MD5 + 大小校验)
// ============================================================================

void ota_from_url(const String& url,
                  const String& expected_md5,
                  long expected_size) {
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("[OTA] Starting OTA update..."));
  Serial.print(F("[OTA] URL: "));
  Serial.println(url);
  if (expected_md5.length() > 0) {
    Serial.printf("[OTA] Expected MD5:  %s\n", expected_md5.c_str());
  }
  if (expected_size > 0) {
    Serial.printf("[OTA] Expected Size: %ld bytes\n", expected_size);
  }

  // ---- 1. 建立 HTTP 连接 ----
  HTTPClient http;
  http.setTimeout(30000);
  http.begin(url);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[OTA] ERROR: HTTP GET failed, code=%d\n", httpCode);
    if (httpCode > 0) {
      Serial.print(F("[OTA] Response: "));
      Serial.println(http.getString());
    } else {
      Serial.printf("[OTA] Error string: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
    return;
  }

  // ---- 2. 获取固件大小 ----
  int contentLength = http.getSize();
  if (contentLength <= 0) {
    Serial.println(F("[OTA] ERROR: Invalid Content-Length, cannot proceed"));
    http.end();
    return;
  }

  // 大小预校验
  if (expected_size > 0 && contentLength != expected_size) {
    Serial.printf("[OTA] ERROR: Size mismatch! Expected %ld, got %d\n",
                  expected_size, contentLength);
    http.end();
    return;
  }

  Serial.printf("[OTA] Firmware size: %d bytes (%.1f KB)\n",
                contentLength, contentLength / 1024.0);

  // ---- 3. 检查 Flash 空间 ----
  if (!Update.begin(contentLength)) {
    Serial.printf("[OTA] ERROR: Update.begin() failed — not enough space? "
                  "Error: %s\n", Update.errorString());
    http.end();
    return;
  }

  // ---- 4. 分块下载并写入, 每块喂狗, 同时计算 MD5 ----
  Serial.println(F("[OTA] Downloading & flashing (chunked, WDT safe)..."));
  WiFiClient* stream = http.getStreamPtr();

  uint8_t buf[4096];
  size_t written = 0;
  int prev_pct = -1;

  // MD5 上下文
  mbedtls_md5_context md5_ctx;
  mbedtls_md5_init(&md5_ctx);
  mbedtls_md5_starts_ret(&md5_ctx);

  while (written < (size_t)contentLength && http.connected()) {
    int len = stream->read(buf, sizeof(buf));
    if (len > 0) {
      Update.write(buf, len);
      mbedtls_md5_update(&md5_ctx, buf, len);
      written += len;

      esp_task_wdt_reset();

      int pct = (written * 100) / contentLength;
      if (pct / 10 > prev_pct / 10) {
        Serial.printf("[OTA] Progress: %d%% (%d / %d bytes)\n",
                      pct, written, contentLength);
        prev_pct = pct;
      }
    } else if (len < 0) {
      Serial.println(F("[OTA] ERROR: Stream read failed"));
      mbedtls_md5_free(&md5_ctx);
      http.end();
      return;
    }
    yield();
  }

  // 完成 MD5
  unsigned char md5_hash[16];
  mbedtls_md5_finish(&md5_ctx, md5_hash);
  mbedtls_md5_free(&md5_ctx);

  char md5_str[33];
  for (int i = 0; i < 16; i++) {
    sprintf(md5_str + i * 2, "%02x", md5_hash[i]);
  }
  md5_str[32] = '\0';
  String computed_md5 = String(md5_str);

  Serial.printf("[OTA] Computed MD5: %s\n", computed_md5.c_str());

  // MD5 校验
  if (expected_md5.length() > 0) {
    if (!computed_md5.equalsIgnoreCase(expected_md5)) {
      Serial.printf("[OTA] ERROR: MD5 mismatch!\n");
      Serial.printf("[OTA]   Expected: %s\n", expected_md5.c_str());
      Serial.printf("[OTA]   Got:      %s\n", computed_md5.c_str());
      http.end();
      return;
    }
    Serial.println(F("[OTA] MD5 check: PASS"));
  }

  // 大小校验
  if (written != (size_t)contentLength) {
    Serial.printf("[OTA] ERROR: Size mismatch! Expected %d, got %d. "
                  "Error: %s\n",
                  contentLength, written, Update.errorString());
    http.end();
    return;
  }

  // ---- 5. 验证并完成 ----
  if (!Update.end()) {
    Serial.printf("[OTA] ERROR: Update.end() failed! Error: %s\n",
                  Update.errorString());
    http.end();
    return;
  }

  if (!Update.isFinished()) {
    Serial.println(F("[OTA] ERROR: Update not finished! Aborting."));
    http.end();
    return;
  }

  http.end();

  Serial.println(F("[OTA] ========================================"));
  Serial.println(F("[OTA] SUCCESS! Firmware flashed."));
  Serial.println(F("[OTA] Rebooting in 2 seconds..."));
  Serial.println(F("[OTA] ========================================"));
  Serial.println();

  delay(2000);
  ESP.restart();
}
