/*
 * device_access_reporter.h
 * ============================================================================
 * 设备主动向 IoT 平台登记并周期刷新设备信息。
 * ============================================================================
 */

#ifndef DEVICE_ACCESS_REPORTER_H
#define DEVICE_ACCESS_REPORTER_H

// 在 loop 中周期调用。WiFi 首次连上/重新连上时立即上报，之后定时刷新。
void device_access_report_loop();

#endif  // DEVICE_ACCESS_REPORTER_H
