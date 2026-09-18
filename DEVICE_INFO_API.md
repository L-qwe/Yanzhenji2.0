# 设备信息接口

该接口用于读取设备 WiFi MAC、BLE 名称、当前固件版本、固件类型、WiFi SSID、STA 联网 IP、HTTP 端口和实时 WiFi 状态。HTTP、AP 和 BLE 返回相同的 UTF-8 JSON 数据。

## 返回数据

连接 WiFi 时的示例：

```json
{
  "mac_address": "70:4B:CA:03:44:6C",
  "firmware_version": "v0.2.1",
  "firmware_type": "验针机",
  "ble_name": "YanZhenJi_704BCA03446C",
  "wifi_ssid": "Factory-WiFi",
  "wifi_ip": "192.168.1.123",
  "wifi_port": 80,
  "wifi_connected": true,
  "wifi_status": "connected"
}
```

未连接 WiFi 时，`wifi_ip` 为空字符串，`wifi_connected` 为 `false`：

```json
{
  "mac_address": "70:4B:CA:03:44:6C",
  "firmware_version": "v0.2.1",
  "firmware_type": "验针机",
  "ble_name": "YanZhenJi_704BCA03446C",
  "wifi_ssid": "",
  "wifi_ip": "",
  "wifi_port": 80,
  "wifi_connected": false,
  "wifi_status": "disconnected"
}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `mac_address` | string | WiFi MAC，格式为带冒号的大写地址；与 MQTT 使用的设备 MAC 一致 |
| `firmware_version` | string | 当前运行固件版本 |
| `firmware_type` | string | 固件类型，当前固定为 `验针机` |
| `ble_name` | string | 设备 BLE 广播名称 |
| `wifi_ssid` | string | 当前连接的 WiFi SSID；未连接时为 `""` |
| `wifi_ip` | string | STA 连接成功后取得的 IP；未连接时为 `""` |
| `wifi_port` | number | 设备 HTTP 服务端口，当前为 `80` |
| `wifi_connected` | boolean | WiFi STA 是否已连接 |
| `wifi_status` | string | WiFi 实时状态，取值见下表 |

`wifi_status` 可能值：`idle`、`no_ssid_available`、`scan_completed`、`connected`、`connect_failed`、`connection_lost`、`disconnected`、`unknown`。

## HTTP（设备已连接的 WiFi）

- 方法：`GET`
- URL：`http://<wifi_ip>:80/api/device-info`
- 成功状态：`200 OK`
- Content-Type：`application/json; charset=utf-8`
- 缓存：`Cache-Control: no-store`
- 跨域：`Access-Control-Allow-Origin: *`

PowerShell 7 调用示例：

```powershell
Invoke-RestMethod -Method Get -Uri 'http://192.168.1.123:80/api/device-info'
```

## AP 热点方式

手机或电脑先连接设备热点 `YanZhenJi_<MAC无冒号>`，再调用：

- 方法：`GET`
- URL：`http://192.168.4.1:80/api/device-info`

```powershell
Invoke-RestMethod -Method Get -Uri 'http://192.168.4.1:80/api/device-info'
```

AP 与普通 HTTP 使用同一路由和相同响应。即使 STA 尚未连上外部 WiFi，也能通过 AP 读取接口；此时 `wifi_ip` 为空且 `wifi_connected` 为 `false`。

## BLE 蓝牙方式

扫描并连接名称为 `YanZhenJi_<WiFi MAC无冒号>` 的设备，然后读取以下 GATT 特征值：

| 项目 | UUID | 属性 | 数据 |
|---|---|---|---|
| Service | `8e7f1a01-2b3c-4d5e-9f01-aabbccddeeff` | Primary Service | 配网与设备信息服务 |
| WiFi 凭证 | `8e7f1a02-2b3c-4d5e-9f01-aabbccddeeff` | Write | 现有配网接口：`SSID|PASSWORD` |
| 配网状态 | `8e7f1a03-2b3c-4d5e-9f01-aabbccddeeff` | Read + Notify | 现有状态接口，如 `CONNECTED,192.168.1.123` |
| **设备信息** | **`8e7f1a04-2b3c-4d5e-9f01-aabbccddeeff`** | **Read** | 与 HTTP 完全相同的 UTF-8 JSON |

每次读取设备信息特征值时，固件会实时重新获取 WiFi 状态和 IP。JSON 长度超过传统 BLE 单包的 20 字节，客户端应使用标准 GATT 长读取；Android/iOS BLE API 和 nRF Connect 均可按特征值读取，不要在应用层只保留第一个 ATT 数据片段。

固件升级后，如果手机仍显示旧的 GATT 特征列表，请断开设备并刷新服务；必要时清除系统蓝牙 GATT 缓存后重连。

## 设备主动上报 IoT 平台

设备连上 WiFi 后会在独立 FreeRTOS 任务中主动调用：

- 方法：`POST`
- URL：`http://10.0.100.65:8080/anton/device-access/report`
- Content-Type：`application/json; charset=utf-8`
- 鉴权：无
- 首次/重新联网：立即上报
- 上报成功：每 60 秒刷新一次
- 上报失败：每 15 秒重试一次

请求仅包含平台要求的 7 个字段：`mac_address`、`firmware_version`、
`firmware_type`、`wifi_ip`、`wifi_port`、`wifi_connected`、`wifi_status`。
设备未连接 WiFi 时不会发送。HTTP 请求在后台任务中执行，不阻塞报警采集、BLE、
MQTT 和设备 Web 服务。

串口日志前缀为 `[DEVICE-ACCESS]`。成功时输出 `report OK`；平台拒绝时输出 HTTP
状态码及截断后的响应；网络请求失败时输出底层错误原因。

## 配置位置

- 固件版本与类型：`src/config.cpp` 中的 `FIRMWARE_VERSION`、`FIRMWARE_TYPE`
- HTTP 端口：`src/config.cpp` 中的 `HTTP_SERVER_PORT`
- JSON 生成：`src/device_info.cpp`
- HTTP/AP 路由：`src/wifi_manager.cpp`
- BLE 特征：`src/ble_provisioning.cpp`
- IoT 平台地址及周期：`src/config.cpp`
- 主动上报实现：`src/device_access_reporter.cpp`
