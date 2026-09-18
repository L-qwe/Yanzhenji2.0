/*
 * sd_storage.cpp
 * ============================================================================
 * TF1 原理图映射:
 *   SCLK=GPIO18, MISO=GPIO19, MOSI=GPIO23, CS=GPIO27, CD=GPIO32(LOW=插卡)
 * ============================================================================
 */

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "config.h"
#include "sd_storage.h"

namespace {

bool g_sd_mounted = false;
bool g_card_was_inserted = false;
unsigned long g_last_mount_attempt_ms = 0;

bool card_inserted() {
  return digitalRead(SD_CARD_DETECT_PIN) == LOW;
}

void recover_interrupted_queue_update() {
  String backup = String(RECORDS_FILE) + F(".bak");
  if (!SD.exists(backup.c_str())) return;

  // .bak 存在说明上次 FIFO 改写未完整结束；备份始终是可信原文件。
  if (SD.exists(RECORDS_FILE)) SD.remove(RECORDS_FILE);
  if (SD.rename(backup.c_str(), RECORDS_FILE)) {
    Serial.println(F("[SD] Recovered retransmit queue from backup"));
  } else {
    Serial.println(F("[SD] ERROR: Failed to recover retransmit queue"));
  }
}

bool mount_sd() {
  if (!card_inserted()) return false;

  g_last_mount_attempt_ms = millis();
  Serial.print(F("[SD] Mounting... "));
  if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_FREQUENCY_HZ)) {
    Serial.println(F("FAILED"));
    g_sd_mounted = false;
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    Serial.println(F("no card"));
    SD.end();
    g_sd_mounted = false;
    return false;
  }

  g_sd_mounted = true;
  const uint64_t total_mb = SD.cardSize() / (1024ULL * 1024ULL);
  const uint64_t used_mb = SD.usedBytes() / (1024ULL * 1024ULL);
  Serial.printf("OK, size=%lluMB, used=%lluMB\n", total_mb, used_mb);
  recover_interrupted_queue_update();
  return true;
}

bool ensure_sd_ready() {
  if (!card_inserted()) {
    if (g_sd_mounted) {
      SD.end();
      g_sd_mounted = false;
      Serial.println(F("[SD] Card removed"));
    }
    return false;
  }
  if (g_sd_mounted) return true;
  if (g_last_mount_attempt_ms != 0 &&
      (unsigned long)(millis() - g_last_mount_attempt_ms) < 5000UL) {
    return false;
  }
  return mount_sd();
}

}  // namespace

bool init_sd_storage() {
  pinMode(SD_CARD_DETECT_PIN, INPUT_PULLUP);
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

  g_card_was_inserted = card_inserted();
  if (!g_card_was_inserted) {
    Serial.println(F("[SD] No card inserted; monitoring continues without storage"));
    return false;
  }
  return mount_sd();
}

void sd_storage_loop() {
  static unsigned long last_check_ms = 0;
  const unsigned long now = millis();
  if ((unsigned long)(now - last_check_ms) < 500UL) return;
  last_check_ms = now;

  const bool inserted = card_inserted();
  if (!inserted && g_card_was_inserted) {
    if (g_sd_mounted) SD.end();
    g_sd_mounted = false;
    Serial.println(F("[SD] Card removed"));
  } else if (inserted && !g_card_was_inserted) {
    Serial.println(F("[SD] Card inserted"));
    if (!g_sd_mounted) mount_sd();
  } else if (inserted && !g_sd_mounted &&
             (unsigned long)(now - g_last_mount_attempt_ms) >= 5000UL) {
    mount_sd();
  }
  g_card_was_inserted = inserted;
}

bool sd_storage_ready() {
  return ensure_sd_ready();
}

bool storage_file_exists(const char* path) {
  return ensure_sd_ready() && SD.exists(path);
}

File open_storage_file(const char* path, const char* mode) {
  if (!ensure_sd_ready()) return File();
  return SD.open(path, mode);
}

bool remove_storage_file(const char* path) {
  if (!ensure_sd_ready()) return false;
  if (!SD.exists(path)) return true;
  return SD.remove(path);
}

bool append_record_to_sd(const String& record, const char* path) {
  if (!ensure_sd_ready()) {
    Serial.println(F("[SD] ERROR: Alarm record not saved (card unavailable)"));
    return false;
  }

  File file = SD.open(path, FILE_APPEND);
  if (!file) {
    Serial.printf("[SD] ERROR: Cannot append %s\n", path);
    return false;
  }
  const size_t written = file.println(record);
  file.flush();
  file.close();

  if (written == 0) {
    Serial.printf("[SD] ERROR: Write failed for %s\n", path);
    return false;
  }
  Serial.printf("[SD] Appended %s: %s\n", path, record.c_str());
  return true;
}

bool remove_first_line(const char* path) {
  if (!ensure_sd_ready() || !SD.exists(path)) return false;

  const String backup = String(path) + F(".bak");
  if (SD.exists(backup.c_str())) SD.remove(backup.c_str());
  if (!SD.rename(path, backup.c_str())) {
    Serial.printf("[SD] ERROR: Cannot back up %s\n", path);
    return false;
  }

  File input = SD.open(backup.c_str(), FILE_READ);
  File output = SD.open(path, FILE_WRITE);
  if (!input || !output) {
    if (input) input.close();
    if (output) output.close();
    SD.remove(path);
    SD.rename(backup.c_str(), path);
    Serial.println(F("[SD] ERROR: Cannot rewrite retransmit queue"));
    return false;
  }

  input.readStringUntil('\n');  // 丢弃队首
  uint8_t buffer[512];
  bool write_ok = true;
  size_t remaining_bytes = 0;
  while (input.available()) {
    const size_t read_count = input.read(buffer, sizeof(buffer));
    if (read_count == 0) break;
    remaining_bytes += read_count;
    if (output.write(buffer, read_count) != read_count) {
      write_ok = false;
      break;
    }
  }
  output.flush();
  input.close();
  output.close();

  if (!write_ok) {
    SD.remove(path);
    SD.rename(backup.c_str(), path);
    Serial.println(F("[SD] ERROR: Queue rewrite failed; original restored"));
    return false;
  }

  SD.remove(backup.c_str());
  if (remaining_bytes == 0) {
    SD.remove(path);
    Serial.println(F("[SD] Retransmit queue empty; file removed"));
  } else {
    Serial.println(F("[SD] First retransmit record removed"));
  }
  return true;
}
