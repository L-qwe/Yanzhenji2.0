/*
 * ota_handler.h
 * ============================================================================
 * OTA 远程固件升级 — MQTT 指令触发 HTTP 下载 + Update 刷写
 *
 * 旧格式: {"cmd":"ota","url":"http://server/firmware.bin"}
 * 新格式: {"msg_type":"firmware_upgrade","firmware":{"file_url":"...","file_md5":"...","file_size":...}}
 * ============================================================================
 */

#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <Arduino.h>

// 从 HTTP URL 下载固件并刷写，成功后自动重启
// expected_md5:  非空则下载后校验 MD5
// expected_size: >0 则下载后校验文件大小
void ota_from_url(const String& url,
                  const String& expected_md5 = "",
                  long expected_size = 0);

#endif  // OTA_HANDLER_H
