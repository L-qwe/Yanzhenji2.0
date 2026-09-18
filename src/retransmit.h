/*
 * retransmit.h
 * ============================================================================
 * 断网续传 — FIFO 从 SD 卡读取缓存记录并补传到 MQTT
 * ============================================================================
 */

#ifndef RETRANSMIT_H
#define RETRANSMIT_H

// 处理补传 (loop 中调用, 每次最多处理1条, 非阻塞)
void process_retransmit();

#endif  // RETRANSMIT_H
