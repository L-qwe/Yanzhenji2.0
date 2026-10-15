/*
 * sd_storage.cpp
 * ============================================================================
 * Micro SD（原理图器件 TF1）映射:
 *   SCLK=GPIO18, MISO=GPIO19, MOSI=GPIO23, CS=GPIO27, CD=GPIO32(LOW=插卡)
 * ============================================================================
 */

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <algorithm>

#include "config.h"
#include "sd_storage.h"

namespace {

bool g_sd_mounted = false;
bool g_flash_ready = false;
bool g_card_was_inserted = false;
unsigned long g_last_mount_attempt_ms = 0;
unsigned long g_last_migration_attempt_ms = 0;

bool card_inserted() {
  return digitalRead(SD_CARD_DETECT_PIN) == LOW;
}

bool append_to_flash(const String& record, const char* path) {
  if (!g_flash_ready) {
    Serial.printf("[FLASH] ERROR: Cannot cache %s; SPIFFS unavailable\n", path);
    return false;
  }

  File file = SPIFFS.open(path, FILE_APPEND);
  if (!file) {
    Serial.printf("[FLASH] ERROR: Cannot open %s\n", path);
    return false;
  }

  const size_t written = file.println(record);
  file.flush();
  file.close();
  if (written == 0) {
    Serial.printf("[FLASH] ERROR: Write failed for %s\n", path);
    return false;
  }

  Serial.printf("[FLASH] Cached %s until SD is available: %s\n",
                path, record.c_str());
  return true;
}

bool migrate_flash_file_to_sd(const char* path) {
  if (!g_flash_ready || !g_sd_mounted || !SPIFFS.exists(path)) return true;

  File input = SPIFFS.open(path, FILE_READ);
  if (!input) {
    Serial.printf("[FLASH->SD] ERROR: Cannot read %s\n", path);
    return false;
  }

  File output = SD.open(path, FILE_APPEND);
  if (!output) {
    input.close();
    Serial.printf("[FLASH->SD] ERROR: Cannot append %s on SD\n", path);
    return false;
  }

  size_t migrated = 0;
  bool write_ok = true;
  while (input.available()) {
    String line = input.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (output.println(line) == 0) {
      write_ok = false;
      break;
    }
    migrated++;
    yield();
  }

  output.flush();
  input.close();
  output.close();

  // 写入失败时保留完整 Flash 文件，优先保证记录不丢失；下次继续重试。
  if (!write_ok) {
    Serial.printf("[FLASH->SD] ERROR: Migration interrupted for %s; Flash copy kept\n",
                  path);
    return false;
  }

  if (!SPIFFS.remove(path)) {
    Serial.printf("[FLASH->SD] ERROR: Migrated %s but could not remove Flash copy\n",
                  path);
    return false;
  }

  Serial.printf("[FLASH->SD] Migrated %u record(s) to SD %s\n",
                static_cast<unsigned int>(migrated), path);
  return true;
}

void migrate_flash_records_to_sd() {
  if (!g_flash_ready || !g_sd_mounted) return;
  migrate_flash_file_to_sd(HISTORY_FILE);
  migrate_flash_file_to_sd(RECORDS_FILE);
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

void recover_interrupted_history_delete() {
  String backup = String(HISTORY_FILE) + F(".delete.bak");
  if (!SD.exists(backup.c_str())) return;

  // 备份仍存在表示删除改写未完整提交，恢复删除前的完整历史。
  if (SD.exists(HISTORY_FILE)) SD.remove(HISTORY_FILE);
  if (SD.rename(backup.c_str(), HISTORY_FILE)) {
    Serial.println(F("[SD] Recovered history from interrupted delete"));
  } else {
    Serial.println(F("[SD] ERROR: Failed to recover history delete backup"));
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
  recover_interrupted_history_delete();
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
  g_flash_ready = SPIFFS.begin(true);
  if (g_flash_ready) {
    Serial.printf("[FLASH] SPIFFS ready, used=%u/%u bytes\n",
                  static_cast<unsigned int>(SPIFFS.usedBytes()),
                  static_cast<unsigned int>(SPIFFS.totalBytes()));
  } else {
    Serial.println(F("[FLASH] ERROR: SPIFFS mount failed; SD fallback unavailable"));
  }

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

  // SD 恢复后迁移 Flash 暂存。失败时每 5 秒重试，Flash 原文件保留。
  if (g_sd_mounted && g_flash_ready &&
      (unsigned long)(now - g_last_migration_attempt_ms) >= 5000UL) {
    g_last_migration_attempt_ms = now;
    migrate_flash_records_to_sd();
  }
}

bool sd_storage_ready() {
  return ensure_sd_ready();
}

bool sd_storage_connected() {
  return g_sd_mounted && card_inserted();
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

bool remove_storage_lines(const char* path,
                          const std::vector<size_t>& line_indices) {
  if (line_indices.empty()) return true;
  if (!ensure_sd_ready() || !SD.exists(path)) return false;

  std::vector<size_t> selected = line_indices;
  std::sort(selected.begin(), selected.end());
  selected.erase(std::unique(selected.begin(), selected.end()), selected.end());

  const String backup = String(path) + F(".delete.bak");
  if (SD.exists(backup.c_str())) {
    // 上次操作若留下备份，先恢复到删除前状态，再执行本次请求。
    if (SD.exists(path)) SD.remove(path);
    if (!SD.rename(backup.c_str(), path)) {
      Serial.printf("[SD] ERROR: Cannot recover delete backup for %s\n", path);
      return false;
    }
  }

  if (!SD.rename(path, backup.c_str())) {
    Serial.printf("[SD] ERROR: Cannot back up %s before delete\n", path);
    return false;
  }

  File input = SD.open(backup.c_str(), FILE_READ);
  File output = SD.open(path, FILE_WRITE);
  if (!input || !output) {
    if (input) input.close();
    if (output) output.close();
    SD.remove(path);
    SD.rename(backup.c_str(), path);
    Serial.printf("[SD] ERROR: Cannot rewrite %s for delete\n", path);
    return false;
  }

  size_t physical_line = 0;
  size_t selected_pos = 0;
  size_t deleted = 0;
  size_t kept = 0;
  bool write_ok = true;

  while (input.available()) {
    String line = input.readStringUntil('\n');
    line.trim();

    while (selected_pos < selected.size() &&
           selected[selected_pos] < physical_line) {
      selected_pos++;
    }
    const bool should_delete = selected_pos < selected.size() &&
                               selected[selected_pos] == physical_line;
    if (should_delete) {
      deleted++;
      selected_pos++;
    } else if (line.length() > 0) {
      if (output.println(line) == 0) {
        write_ok = false;
        break;
      }
      kept++;
    }
    physical_line++;
    yield();
  }

  output.flush();
  input.close();
  output.close();

  if (!write_ok || deleted == 0) {
    SD.remove(path);
    SD.rename(backup.c_str(), path);
    Serial.printf("[SD] ERROR: History delete failed for %s\n", path);
    return false;
  }

  if (!SD.remove(backup.c_str())) {
    SD.remove(path);
    SD.rename(backup.c_str(), path);
    Serial.printf("[SD] ERROR: Cannot commit history delete for %s\n", path);
    return false;
  }

  if (kept == 0) SD.remove(path);
  Serial.printf("[SD] Deleted %u record(s) from %s\n",
                static_cast<unsigned int>(deleted), path);
  return true;
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

bool append_record_with_flash_fallback(const String& record, const char* path) {
  if (append_record_to_sd(record, path)) return true;
  return append_to_flash(record, path);
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
