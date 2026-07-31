# Android SDK 日志、手机连接与 U 盘导出流程

> 本文描述当前 Android SDK 已有实现，不是 Linux 方案，也不是未来规划。
>
> 范围包括：应用启动、日志、BLE、Wi-Fi、手机控制、实时视频传输、设备状态上报、蓝牙时间同步、U 盘识别和数据集导出。
>
> 传感器采集、视频编码和传感器对齐的详细流程，参见 `ANDROID_SDK_DATA_ACQUISITION_FLOW.md`。

本文同样按“数据/命令从哪里来、在哪个线程处理、怎样交给下一线程、是否有显式队列、发生什么复制、最终到哪里”的方式说明。Android Binder、BLE stack、socket 内核缓冲和文件系统 page cache 属于平台内部实现；只有源码明确建立的容器才称为项目显式 queue/buffer。

---

## 1. 总体架构

当前 SDK 的设备连接和数据管理可以分成四条相互配合的链路：

```text
链路 A：日志
Java Log / Native NATIVE_LOG*
        ├── Android logcat
        ├── 应用级 app.log
        └── 录制数据集内 capture.log

链路 B：手机首次连接和 Wi-Fi 配网
手机
  → BLE 广播发现设备
  → GATT 写入 SSID、密码
  → Android 连接指定 Wi-Fi
  → BLE 通知连接状态和设备 IP
  → 手机改用 Wi-Fi/TCP 连接设备

链路 C：手机正式控制和实时预览
手机
  ├── TCP 8801：控制命令、响应、状态、故障事件
  └── TCP 8802：RGB 编码视频

链路 D：U 盘导出
Android StorageVolume 检测可移动存储
  → JNI 启动 DatasetExporter
  → 查找 capture_status.json 为 complete 的数据集
  → 复制到 U盘/Export/数据集名.tmp
  → 重命名为最终目录
  → 删除设备本地原数据集
```

这四条链路不是彼此独立的：

- 手机发出的开始/停止采集命令最终进入统一的 `OperationCoordinator`。
- TCP 状态包会读取电池、Wi-Fi、存储空间和采集状态。
- BLE 还承担设备间时间同步命令。
- U 盘导出只处理已经正常结束、状态为 `complete` 的数据集。
- Native 日志既写应用级日志，也在录制期间写入当前数据集。

---

## 2. 主要代码文件及职责

| 文件 | 当前职责 |
|---|---|
| `VrNativeActivity.java` | 主 Activity；绑定 BLE 服务；采集平台状态；注册 U 盘广播；选择 U 盘目录；调用 JNI 启停导出器 |
| `BootReceiver.java` | 开机后启动 BLE 前台服务，失败后延迟重试 |
| `BleService.java` | 独立进程内的 BLE 前台服务；启动热点、GATT Server、BLE 广播和时间同步会话 |
| `BleServerManager.java` | BLE GATT 服务、特征、连接、读写、通知、广播 |
| `BleAidlImpl.java` | BLE 服务与主 Activity 之间的 AIDL 桥；执行 Wi-Fi 配网并转发控制命令 |
| `WifiConnector.java` | 连接指定 Wi-Fi、轮询连接状态、取得 STA IP、处理超时 |
| `HotspotManager.java` | 启动设备热点，热点 SSID 为 `BOARD` |
| `IBleService.java` | BLE 服务对主进程暴露的 Binder 接口 |
| `IBleCallback.java` | BLE 命令、连接状态、Wi-Fi 结果的反向回调 |
| `BleServiceJni.cpp` | Java BLE 服务到 Native 蓝牙时间同步实现的 JNI 桥 |
| `ble_time_sync/*` | BLE 时间同步协议、状态机和每设备会话 |
| `protocol_adapter.cpp` | TCP 8801/8802 服务、协议解析、状态/故障上报、视频发送 |
| `packet_codec.cpp/.h` | 手机 TCP 协议包解析、序列化、帧封装 |
| `OperationCoordinator.cpp/.h` | 合并本地和手机操作，维护权威运行模式 |
| `platform_state_bridge.cpp/.h` | Java 平台状态到 Native TCP 状态包的桥 |
| `SdkStateBridge.cpp/.h` | Native 采集状态、存储和故障状态汇总 |
| `DatasetExporter.cpp/.h` | 后台扫描并导出完整数据集 |
| `NativeLogger.cpp/.h` | Native logcat、应用文件日志、数据集文件日志和日志轮转 |
| `DatasetRecorder.cpp` | 开始数据集日志、结束时刷新并关闭数据集日志 |
| `main.cpp` | Native 总入口；启动日志、协议服务和导出 JNI；销毁时统一停止 |

---

## 3. 应用和后台连接服务的启动

### 3.1 AndroidManifest 声明

当前 Manifest 声明了以下与本流程有关的权限：

- `INTERNET`
- `RECEIVE_BOOT_COMPLETED`
- `FOREGROUND_SERVICE`
- `FOREGROUND_SERVICE_CONNECTED_DEVICE`
- `READ_EXTERNAL_STORAGE`
- `WRITE_EXTERNAL_STORAGE`
- `MANAGE_EXTERNAL_STORAGE`
- Android 旧版蓝牙权限
- `BLUETOOTH_SCAN`
- `BLUETOOTH_CONNECT`
- `BLUETOOTH_ADVERTISE`
- `ACCESS_FINE_LOCATION`
- `ACCESS_COARSE_LOCATION`
- `CHANGE_WIFI_STATE`
- `CHANGE_NETWORK_STATE`
- `ACCESS_NETWORK_STATE`
- `ACCESS_WIFI_STATE`
- `WAKE_LOCK`

`BleService` 具有以下特征：

- `android:exported="false"`
- `android:directBootAware="true"`
- `foregroundServiceType="connectedDevice"`
- 运行在独立进程 `com.ssnwt.egoserver`

因此，BLE 生命周期与主采集 Activity 不完全绑定。主 Activity 未运行时，开机接收器仍可启动 BLE 服务。

### 3.2 开机启动

`BootReceiver` 接收：

- `BOOT_COMPLETED`
- `LOCKED_BOOT_COMPLETED`
- `QUICKBOOT_POWERON`
- HTC Quick Boot 广播

收到有效启动广播后：

```text
BootReceiver
  → startForegroundService(BleService)
  → 若抛出异常
  → 主线程延迟 5 秒重试一次
```

### 3.3 BLE 服务创建

`BleService.onCreate()` 的顺序是：

1. 创建通知渠道。
2. 以常驻通知启动前台服务。
3. 创建并启动 `HotspotManager`。
4. 创建 `WifiConnector`。
5. 创建 `BleServerManager`。
6. 注册控制通道就绪和设备断开监听。
7. 调用 `nativeInitBleService(filesDir)` 初始化 Native 日志环境。
8. 创建 `BleAidlImpl`。
9. 启动 BLE 广播。

如果厂商 `AndroidInterface` 尚未初始化，服务先请求初始化；成功后再广播。广播启动失败时，主线程每隔 1 秒重新尝试。

`onStartCommand()` 返回 `START_STICKY`，Android 回收服务后可以重建它。

### 3.4 主 Activity 绑定 BLE 服务

`VrNativeActivity` 使用 `bindService(..., BIND_AUTO_CREATE)` 绑定 `BleService`。

绑定完成后：

1. 得到 `IBleService` Binder。
2. 注册 `IBleCallback`。
3. 接收 BLE 命令、BLE 连接变化、Wi-Fi 成功或失败状态。

如果服务连接意外断开：

- Activity 清空 Binder 状态；
- 再次调用绑定流程。

---

## 4. 日志系统

## 4.1 Java 日志

Java 层主要使用 Android `Log.i/w/e/d`。

日志进入系统 logcat，覆盖：

- Activity 生命周期
- BLE 服务生命周期
- BLE 广播与 GATT 连接
- GATT 特征读写
- Wi-Fi 配网过程
- Wi-Fi IP 查询
- U 盘挂载、卸载和目录选择
- AIDL 注册与断开
- 电池、网络等平台状态

这些 Java 日志本身不全部进入 Native 的 `app.log`。需要排查 Java 流程时，仍应保留 logcat。

## 4.2 Native 统一日志入口

大部分核心 C++ 模块使用：

```text
NATIVE_LOGD
NATIVE_LOGI
NATIVE_LOGW
NATIVE_LOGE
```

每次调用同时执行两件事：

1. 调用 `__android_log_vprint()` 写入 logcat。
2. 通过 `spdlog` 写入文件。

文件中的每条日志包含：

- 日志等级
- `boot_ms`
- 模块 tag
- 正文

其中 `boot_ms` 来自单调启动时钟，用于把日志事件与相机、IMU 和协议事件放在同一启动时间轴上分析。

## 4.3 应用级文件日志

Native 引擎启动时调用：

```text
NativeLoggerInit(storagePath)
```

BLE 独立服务进程也会通过 `nativeInitBleService(filesDir)` 调用同一个初始化入口。

应用级日志路径为：

```text
<应用 filesDir>/logs/app.log
```

默认轮转策略：

- 单文件最大 32 MiB
- 保留 1 个轮转文件
- 使用多线程安全的 rotating file sink

## 4.4 数据集日志

每次新建录制数据集时，`DatasetRecorder` 调用：

```text
NativeLoggerStartDataset(datasetDir)
```

此后同一条 Native 日志会同时写入：

```text
<应用 filesDir>/logs/app.log
<datasetDir>/capture.log
```

录制结束时调用：

```text
NativeLoggerStopDataset()
```

停止过程先等待日志刷新，再移除当前数据集 sink。因此 `capture.log` 跟随该次数据集一起被 U 盘导出。

## 4.5 日志刷新线程

`NativeLogger` 内部有一个独立刷新线程：

- 正常情况下每 3 秒刷新一次；
- 切换数据集日志时可请求立即刷新并等待完成；
- 关闭 Logger 时发出停止请求、等待线程退出，然后释放 sink。

日志写入接口受互斥锁保护。它没有为每条日志创建线程。

## 4.6 U 盘专项调试日志

`VrNativeActivity` 另外维护：

```text
<externalFilesDir>/usb_debug.log
```

`appendUsbDebug()` 以追加方式写入：

- 墙上时钟日期时间，精确到毫秒
- U 盘广播 action
- StorageVolume 枚举结果
- volume path、state、removable、mounted、primary
- 可读写状态
- 导出根目录创建结果
- JNI 导出器启停记录

这份日志用于排查“系统是否识别 U 盘”和“为什么没有启动导出”，不属于某个采集数据集。

---

## 5. BLE 设备发现

## 5.1 BLE 服务结构

当前设备作为 BLE Peripheral 和 GATT Server。

`BleServerManager` 创建两组 GATT 服务：

### Wi-Fi 配网服务

服务 UUID：

```text
0000FFE0-0000-1000-8000-00805F9B34FB
```

特征：

| 特征 | UUID | 方向 | 用途 |
|---|---|---|---|
| Wi-Fi SSID | `FFE1` | 手机写入 | 目标 Wi-Fi 名称 |
| Wi-Fi Password | `FFE2` | 手机写入 | 目标 Wi-Fi 密码 |
| Wi-Fi Status | `FFE3` | 手机写触发；设备通知 | 触发连接并返回连接状态 |
| IP Address | `FFE4` | 设备通知/读取 | 返回设备连接 Wi-Fi 后的 IP |

### BLE 控制服务

服务 UUID：

```text
a1b2c3d4-e5f6-7890-abcd-1234567890ab
```

特征：

| 特征 | UUID 尾部 | 方向 | 用途 |
|---|---:|---|---|
| Control Command | `0005` | 手机写入 | BLE 控制或时间同步 JSON |
| Command Response | `0006` | 设备通知 | 普通成功响应 |
| Error Message | `0007` | 设备通知 | 失败响应 |

## 5.2 BLE 广播

广播只有在所有 GATT 服务添加成功后才正式开始。

广播数据包含厂商数据，厂商 ID 为 `4884`。设备序列号由厂商 Android 接口获取，并用于设备识别；实现会根据载荷大小对序列号做安全截断。

广播状态由以下字段保护：

- `isAdvertising`
- `gattServicesReady`
- `advertisingStartPending`

这样可以避免 GATT 服务尚未建立时过早广播，也避免重复启动广播。

## 5.3 多设备与会话所有权

`BleServerManager` 维护已连接设备集合和通知订阅。

Wi-Fi 配网过程只允许一个设备占用当前配网会话：

- 第一个写入配网数据的设备成为 provisioning device；
- 其他设备的配网写入会被拒绝；
- 配网结束或设备断开后清理 pending SSID、密码和订阅状态。

SSID 最多 32 字节，密码最多 64 字节。当前实现不接受 prepared write 或非零 offset 的分片写入。

---

## 6. 手机通过 BLE 配置 Wi-Fi

完整流程如下：

```text
手机发现 BLE 设备并连接
  → 订阅 FFE3 Wi-Fi 状态通知
  → 订阅 FFE4 IP 地址通知
  → 写 FFE1：SSID
  → 写 FFE2：密码
  → 写 FFE3：0x01
  → BleServerManager 校验参数
  → BleAidlImpl 收到 onWifiProvisionRequested
  → Wi-Fi 状态通知 CONNECTING
  → WifiConnector.connectWifi()
  → 成功：通知 CONNECTED 和 IP
  → 延迟 750 ms 结束配网会话
```

### 6.1 Wi-Fi 状态值

| 值 | 含义 |
|---:|---|
| 0 | IDLE |
| 1 | CONNECTING |
| 2 | CONNECTED |
| 3 | FAILED |

### 6.2 FFE3 触发检查

设备收到 FFE3 写入后检查：

- 载荷是否为一个字节；
- 值是否为 `0x01`；
- SSID 是否已经写入；
- 密码是否已经写入；
- 回调监听器是否存在。

任一条件不满足时，返回失败状态，而不会开始 Wi-Fi 连接。

### 6.3 WifiConnector 的连接过程

`WifiConnector.connectWifi()`：

1. 检查 SSID 和密码。
2. 若当前已经连接到同一 SSID，并且已经取得 IP，则直接成功。
3. 否则调用厂商 Wi-Fi 工具连接。
4. 每 2 秒轮询一次连接状态。
5. SSID 匹配但 IP 尚未分配时继续轮询。
6. 达到连接超时后回调失败。

取得 IP 时遍历 `ConnectivityManager.getAllNetworks()`，选择具有 Wi-Fi transport 的网络，再从对应 LinkProperties 中取得地址。

### 6.4 成功后的手机切换

设备通过 BLE 通知：

- Wi-Fi 状态为 CONNECTED；
- 设备 STA IP 地址。

手机收到 IP 后，应在与设备可达的 Wi-Fi 网络中建立：

- TCP 8801 控制连接；
- 需要预览时再建立 TCP 8802 视频连接。

BLE 在此后仍可保留，用于时间同步和轻量控制，但大数据传输不走 BLE。

---

## 7. 设备热点

`BleService` 创建时同时启动 `HotspotManager`。

当前热点 SSID：

```text
BOARD
```

热点由厂商 Wi-Fi 接口启动。`HotspotManager` 会检查热点状态并避免重复请求。

服务销毁时的 `stop()` 当前不会强制关闭设备热点；代码注释将热点视为设备级服务，生命周期不完全归属于单次 BLE 服务实例。

因此，当前系统支持两种 Wi-Fi 关系：

- 设备作为 STA，连接手机通过 BLE 下发的目标 Wi-Fi；
- 设备启动自身热点，手机连接设备热点。

无论采用哪一种，只要手机能够访问设备 IP，后续 TCP 控制协议相同。

---

## 8. BLE 控制通道与时间同步

## 8.1 普通 BLE 控制命令

手机向 Control Command 特征写入 UTF-8 字符串。

调用链：

```text
BleServerManager.onCharacteristicWriteRequest
  → onControlCommand(device, command)
  → BleAidlImpl
  → 先交给 BleService 判断是否为时间同步命令
  ├── 是：进入 Native BLE time-sync
  └── 否：通过 AIDL callback 转交 VrNativeActivity
```

Activity 产生响应时可调用 `IBleService.sendCommandResponse()` 或 `sendErrorMessage()`，最终通过特征通知发给手机。

## 8.2 时间同步命令

当前识别的时间同步类型：

- `start_time_sync`
- `time_sync_request`
- `cancel_sync`
- `sync_result`
- `get_time_sync_status`

每个 BLE 设备地址对应一个独立 Native 时间同步 handle：

```text
BluetoothDevice address
  → BleService.timeSyncHandles
  → nativeCreateTimeSyncHandle()
  → BleTimeSyncService
```

控制通道通知订阅完成时，设备调用：

```text
nativeOnBleReady(handle, SystemClock.elapsedRealtimeNanos())
```

收到同步命令时调用：

```text
nativeOnBleCommand(handle, commandJson, recvBootTimeNs)
```

这里传入的是 Android `elapsedRealtimeNanos()`，与设备单调启动时间域一致。

设备断开时：

1. `nativeOnBleDisconnected(handle)`
2. `nativeDestroyTimeSyncHandle(handle)`
3. 从 Java Map 删除该地址会话

这避免不同手机或重连会话共享上一连接的时间同步状态。

## 8.3 与采集内部同步的区别

必须区分两种同步：

- BLE 时间同步：估计手机时间与设备时间的关系。
- 采集内部同步：用相机中间曝光时间对齐 Head Pose、双手和其他传感器。

BLE 时间同步不是相机帧内部对齐的替代品。它解决跨设备时间关系，采集内部同步解决同一设备各数据源的时间关系。

---

## 9. Wi-Fi/TCP 手机控制

Native 引擎启动时调用：

```text
protocol_adapter::Start(storagePath)
```

退出时调用：

```text
protocol_adapter::Stop()
```

协议服务创建两个 TCP 监听端口：

| 端口 | 用途 |
|---:|---|
| 8801 | 命令、响应、状态和事件 |
| 8802 | RGB 编码视频流 |

## 9.1 TCP 8801 控制连接

控制服务执行：

```text
socket
  → SO_REUSEADDR
  → bind(0.0.0.0:8801)
  → listen
  → accept
  → recv
  → 按 EG 帧格式拆包
  → protobuf/协议消息解析
  → DispatchCommand
```

协议外层帧：

- Magic：`0x4547`
- 固定头长度：6 字节
- 最大 payload：16 MiB
- 当前协议版本：1

TCP 是字节流，单次 `recv()` 不保证对应一个完整消息。因此代码保留接收缓存，处理：

- 半包
- 多包粘连
- 非法前缀
- 超长 payload
- 解析失败

## 9.2 支持的手机命令

| 命令 | 数值 | 行为 |
|---|---:|---|
| START_COLLECT | 1 | 请求开始手机控制的采集 |
| STOP_COLLECT | 2 | 请求停止采集 |
| START_VIDEO | 3 | 开启手机预览，并返回视频端口 8802 |
| STOP_VIDEO | 4 | 停止手机预览 |
| HEARTBEAT | 5 | 更新控制连接心跳并返回响应 |
| GET_PARAM | 10 | 读取支持的参数，目前包含自定义 NTP 操作 |
| SET_PARAM | 11 | 设置支持的参数，目前包含自定义 NTP 操作 |
| REBOOT | 20 | 请求设备重启 |

命令不是直接随意操作编码器，而是经过 `OperationCoordinator`。这样本地按键、手机采集和手机预览可以落入统一状态机。

## 9.3 操作模式

设备向手机报告以下权威模式：

| 值 | 模式 |
|---:|---|
| 0 | Idle |
| 1 | Phone Preview |
| 2 | Local Record |
| 3 | Local Record With Preview |
| 4 | Phone Record |

同时报告阶段：

- Stable
- Starting
- Stopping
- Error

这两个字段应作为手机 UI 的状态依据，不应只根据按钮点击结果推断设备状态。

## 9.4 响应码

当前协议定义：

| 代码 | 含义 |
|---:|---|
| 0 | OK |
| 100 | Unknown Command |
| 101 | Invalid Param |
| 102 | Device Busy |
| 200 | Storage Full |
| 300 | Wi-Fi Disconnected |
| 400 | Camera Failed |
| 401 | Microphone Failed |
| 500 | Internal Error |

## 9.5 状态上报

控制连接存在时，服务默认每 1 秒构造一次状态包。权威状态变化也可主动唤醒发送。

状态内容包括：

- 电池电量
- 是否充电
- 电池电压
- 电池温度
- Wi-Fi RSSI
- Wi-Fi SSID
- Wi-Fi 信道
- 操作模式
- 操作阶段
- 状态 revision
- 总存储空间
- 剩余存储空间
- 外设健康状态

Java `VrNativeActivity` 负责采集电池和 Wi-Fi 平台信息，再通过 JNI/bridge 更新 Native 快照。Native 将平台快照与 SDK 采集状态组合成对外状态包。

## 9.6 故障事件

协议支持：

- Fault raised
- Fault cleared
- Connection changed

故障包含：

- code
- description
- level
- raise time

Wi-Fi 曾连接但随后不可用时，协议层会产生 Wi-Fi 连接异常事件；恢复后清除相应状态，避免每次循环重复上报同一故障。

## 9.7 心跳和断开

HEARTBEAT 更新最近心跳时间。

控制 socket 出现以下情况时结束当前客户端会话：

- 对端正常关闭；
- `recv()` 返回不可恢复错误；
- 发送失败；
- 服务停止。

监听线程继续存活，可接受手机重新连接。

---

## 10. TCP 8802 实时 RGB 视频

## 10.1 视频来源

TCP 视频不是重新采集或软件转码。

数据路径：

```text
RGB CameraEncoder
  → MediaCodec 编码输出
  → IRgbEncodedSink
  → protocol_adapter 视频队列
  → TCP 8802
  → 手机解码显示
```

因此，落盘视频和手机预览共享同一 RGB 硬件编码结果，避免额外编码负载。

## 10.2 视频连接

手机先在 8801 发送 START_VIDEO。

设备：

1. 通过 `OperationCoordinator` 请求预览。
2. 返回视频端口 `8802`。
3. 手机再连接 8802。
4. 视频服务接受连接，并标记客户端需要 bootstrap。

## 10.3 新客户端解码启动

新手机连接后不能只从任意 P 帧开始解码。

协议层缓存编码参数和关键帧相关数据。新客户端连接时先发送 bootstrap，使解码器获得必要的 codec config/关键帧，然后再发送后续实时帧。

编码数据如果是 length-prefixed NAL，发送前可转换为 Annex-B。

## 10.4 视频线程和背压

相机编码输出线程不直接阻塞等待网络。

协议层使用受保护的视频队列，把编码器生产和 socket 发送解耦：

- 编码输出放入队列；
- 视频网络线程从队列取帧；
- 网络发送失败时关闭客户端；
- 向 `OperationCoordinator` 报告网络错误；
- 等待新的 8802 客户端。

这种结构避免慢手机直接长时间阻塞 MediaCodec 输出处理，但队列仍必须有限制，否则慢网络会造成内存无限增长。当前代码包含对过大 payload 和队列压力的丢帧保护。

---

## 11. U 盘识别

## 11.1 Activity 初始化

Activity 获取：

```text
getExternalFilesDir(null)
```

内部数据集根目录为：

```text
<externalFilesDir>/dataset
```

同时检查 `Environment.isExternalStorageManager()`。若没有“所有文件访问”权限，则打开应用专属或全局管理外部存储权限设置页。

## 11.2 U 盘广播

Activity 动态注册接收：

- `Intent.ACTION_MEDIA_MOUNTED`
- `Intent.ACTION_MEDIA_EJECT`
- `com.ssnwt.action.MEDIA_MOUNTED`
- `com.ssnwt.action.MEDIA_EJECT`

系统媒体广播使用 `file` data scheme。

每次收到广播后，不直接相信广播携带的路径，而是重新执行：

```text
refreshExportUsbRoot()
```

Activity 启动并完成初始化时也会主动执行一次，因此 U 盘早于应用插入时仍可被发现。

## 11.3 可移动卷筛选

`findMountedUsbRoot()` 使用 `StorageManager.getStorageVolumes()` 遍历卷。

选择条件：

1. `volume.getState()` 等于 `MEDIA_MOUNTED`
2. `volume.isRemovable()` 为 true
3. `volume.getDirectory()` 非空

选中后使用该目录绝对路径作为 U 盘根目录。

当前逻辑返回第一个符合条件的可移动卷，没有提供多 U 盘选择界面。

## 11.4 导出根目录

设备在 U 盘根目录下创建：

```text
<USB_ROOT>/Export
```

只有目录已存在或 `mkdirs()` 成功，才调用：

```text
nativeStartExporter(<USB_ROOT>/Export)
```

如果检测到的导出根路径和当前活动路径相同，不重复启动导出线程。

若换成另一块 U 盘：

1. 停止旧导出器；
2. 使用新路径启动导出器。

若没有检测到可移动卷而之前存在活动导出：

1. 调用 `nativeStopExporter()`；
2. 清空活动导出路径；
3. 播放 U 盘卸载提示音。

---

## 12. JNI 到 Native 导出器

### 12.1 启动

`nativeStartExporter(exportPath)`：

1. 检查全局 Native engine 是否存在。
2. 读取 Java 传入的 U 盘导出路径。
3. 计算本地数据集路径：

```text
<storagePath>/dataset
```

4. 先停止可能已存在的导出线程。
5. `DatasetExporter.init(datasetPath)`
6. `DatasetExporter.start(exportPath)`

### 12.2 停止

`nativeStopExporter()` 调用 `DatasetExporter.stop()`：

- 将原子变量 `mRunning` 设为 false；
- 唤醒条件变量；
- `join()` 等待导出线程退出。

因为文件复制循环也检查 `mRunning`，拔出 U 盘后停止请求能够中断后续数据集处理。

---

## 13. 数据集导出完整流程

## 13.1 导出线程

`DatasetExporter.start()` 只创建一个工作线程。若已经运行，重复 start 会被忽略。

线程循环：

```text
读取 datasetPath 和 exportPath
  → 列举本地 dataset 目录下的条目
  → 逐个判断数据集是否 complete
  → 导出所有 complete 数据集
  → 播放成功、失败或无数据提示
  → 条件变量等待 300 秒
  → 再次扫描
```

因此 U 盘保持插入时，新录制完成的数据集最迟会在下一次 5 分钟扫描中被导出，不要求重新插拔 U 盘。

## 13.2 完整数据集判定

每个候选目录读取：

```text
<datasetDir>/capture_status.json
```

只有文件内容包含：

```json
"state": "complete"
```

才允许导出。

以下目录不会导出：

- 正在录制的数据集；
- 非正常终止且未标记 complete 的数据集；
- 缺少 `capture_status.json` 的目录；
- 状态文本不匹配的目录。

这保证 U 盘不会拿到一个仍在持续写入的视频和 CSV 集合。

## 13.3 临时目录

假设本地数据集名为：

```text
20260724_103000
```

导出过程先创建：

```text
<USB_ROOT>/Export/20260724_103000.tmp
```

而不是直接创建最终目录。

复制成功后才执行：

```text
rename(
  20260724_103000.tmp,
  20260724_103000
)
```

因此，U 盘侧看到 `.tmp` 表示导出尚未完成；没有 `.tmp` 后缀的目录才是已完成提交的导出结果。

## 13.4 文件复制

当前 `copyFile()` 使用 POSIX 文件接口：

```text
open(src, O_RDONLY)
open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666)
循环 read/write
fsync(dst)
close
```

复制缓冲区为：

```text
1 MiB
```

实现处理：

- `read()` 被 `EINTR` 中断后重试；
- `write()` 短写，通过循环写完当前块；
- 目标文件复制完成后 `fsync()`；
- 任一文件失败则删除整个 `.tmp` 目录。

## 13.5 导出成功后的本地删除

最终目录重命名成功后，导出器删除设备本地原数据集：

```text
USB 完整目录已生成
  → 删除本地数据集内各文件
  → rmdir 本地数据集目录
```

所以当前行为本质上是“迁移到 U 盘”，不是“在 U 盘保留副本、设备继续保留原件”。

这一点非常重要：

- 导出成功后，本地数据集不再存在；
- 如果产品需求是双份保留，需要改变现有删除策略；
- 如果 USB 已成功但本地删除失败，函数仍返回失败并记录告警。

## 13.6 导出失败

下列情况会导致当前数据集导出失败：

- U 盘目录创建失败；
- 源文件无法打开；
- 目标文件无法创建；
- U 盘空间不足；
- 复制期间 U 盘被拔出；
- read/write/fsync 相关错误；
- `.tmp` 重命名失败；
- 本地目录删除失败。

失败时：

- 记录 Native 日志；
- 尝试删除 `.tmp`；
- 保留尚未成功迁移的本地数据集；
- 本轮结束后播放导出失败提示。

## 13.7 当前实现的校验边界

当前实现已具备：

- 完整状态门槛；
- 临时目录；
- 文件逐字节复制；
- 目标文件 `fsync()`；
- 复制失败清理；
- 成功后目录重命名。

当前实现没有：

- SHA-256、MD5 或 CRC 校验；
- 复制后逐文件重新读取比对；
- 文件大小清单；
- 导出 manifest；
- U 盘总剩余空间的预估检查；
- 同名最终目录的覆盖或版本策略；
- 跨层级递归复制。

`DatasetExporter` 当前按数据集目录的直接子项逐个调用 `copyFile()`，默认数据集根下是普通文件。如果数据集内部增加子目录，现有导出器不能递归复制它们。

这部分是对当前代码能力边界的说明，不代表 U 盘导出功能不存在。

---

## 14. U 盘拔出与并发

### 14.1 正常待机时拔出

```text
MEDIA_EJECT
  → refreshExportUsbRoot
  → 找不到 mounted removable volume
  → nativeStopExporter
  → join 导出线程
  → 清空 mActiveExportRoot
```

### 14.2 复制过程中拔出

可能出现：

- 正在执行的 read/write 返回错误；
- 导出线程将当前任务标记失败；
- `.tmp` 清理可能因为卷已卸载而失败；
- Java 收到 EJECT 后同时请求停止导出器；
- `stop()` 最终等待工作线程退出。

代码通过 `mRunning` 原子变量和线程 join 保护导出器生命周期，但物理拔盘无法保证所有文件系统调用都立即返回。

### 14.3 与录制并发

导出线程与采集写入线程可以同时存在，但它只选择 `capture_status.json` 为 complete 的旧数据集。

因此正常状态下：

- 当前录制目录持续写入；
- 当前目录状态不是 complete；
- 导出器跳过它；
- 录制关闭并完成状态提交后，后续扫描才导出。

---

## 15. 线程与进程模型

| 所属 | 线程/执行上下文 | 主要工作 |
|---|---|---|
| 主应用进程 | Activity 主线程 | 生命周期、AIDL 回调、广播、U 盘检测 |
| BLE 独立进程 | BLE Service 主线程 | 服务生命周期、GATT 回调分发、Wi-Fi 状态更新 |
| BLE 独立进程 | Android BLE Binder/GATT 回调 | 连接、读写和通知结果 |
| BLE 独立进程 | Main Handler | 串行化广播重试、配网和回调 |
| Native | Logger flush thread | 每 3 秒及按请求刷新文件日志 |
| Native | TCP 8801 control thread | accept、recv、命令解析、状态发送 |
| Native | TCP 8802 video thread | accept、视频队列发送 |
| Native | DatasetExporter worker | 扫描、复制、fsync、重命名和删除 |
| Native | CameraEncoder output thread | 取得编码帧并投递给落盘和网络 sink |

### 15.1 BLE/Wi-Fi 配网链路

```text
手机 BLE 写 characteristic
→ Android BLE stack（平台内部线程/缓冲）
→ GATT callback（BLE 独立进程）
→ 复制 characteristic value 为 Java byte[]/String
→ Main Handler 串行执行配网任务
→ WifiConnector 调 Android Wi-Fi API
→ 状态写回 characteristic
→ notify 手机
```

- 项目没有建立一个 C++ `std::queue` 来传 BLE 包；
- BLE 包排队、分片、重传等主要由 Android Bluetooth stack 内部完成；
- 项目显式的串行化工具是 Java `Handler/Looper` 消息队列，它是 Android API 管理的应用级消息队列；
- GATT callback 不应执行长时间 Wi-Fi 连接操作，而是把任务 post 给 Handler；
- 跨 BLE Service 与 Activity 时走 AIDL/Binder，参数会被 Binder 序列化/复制，二者不共享 Java 对象或指针。

### 15.2 TCP 8801 控制链路

```text
手机 socket send
→ 内核 socket receive buffer
→ TCP control thread recv()
→ 线程局部接收/解析缓冲
→ 命令解析
→ OperationCoordinator
→ 采集状态机/Native 引擎
→ 构造响应
→ send() → 内核发送缓冲 → 手机
```

- 8801 由独立 control thread 负责 accept、recv、解析和回复；
- 项目没有为普通控制命令再建立一个媒体式 FIFO；
- TCP 字节首先进入内核 socket buffer，`recv()` 再复制到应用缓冲，这是操作系统边界上的复制；
- 解析后的命令是小型字符串/结构体，随后通过协调器触发状态转换；
- 控制线程绝不能直接承担视频发送或数据集文件复制，否则会拖延心跳和命令响应。

### 15.3 TCP 8802 视频链路

```text
CameraEncoder output thread
→ MediaCodec 输出 packet
→ IRgbEncodedSink
→ 复制/封装为队列拥有的 encoded payload
→ mutex 保护的 videoQueue（容量 24）
→ TCP video thread
→ send() 到内核 socket buffer
→ 手机
```

这里存在项目显式生产者—消费者队列：

- 生产者：RGB `CameraEncoder` output thread；
- 消费者：TCP 8802 video thread；
- 容器：受 mutex/CV 保护的编码帧队列；
- 容量：24；队列满时丢弃最旧帧，避免慢手机无限占用内存；
- 内存：编码 packet 需要复制/封装成队列能够独立拥有的 payload；出队后 socket `send()` 再把字节交给内核；
- 不传原始 RGB 像素，不进行第二次视频编码；
- codec bootstrap/关键解码数据另有缓存，用于新客户端从可解码位置开始。

因此 8802 是第二份文档中最典型的显式 queue 链路。落盘 writer 与网络队列是两个 sink：网络慢会触发积压/丢帧策略，不应阻塞本地 MP4 写入。

### 15.4 Native 日志链路

```text
任意业务线程 NATIVE_LOG*
→ 格式化一条日志文本
├→ logcat
└→ mutex 保护的当前文件 sink/流
    → Logger flush thread 周期 flush
    → 日志文件
```

- 多个业务线程是生产者，但当前机制更接近“互斥保护的共享文件 sink”，不是每条日志先进入显式 `std::queue` 再由单一 writer 写；
- mutex 防止多线程日志内容交叉写到一半；
- flush thread 负责周期性刷新，不代表所有实际 `write` 都只发生在 flush thread；
- 日志文本在格式化时形成应用内存，流/stdio 与文件系统还可能有平台内部缓冲；
- 切换普通日志与数据集 `capture.log` 时必须在锁保护下刷新并切换 sink，防止旧 session 的日志落入新文件。

### 15.5 U 盘导出链路

```text
Activity 主线程收到 MEDIA_MOUNTED
→ 枚举 StorageVolume 并得到路径
→ JNI nativeStartExporter
→ 启动唯一 DatasetExporter worker
→ worker 扫描 complete 数据集
→ 1 MiB 用户态复制缓冲
→ read 源文件 / write 目标 .tmp
→ fsync
→ rename .tmp 为正式目录
→ 删除本地源数据集
```

- Activity/JNI 只提交路径和启动请求，不在主线程复制文件；
- 只有一个显式 Exporter worker，不存在“读取线程 → 文件块 queue → 写线程”的流水线；
- 同一个 worker 顺序完成 read/write，每次使用约 1 MiB 缓冲；每块数据从内核 page cache 复制到用户缓冲，再交给目标文件写入路径；
- `.tmp` 目录是跨崩溃的事务状态，不是内存 queue；
- `fsync + rename` 用于保证只有完整复制的数据集才以正式目录出现；
- 导出只处理 `capture_status.json=complete`，从而避免与仍在录制的数据集并发读取。

### 15.6 各链路的显式队列结论

| 链路 | 生产者 → 消费者 | 项目显式缓冲 | 满/慢时行为 |
|---|---|---|---|
| BLE 配网 | GATT callback → Handler | Java Handler 消息队列 | 由 Looper 串行处理 |
| AIDL | BLE 进程 ↔ Activity | 无项目 C++ queue；Binder 内部缓冲 | 平台管理 |
| TCP 8801 控制 | socket → control thread | 无额外项目 FIFO；有内核 socket buffer | 断开/错误处理 |
| TCP 8802 视频 | encoder output → video thread | 有界 video queue，容量 24 | 丢最旧帧 |
| Native 日志 | 多业务线程 → 文件 sink | mutex 保护共享 sink；非日志 FIFO | 调用线程短暂等锁 |
| U 盘导出 | Activity/JNI → exporter worker | 启动状态；无文件块 queue | 单 worker 顺序复制 |

关键并发原则是：GATT 与 Activity 通过 AIDL 隔离；控制和视频使用不同线程/端口；日志 sink 用 mutex 保证完整写入；Exporter 单实例；只有视频发送以有界显式队列与编码输出解耦。

---

## 16. 从手机连接到采集的完整顺序

```text
设备开机
  → BootReceiver 启动 BleService
  → BleService 启动前台通知
  → 启动热点
  → 创建 GATT Server
  → GATT 服务全部 ready
  → BLE 广播

手机发现设备
  → BLE 连接
  → 订阅状态和响应特征
  → 写入 SSID/密码/连接触发
  → 设备连接 Wi-Fi
  → BLE 返回 Wi-Fi 状态和设备 IP

手机连接设备 IP:8801
  → 建立控制会话
  → 接收周期状态
  → 发送 HEARTBEAT
  → 发送 START_COLLECT
  → OperationCoordinator 请求采集
  → 数据集开始，capture.log 开始写入

需要预览
  → 手机发送 START_VIDEO
  → 设备返回 8802
  → 手机连接 8802
  → 收到 codec bootstrap 和实时 RGB 编码帧

停止采集
  → 手机发送 STOP_COLLECT
  → 数据文件全部关闭
  → capture_status.json 提交 complete
  → capture.log 刷新并关闭

若 U 盘已插入
  → Exporter 本轮或下一个 300 秒周期发现该数据集
  → 复制至 Export/<dataset>.tmp
  → rename 为 Export/<dataset>
  → 删除设备本地数据集
```

---

## 17. 从插入 U 盘到导出完成的完整顺序

```text
插入 U 盘
  → Android 挂载卷
  → Activity 收到 MEDIA_MOUNTED
  → StorageManager 枚举 StorageVolume
  → 选择 mounted + removable 的卷
  → 创建 USB_ROOT/Export
  → JNI nativeStartExporter
  → 停止旧 Exporter
  → 设置本地 datasetRoot 和 USB exportRoot
  → 启动一个工作线程
  → 扫描本地数据集目录
  → 读取每个 capture_status.json
  → 跳过非 complete
  → 删除遗留的同名 .tmp
  → 创建新 .tmp
  → 以 1 MiB 缓冲复制每个文件
  → 每个目标文件 fsync
  → 全部成功后 rename
  → 删除本地源数据集
  → TTS 提示导出完成
  → 等待 300 秒后复查
```

---

## 18. 服务停止和资源释放

### 18.1 Activity 销毁

Activity：

- 注销 BLE callback；
- 解绑 BLE 服务；
- 注销 USB 广播；
- 停止与 Activity 生命周期绑定的平台状态更新。

BLE 是前台 `START_STICKY` 服务，因此解绑不必然表示 BLE 服务销毁。

### 18.2 BLE 服务销毁

`BleService.onDestroy()`：

1. 调用 HotspotManager stop，但当前不强制关闭设备级热点。
2. 清空 Handler 回调。
3. 断开 WifiConnector 的待处理连接和超时。
4. 关闭 BleServerManager、停止广播和 GATT Server。
5. 遍历所有时间同步 handle。
6. 通知 Native 断开并销毁 handle。
7. 清空 AIDL 实现引用。

### 18.3 Native 引擎退出

Native 主循环 cleanup：

1. 停止相机和采集资源。
2. 停止操作协调器。
3. `protocol_adapter::Stop()` 关闭 TCP 服务和客户端。
4. `NativeLoggerShutdown()` 最终刷新并停止日志线程。

`DatasetExporter` 是 engine 成员，其析构函数会调用 `stop()`，保证工作线程不悬空。

---

## 19. 数据、控制和日志最终去向

### 19.1 设备内部

```text
<externalFilesDir>/
├── dataset/
│   └── <session>/
│       ├── capture_status.json
│       ├── capture.log
│       ├── 视频、音频和传感器文件
│       └── 其他采集元数据
├── logs/
│   ├── app.log
│   └── app.log.<轮转文件>
└── usb_debug.log
```

### 19.2 U 盘

```text
<USB_ROOT>/
└── Export/
    ├── <session-A>/
    │   ├── capture_status.json
    │   ├── capture.log
    │   ├── 视频、音频和传感器文件
    │   └── 其他采集元数据
    └── <session-B>.tmp/        # 仅在导出尚未完成或异常时可能出现
```

### 19.3 手机

手机实时获得：

- BLE 广播信息
- Wi-Fi 配网状态
- 设备 IP
- BLE 控制/时间同步响应
- TCP 设备状态
- TCP 命令响应
- TCP 故障和连接事件
- TCP RGB 实时编码视频

当前 U 盘导出是设备到可移动存储的文件迁移；不是通过 BLE 传文件，也不是通过 TCP 下载完整数据集。

---

## 20. 排障顺序

### 20.1 手机发现不到设备

依次检查：

1. `BleService` 是否已由 BootReceiver 启动。
2. 前台通知是否存在。
3. 蓝牙权限是否授予。
4. 厂商 AndroidInterface 是否初始化成功。
5. GATT 两组服务是否全部添加成功。
6. 广播是否因失败进入 1 秒重试。
7. logcat 中 `BleService`、`BleServerManager` 日志。

### 20.2 BLE 能连接但 Wi-Fi 配网失败

依次检查：

1. FFE1 SSID 是否超过 32 字节。
2. FFE2 密码是否超过 64 字节。
3. FFE3 是否写入单字节 `0x01`。
4. 手机是否订阅 FFE3/FFE4 通知。
5. 是否有另一手机占用 provisioning session。
6. 厂商 Wi-Fi API 是否返回成功。
7. SSID 已连接但 DHCP IP 是否仍为空。
8. 是否触发 WifiConnector 超时。

### 20.3 已配网但手机连不上 TCP

依次检查：

1. BLE 返回的是 STA IP 还是预期网络 IP。
2. 手机与设备是否处于可路由网络。
3. 8801、8802 是否被网络隔离或防火墙阻止。
4. `protocol_adapter` 是否已随 Native engine 启动。
5. 8801 是否已有其他控制客户端。
6. `app.log` 中 `control_listen`、`control_client_connected`。

### 20.4 手机控制正常但没有视频

依次检查：

1. START_VIDEO 响应是否成功。
2. 手机是否另行连接 8802。
3. RGB MediaCodec 是否已经 ready。
4. 视频客户端是否获得 bootstrap。
5. 是否出现 payload 过大或队列丢帧。
6. 是否出现 `video_send_failed`。

### 20.5 插 U 盘没有导出

依次检查：

1. `usb_debug.log` 是否收到 MOUNTED 广播。
2. `StorageVolume` 是否为 mounted。
3. `isRemovable()` 是否为 true。
4. `getDirectory()` 是否非空。
5. U 盘根目录是否可写。
6. `Export` 是否创建成功。
7. 是否取得 MANAGE_EXTERNAL_STORAGE 权限。
8. 本地数据集是否存在 `capture_status.json`。
9. 状态是否严格包含 `"state": "complete"`。
10. Native `DatasetExporter` 日志是否出现 open/write/rename 错误。

### 20.6 U 盘中存在 `.tmp`

说明最后一次导出未完成，常见原因：

- 复制中途拔盘；
- U 盘空间不足；
- 文件系统写入失败；
- 应用或设备异常退出；
- 最终 rename 失败。

下一次处理同一数据集时，代码会先尝试删除同名 `.tmp`，再重新复制。

---

## 21. 当前实现中必须明确的行为

1. BLE 主要负责发现、配网、少量命令和跨设备时间同步，不承载视频。
2. Wi-Fi 是承载手机正式控制和实时视频的网络。
3. TCP 8801 和 8802 分离，控制连接与视频连接必须分别建立。
4. 手机控制与本地操作统一进入 `OperationCoordinator`。
5. 手机应以设备上报的 operation mode/phase 为准，而不是自己猜测状态。
6. Native 日志同时进入 logcat 和文件；Java 日志主要依赖 logcat。
7. 每次录制产生独立 `capture.log`，它随数据集一起导出。
8. U 盘只导出 `complete` 数据集。
9. U 盘导出使用 `.tmp` 后重命名，避免把半成品当作完成目录。
10. 导出成功后删除设备本地源数据集，当前是迁移语义。
11. U 盘保持插入时，导出器每 300 秒重新扫描。
12. 当前导出器不做哈希校验，也不递归复制子目录。
13. `usb_debug.log` 是 U 盘识别的首要诊断依据。
14. BLE 时间同步和相机帧对传感器同步是两个不同层次的问题。

---

## 22. 一句话总结

当前 Android SDK 已经形成完整的设备闭环：BLE 负责设备发现、Wi-Fi 配网、轻量控制和时间同步，Wi-Fi/TCP 负责手机控制、状态上报和实时 RGB 视频，Native 日志同时覆盖应用与单次数据集，U 盘插入后由独立工作线程自动迁移所有已完成数据集，并通过 `.tmp`、`fsync` 和最终重命名降低半成品被误认的风险。
