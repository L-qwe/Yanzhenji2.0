/*
 * sd_storage.h
 * ============================================================================
 * SD 卡存储管理 — 挂载/热插拔、Flash 暂存迁移、FIFO 补传队列。
 * 报警历史只追加不定时清理，实现跨重启的长期保存。
 * ============================================================================
 */

#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <Arduino.h>
#include <FS.h>
#include <vector>

// 初始化 SPI 与卡检测；无卡时不会阻塞启动。
bool init_sd_storage();

// 在 loop 中调用，检测拔卡/插卡并自动卸载/重挂载。
void sd_storage_loop();

bool sd_storage_ready();

// 只读取当前状态，不触发挂载；true 表示 SD 已插入且成功挂载可用。
bool sd_storage_connected();
bool storage_file_exists(const char* path);
File open_storage_file(const char* path, const char* mode);
bool remove_storage_file(const char* path);

// 按文件中的物理行号（从 0 开始）批量删除，使用备份文件防止改写失败丢数据。
bool remove_storage_lines(const char* path, const std::vector<size_t>& line_indices);

// 追加一条报警记录并立即 flush；返回是否成功落盘。
bool append_record_to_sd(const String& record, const char* path);

// 优先写 SD；SD 不可用时写入内部 Flash，待 SD 挂载后自动迁移。
bool append_record_with_flash_fallback(const String& record, const char* path);

// FIFO 删除第一行。使用备份文件，异常失败时恢复原队列。
bool remove_first_line(const char* path);

#endif  // SD_STORAGE_H
