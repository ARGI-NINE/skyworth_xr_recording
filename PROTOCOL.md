# EgoCollect 通讯协议文档

> 版本：v1.4  
> 日期：2026-07-14  
> 适用：当前 SDK 1.5.0 设备端 / 手机端联调

---

## 目录

1. [系统架构](#1-系统架构)
2. [BLE 蓝牙协议](#2-ble-蓝牙协议)
3. [WiFi 控制与状态协议](#3-wifi-控制与状态协议)
4. [视频流协议](#4-视频流协议)
5. [完整工作流程](#5-完整工作流程)
6. [故障码定义](#6-故障码定义)

---

## 1. 系统架构

```
┌─────────────────────────────────────────────────────┐
│                   Android 手机 App                    │
│                                                     │
│  ┌──────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ BLE 配网  │  │ TCP 控制通道  │  │ TCP 视频通道   │  │
│  │ (GATT)   │  │ (端口 8801)   │  │ (端口 8802)   │  │
│  └────┬─────┘  └──────┬───────┘  └──────┬────────┘  │
└───────┼───────────────┼─────────────────┼───────────┘
        │               │                 │
   ┌────┴────┐    ┌─────┴──────┐   ┌──────┴──────────┐
   │ 蓝牙 BLE │    │  WiFi TCP  │   │  WiFi TCP       │
   │ 外设模式  │    │  Server    │   │  Server         │
   └─────────┘    └────────────┘   └─────────────────┘
        │               │                 │
   ┌────┴───────────────┴─────────────────┴──────────┐
   │              采集设备 (Android / SDK)             │
   │                                                  │
   │  ┌──────────┐ ┌──────────┐ ┌──────────────────┐  │
   │  │ BLE GATT │ │ TCP Srv  │ │ H.265/HEVC 推流   │  │
   │  │ Server   │ │ :8801    │ │ RGB only / :8802 │  │
   │  └──────────┘ └──────────┘ └──────────────────┘  │
   │                                                  │
   │  WiFi STA 模式 (连路由器) + BLE Peripheral       │
   └──────────────────────────────────────────────────┘
```

### 1.1 三个通讯阶段

| 阶段 | 通讯方式 | 用途 | 持续时长 |
|------|---------|------|---------|
| **配网** | BLE GATT | 手机发送 WiFi SSID + 密码 | 一次性，约 5-20 秒 |
| **控制** | TCP :8801 | 发送指令、接收状态和故障 | 持久连接 |
| **视频** | TCP :8802 | 接收 RGB H.265 / HEVC 视频流 | 按需开启/关闭 |

### 1.2 端口分配

| 端口 | 协议 | 方向 | 说明 |
|------|------|------|------|
| BLE FFE0 Service | GATT Write/Notify | 双向 | WiFi 配网服务 |
| BLE Time Sync Service | GATT Write/Notify | 双向 | BLE 四时间戳时间同步与控制服务 |
| TCP 8801 | EG 帧 + Protobuf + UTF-8 JSON | 双向 | 控制指令、状态上报、故障告警、`custom_ntp` 时间交换 |
| TCP 8802 | HEVC Annex-B 裸流 | 设备→手机 | RGB 实时预览 |

### 1.3 时间同步路径总览

本协议定义两条完全独立的时间同步路径，二者的角色、算法和结果语义必须严格分开：

| 路径 | 承载 | 主要对接方 | 时间语义 | 结果计算方 |
|------|------|------------|----------|------------|
| `TCP/NTP` | 复用 `TCP 8801` 控制连接；EG 帧负责启停/查询，UTF-8 JSON 负责时间交换 | 手机 App / NTP client | UTC / Unix epoch 时间 | `client` |
| `BLE/四时间戳` | BLE Time Sync Service 上的 JSON 命令 | Linux 设备 / BLE initiator | UTC ns 协议时间戳 | `initiator` |

强制约束：

- `TCP/NTP` 同步的是 UTC / epoch 时间，不是任何设备的内部单调时钟。
- `TCP/NTP` 路径中，SDK 是 NTP `server`，发起端是 NTP `client`，offset / RTT 必须由 `client` 根据本项目自定义时间交换结果自行计算。
- `TCP/NTP` 路径中的 `CMD_SET_PARAM` / `CMD_GET_PARAM` 与 `custom_ntp` JSON 时间交换都复用同一条 `TCP 8801` 控制连接；不得写入或要求外部通用 NTP 规范中的 UDP/123、独立端口或外部标准报文格式。
- `BLE/四时间戳` 路径中的 `offset_ns`、`round_trip_ns`、统计结果与 `sync_result` 只属于 BLE 时间同步协议；`TCP/NTP` 路径只交换 `t1/t2/t3` 原始 UTC 时间戳并由 `client` 本地记录 `t4`。
- `utc 与某台设备内部单调时钟的差值` 与 `跨设备同步计算出的 offset` 是不同概念，协议中不得混写、不得等同、不得要求对端理解另一台设备的内部时钟实现。
- 两条路径都只处理 UTC 语义，不引入 `boottime`、单调时钟或启动时长语义。

---

## 2. BLE 蓝牙协议

### 2.1 BLE 配网 GATT 服务

```
Service UUID: 0000FFE0-0000-1000-8000-00805F9B34FB

Characteristics:
┌──────────┬──────────────────────────┬───────┬──────────────┐
│ UUID     │ 用途                      │ 属性   │ 数据格式       │
├──────────┼──────────────────────────┼───────┼──────────────┤
│ FFE1     │ WiFi SSID                │ Write │ UTF-8 字符串   │
│ FFE2     │ WiFi 密码                 │ Write │ UTF-8 字符串   │
│ FFE3     │ WiFi 连接状态 + 触发      │ Write/Notify │ 1 字节 uint8   │
│ FFE4     │ 设备 IP 地址              │ Notify│ UTF-8 字符串   │
└──────────┴──────────────────────────┴───────┴──────────────┘
```

说明：

- 手机 App 通过本服务完成 WiFi 配网。
- BLE 四时间戳时间同步不复用 FFE1-FFE4；其控制承载见 2.5。

### 2.2 Characteristic 详细说明

#### FFE1 — WiFi SSID
```
UUID:   0000FFE1-0000-1000-8000-00805F9B34FB
方向:   手机 → 设备
操作:   GATT Write
格式:   UTF-8 编码字符串，最大 32 字节
示例:   "MyWiFi"
触发:   收到后设备暂存，等 FFE3 触发后再实际连接
```

#### FFE2 — WiFi 密码
```
UUID:   0000FFE2-0000-1000-8000-00805F9B34FB
方向:   手机 → 设备
操作:   GATT Write
格式:   UTF-8 编码字符串，最大 64 字节
示例:   "pwd12345"
触发:   同上
```

#### FFE3 — WiFi 连接状态
```
UUID:   0000FFE3-0000-1000-8000-00805F9B34FB
方向:   设备 → 手机 (Notify) / 手机 → 设备 (Write)
操作:   Notify (设备上报) / Write (手机触发连接)

设备上报值 (Notify):
  0x00 = 空闲 (IDLE)
  0x01 = 正在连接 WiFi (CONNECTING)
  0x02 = WiFi 已连接 (CONNECTED)
  0x03 = WiFi 连接失败 (FAILED)

手机触发值 (Write):
  0x01 = 开始连接 WiFi (手机写完 SSID + 密码后发送)
```

#### FFE4 — 设备 IP 地址
```
UUID:   0000FFE4-0000-1000-8000-00805F9B34FB
方向:   设备 → 手机 (Notify)
操作:   Notify
格式:   UTF-8 字符串，如 "192.168.1.105"
触发:   设备 WiFi 连接成功并获取到 IP 后立即 Notify
       手机收到此地址后断开 BLE，转为 WiFi TCP 通讯
```

#### CCCD (Client Characteristic Configuration Descriptor)
```
UUID:   00002902-0000-1000-8000-00805F9B34FB (标准)
说明:   手机端会写入 0x0001 (ENABLE_NOTIFICATION) 到 FFE3 和 FFE4 的
       CCCD 来启用 Notify。设备端需支持此标准流程。
```

### 2.3 BLE 配网时序

```
手机 App (GATT Client)              采集设备 (GATT Server)
        │                                    │
        │──── 扫描 BLE 广播 ───────────────→  │
        │──── 连接 GATT ──────────────────→  │
        │←─── 连接成功 ────────────────────  │
        │──── 发现服务 ───────────────────→  │
        │←─── 返回 FFE0 Service ───────────  │
        │──── Write CCCD(FFE3)=0x0001 ────→  │
        │──── Write CCCD(FFE4)=0x0001 ────→  │
        │──── Write FFE1="MyWiFi" ───────→  │
        │──── Write FFE2="pwd12345" ─────→  │
        │──── Write FFE3=0x01 ───────────→  │
        │←─── Notify FFE3=0x01 ───────────  │
        │←─── Notify FFE3=0x02 ───────────  │
        │←─── Notify FFE4="192.168.1.105"   │
        │──── 断开 BLE ───────────────────→  │
        │════ 切换为 WiFi TCP 通讯 ═══════════│
```

### 2.4 设备端 BLE 广播要求

本节定义 BLE 配网广播的必选匹配条件与设备选择规则。客户端必须按 Service UUID 和
Manufacturer Company ID 联合筛选，不得只按设备名、Manufacturer Payload 或 BLE address
判断设备。

#### 2.4.1 当前 SDK 广播配置

```
广播模式:       可连接 BLE 广播（ADV_IND）
Complete Name:  SXR_1
Service UUID:   0000FFE0-0000-1000-8000-00805F9B34FB
短 UUID:        0xFFE0
Company ID:     0x1314（十进制 4884）
```

客户端必须合并 Advertising Data 和 Scan Response 后进行筛选。合并后的广播记录
必须同时包含 FFE0 Service UUID 和 Company ID `0x1314`；其余字段用于展示和区分设备：

```
Complete Local Name:  SXR_1
Service UUID:        0000FFE0-0000-1000-8000-00805F9B34FB
Company ID:          0x1314
Manufacturer Payload: 设备标识（内容和长度可变）
```

#### 2.4.2 Manufacturer Payload 格式

Manufacturer Data 的格式定义如下：

| 字段 | 长度 | 编码 | 约束 |
|------|------|------|------|
| Company ID | 2 字节（由 BLE/Android API 单独表示） | BLE Company ID | `0x1314` / `4884` |
| Payload | 可变 | 设备端定义；推荐 UTF-8 / ASCII | 不作为准入筛选条件，可作为设备标识展示 |

例如，当前某台设备可能广播以下值：

```text
Company ID:       0x1314
Payload hex:      0x36303156523032303737
Payload UTF-8:    601VR02077
Payload length:   10 bytes
```

上述 Payload 仅为设备标识示例，不是协议固定值。`0x1314` 是 Company ID，Android API
会将其与 Manufacturer Payload 分开提供。客户端应展示可读 Payload（无法按 UTF-8
解码时展示十六进制）和设备名/address，允许用户从全部合格设备中选择。

#### 2.4.3 客户端筛选和连接流程

客户端必须执行以下流程：

1. 扫描 BLE 广播，并合并 Advertising Data 与 Scan Response。
2. 只接受 Service UUID 为 `0xFFE0`（完整 UUID 为
   `0000FFE0-0000-1000-8000-00805F9B34FB`）的记录。
3. 只接受 Manufacturer Company ID 为 `0x1314` / `4884` 的记录。
4. 不按 Manufacturer Payload 固定值过滤；将 Payload 作为设备标识展示，并允许用户
   从多条同时满足步骤 2、3 的记录中选择目标设备。
5. 使用用户所选扫描记录中的 BLE address 发起连接；禁止写死 MAC 地址，
   也禁止使用只匹配到 `0x1000F000` 的记录的 address。
6. 连接成功后发现 GATT 服务，使用 `FFE0` 下的 `FFE1`-`FFE4` 完成 WiFi 配网；
   如需时间同步，再发现并使用 `a1b2c3d4-e5f6-7890-abcd-1234567890ab` 控制服务。

设备名 `SXR_1`、Manufacturer Payload 和 BLE address 均可用于展示和区分扫描结果，但
它们都不是准入条件。客户端不得保存或预配置某个固定 Payload 或地址作为唯一设备
识别条件。

#### 2.4.4 `0x1000F000` 服务的边界

`0x1000F000` 不属于本协议，也不是当前 SDK 配网所需的 Service UUID。设备系统中的
`SVR_ConnectionHelper`、`SVR_DeviceFindUtils` 和 `SVR_BleAdvertiser` 创建该服务，
因此扫描器会看到另一个同名 `SXR_1` 的 BLE 广播记录。客户端必须忽略该记录：

```text
0x1000F000  → 系统/厂商发现服务，非本协议
0xFFE0     → 当前 SDK 配网服务，必须使用
```

当前 SDK `BleServerManager` 不控制 `0x1000F000`，协议中不提供关闭配置。该服务
是否关闭由设备系统/厂商决定；Linux client 不得依赖、连接或解析该服务。保留该服务
不影响当前 SDK 通过 `0xFFE0` 进行配网。

### 2.5 BLE 四时间戳时间同步（Linux 设备互通协议）

本节定义 BLE 四时间戳时间同步协议。该协议面向 Linux 设备或其他非手机端设备互通，协议对外只暴露 UTC ns 语义和明确的原始量/统计量；对端不需要理解另一台设备内部采用何种单调时钟实现。

#### 2.5.1 角色与建链前提

| 角色 | 职责 |
|------|------|
| `initiator` | 发起同步会话，发送 `start_time_sync`，逐次发出 `time_sync_request`，在本地记录 `t1` 与 `t4`，依据四时间戳公式计算 offset / RTT 与统计结果。 |
| `responder` | 接收 `time_sync_request`，记录 `t2` 与 `t3`，回写 `time_sync_reply`。 |

前提：

- 双方必须已完成 BLE 连接，并为 `CHAR_CMD_RESPONSE_UUID` 与 `CHAR_ERROR_MSG_UUID` 启用 notify。
- BLE 时间同步 JSON 中所有以 `_ns` 结尾的字段都必须使用十进制字符串编码，禁止使用 JSON number。
- `t1_ns`、`t2_ns`、`t3_ns`、`t4_ns` 全部表示 UTC ns，即自 `1970-01-01T00:00:00Z` 起的纳秒时间戳。
- `offset_ns`、`round_trip_ns` 以及各统计量都以 ns 为单位，在 JSON 中同样使用十进制字符串编码。
- 协议不承载 `boottime`、单调时钟偏移或启动时长语义。

#### 2.5.2 GATT service / characteristic

```
Service UUID: a1b2c3d4-e5f6-7890-abcd-1234567890ab

Characteristics:
┌──────────┬──────────────────────────────────────────┬──────────────┬────────────────────┐
│ UUID     │ 用途                                      │ 属性         │ 数据格式             │
├──────────┼──────────────────────────────────────────┼──────────────┼────────────────────┤
│ ...0005  │ Control Command                           │ Write        │ UTF-8 JSON           │
│ ...0006  │ Command Response                          │ Notify       │ UTF-8 JSON           │
│ ...0007  │ Error Message                             │ Notify       │ UTF-8 JSON           │
└──────────┴──────────────────────────────────────────┴──────────────┴────────────────────┘
```

完整 UUID：

- `CHAR_CONTROL_CMD_UUID = a1b2c3d4-e5f6-7890-abcd-000000000005`
- `CHAR_CMD_RESPONSE_UUID = a1b2c3d4-e5f6-7890-abcd-000000000006`
- `CHAR_ERROR_MSG_UUID = a1b2c3d4-e5f6-7890-abcd-000000000007`

#### 2.5.3 消息方向、操作类型与完整时序

控制载体统一为 UTF-8 JSON 文本。每条指令和响应都必须带 `type` 字段；所有请求和响应都必须带 `op` 字段，用于区分操作类型，至少区分 `sync` 与 `query`。

```
initiator                                 responder
      │                                         │
      │ Write control_cmd                       │
      │ {"type":"start_time_sync",              │
      │  "op":"sync","action":"start",          │
      │  "session_id":1}                        │
      │────────────────────────────────────────→│
      │                                         │
      │ Notify cmd_response                     │
      │ {"type":"time_sync_status",             │
      │  "op":"sync","status":"started",        │
      │  "session_id":1}                        │
      │←────────────────────────────────────────│
      │                                         │
      │ 记录 t1                                 │
      │ Write control_cmd                       │
      │ {"type":"time_sync_request",            │
      │  "op":"sync","phase":"sync",            │
      │  "session_id":1,"sample_index":0,       │
      │  "t1_ns":"1700000000000000001"}         │
      │────────────────────────────────────────→│
      │                                         │
      │                                记录 t2  │
      │                                记录 t3  │
      │ Notify cmd_response                     │
      │ {"type":"time_sync_reply",              │
      │  "op":"sync","status":"ok",             │
      │  "phase":"sync","session_id":1,         │
      │  "sample_index":0,                      │
      │  "t1_ns":"1700000000000000001",         │
      │  "t2_ns":"1700000000001000001",         │
      │  "t3_ns":"1700000000001000501"}         │
      │←────────────────────────────────────────│
      │ 记录 t4 并计算单样本 offset / RTT        │
      │                                         │
      │ ... 共 sync 8 次，再 verify 4 次 ...    │
      │                                         │
      │ Write control_cmd                       │
      │ {"type":"get_time_sync_status",         │
      │  "op":"query","query":"status",         │
      │  "session_id":1}                        │
      │────────────────────────────────────────→│
      │                                         │
      │ Notify cmd_response                     │
      │ {"type":"time_sync_status",             │
      │  "op":"query","status":"synced",        │
      │  "session_id":1,...}                    │
      │←────────────────────────────────────────│
```

时序步骤：

1. `initiator` 发送 `start_time_sync`，`op` 必须为 `sync`，用于创建新的同步会话。
2. `responder` 返回 `time_sync_status`，`status` 必须为 `started`，确认会话已建立。
3. `initiator` 在发送 `time_sync_request` 之前记录 `t1`，并将 `t1_ns` 随请求发给 `responder`。`t1` 的方向必须是 `initiator -> responder`。
4. `responder` 收到 `time_sync_request` 的瞬间记录 `t2`。
5. `responder` 在发出 `time_sync_reply` 之前记录 `t3`，并把 `t1_ns`、`t2_ns`、`t3_ns` 一并回传给 `initiator`。
6. `initiator` 收到 `time_sync_reply` 的瞬间记录 `t4`。`t4_ns` 不在 JSON 中传输，只在 `initiator` 本地参与计算。
7. `initiator` 对每个样本按照四时间戳公式计算 `offset_ns` 与 `round_trip_ns`，完成 `sync` 阶段后继续 `verify` 阶段，最后生成 `sync_result`。
8. `initiator` 如需主动查询当前状态，必须发送 `get_time_sync_status`，`op` 必须为 `query`。

补充说明：

- 发起端每次只允许有一个 pending sample。
- `phase` 先走 `sync`，再走 `verify`。
- `session_id` 每次重新开始同步时递增 1。
- `sample_index` 在同一 `session_id` 内按 phase 分别从 `0` 开始；因此唯一键应视为 `session_id + phase + sample_index`。

#### 2.5.4 消息格式

1. 发起同步请求

```json
{
  "type": "start_time_sync",
  "op": "sync",
  "action": "start",
  "session_id": 1
}
```

2. 发起同步响应

```json
{
  "type": "time_sync_status",
  "op": "sync",
  "status": "started",
  "session_id": 1
}
```

3. 同步样本请求

```json
{
  "type": "time_sync_request",
  "op": "sync",
  "phase": "sync",
  "session_id": 1,
  "sample_index": 0,
  "t1_ns": "1700000000000000001"
}
```

字段说明：

| 字段 | 类型 | 方向 | 必填 | 说明 |
|------|------|------|------|------|
| `type` | string | 发起端 → 应答端 | 是 | 固定为 `time_sync_request` |
| `op` | string | 发起端 → 应答端 | 是 | 固定为 `sync` |
| `phase` | string | 发起端 → 应答端 | 是 | `sync` 或 `verify` |
| `session_id` | uint32 | 发起端 → 应答端 | 是 | 同步会话 ID |
| `sample_index` | uint32 | 发起端 → 应答端 | 是 | 当前 phase 下的样本序号 |
| `t1_ns` | decimal string | 发起端 → 应答端 | 是 | `initiator` 在发送请求前记录的 UTC ns |

4. 同步样本响应

```json
{
  "type": "time_sync_reply",
  "op": "sync",
  "status": "ok",
  "phase": "sync",
  "session_id": 1,
  "sample_index": 0,
  "t1_ns": "1700000000000000001",
  "t2_ns": "1700000000001000001",
  "t3_ns": "1700000000001000501"
}
```

字段说明：

| 字段 | 类型 | 方向 | 必填 | 说明 |
|------|------|------|------|------|
| `type` | string | 应答端 → 发起端 | 是 | 固定为 `time_sync_reply` |
| `op` | string | 应答端 → 发起端 | 是 | 固定为 `sync` |
| `status` | string | 应答端 → 发起端 | 是 | 固定为 `ok` |
| `phase` | string | 应答端 → 发起端 | 是 | 必须与请求一致 |
| `session_id` | uint32 | 应答端 → 发起端 | 是 | 必须与请求一致 |
| `sample_index` | uint32 | 应答端 → 发起端 | 是 | 必须与请求一致 |
| `t1_ns` | decimal string | 应答端 → 发起端 | 是 | 回显请求中的 `t1_ns` |
| `t2_ns` | decimal string | 应答端 → 发起端 | 是 | `responder` 收到请求时记录的 UTC ns |
| `t3_ns` | decimal string | 应答端 → 发起端 | 是 | `responder` 发出回复时记录的 UTC ns |

发起端本地补全：

- `t4_ns` 不在 JSON 中传输，由 `initiator` 在收到 reply 的瞬间记录为 UTC ns。

5. 同步完成结果

```json
{
  "type": "sync_result",
  "op": "sync",
  "status": "synced",
  "session_id": 1,
  "adopted_offset_ns": "1234000",
  "adopted_rtt_ns": "3210000",
  "adopted_offset_source": "verify",
  "sync_avg_offset_ns": "1200000",
  "sync_avg_rtt_ns": "3500000",
  "verify_avg_offset_ns": "1234000",
  "verify_avg_rtt_ns": "3210000",
  "sync_verify_delta_ns": "34000",
  "sync_sample_count": 8,
  "sync_filtered_sample_count": 4,
  "verify_sample_count": 4,
  "verify_filtered_sample_count": 2
}
```

结果字段说明：

| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | string | 固定为 `sync_result` |
| `op` | string | 固定为 `sync` |
| `status` | string | 固定为 `synced` |
| `session_id` | uint32 | 当前同步会话 ID |
| `adopted_offset_ns` | decimal string | `initiator` 依据四时间戳公式计算出的最终时钟偏差；正值表示 `responder` 的 UTC 时钟领先于 `initiator` |
| `adopted_rtt_ns` | decimal string | `initiator` 依据四时间戳公式计算出的最终往返时延 |
| `adopted_offset_source` | string | 最终采用 `sync` 统计值还是 `verify` 统计值 |
| `sync_avg_offset_ns` / `sync_avg_rtt_ns` | decimal string | `sync` 阶段保留样本的统计平均值 |
| `verify_avg_offset_ns` / `verify_avg_rtt_ns` | decimal string | `verify` 阶段保留样本的统计平均值 |
| `sync_verify_delta_ns` | decimal string | `verify_avg_offset_ns - sync_avg_offset_ns` |
| `sync_sample_count` / `verify_sample_count` | uint32 | 各阶段收到的总样本数 |
| `sync_filtered_sample_count` / `verify_filtered_sample_count` | uint32 | 各阶段进入统计的样本数 |

6. 错误结果

```json
{
  "type": "time_sync_status",
  "op": "sync",
  "status": "failed",
  "session_id": 1,
  "error": "mismatched_reply"
}
```

当前错误码：

| 错误码 | 触发条件 |
|--------|----------|
| `not_started` | 未开始同步就收到 reply |
| `mismatched_reply` | `session_id` 或 `sample_index` 与 pending request 不匹配 |
| `ble_not_ready` | 收到 reply 时 BLE notify / 会话尚未 ready |
| `malformed_command` | JSON 缺少必须字段、字段类型错误或 `_ns` 字段不是十进制字符串 |

#### 2.5.5 统计口径、取消、超时与重试

- `sync` 阶段目标采样数固定为 `8`。
- `verify` 阶段目标采样数固定为 `4`。
- 若某阶段样本数 `< 3`，直接对全部样本做算术平均。
- 若某阶段样本数 `>= 3`，按 `round_trip_ns` 升序排序，仅保留前 `ceil(N/2)` 个低 RTT 样本。
- 因此当前默认会保留 `sync` 阶段 `4` 个样本、`verify` 阶段 `2` 个样本。
- `adopted_offset_ns` / `adopted_rtt_ns` 优先取 `verify` 阶段平均值；若没有 verify 样本，退回 `sync` 阶段平均值。
- `sync_verify_delta_ns = verify_avg_offset_ns - sync_avg_offset_ns`。

控制行为：

- `{"type":"cancel_sync","op":"sync","action":"cancel","session_id":1}`：立即取消当前会话并复位。
- 已处于 `sync/verify` 过程时再次收到 `start_time_sync`：必须终止旧会话并启动新 `session_id`。
- 每个样本请求的超时由 `initiator` 负责判定；默认单样本超时为 `1000ms`。
- 单个样本超时后，`initiator` 必须重发当前 `sample_index`；单个样本最多重试 `3` 次。
- 任一阶段连续重试失败达到 `3` 次时，必须结束会话并返回 `time_sync_status` 错误结果。
- 单条 JSON 命令必须完整承载于一次 characteristic write。

#### 2.5.6 时间语义与计算公式

四时间戳全部使用 UTC ns：

| 字段 | 时钟域 | 说明 |
|------|--------|------|
| `t1_ns` | `initiator` UTC | 发起端发请求前时间 |
| `t2_ns` | `responder` UTC | 应答端收请求时间 |
| `t3_ns` | `responder` UTC | 应答端发回复时间 |
| `t4_ns` | `initiator` UTC | 发起端收回复时间 |

`initiator` 必须先将字符串字段解析为有符号 64 位整数，再按下式计算单样本结果：

```text
offset_ns = ((t2_ns - t1_ns) + (t3_ns - t4_ns)) / 2
round_trip_ns = (t4_ns - t1_ns) - (t3_ns - t2_ns)
```

符号语义：

- `offset_ns > 0` 表示 `responder` 的 UTC 时钟领先于 `initiator`。
- 若 `initiator` 希望把本地 UTC 时间对齐到 `responder`，则可使用 `utc_aligned_ns = utc_local_ns + offset_ns`。

#### 2.5.7 状态查询

BLE 时间同步必须提供主动状态查询命令：

```json
{
  "type": "get_time_sync_status",
  "op": "query",
  "query": "status",
  "session_id": 1
}
```

返回：

```json
{
  "type": "time_sync_status",
  "op": "query",
  "status": "synced",
  "session_id": 1,
  "phase": "complete",
  "last_sample_index": 11,
  "last_t1_ns": "1700000000000000001",
  "last_t2_ns": "1700000000001000001",
  "last_t3_ns": "1700000000001000501",
  "last_t4_ns": "1700000000002000001",
  "adopted_offset_ns": "1234000",
  "adopted_rtt_ns": "3210000",
  "sync_sample_count": 8,
  "sync_filtered_sample_count": 4,
  "verify_sample_count": 4,
  "verify_filtered_sample_count": 2,
  "sync_avg_offset_ns": "1200000",
  "sync_avg_rtt_ns": "3500000",
  "verify_avg_offset_ns": "1234000",
  "verify_avg_rtt_ns": "3210000",
  "sync_verify_delta_ns": "34000"
}
```

查询字段语义：

- `status`：`idle` / `sync` / `verify` / `synced` / `failed` / `cancelled`
- `phase`：当前阶段
- `last_sample_index`：最后一个已接收样本序号
- `last_t1_ns` / `last_t2_ns` / `last_t3_ns` / `last_t4_ns`：最后一个已接收完整样本的四时间戳原始量，类型均为十进制字符串
- `adopted_offset_ns` / `adopted_rtt_ns`：当前会话采用的最终统计值，类型均为十进制字符串
- `sync_*` / `verify_*`：各阶段统计值和样本数；所有 `*_ns` 字段均为十进制字符串

---

## 3. WiFi 控制与状态协议

### 3.1 传输帧格式

所有 TCP :8801 的数据经过统一的帧封装：

```
 0        2        6                                        N
├────────┼────────┼────────────────────────────────────────┤
│ Magic  │ Length │  Protobuf 序列化的 Packet 消息            │
│ 0x4547 │ 4B BE  │                                        │
│ ("EG") │        │                                        │
└────────┴────────┴────────────────────────────────────────┘

字段说明:
  Magic (2 bytes) : 0x45 0x47 (ASCII "EG")，帧起始标识
  Length (4 bytes): Payload 字节数，大端序 (Big-Endian)
  Payload (N bytes): Protobuf 序列化后的 Packet 消息

最大帧长: 16 MB
```

### 3.2 Protobuf 消息定义

#### 3.2.1 顶层消息结构

```protobuf
message Packet {
  uint32 seq = 1;
  uint64 timestamp_ms = 2;
  uint32 version = 3;

  oneof payload {
    Command command = 10;
    Status status = 20;
    Response response = 30;
    Event event = 40;
  }
}
```

#### 3.2.2 控制指令 (手机 → 设备)

```protobuf
message Command {
  CommandType cmd = 1;
  map<string, string> params = 10;

  enum CommandType {
    CMD_UNSPECIFIED = 0;
    CMD_START_COLLECT = 1;
    CMD_STOP_COLLECT = 2;
    CMD_START_VIDEO = 3;
    CMD_STOP_VIDEO = 4;
    CMD_HEARTBEAT = 5;
    CMD_GET_PARAM = 10;
    CMD_SET_PARAM = 11;
    CMD_REBOOT = 20;
  }
}
```

本协议命令集定义如下 8 个命令：

| 命令 | 值 | 当前约定 |
|------|----|----------|
| `CMD_START_COLLECT` | 1 | 开始采集。若已在录制则返回 `OK`。 |
| `CMD_STOP_COLLECT` | 2 | 停止采集。 |
| `CMD_START_VIDEO` | 3 | 打开视频预览开关，仅对 RGB 编码流生效。 |
| `CMD_STOP_VIDEO` | 4 | 关闭视频预览并断开 :8802 推流。 |
| `CMD_HEARTBEAT` | 5 | 保活 / 连通性确认。 |
| `CMD_GET_PARAM` | 10 | 查询参数。用于查询自实现 NTP server 服务状态。 |
| `CMD_SET_PARAM` | 11 | 设置参数。用于启动、停止或配置自实现 NTP server 服务。 |
| `CMD_REBOOT` | 20 | 设备管理命令。 |

说明：

- 除上述 8 个命令外，其他历史命令值均不属于本协议约定。
- 即使底层 proto / enum 仍保留历史值，手机端也不应再发送。

#### 3.2.3 自实现 NTP 对时约定（SDK 为 server，client 自行计算 offset）

本节定义 `TCP/NTP` 路径上的控制、状态与时间交换协议。这里的 NTP 指本项目自实现的时间服务，不对应外部通用 NTP 规范定义：

- SDK 必须作为 NTP `server` 提供 UTC 时间服务。
- 手机 App 或其他控制端必须作为 NTP `client` 发起同步。
- NTP 同步目标是 UTC / Unix epoch 时间。
- `client` 必须依据本项目自定义时间交换中的四时间戳语义，自行计算本机相对 SDK 的 offset 与 RTT。
- SDK 不负责在协议层返回 `client` 的 offset / RTT 结果。
- `CMD_SET_PARAM` / `CMD_GET_PARAM` 走 `TCP 8801` 上的 EG 帧 + Protobuf 控制协议。
- `custom_ntp` 时间交换不另开端口、不另起 TCP 连接、不使用 UDP/123；`client` 必须在 `CMD_SET_PARAM(action=start_ntp_server)` 成功后，复用当前 `TCP 8801` 控制连接发送 UTF-8 JSON 对象，SDK 也在同一连接上返回 UTF-8 JSON 对象。
- 同一条 `TCP 8801` 连接上，首字节为 `EG` 的负载按标准控制包解析，首字节为 `{` 的负载按 `custom_ntp` JSON 解析；SDK 当前实现返回 JSON 时会在对象末尾追加换行符 `\n`。
- `CMD_SET_PARAM(action=stop_ntp_server)` 只停止 custom time service；`CMD_GET_PARAM(key=ntp_status)` 只查询 service/server 状态，不承载 `t1/t2/t3` 样本数据。
- 当 custom time service 未处于 `running` 状态时，任何 `custom_ntp` JSON 请求都必须返回 `type=time_sync_status`、`status=failed`、`last_error=ntp_server_not_running`。

1. 启动或停止 NTP server：`CMD_SET_PARAM`

```protobuf
command {
  cmd: CMD_SET_PARAM
  params {
    key: "scope"
    value: "time"
  }
  params {
    key: "op"
    value: "sync"
  }
  params {
    key: "protocol"
    value: "custom_ntp"
  }
  params {
    key: "action"
    value: "start_ntp_server"
  }
}
```

停止命令：

```protobuf
command {
  cmd: CMD_SET_PARAM
  params {
    key: "scope"
    value: "time"
  }
  params {
    key: "op"
    value: "sync"
  }
  params {
    key: "protocol"
    value: "custom_ntp"
  }
  params {
    key: "action"
    value: "stop_ntp_server"
  }
}
```

字段约定：

| 参数 | 必填 | 说明 |
|------|------|------|
| `scope` | 是 | 固定为 `time` |
| `op` | 是 | 固定为 `sync`，表示同步类操作 |
| `protocol` | 是 | 固定为 `custom_ntp` |
| `action` | 是 | `start_ntp_server` 或 `stop_ntp_server` |

返回约定：

- 成功返回 `code=OK`。
- `response.data` 必须包含 `scope=time`、`op=sync`、`protocol=custom_ntp`、`action`、`role=server` 与当前 `state`。
- 若参数非法，返回 `ERR_INVALID_PARAM`。
- 若服务忙或状态不允许切换，返回 `ERR_DEVICE_BUSY`。

2. 查询 NTP server 状态：`CMD_GET_PARAM`

```protobuf
command {
  cmd: CMD_GET_PARAM
  params {
    key: "scope"
    value: "time"
  }
  params {
    key: "op"
    value: "query"
  }
  params {
    key: "protocol"
    value: "custom_ntp"
  }
  params {
    key: "key"
    value: "ntp_status"
  }
}
```

返回约定：

- 成功返回 `code=OK`，`message="ntp status"`。
- `response.data` 必须包含下表字段：

| 键 | 类型 | 说明 |
|----|------|------|
| `scope` | string | 固定为 `time` |
| `op` | string | 固定为 `query` |
| `protocol` | string | 固定为 `custom_ntp` |
| `key` | string | 固定为 `ntp_status` |
| `role` | string | 固定为 `server` |
| `state` | string | `stopped` / `starting` / `running` / `failed` |
| `server_time_utc_ns` | decimal string | 返回包生成时刻的 SDK UTC ns |
| `last_start_time_ms` | decimal string | 最近一次启动时间 |
| `last_stop_time_ms` | decimal string | 最近一次停止时间；未停止过时为 `0` |
| `last_client_request_utc_ns` | decimal string | 最近一次收到 NTP client 请求时的 UTC ns；没有时为 `0` |
| `last_server_response_utc_ns` | decimal string | 最近一次发出 NTP server 响应时的 UTC ns；没有时为 `0` |
| `last_error` | string | 失败原因；仅失败时携带 |

NTP client 计算语义：

- `client` 必须依据本项目自实现 NTP 路径定义的四时间戳自行计算 `offset` 与 `round trip delay`；其中 `t1_ns` 由 `client` 发出请求前记录，`t2_ns` / `t3_ns` 由 SDK 在 `time_sync_reply` 中返回，`t4_ns` 由 `client` 收到回复时本地记录。
- `CMD_GET_PARAM` 返回的是 server 状态与 server 原始 UTC 时间信息，不返回 `client` 计算结果。

3. 同一 TCP 8801 连接上的 `custom_ntp` JSON 时间交换

承载与通用约束：

- 每条消息都必须是单个完整的 UTF-8 JSON 对象。
- 所有 `*_ns` 字段都必须使用十进制字符串编码，禁止使用 JSON number。
- `protocol` 固定为 `custom_ntp`，`role` 固定为 `server`，`clock_domain` 固定为 `UTC`。

请求消息：

| `type` | `op` | 必填字段 | 说明 |
|--------|------|----------|------|
| `start_time_sync` | `sync` | `session_id` | 创建或重置一个时间同步会话。成功后返回 `time_sync_status`，其中 `status=started`。 |
| `time_sync_request` | `sync` | `phase`、`session_id`、`sample_index`、`t1_ns` | 发起一个样本交换。`phase` 只允许 `sync` 或 `verify`。成功后返回 `time_sync_reply`。 |
| `get_time_sync_status` | `query` | 无 | 查询当前 custom time service / session 状态。成功后返回 `time_sync_status`。 |
| `cancel_sync` | `sync` | `session_id` | 取消当前会话。成功后返回 `time_sync_status`，其中 `status=cancelled`。 |

`time_sync_status` 响应字段：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `type` | string | 是 | 固定为 `time_sync_status` |
| `op` | string | 是 | 回显本次操作类型；`sync` 或 `query` |
| `protocol` | string | 是 | 固定为 `custom_ntp` |
| `role` | string | 是 | 固定为 `server` |
| `state` | string | 是 | `stopped` / `starting` / `running` / `failed` |
| `status` | string | 是 | `started` / `idle` / `sync` / `verify` / `cancelled` / `failed` 等当前会话状态 |
| `phase` | string | 是 | 当前会话 phase；无活跃会话时为 `idle` |
| `server_time_utc_ns` | decimal string | 是 | 生成响应时的 SDK UTC ns |
| `last_client_request_utc_ns` | decimal string | 是 | 最近一次收到 `time_sync_request` 时记录的 UTC ns；没有时为 `0` |
| `last_server_response_utc_ns` | decimal string | 是 | 最近一次发出 `time_sync_reply` 时记录的 UTC ns；没有时为 `0` |
| `session_id` | uint32 | 否 | 当前或最近一次会话 ID；仅 `session_id != 0` 时返回 |
| `last_sample_index` | uint32 | 否 | 最近一次处理的样本序号；仅处理过样本时返回 |
| `last_error` | string | 否 | 失败原因，例如 `malformed_command`、`unsupported_command`、`ntp_server_not_running`、`mismatched_session` |

`time_sync_reply` 响应字段：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `type` | string | 是 | 固定为 `time_sync_reply` |
| `op` | string | 是 | 固定为 `sync` |
| `status` | string | 是 | 固定为 `ok` |
| `protocol` | string | 是 | 固定为 `custom_ntp` |
| `role` | string | 是 | 固定为 `server` |
| `clock_domain` | string | 是 | 固定为 `UTC` |
| `phase` | string | 是 | 必须与请求一致，`sync` 或 `verify` |
| `session_id` | uint32 | 是 | 必须与请求一致 |
| `sample_index` | uint32 | 是 | 必须与请求一致 |
| `t1_ns` | decimal string | 是 | 回显请求中的 `t1_ns` |
| `t2_ns` | decimal string | 是 | SDK 收到本次 `time_sync_request` 时记录的 UTC ns |
| `t3_ns` | decimal string | 是 | SDK 发出本次 `time_sync_reply` 时记录的 UTC ns |

最小消息流：

1. `client` 通过 `CMD_SET_PARAM(action=start_ntp_server)` 使 service 进入 `running`。
2. `client` 在同一条 `TCP 8801` 连接上发送 `{"type":"start_time_sync","op":"sync","session_id":1}`。
3. SDK 返回 `time_sync_status`，至少包含 `protocol`、`role`、`state`、`status`、`phase` 与 `server_time_utc_ns`。
4. `client` 逐次发送 `time_sync_request`，SDK 逐次返回 `time_sync_reply`；`client` 本地补全 `t4_ns` 后计算 offset / RTT。
5. `client` 可随时发送 `get_time_sync_status` 查询，或发送 `cancel_sync` / `CMD_SET_PARAM(action=stop_ntp_server)` 结束该路径。

#### 3.2.4 状态上报 (设备 → 手机)

```protobuf
message Status {
  BatteryInfo battery = 1;
  WifiInfo wifi = 2;
  DeviceState state = 3;
  StorageInfo storage = 4;
  repeated PeripheralState peripherals = 10;
}

message BatteryInfo {
  uint32 level = 1;
  bool charging = 2;
  float voltage = 3;
  optional float temperature = 4;
}

message WifiInfo {
  int32 rssi = 1;
  string ssid = 2;
  uint32 channel = 3;
}

message DeviceState {
  DeviceWorkingState working = 1;
  enum DeviceWorkingState {
    IDLE = 0;
    COLLECTING = 1;
    UPLOADING = 2;
    SLEEPING = 3;
    FAULT = 4;
  }
}

message StorageInfo {
  uint64 total_bytes = 1;
  uint64 free_bytes = 2;
}

message PeripheralState {
  string name = 1;
  bool connected = 2;
  bool healthy = 3;
}
```

状态口径以当前 SDK 内部状态为准，只描述当前 SDK 已有状态来源：

- `battery`：来自平台电池快照，当前可带 `level`、`charging`、`voltage`、`temperature`。
- `storage`：来自设备当前存储目录所在文件系统的 `total_bytes` / `free_bytes`。
- `wifi`：来自当前平台 WiFi 快照，包含 `ssid`、`rssi`、`channel`。
- `peripherals`：当前 SDK 现有聚合项为 `camera`、`imu`、`mic`。
- `BLE`：当前 SDK 内部存在 BLE 配网 / 连接状态，但控制通道 `Status` 里没有单独 protobuf 字段；因此本版本只把 BLE 视为设备基础连接状态的一部分，不额外扩展字段，不伪称控制包里已有独立 `ble` 字段。

当前 SDK 的 `state.working` 使用约束：

- 当前实际使用值以 `IDLE`、`COLLECTING`、`FAULT` 为主。
- `UPLOADING`、`SLEEPING` 在当前版本中不作为控制通道必须上报的状态。

采集状态请沿用 `capture_status` 的状态类型，但要区分“实时控制状态”和“本地落盘最终态”：

| 口径 | 状态 | 当前语义 | 与 `state.working` 的关系 |
|------|------|----------|---------------------------|
| 实时控制状态 | `recording` | 正在采集中 | 映射为 `COLLECTING` |
| 实时控制状态 | `finalizing` | 已停止采集请求，正在收尾写盘 | 仍映射为 `COLLECTING` |
| 实时控制状态 | `idle` | 当前未在采集 / 收尾 | 映射为 `IDLE` |
| 本地文件最终态 | `complete` | `capture_status.json` 最终写盘完成后的文件侧状态 | 不作为实时控制状态单独上报 |

补充说明：

- 手机端如需展示实时采集状态，应优先沿用 `recording / finalizing / idle` 这组口径。
- `complete` 当前只用于本地 `capture_status.json` 最终态，不应当成实时控制通道状态来理解。
- 控制通道当前不会为了此事再扩展新的 `working` 枚举。
- 状态推送周期以当前 SDK 行为为准；当前控制连接建立后会先推送一次快照，随后按秒级周期刷新。

#### 3.2.5 事件字段约定 (设备 → 手机)

```protobuf
message Event {
  oneof event {
    FaultEvent fault = 1;
    FaultCleared fault_cleared = 2;
    ConnectionStateChanged connection_changed = 3;
  }
}

message FaultEvent {
  string code = 1;
  string desc = 2;
  FaultLevel level = 3;
  uint64 raise_time_ms = 4;

  enum FaultLevel {
    INFO = 0;
    WARN = 1;
    ERROR = 2;
    FATAL = 3;
  }
}

message FaultCleared {
  string code = 1;
}

message ConnectionStateChanged {
  ConnectionState state = 1;
  enum ConnectionState {
    DISCONNECTED = 0;
    CONNECTING = 1;
    CONNECTED = 2;
    RECONNECTING = 3;
  }
}
```

约束：

- 只有 `error` / `fault` 信息才通过协议上报，其他基础日志、调试日志、普通运行日志不上报。
- `fault_cleared` 仅用于支持自动恢复的故障。
- `connection_changed` 虽仍存在于现有消息结构中，但当前版本不把它作为正式对外上报主路径；手机端不应依赖它作为常规状态事件。

#### 3.2.6 应答 (双向)

```protobuf
message Response {
  uint32 req_seq = 1;
  ResultCode code = 2;
  string message = 3;
  map<string, string> data = 10;
}
```

应重点处理的应答码：

| 结果码 | 值 | 说明 |
|--------|----|------|
| `OK` | 0 | 成功 |
| `ERR_UNKNOWN_CMD` | 100 | 未支持 / 已下线命令，或保留命令尚未接入 |
| `ERR_INVALID_PARAM` | 101 | 参数不符合当前约定 |
| `ERR_DEVICE_BUSY` | 102 | 当前任务忙，例如 NTP 正在执行 |
| `ERR_STORAGE_FULL` | 200 | 空间不足，无法开始或继续采集 |
| `ERR_INTERNAL` | 500 | 内部错误 |

#### 3.2.7 时间字段与时钟语义

控制通道中的时间字段需要按下表理解：

| 字段 | 传输位置 | 传输类型 | 时钟域 | 语义 |
|------|----------|----------|--------|------|
| `Packet.timestamp_ms` | Protobuf `Packet` | `uint64` | UTC ms | 设备发送该控制包时的 UTC 毫秒时间戳。 |
| `server_time_utc_ns` | `response.data` / `time_sync_status` | decimal string | UTC ns | `CMD_GET_PARAM ntp_status` 或 `custom_ntp` 状态响应生成时的 SDK UTC 时间。 |
| `last_client_request_utc_ns` | `response.data` / `time_sync_status` | decimal string | UTC ns | SDK 最近一次收到 NTP client `time_sync_request` 时的 UTC 时间。 |
| `last_server_response_utc_ns` | `response.data` / `time_sync_status` | decimal string | UTC ns | SDK 最近一次发出 `time_sync_reply` 时的 UTC 时间。 |
| `last_start_time_ms` / `last_stop_time_ms` | `response.data` | decimal string | UTC ms | NTP server 服务最近启动/停止时刻。 |
| `t1_ns` | `time_sync_request` / `time_sync_reply` | decimal string | UTC ns | `client` 发请求前记录，并由 SDK 在 reply 中回显。 |
| `t2_ns` / `t3_ns` | `time_sync_reply` | decimal string | UTC ns | SDK 在处理该样本时记录的原始 UTC 时间戳。 |
| `clock_domain` | `time_sync_reply` | string | `UTC` | 明确 `t1_ns` / `t2_ns` / `t3_ns` 的时钟域固定为 UTC。 |

补充说明：

- `timestamp_ms` 是控制协议包头时间，语义为 UTC ms。
- NTP 路径中，协议不定义由 SDK 返回 `client` offset / RTT 的字段。
- NTP 路径中的 `server_time_utc_ns`、`last_client_request_utc_ns`、`last_server_response_utc_ns` 必须使用十进制字符串编码。
- BLE 四时间戳路径与 `TCP/NTP` 路径各自维护独立的 `t1_ns`~`t4_ns` 采样过程；BLE 的 `adopted_offset_ns`、`sync_result` 等统计结果不能与 NTP 路径混用。

### 3.3 指令交互流程示例

#### 3.3.1 开始采集

```
手机                                    设备
 │                                       │
 │  Packet{                               │
 │    seq: 1                              │
 │    command: { cmd: CMD_START_COLLECT } │
 │  }                                     │
 │──────────────────────────────────────→ │
 │                                        │ 启动采集...
 │  Packet{                               │
 │    seq: 2                              │
 │    response: {                         │
 │      req_seq: 1                        │
 │      code: OK                          │
 │      message: "collect started"        │
 │    }                                   │
 │  }                                     │
 │←────────────────────────────────────── │
 │                                        │
 │  Packet{ seq: 3, status: {             │
 │    battery: { level: 85 },             │
 │    state: { working: COLLECTING }      │
 │  }}                                    │
 │←────────────────────────────────────── │
```

#### 3.3.2 心跳

```
手机                                    设备
 │                                       │
 │  Packet{seq:N, command:{cmd:CMD_HEARTBEAT}} │
 │──────────────────────────────────────→│
 │                                        │
 │  Packet{seq:M, response:{             │
 │    req_seq:N, code:OK,                │
 │    message:"heartbeat ok"             │
 │  }}                                   │
 │←──────────────────────────────────────│
```

#### 3.3.3 NTP 对时

```
手机                                    设备
 │                                       │
 │  CMD_SET_PARAM                        │
 │  scope=time                           │
 │  op=sync                              │
 │  protocol=custom_ntp                  │
 │  action=start_ntp_server              │
 │──────────────────────────────────────→│
 │                                        │ 启动自实现 NTP server
 │  Response: code=OK                     │
 │  data.op="sync"                        │
 │  data.action="start_ntp_server"        │
 │  data.role="server"                    │
 │  data.state="running"                  │
 │←──────────────────────────────────────│
 │                                       │
 │  {"type":"start_time_sync","op":"sync",...}            │
 │──────────────────────────────────────→│
 │                                        │
 │  {"type":"time_sync_status",            │
 │   "protocol":"custom_ntp",              │
 │   "role":"server","status":"started"...}│
 │←──────────────────────────────────────│
 │                                        │
 │  {"type":"time_sync_request",           │
 │   "op":"sync","phase":"sync",           │
 │   "t1_ns":"..."}                        │
 │──────────────────────────────────────→│
 │                                        │
 │  {"type":"time_sync_reply",             │
 │   "protocol":"custom_ntp",              │
 │   "role":"server",                      │
 │   "clock_domain":"UTC",                 │
 │   "t1_ns":"...","t2_ns":"...","t3_ns":"..."} │
 │←──────────────────────────────────────│
 │                                        │
 │  ... client 本地记录 t4 并自行计算 offset / RTT ...     │
 │                                       │
 │  CMD_GET_PARAM                         │
 │  scope=time                            │
 │  op=query                              │
 │  protocol=custom_ntp                   │
 │  key=ntp_status                        │
 │──────────────────────────────────────→│
 │                                        │
 │  Response: code=OK                     │
 │  data.op="query"                       │
 │  data.protocol="custom_ntp"            │
 │  data.role="server"                    │
 │  data.state="running"                  │
 │  data.server_time_utc_ns="..."         │
 │←──────────────────────────────────────│
```

说明：

- `client` 依据本项目自实现 NTP 路径中的四时间戳语义自行计算 offset 与 RTT。
- `start_ntp_server` / `stop_ntp_server` 与 `ntp_status` 查询走 EG 帧控制协议；`start_time_sync` / `time_sync_request` / `get_time_sync_status` / `cancel_sync` 走同一条 `TCP 8801` 连接上的 JSON。
- 该路径与 BLE 四时间戳同步不同；BLE 路径的 `t1/t2/t3/t4` 与 `adopted_offset_ns` 定义仅适用于 2.5 节。

#### 3.3.4 故障上报

```
设备                                    手机
 │                                       │
 │  Packet{                               │
 │    seq: 100                            │
 │    event: {                            │
 │      fault: {                          │
 │        code: "E_SD_FULL",              │
 │        desc: "storage free space below SDK threshold", │
 │        level: ERROR                    │
 │      }                                 │
 │    }                                   │
 │  }                                     │
 │──────────────────────────────────────→ │
 │                                        │
 │  ... 仅错误类信息上报，普通日志不经过协议 ... │
```

## 4. 视频流协议

### 4.1 视频通道

```
端口:    TCP 8802
方向:    设备 → 手机
触发:    手机发送 CMD_START_VIDEO 后，设备进入视频可发送状态
停止:    手机发送 CMD_STOP_VIDEO 或断开 TCP 8802 连接
返回:    CMD_START_VIDEO 的 response.data 带回:
         - port = "8802"
         - stream_state = "active"
         - stream_codec = "hevc"
         - stream_format = "annex-b"
```

说明：

- `stream_state = "active"` 表示“设备已打开视频发送开关并可向 :8802 推流”，不是“首帧已经送达”的确认。
- `CMD_START_VIDEO` 会在需要时顺带拉起录制；若录制无法启动，会直接返回错误而不是进入 `armed` 之类的中间态。

### 4.2 编码格式

```
格式:   H.265 / HEVC
来源:   本协议仅描述 RGB 视频流
打包:   Annex-B (带 Start Code) / Byte Stream

典型 HEVC 流结构:
  [VPS] [SPS] [PPS] [IDR] [P] [P] ... [IDR] [P] [P] ...
```

约束：

- 本协议只描述 RGB 编码流；灰度流或其他分组不在本协议范围内。
- 设备侧收到的编码输出若为长度前缀格式，会在发送前转换为 Annex-B；若本身已是 Annex-B，则按原样发送。

### 4.3 当前编码参数口径

| 参数 | 当前口径 |
|------|----------|
| 编码器 MIME | `video/hevc` |
| 视频类型 | RGB only |
| 输入模式 | RGB 走 Surface 编码路径 |
| 打包格式 | Annex-B |
| 关键帧间隔 | 当前编码器配置为 1 秒 |
| 帧率字段 | 由当前编码器实例配置提供，作为编码器配置提示值 |
| 分辨率 / 码率 | 跟随当前 SDK 编码器实例配置，不在协议层再次固定 |
| B-frames | 当前版本在支持的设备上关闭，目标是保持输出时间戳单调 |
| 色彩参数 | RGB Surface 路径使用 BT709 / LIMITED / SDR_VIDEO |

---

## 5. 完整工作流程

### 5.1 设备启动到正常工作状态机

```
设备上电
  │
  ▼
┌──────────┐
│ 初始化     │
│ - WiFi 初始化
│ - BLE 广播
│ - 控制/视频服务启动 │
└────┬─────┘
     │
     ▼
┌──────────┐    手机 BLE 扫描 & 连接
│ BLE 配网  │◄────────────────────────── 手机
│ (等待 WiFi │  手机写入 SSID + 密码
│  凭据)     │  触发连接
└────┬─────┘
     │ WiFi 连接成功
     ▼
┌──────────┐    手机通过 TCP :8801 连接
│ 空闲状态  │◄────────────────────────── 手机
│ (IDLE)    │
└────┬─────┘
     │ 收到 CMD_START_COLLECT
     ▼
┌──────────┐
│ 采集中     │  定期上报 Status
│            │  capture_status:
│            │  - recording
│            │  - finalizing
└────┬─────┘
     │ 收到 CMD_STOP_COLLECT
     ▼
┌──────────┐
│ 回到空闲   │  实时状态以 idle 为主
│            │  本地文件最终可写为 complete
└──────────┘

视频子状态:
  ┌───────┐  CMD_START_VIDEO  ┌──────────┐
  │ IDLE  │ ─────────────────→ │ STREAMING│ (TCP :8802，RGB HEVC)
  │       │ ←───────────────── │          │
  └───────┘  CMD_STOP_VIDEO    └──────────┘
```

### 5.2 连接异常处理

| 异常情况 | 当前设备端行为 | 手机端建议 |
|---------|---------------|-----------|
| WiFi 断开 | 上报 `E_WIFI_LOST` | 提示断连并重连 |
| TCP 8801 断开 | 关闭当前控制连接；视频请求状态复位 | 自动重连控制通道 |
| TCP 8802 断开 | 停止当前视频发送，不影响采集状态 | 需要预览时重新连接 |
| BLE 断开 (配网阶段) | 回到广播 / 等待连接 | 提示用户重新连接 |
| 心跳无响应 | 当前控制服务未定义额外强制动作 | 由手机端自行判定断线策略 |

### 5.3 设备端需要实现的服务/功能清单

| 序号 | 模块 | 要求 |
|------|------|----------|
| 1 | BLE GATT Server | 注册 FFE0 Service + FFE1-FFE4 Characteristics |
| 2 | BLE 广播 | ADV_IND，广播设备名 |
| 3 | WiFi STA | 接收 SSID/密码后连接路由器，获取 DHCP IP |
| 4 | TCP Server :8801 | 监听控制连接，EG 帧 + Protobuf 控制协议，并复用承载 `custom_ntp` UTF-8 JSON |
| 5 | TCP Server :8802 | 监听视频连接，推送 RGB HEVC Annex-B 裸流 |
| 6 | 采集控制 | 仅约定 8 个控制命令 |
| 7 | 状态采集 | 上报电量 / 存储 / WiFi / 采集状态 |
| 8 | `TCP/NTP` 时间对齐 | 通过 `CMD_SET_PARAM` / `CMD_GET_PARAM` 管理 custom time service，并在同一 `TCP 8801` 连接上进行 JSON 时间交换；UTC offset / RTT 由 NTP client 自行计算 |
| 9 | `BLE/四时间戳` 时间同步 | 提供 BLE Time Sync Service、四时间戳同步、结果通知与主动状态查询 |
| 10 | 故障上报 | 仅上报限定故障类别，且仅上传 fault/error 信息 |

### 5.4 对接方职责

| 对接方 | 必须实现 |
|--------|----------|
| 手机 App | BLE FFE0 配网、TCP :8801 控制/状态、TCP :8802 视频、NTP client、`CMD_SET_PARAM`/`CMD_GET_PARAM` 的 NTP server 控制与状态查询，以及同一 `TCP :8801` 连接上的 `custom_ntp` JSON 时间交换 |
| Linux 设备 | BLE Time Sync Service 对接、`start_time_sync`、`time_sync_request`、`time_sync_reply`、`sync_result`、`cancel_sync`、`get_time_sync_status` |

---

## 6. 故障码定义

当前版本只保留以下故障类别，且只有 error 信息才上报，其他基础日志不上报：

| 故障码 | 含义 | 级别 | 说明 |
|--------|------|------|------|
| `E_BAT_LOW` | 低电量 | WARN | 低电量告警类别；当前版本仅在相应错误源接入控制协议时上报。 |
| `E_SD_FULL` | SD 卡空间不足 | ERROR | 当前 SDK 已直接使用，空间恢复后可清除。 |
| `E_CAM_INIT_FAIL` | 摄像头报错 | FATAL | 当前 SDK 已直接使用，主要对应摄像头初始化失败类错误。 |
| `E_WIFI_LOST` | WiFi 断连 | WARN | 当前 SDK 已直接使用，可自动恢复并清除。 |
| `E_OVERHEAT` | 设备严重过热 | FATAL | 设备端读取 skin 温度传感器的最高值；达到 `80.0°C` 时上报，降到 `78.0°C` 以下时清除。Android Thermal Status 的 `SEVERE` 不单独触发此故障。 |
| `E_SYSTEM_FAULT` | 系统崩溃 / 严重内部故障 | FATAL | 当前 SDK 已直接使用，兜底承接严重 SDK / 系统故障。 |

补充说明：

- 当前 SDK 控制通道里已直接可见的故障上报主要是 `E_SD_FULL`、`E_CAM_INIT_FAIL`、`E_WIFI_LOST`、`E_SYSTEM_FAULT`。
- `E_BAT_LOW` 属于当前版本保留的协议约定；`E_OVERHEAT` 已按上述 skin 温度阈值接入设备端上报链路。
- 不再扩展其他基础故障码；手机端仅需按上表处理。

---

> 本协议以本文为准。  
> 新增命令、字段或故障类别时，必须同步修订本文。
