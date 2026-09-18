# 验针机报警监控 2.0 设计说明

## 与 1.x 的核心差异

- 报警历史从 ESP32 Flash/SPIFFS 迁移到外置 SD 卡。
- `/history.txt` 永久追加保存，固件不再执行按天数清理。
- `/records.txt` 只作为离线 MQTT 补传队列；补传成功才删除队首。
- 补传队列改写前先生成 `.bak`，若改写中断，下次挂载自动恢复。
- GPIO32 监控 Micro SD 卡座 CD，支持运行时拔卡/插卡和自动重挂载。
- GPIO35 监控 BQ24074 PGOOD#，掉电/恢复均产生 MQTT 状态事件。

## SD 文件格式

两份文件均为 UTF-8 文本，每行格式：

```text
<首次触发时的millis>|<YYYY-MM-DD HH:mm:ss.SSS>|<2.5秒分组内报警次数>
```

例如：

```text
123456|2026-09-18 10:25:31.127|2
```

`/history.txt` 是完整历史，不因 MQTT 上报成功而删除；`/records.txt` 只保存离线期间
尚未成功上报的记录。

## PGOOD# 逻辑

BQ24074 `PGOOD#` 是开漏低有效信号，底板通过 R23 以 10K 上拉到 3.3V：

| GPIO35 | 含义 | MQTT |
|---|---|---|
| LOW | 外部 5V 有效，市电供电 | `power_source=mains`, `power_lost=false` |
| HIGH | 外部 5V 无效，已切换电池 | `power_source=battery`, `power_lost=true` |

固件使用 100ms 消抖。状态变化立即发布；MQTT 重连时也会再次发布当前状态。

## 失败行为

- 无 SD 卡：设备继续报警采集和在线 MQTT 上报，但串口明确记录落盘失败。
- SD 写入失败：不会伪报“已保存”；历史与补传队列分别输出成功/失败状态。
- SD 在 FIFO 更新中掉电：保留 `/records.txt.bak`，下次挂载时恢复原队列。
- MQTT 离线：报警同时写入永久历史和补传队列；恢复联网后逐条补传。
