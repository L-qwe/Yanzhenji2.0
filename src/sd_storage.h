/*
 * sd_storage.h
 * ============================================================================
 * SD 卡存储管理 — 挂载/热插拔、追加记录、FIFO 补传队列。
 * 报警历史只追加不定时清理，实现跨重启的长期保存。
 * ============================================================================
 */

#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <Arduino.h>
#include <FS.h>

// 初始化 SPI 与卡检测；无卡时不会阻塞启动。
bool init_sd_storage();

// 在 loop 中调用，检测拔卡/插卡并自动卸载/重挂载。
void sd_storage_loop();

bool sd_storage_ready();
bool storage_file_exists(const char* path);
File open_storage_file(const char* path, const char* mode);
bool remove_storage_file(const char* path);

// 追加一条报警记录并立即 flush；返回是否成功落盘。
bool append_record_to_sd(const String& record, const char* path);

// FIFO 删除第一行。使用备份文件，异常失败时恢复原队列。
bool remove_first_line(const char* path);

#endif  // SD_STORAGE_H
