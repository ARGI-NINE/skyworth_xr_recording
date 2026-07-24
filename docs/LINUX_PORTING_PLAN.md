# SXR EGO Linux 原生等功能重建框架设计

> 本文基于当前 SDK 1.5.0 的源码、`Readme.md`、`PROTOCOL.md` 和数据产物重新制定。  
> 本方案不是 Android 到 Linux 的移植方案，不追求复用 Android 工程结构，也不讨论 Java/JNI/AIDL 如何逐项替换。目标是在 Linux 上从零建立一个功能兼容的原生采集服务。

## 1. 文档目标

本文直接回答以下问题：

1. Linux 版本需要实现哪些功能，哪些行为必须与当前 SDK 一致；
2. 建议建立什么进程、模块、目录和代码文件；
3. 每个 `.h/.cpp` 负责什么，暴露什么接口，由谁调用；
4. 哪些模块开线程，线程由谁创建和停止；
5. 哪些通路使用队列、环形缓冲区或内存池；
6. 相机、IMU、音频、Head Pose、Hand、Controller 如何进入统一时间轴；
7. 视频编码、落盘、实时预览和动态挂载 writer 如何协同；
8. 程序启动、状态转移、故障、停止和恢复如何实现；
9. 如何验证 Linux 新系统与当前 SDK 功能一致。

本文给的是可以直接拆分研发任务的框架设计，不包含真实函数实现。

---

## 2. 重建结论

### 2.1 新系统不沿用 Android 应用框架

新系统不保留：

- APK、Gradle、Activity、Service、BroadcastReceiver；
- Java、JNI、AIDL、Binder；
- `android_main`、`native_app_glue`；
- Android system property、Android storage path；
- `AHardwareBuffer`、`ANativeWindow`、MediaCodec、AAudio、ASensor；
- 当前 `main.cpp` 中把相机、OpenXR、渲染、编码、协议和录制混合在一起的结构。

新系统采用：

- 一个由 systemd 托管的 Linux daemon；
- 普通 C++20 executable；
- 明确的 Camera、Tracking、Audio、IMU、Clock、Encoder、Dataset 接口；
- POSIX fd、epoll/eventfd/timerfd/signalfd；
- dma-buf 和 sync fence；
- V4L2/libcamera/自有相机 SDK；
- Vulkan、硬件 ISP 或 CPU 图像处理；
- V4L2 M2M/厂商硬编码；
- ALSA、IIO/自有 IMU SDK；
- BlueZ、NetworkManager 或 wpa_supplicant；
- journald、udev、sysfs。

### 2.2 功能一致，不要求内部实现一致

Linux 版必须保持的外部能力：

- BLE 配网和 BLE 四时间戳同步；
- Wi-Fi STA 连接、IP、SSID、RSSI、channel 和断网事件；
- TCP `8801` 控制、状态、故障和 custom NTP；
- TCP `8802` RGB HEVC Annex-B 预览；
- 五种权威业务模式和 `state_revision`；
- 本地录制、手机录制、预览、录制中动态开关预览；
- 快照；
- RGB、tracking、ctrl 视频；
- AAC 音频；
- accel、gyro；
- head pose；
- hand tracking 或 controller poses；
- 相机参数、IMU 标定；
- 当前数据集文件名、CSV/JSON schema 和时间语义；
- fragmented MP4；
- 磁盘不足、相机失败、麦克风失败、Wi-Fi 丢失、过热和系统故障。

Linux 版不要求：

- 使用 Qualcomm Android 相机库；
- 使用当前相机数量和物理拓扑；
- 使用 Android OpenXR runtime；
- 显示 XR 场景或头显相机预览；
- 保持当前类名和线程结构；
- 新相机的分辨率、FOV、内参和画质数值等于旧相机。

### 2.3 OpenXR 和渲染的定位

Head Pose、Hand、Controller 功能必须保留，但不应继续控制整个程序。

建议将其收敛为独立 `TrackingService`：

- 首选 backend 可以是 Linux OpenXR；
- 也允许自研 VIO/SLAM、手势算法或其他 tracking SDK；
- 上层只依赖 `ITrackingBackend`；
- XR 场景、cube、quad layer、头显相机预览、XR swapchain 渲染全部取消；
- 如果 OpenXR runtime 要求 graphics binding，则保留一个最小无画面 graphics context；
- 如果 runtime 支持 headless session，则不创建 graphics context；
- 图像 SBS、灰度转换和手势叠加属于媒体处理，不属于 XR 场景渲染。

---

## 3. 当前 SDK 的兼容基线

### 3.1 权威业务状态

以当前 `PROTOCOL.md` 顶部“录制与预览统一状态机”为准：

| OperationMode | 编码器 | 落盘 | TCP 8802 |
|---|---:|---:|---:|
| `MODE_IDLE` | 关 | 关 | 关 |
| `MODE_PHONE_PREVIEW` | 开 | 关 | 开 |
| `MODE_LOCAL_RECORD` | 开 | 开 | 关 |
| `MODE_LOCAL_RECORD_WITH_PREVIEW` | 开 | 开 | 开 |
| `MODE_PHONE_RECORD` | 开 | 开 | 开 |

阶段：

- `PHASE_STABLE`
- `PHASE_STARTING`
- `PHASE_STOPPING`
- `PHASE_ERROR`

每次关键状态变化递增 `state_revision`。所有 TCP 命令、本地按键、自动停止、低存储、网络故障和媒体异步完成事件都必须进入同一个串行协调队列。

### 3.2 兼容数据集

```text
dataset/<YYYYMMDD_HHMMSS>/
├── rgb.mp4
├── rgb_metainfo.csv
├── tracking.mp4
├── tracking_metainfo.csv
├── ctrl.mp4
├── ctrl_metainfo.csv
├── audio.m4a
├── audio_metainfo.csv
├── accel.csv
├── gyro.csv
├── head_pose.csv
├── hand_tracking.csv              # hand 模式
├── controller_poses.csv           # controller 模式
├── camera_params_rgb.json
├── camera_params_tracking.json
├── camera_params_ctrl.json
├── imu_calibration.json
└── capture_status.json
```

视频 metadata：

```text
frame_index,frame_id,pts_us,exposure_start_utc_ns,
exposure_duration_ns,gain,mid_exposure_utc_ns
```

音频 metadata：

```text
packet_index,pts_us,capture_utc_ns
```

### 3.3 编码基线

| 流 | 编码 | 布局 | 默认帧率 | 默认码率 |
|---|---|---|---:|---:|
| RGB | HEVC | 左右眼 SBS，2W×H | 30 | 8 Mbps |
| Tracking | HEVC | 左右灰度兼容布局 | 60 | 4 Mbps |
| Ctrl | HEVC | 左右灰度兼容布局 | 60 | 4 Mbps |
| Audio | AAC-LC | 44.1 kHz mono | — | 96 kbps |

约束：

- 视频关闭 B-frame；
- decode order 等于 presentation order；
- fMP4 每个 sample 一个 fragment；
- 文件 PTS 从确认后的第一帧 IDR 开始归零；
- TCP 8802 使用 RGB HEVC Annex-B；
- 文件 writer 和 TCP preview 可以同时消费同一个编码 access unit；
- 网络阻塞不得影响文件写入。

---

## 4. 总体架构

### 4.1 进程模型

第一版只使用一个主进程：

```text
egocollectd
├── Main/EventLoop
├── OperationCoordinator
├── CameraService
├── TrackingService
├── ImuService
├── AudioService
├── TimeService
├── MediaPipeline
├── DatasetService
├── ControlServer :8801
├── VideoServer :8802
├── BleService
├── WifiService
└── HealthService
```

辅助程序：

```text
egocollectctl       本机控制和状态查询
egocollect-replay   离线回放完整 pipeline
egocollect-calib    相机/IMU 标定工具
egocollect-diag     硬件和数据诊断
```

不建议第一版把相机、编码器、数据写入拆成多个进程。这样可以避免：

- dma-buf fd 跨进程传递；
- fence 所有权复杂化；
- 多进程崩溃恢复；
- 高带宽 IPC；
- 额外的帧复制。

BLE/Wi-Fi 将来可以拆成独立进程，但不是第一版要求。

### 4.2 分层

```text
┌────────────────────────────────────────────────────────────┐
│ Compatibility Layer                                       │
│ BLE / TCP 8801 / TCP 8802 / Dataset schemas               │
├────────────────────────────────────────────────────────────┤
│ Application Domain                                        │
│ OperationCoordinator / SessionCoordinator / FaultManager   │
├────────────────────────────────────────────────────────────┤
│ Capture and Media                                          │
│ Camera / Tracking / IMU / Audio / Sync / GPU / Encoder     │
├────────────────────────────────────────────────────────────┤
│ Stable Interfaces                                          │
│ ICameraBackend / ITrackingBackend / IVideoEncoder / IClock │
├────────────────────────────────────────────────────────────┤
│ Linux Backends                                             │
│ own-camera / OpenXR / VIO / V4L2 / ALSA / IIO / BlueZ      │
└────────────────────────────────────────────────────────────┘
```

### 4.3 总数据流

```text
Camera Drivers
    │ Frame + exposure timestamp + dma-buf
    ▼
CameraService
    ▼
FrameSynchronizer
    ▼ FrameSet
FrameRouter
    ├──────────────► SnapshotService
    ├──────────────► Calibration/diagnostics
    └──────────────► TimeAligner
                         │
                         ├── TrackingService
                         ├── ImuService
                         └── Audio clock mapping
                         ▼
                    AlignedFrameSet
                         ▼
                    ImageProcessor
                         ▼
                     VideoEncoder
                         ▼ EncodedAccessUnit
               EncodedPacketRouter
                  ├────────► FileSink/fMP4
                  └────────► TCP 8802
```

---

## 5. 建议仓库目录

```text
egocollect-linux/
├── CMakeLists.txt
├── cmake/
│   ├── Toolchain-aarch64.cmake
│   ├── FindOpenXR.cmake
│   └── BuildOptions.cmake
├── configs/
│   ├── egocollect.toml
│   ├── cameras.yaml
│   └── logging.toml
├── schemas/
│   ├── egocollect.proto
│   ├── dataset_schema.json
│   └── config_schema.json
├── include/ego/
│   ├── result.h
│   ├── ids.h
│   ├── clock_types.h
│   ├── frame_types.h
│   ├── sensor_types.h
│   ├── tracking_types.h
│   ├── media_types.h
│   ├── operation_types.h
│   └── capabilities.h
├── src/
│   ├── app/
│   ├── core/
│   ├── time/
│   ├── camera/
│   ├── tracking/
│   ├── imu/
│   ├── audio/
│   ├── media/
│   ├── dataset/
│   ├── protocol/
│   ├── connectivity/
│   └── platform/
├── apps/
│   ├── egocollectd/
│   ├── egocollectctl/
│   ├── replay/
│   ├── calibrate/
│   └── diagnose/
├── packaging/
│   ├── systemd/
│   ├── udev/
│   └── tmpfiles.d/
└── tests/
    ├── unit/
    ├── contract/
    ├── replay/
    ├── compatibility/
    ├── hardware/
    └── soak/
```

---

## 6. 公共类型文件

这些头文件不能 include OpenXR、V4L2、Vulkan、ALSA、BlueZ 或厂商头文件。

### 6.1 `include/ego/result.h`

职责：

- 定义统一错误类型 `Error`；
- 定义 `Result<T>` / `Status`；
- 定义错误 domain：camera、tracking、encoder、storage、network、time；
- 保存错误码、消息、底层 errno/driver code、是否可恢复。

主要接口：

```text
Status::Ok()
Status::Error(domain, code, message)
Result<T>::value()
Result<T>::error()
```

线程：无。  
内存池：无。  
注意：普通预期错误不能通过 exception 穿过模块边界。

### 6.2 `include/ego/ids.h`

职责：

- 强类型 ID：`CameraId`、`StreamId`、`SessionId`、`FrameId`；
- 避免把不同 uint64 ID 混用；
- 提供字符串序列化。

线程：无。  
内存池：无。

### 6.3 `include/ego/clock_types.h`

职责：

- 定义 `ClockDomain`；
- 定义 `Timestamp`、`ClockMapping`、`ClockQuality`；
- 所有 timestamp 必须同时携带数值和 domain。

时钟域至少包括：

```text
CameraHardware
ImuHardware
AudioHardware
Boottime
Monotonic
MonotonicRaw
RealtimeUtc
Tai
XrTime
```

线程：无。  
内存池：无。

### 6.4 `include/ego/frame_types.h`

职责：

- 定义图像 buffer、plane、fence、Frame、FrameSet；
- 规定 dma-buf fd 和 lease 所有权；
- 定义曝光、gain、crop、format、sequence metadata。

关键类型：

```text
UniqueFd
BufferPlane
ImageBuffer
CameraMetadata
CameraFrame
FrameSet
FrameSetQuality
```

要求：

- `ImageBuffer` 可移动，不可隐式复制；
- 跨线程必须持有 `BufferLease`；
- fence fd 只允许一个明确的 owner；
- 不能只传裸 `int fd`。

线程：无。  
内存池：对象本身由 FrameObjectPool 提供，像素 buffer 由驱动或 GPU pool 管理。

### 6.5 `include/ego/sensor_types.h`

职责：

- 定义 `AccelSample`、`GyroSample`、`AudioChunk`；
- 定义温度、电池、存储、Wi-Fi snapshot；
- 定义 sample validity 和 sequence。

线程：无。  
内存池：AudioChunk 的 PCM payload 使用 AudioBlockPool。

### 6.6 `include/ego/tracking_types.h`

职责：

- 定义 `Pose`、`PoseSample`；
- 定义左右手关节；
- 定义 controller pose/button；
- 定义 validity、confidence、tracking state；
- 内部四元数统一 `(x,y,z,w)`。

线程：无。  
内存池：无，tracking sample 为小对象。

### 6.7 `include/ego/media_types.h`

职责：

- 定义 `ProcessedFrame`；
- 定义 `FrameTag`；
- 定义 `EncodedAccessUnit`；
- 定义 codec config、NAL 类型、IDR、EOS、PTS/DTS。

要求：

- 编码 payload 使用引用计数的 EncodedBuffer；
- FileSink 和 NetworkSink 共享 payload，不重复复制。

### 6.8 `include/ego/operation_types.h`

职责：

- 定义五个 `OperationMode`；
- 定义四个 `OperationPhase`；
- 定义 `StateRevision`；
- 定义所有 coordinator event 和 stop reason；
- 值必须与 protobuf wire 定义一致。

### 6.9 `include/ego/capabilities.h`

职责：

- 汇总 camera、tracking、encoder、audio、IMU 能力；
- 启动时完成 capability negotiation；
- 记录实际启用的格式和降级项。

---

## 7. 应用入口文件

### 7.1 `apps/egocollectd/main.cpp`

职责：

- 只处理命令行参数；
- 创建 `Application`；
- 调用 `initialize()`、`run()`、`shutdown()`；
- 把退出码返回给 systemd。

禁止：

- 创建相机线程；
- 处理业务命令；
- 直接操作 encoder；
- 直接写数据文件。

线程：main thread。  
内存池：无。

### 7.2 `src/app/application.h/.cpp`

职责：

- 依赖装配；
- 按顺序初始化所有 service；
- 启动 systemd notify/watchdog；
- 控制全局 shutdown；
- 维护 service 的唯一所有权。

主要接口：

```text
initialize(config)
run()
requestShutdown(reason)
shutdown()
```

初始化顺序：

```text
Logger
→ Config
→ EventLoop
→ TimeService
→ Storage/SystemState
→ Camera/Tracking/IMU/Audio probe
→ Media/Dataset
→ OperationCoordinator
→ TCP/BLE/Wi-Fi
→ READY
```

停止顺序反向，但先由 OperationCoordinator 完成媒体统一停止。

线程：不额外创建线程；调用各 service 的 `start()`。  
内存池：创建并持有各专用 pool。

### 7.3 `src/app/service_registry.h/.cpp`

职责：

- 保存 service 引用；
- 提供显式 dependency access；
- 便于测试注入 fake/replay backend。

不做：

- 全局 singleton；
- 隐式 service locator；
- 自动创建线程。

### 7.4 `src/app/shutdown_controller.h/.cpp`

职责：

- 接收 SIGTERM、SIGINT、watchdog failure；
- 使用 eventfd 唤醒主事件循环；
- 保证 shutdown 只执行一次；
- 设置最大优雅停止 deadline。

线程：信号由 signalfd 在主事件循环处理，不建立异步 signal handler。

---

## 8. Core 业务状态文件

### 8.1 `src/core/operation_coordinator.h/.cpp`

这是业务状态唯一权威。

职责：

- 实现五模式、四阶段状态机；
- 所有事件串行处理；
- 递增 `state_revision`；
- 计算目标 `ResourcePlan`；
- 调用 Media、Dataset、Preview service；
- 处理异步完成事件的 revision 校验；
- 触发主动 Status。

主要接口：

```text
post(OperationEvent)
snapshot()
waitUntilStable(timeout)
```

内部事件：

```text
PhoneStartCollect
LocalStartCollect
StopCollect
StartPreview
StopPreview
LowStorage
CameraFailure
EncoderFailure
NetworkFailure
WriterArmed
WriterFinalized
MediaStopped
Shutdown
```

线程：

- 独立一个 coordinator thread；
- 只有该线程可以修改 OperationState；
- TCP、BLE、按键、health 线程只能 `post()`。

队列：

- 有界 MPSC command queue；
- 容量建议 256；
- 控制事件不得静默丢弃；
- 队列满属于系统故障。

内存池：

- 事件都是小对象，不需要全局池；
- 队列节点可预分配 256 个，避免故障风暴时分配失败。

锁规则：

- coordinator 内部状态只由单线程访问，通常不需要状态 mutex；
- 不在 coordinator callback 中执行 join、send、flush、drain；
- 长操作返回 future/event，并携带发起 revision。

### 8.2 `src/core/operation_state.h/.cpp`

职责：

- 保存 mode、phase、revision；
- 纯函数验证状态转移；
- 从状态推导 `isRecording`、`shouldEncode`、`shouldStream`；
- 生成 wire snapshot。

线程：无独立线程。  
内存池：无。

### 8.3 `src/core/resource_plan.h/.cpp`

职责：

- 将 OperationState 转换为资源目标：

```text
encoderRequired
datasetRequired
previewRequired
trackingRequired
audioRequired
imuRequired
```

- 比较当前 plan 与目标 plan；
- 生成有序资源动作。

示例：

```text
PHONE_PREVIEW → PHONE_RECORD
保持 encoder
保持 8802
创建 dataset
arm writer
请求 IDR
```

线程：由 coordinator thread 调用。

### 8.4 `src/core/fault_manager.h/.cpp`

职责：

- 管理活动 fault 和 cleared fault；
- 去重、升级、恢复；
- 映射当前协议故障码；
- 决定 WARN/ERROR/FATAL；
- 将 fatal fault 投递 coordinator。

线程：

- 无独立线程；
- 内部使用短临界区 mutex；
- 不能在锁内发送网络消息。

### 8.5 `src/core/health_service.h/.cpp`

职责：

- 周期检查最后 camera/IMU/audio sample 时间；
- 检查队列深度、drop、encoder latency；
- 检查存储、温度、内存、fd 数量；
- 生成 health snapshot；
- 超阈值时通知 FaultManager。

线程：

- 不开专用 thread；
- 由主 EventLoop 的 timerfd 每秒触发；
- 耗时 sysfs 查询放入低优先级 platform worker。

### 8.6 `src/core/event_bus.h/.cpp`

职责：

- 只传递低频 control/status event；
- 不传图像、PCM 或 IMU 高频数据；
- 支持订阅 OperationState、fault、connectivity。

线程：

- callback 在发布者线程执行会导致耦合，因此统一投递到 EventLoop；
- 订阅列表只在初始化阶段修改。

---

## 9. 时间系统文件

时间系统是新框架的核心。

### 9.1 `src/time/system_clock.h/.cpp`

职责：

- 封装 `CLOCK_BOOTTIME`、`CLOCK_REALTIME`、`CLOCK_MONOTONIC_RAW`、`CLOCK_TAI`；
- 提供成对采样；
- 读取 clock resolution；
- 不允许业务模块直接调用 `clock_gettime()`。

主要接口：

```text
now(domain)
samplePair(domainA, domainB)
```

线程：无。

### 9.2 `src/time/clock_mapper.h/.cpp`

职责：

- 将 camera/IMU/audio hardware tick 映射到 boottime；
- 支持 offset 和 drift；
- 使用滑动窗口拟合：

```text
boottime_ns = scale × device_tick + offset
```

- 输出 uncertainty；
- 检测 reset、wrap、跳变和漂移异常。

线程：

- 不开线程；
- 每个 source 一个 mapper；
- producer thread 添加同步点；
- TimeService 查询时使用 immutable snapshot，避免高频锁竞争。

内存：

- 每个 mapper 固定长度样本环，建议 64～256 点；
- 不使用动态增长 vector。

### 9.3 `src/time/time_service.h/.cpp`

职责：

- 管理所有 ClockMapper；
- 统一转换为 boottime 和 UTC；
- 录制开始时冻结 session UTC mapping snapshot；
- 监控 realtime↔boottime offset 变化；
- 向 session manifest 输出 clock quality。

主要接口：

```text
toBoottime(Timestamp)
toUtc(Timestamp, SessionClockSnapshot)
createSessionSnapshot()
quality(source)
```

线程：

- 无独立 thread；
- mapper update 来自各 source；
-状态变化通过 EventLoop 上报。

### 9.4 `src/time/frame_time_aligner.h/.cpp`

职责：

- 以每个视频帧的 mid-exposure boottime 为锚点；
- 查询 head pose、hand、controller；
- 计算 IMU sample window；
- 标记数据有效性和时间误差；
- 生成 `AlignedFrameSet`。

对齐规则：

| 数据 | 方法 |
|---|---|
| Head Pose | 在 mid-exposure 时查询或插值 |
| Hand/Controller | 在 mid-exposure 时查询或插值 |
| IMU | 保留上一视频锚点到当前锚点之间的全部样本 |
| Audio | 用连续 sample clock 映射，不逐帧裁成文件 |
| 慢速状态 | last-known-value |

线程：

- 独立一个 aligner thread；
- 输入为 FrameSet bounded queue；
- 不能在 camera callback 中同步等待 OpenXR；
- Tracking 查询通过 TrackingService request queue 或本地 ring snapshot。

队列：

- 容量按最大相机帧率和允许延迟计算；
- 建议起始值 8 个 FrameSet；
- recording 路径满时上报并停止，不允许无限堆积。

### 9.5 `src/time/external_time_sync.h/.cpp`

职责：

- 实现 TCP custom NTP 和 BLE 四时间戳共享的数学工具；
- 计算 offset、RTT、过滤和统计；
- 不负责 socket 或 BLE transport；
- 两种协议会话状态必须独立。

线程：无；由各协议 service 调用。

---

## 10. Camera 文件

### 10.1 `src/camera/i_camera_backend.h`

职责：

- 定义相机 backend contract；
- 隔离自有相机 SDK、libcamera、V4L2。

主要接口：

```text
probe()
configure(CameraTopology)
start(FrameCallback)
stop()
getCalibration(CameraId)
getClockDescriptor(CameraId)
```

约束：

- callback 只交付 Frame，不做编码；
- callback 返回后 buffer 的生命周期由 lease 保证；
- backend 必须报告 timestamp clock domain；
- stop 返回后不再发生新 callback。

### 10.2 `src/camera/own_camera_backend.h/.cpp`

职责：

- 对接你们自己的驱动或 Camera SDK；
- 将厂商 buffer 转为公共 ImageBuffer；
- 转换 metadata；
- 管理 driver buffer dequeue/queue；
- 添加 hardware tick↔boottime 同步点。

线程：

- 若驱动为阻塞 `DQBUF`：一个 camera poll thread，可同时 poll 多个 fd；
- 若自有 SDK 已有 callback thread：不再额外创建 capture thread；
- callback 只构造 FrameEnvelope 并投递。

内存池：

- 图像 buffer 使用驱动预分配池；
- 建议每个 stream 6～10 个 buffer，按实际 pipeline latency测量；
- FrameEnvelope 使用固定对象池；
- callback 内禁止 malloc 大块像素内存。

### 10.3 `src/camera/camera_topology.h/.cpp`

职责：

- 从 `cameras.yaml` 加载物理相机和逻辑 stream；
- 定义 rgb_left/right、tracking_left/right、ctrl_left/right；
- 定义 sync group、输出布局、帧率、format；
- 不硬编码 `/dev/videoN`。

线程：无。

### 10.4 `src/camera/camera_service.h/.cpp`

职责：

- 选择 backend；
- probe/configure/start/stop；
- 维护 camera health 和最后帧时间；
- 将 Frame 投递 FrameSynchronizer；
- 响应热插拔/driver reset。

线程：

- 本身不开线程；
- backend 拥有 capture thread；
- control 方法由 coordinator 通过异步 command 调用。

### 10.5 `src/camera/frame_synchronizer.h/.cpp`

职责：

- 将单 camera Frame 组成 FrameSet；
- 支持 hardware sequence、trigger ID、timestamp matching；
- 计算跨相机 skew；
- 处理缺帧、迟到、乱序和 sequence reset。

线程：

- 独立 synchronizer thread；
- 输入为 MPSC queue；
- 输出为 SPSC queue 到 FrameTimeAligner。

队列与内存：

- 每个 camera 一个小型 reorder ring；
- 容量建议 `ceil(fps × max_wait_seconds) + 2`；
- 通常 4～8 帧；
- 超时 Frame 必须释放回 driver pool；
- 不允许用 map 无界增长。

### 10.6 `src/camera/calibration_repository.h/.cpp`

职责：

- 加载、校验和版本化相机/IMU 标定；
- 根据分辨率、crop、binning 选择内参；
- 提供 Camera→Body 和 IMU→Body 外参；
- 验证 device ID 和 checksum；
- 生成兼容 JSON view。

线程：

- 启动时读取；
- 运行期只读 immutable snapshot；
- 更新标定只允许 idle 状态并原子替换。

### 10.7 `src/camera/frame_object_pool.h/.cpp`

职责：

- 预分配 FrameEnvelope、FrameSet、AlignedFrameSet 小对象；
- 不管理相机像素内存；
- 提供 RAII handle；
- 统计池耗尽。

大小：

```text
FrameEnvelope ≥ 所有 driver buffers 总数 + 25%
FrameSet ≥ aligner queue + GPU in-flight + 2
```

池耗尽：

- preview 可丢旧帧；
- recording 记录 fatal backpressure 并有序停止；
- 禁止回退到无限 heap allocation。

### 10.8 `src/camera/frame_router.h/.cpp`

职责：

- 将 FrameSet 分发到 recording、preview、snapshot、diagnostics；
- 不复制像素；
- 每个 consumer 持有 lease；
- 实现不同 backpressure 策略。

线程：在 aligner thread 完成轻量分发，不做耗时处理。

---

## 11. Tracking 文件

### 11.1 `src/tracking/i_tracking_backend.h`

职责：

- 定义 Head、Hand、Controller 数据接口；
- 上层不依赖 OpenXR 类型。

主要接口：

```text
probe()
start()
stop()
queryHeadPose(boottimeNs)
queryHands(boottimeNs)
queryControllers(boottimeNs)
pollInputEvents()
```

### 11.2 `src/tracking/openxr_tracking_backend.h/.cpp`

职责：

- 创建 Linux OpenXR instance/session；
- 建立 root/local/view/action spaces；
- 创建 hand trackers 和 controller actions；
- 处理 session event；
- 将 boottime 转成 XrTime；
- 在指定历史时刻执行 locate；
- 将 XrPose 转成内部 Pose。

明确不做：

- 场景渲染；
- 相机纹理显示；
- XR swapchain 图像合成；
- cube、quad、KTX、UI。

线程：

- 一个专用 OpenXR owner thread；
- 所有 runtime 调用默认在该线程串行；
- tracking query 通过有界 request queue；
- owner thread 同时 pump OpenXR event 和最小 frame loop。

队列：

- query request queue 建议 128；
- response 写入 promise 或无锁 response slot；
- aligner 设置短 deadline；
- 超时标记 tracking invalid，不阻塞 camera capture。

### 11.3 `src/tracking/xr_session_loop.h/.cpp`

职责：

- 管理 `READY/RUNNING/STOPPING/EXITING`；
- 执行必要的 `xrWaitFrame/xrBeginFrame/xrEndFrame`；
- 提交零 layer 或 runtime 要求的最小 layer；
- 保证 tracking runtime 处于可提供数据状态。

线程：OpenXR owner thread 内运行，不另开线程。

### 11.4 `src/tracking/minimal_graphics_binding.h/.cpp`

职责：

- 只在 OpenXR runtime 不支持 headless 时创建最小 graphics binding；
- backend 可为 Vulkan/EGL/OpenGL；
- 不创建业务渲染资源；
- 提供 session create 所需 handles。

线程：只能由 OpenXR owner thread 创建和销毁。

### 11.5 `src/tracking/pose_ring_buffer.h/.cpp`

职责：

- 缓存连续 pose/hand/controller sample；
- 支持时间范围查询；
- position 插值；
- quaternion SLERP；
- 禁止超过阈值的远距离外推。

内存：

- 固定时间窗口 3～5 秒；
- 按最大 tracking rate 预分配；
- 单生产者单消费者时使用 SPSC ring。

### 11.6 `src/tracking/tracking_service.h/.cpp`

职责：

- 选择 OpenXR/VIO/replay backend；
- 对上提供统一 query；
- 保存 health、confidence、last sample；
- 将 controller button 转为 OperationEvent；
- 生成兼容 hand/controller CSV sample。

线程：backend owner thread；service 本身不再开线程。

### 11.7 `src/tracking/vio_tracking_backend.h/.cpp`

职责：

- 作为未来不依赖 OpenXR 的 Head Pose backend；
- 接收相机/IMU；
- 输出相同 Pose contract。

第一版若不用可以只保留接口 target，不实现产品逻辑。

---

## 12. IMU 文件

### 12.1 `src/imu/i_imu_backend.h`

接口：

```text
probe()
configure(rate, ranges)
start(ImuCallback)
stop()
clockDescriptor()
```

### 12.2 `src/imu/own_imu_backend.h/.cpp`

职责：

- 连接自有 IMU driver/SDK；
- 读取 accel/gyro；
- 提供 hardware timestamp；
- 处理 rollover/reset；
- 添加 clock mapping 同步点。

线程：

- 阻塞设备读取时一个 IMU capture thread；
- callback SDK 情况不额外创建；
- capture thread 不写 CSV。

### 12.3 `src/imu/imu_normalizer.h/.cpp`

职责：

- raw unit 转 m/s² 和 rad/s；
- axis 转 Body 坐标；
- 应用 bias/scale/nonorthogonality；
- 保留 raw 和 calibrated validity。

线程：在 IMU capture thread 做固定成本计算。

### 12.4 `src/imu/imu_ring_buffer.h/.cpp`

职责：

- 保存完整高频 IMU；
- 支持按 `[previousVideoAnchor, currentVideoAnchor]` 取窗口；
- 支持 recorder 独立顺序消费。

内存：

- 固定 2～5 秒；
- 1 kHz、双流时按最大 rate 预分配；
- 使用 sequence 检测覆盖；
- recording consumer 不能悄悄漏样。

### 12.5 `src/imu/imu_service.h/.cpp`

职责：

- backend 生命周期；
- normalizer；
- ring；
- health/drop 计数；
- 向 DatasetSession 提供顺序 sample stream。

线程：使用 backend capture thread，不另开线程。

---

## 13. Audio 文件

### 13.1 `src/audio/i_audio_backend.h`

接口：

```text
probe()
configure(sampleRate, channels, format, period)
start(AudioCallback)
stop()
clockDescriptor()
```

### 13.2 `src/audio/alsa_audio_backend.h/.cpp`

职责：

- ALSA PCM open/configure/read；
- 获取 hardware timestamp；
- 处理 xrun/recover；
- 报告实际 sample rate 和 latency。

线程：

- 一个 audio capture thread；
- 使用 blocking read/poll；
- 不在该线程执行 AAC 编码或文件写入。

### 13.3 `src/audio/audio_block_pool.h/.cpp`

职责：

- 预分配固定 PCM block；
- block 大小等于 ALSA period 或其整数倍；
- capture、encoder 间用 RAII handle。

大小：

```text
blockCount = ceil(maxAudioPipelineLatency / periodDuration) + safety
```

建议从 16～32 blocks 起测。

池耗尽：

- 记录 xrun/backpressure；
- recording 状态下触发音频故障；
- 不临时分配无限 PCM buffer。

### 13.4 `src/audio/aac_encoder.h/.cpp`

职责：

- PCM→AAC-LC；
- 输出 codec config；
- PTS 由累计 sample count 生成；
- 输出 AudioAccessUnit。

线程：

- 一个 audio encode thread；
- 输入 SPSC queue；
- 该线程拥有 AAC encoder handle。

### 13.5 `src/audio/audio_service.h/.cpp`

职责：

- backend、pool、AAC encoder 生命周期；
- 建立 audio hardware time→boottime mapping；
- 将 AudioAccessUnit 交给 DatasetSession；
- 维护 audio metadata。

---

## 14. Media 文件

### 14.1 `src/media/image_processor.h`

接口：

```text
probe()
configure(ProcessingGraph)
submit(AlignedFrameSet)
flush()
stop()
```

### 14.2 `src/media/vulkan_image_processor.h/.cpp`

职责：

- import dma-buf；
- 等待 acquire fence；
- YUV/RGB/gray format conversion；
- RGB 左右眼 SBS；
- tracking/ctrl 兼容布局；
- 可选 hand/controller overlay；
- 输出 encoder-compatible buffer；
- 输出 release fence。

线程：

- 一个 GPU submission thread；
- 所有 Vulkan queue submit 在该线程；
- CPU 线程不直接竞争同一个 VkQueue。

队列：

- recording 输入 queue 4～8；
- preview 不建立第二套处理，复用 RGB 编码结果；
- queue 满时不能无限增长。

内存池：

- 每个输出 stream 一个 GPU image pool；
- 每个 stream 建议 4～6 张，按 camera+GPU+encoder in-flight 实测；
- descriptor set、command buffer 预分配；
- shader pipeline 启动时创建，不逐帧创建。

### 14.3 `src/media/cpu_image_processor.h/.cpp`

职责：

- 提供像素正确性的 reference backend；
- 用于 CI、replay 和硬件诊断；
- 支持相同 ProcessingGraph。

不作为高分辨率量产默认路径。

### 14.4 `src/media/processing_graph.h/.cpp`

职责：

- 根据 topology 和配置生成静态 graph；
- 节点：import、convert、crop、scale、SBS、gray、overlay；
- 启动时验证格式兼容；
- 运行时不做动态 graph 重建。

### 14.5 `src/media/i_video_encoder.h`

接口：

```text
probe()
configure(EncodeConfig)
start(OutputCallback)
queue(ProcessedFrame, FrameTag)
requestIdr()
signalEos()
drain()
stop()
```

约束：

- `queue()` 明确传 capture PTS；
- `signalEos()` 和 `stop()` 不等价；
- stop 返回后 handle 已释放；
- 一个 encoder handle 只有一个 owner。

### 14.6 `src/media/v4l2_video_encoder.h/.cpp`

职责：

- V4L2 M2M/厂商 codec 配置；
- dma-buf input；
- HEVC output dequeue；
- VPS/SPS/PPS；
- IDR 请求；
- EOS/drain；
- 将 driver flag 和 NAL parser 结果写入 access unit。

线程：

- 每个 encoder instance 一个 output dequeue thread；
- RGB、tracking、ctrl 最多三个；
- input queue 由 GPU submission thread 投递；
- output thread 是该 encoder 和对应 file writer 的唯一串行 owner。

内存：

- codec capture buffers 启动时分配；
- EncodedBufferPool 管理复制后的 access unit；
- 如驱动 buffer 能安全引用到 sink 完成，可以零复制，否则只复制一次。

### 14.7 `src/media/encoded_buffer_pool.h/.cpp`

职责：

- 提供编码 access unit slab；
- 避免每帧 `std::vector` 扩容；
- 支持 FileSink 和 NetworkSink 共享。

设计：

- 按小/中/大三个 size class；
- 总字节上限固定；
- 记录 high-water mark；
- 网络慢时丢网络引用，不保留 pool；
- 文件慢时不能丢，进入故障停止。

### 14.8 `src/media/hevc_parser.h/.cpp`

职责：

- 解析 Annex-B/length-prefixed NAL；
- 识别 VPS/SPS/PPS、IDR、CRA；
- 验证“真实 IDR”；
- 格式转换；
- 不进行完整视频解码。

线程：纯函数/无线程。

### 14.9 `src/media/idr_gate.h/.cpp`

职责：

- 实现动态 writer ARMING；
- 请求 IDR 后过滤文件首帧；
- IDR 前帧继续送 TCP 8802，不写文件；
- 缓存 VPS/SPS/PPS；
- 真实 IDR 成为 fMP4 第一帧和 PTS 零点；
- 超时回滚到 preview。

线程：

- 只由 RGB encoder output thread 调用；
- 状态变化投递 coordinator。

### 14.10 `src/media/encoded_packet_router.h/.cpp`

职责：

- 同一 EncodedAccessUnit 分发给 FileSink 和 PreviewSink；
- RGB 可进网络；
- tracking/ctrl 只落盘；
- 根据当前 ResourcePlan 动态 attach/detach sink。

线程：每个 encoder output thread 内调用。

### 14.11 `src/media/media_pipeline.h/.cpp`

职责：

- 统一管理 ImageProcessor 和三个 VideoEncoder；
- 根据 ResourcePlan 启停；
- 动态 arm/finalize writers；
- 关闭新输入、发送 EOS、等待 drain；
- 提供异步完成 event。

线程：

- 本身不开长期 thread；
- 使用 GPU thread 和 encoder output threads；
- control 操作通过专用 media command queue 串行执行。

### 14.12 `src/media/snapshot_service.h/.cpp`

职责：

- 接收一次性 snapshot 请求；
- 选择下一组完整 FrameSet；
- CPU/GPU 转换后写 PNG/JPEG；
- 不能阻塞 camera callback；
- 同时最多一个活动请求。

线程：

- 一个低优先级 snapshot worker；
- queue 容量 1～2；
- 快照失败不影响录制。

---

## 15. Dataset 文件

### 15.1 `src/dataset/dataset_service.h/.cpp`

职责：

- 创建和管理唯一活动 DatasetSession；
- 检查存储；
- 建立 `.partial` 目录；
- 协调 audio、IMU、pose、video writer；
- 完成后原子 rename；
- 启动时扫描未完成 session。

线程：

- 本身无高频线程；
- start/stop 由 coordinator 触发；
- finalize 通过异步任务执行，完成后投递 revision event。

### 15.2 `src/dataset/dataset_session.h/.cpp`

职责：

- 保存 SessionId、路径、SessionClockSnapshot；
- 持有所有 writer；
- 提供 `recording/finalizing/complete` 状态；
- 生成 capture_status 和 manifest；
- 统计各流 sample/drop。

原则：

- 一个 session 只有一个 owner；
- 不允许第二次 start；
- stop 幂等；
- writer attach/finalize 状态显式。

### 15.3 `src/dataset/fmp4_writer.h/.cpp`

职责：

- 写 `ftyp+moov`；
- 每 sample 写 `styp+moof+mdat`；
- HEVC/AAC track；
- crash-safe append；
- fsync policy 可配置。

线程：

- 不内部开线程；
- 只能由对应 encoder output thread 调用；
- 一个实例只能有一个调用线程。

可参考当前 `FMP4Writer` 行为，但作为新模块重新测试。

### 15.4 `src/dataset/video_track_writer.h/.cpp`

职责：

- 管理 fMP4 video track；
- 管理兼容 metadata CSV；
- PTS 零基；
- 保证视频 sample 与 metadata 一一对应；
- finalize/close。

线程：

- 由对应 encoder output thread 独占；
- 不单独开 disk thread；
- 避免视频和 metadata 分属两个队列导致行数不一致。

### 15.5 `src/dataset/audio_track_writer.h/.cpp`

职责：

- 写 audio fMP4；
- 写 `audio_metainfo.csv`；
- sample count 到 PTS；
- capture UTC。

线程：由 audio encode thread 独占。

### 15.6 `src/dataset/imu_writer.h/.cpp`

职责：

- 写 accel/gyro CSV；
- 按 timestamp 排序；
- 批量缓冲；
- 定期 flush；
- 记录丢样。

线程：

- 一个 IMU writer thread；
- 输入 SPSC queue；
- 不在 IMU capture thread 做文件 I/O。

队列：

- 至少容纳 2 秒最大 ODR；
- queue 满时视为录制完整性故障。

### 15.7 `src/dataset/tracking_writer.h/.cpp`

职责：

- 写 head_pose；
- 写 hand 或 controller；
- 100 ms 可配置 reorder window；
- 保证 UTC timestamp 单调；
- 保存 invalid/confidence 的兼容策略。

线程：

- 一个 tracking writer thread；
- head/hand/controller 共用，保证同一时间顺序；
- 输入为小对象 queue。

### 15.8 `src/dataset/calibration_writer.h/.cpp`

职责：

- 从 CalibrationRepository 导出兼容相机 JSON；
- 导出 IMU calibration；
- 每 session 只写一次；
- 记录 calibration version/checksum。

线程：session start 的低频任务，不另开线程。

### 15.9 `src/dataset/session_manifest_writer.h/.cpp`

职责：

- 新增内部 `session_manifest.json`；
- 不替代现有兼容文件；
- 记录实际设备、软件版本、clock mapping、queue drops、encoder 参数；
- 用于诊断和追溯。

### 15.10 `src/dataset/recovery_service.h/.cpp`

职责：

- 扫描 `.partial`；
- 验证最后完整 fMP4 fragment；
- 生成 recovery status；
- 不伪造 `complete`；
- 保留可读数据。

线程：启动时低优先级 worker。

### 15.11 `src/dataset/dataset_export_service.h/.cpp`

职责：

- 保留当前 SDK 的数据集导出能力；
- 枚举已完成和可恢复 session；
- 将选定 session 复制到指定挂载点；
- 使用临时目录，全部复制和校验成功后原子 rename；
- 校验目标可用空间、文件大小和可选 checksum；
- 支持 cancel、进度、完成和错误事件；
- 绝不导出正在写入的活动 session；
- 导出失败不修改源数据。

主要接口：

```text
listExportableSessions()
startExport(sessionId, targetRoot)
cancelExport(exportId)
queryProgress(exportId)
```

线程：

- 一个低优先级 export worker；
- 同时只执行一个 export；
- 使用分块 buffer，不能一次把大文件读入内存；
- 进度通过 EventLoop 发布。

内存：

- 固定 1～4 MiB copy buffer；
- 不使用媒体内存池；
- buffer 大小按存储吞吐实测配置。

---

## 16. Protocol 和网络文件

### 16.1 `src/protocol/packet_codec.h/.cpp`

职责：

- EG frame 编解码；
- protobuf payload；
- 长度、magic、version 校验；
- 处理粘包和半包；
- 限制最大 packet；
- 不处理业务状态。

线程：纯解析器，无线程。

### 16.2 `src/protocol/command_dispatcher.h/.cpp`

职责：

- protobuf Command → OperationEvent；
- 参数校验；
- 生成立即 Response；
- 不直接 start/stop encoder；
- 所有状态变更由 coordinator。

线程：ControlServer event loop thread。

### 16.3 `src/protocol/control_server.h/.cpp`

职责：

- TCP 8801 listen/accept/read/write；
- 同一连接区分 `EG` frame 和 newline JSON；
- 连接断开清理；
- 输出 Response、Status、Fault；
- 限制单连接 buffer。

线程：

- 使用主 epoll EventLoop；
- 不需要一连接一线程；
- protobuf 编解码在 event loop 做；
- 大量序列化可转 control worker，但第一版不需要。

内存：

- 每连接固定 read buffer；
- 最大 packet 按协议限制；
- write queue 有字节上限；
- Status 可合并为最新 revision，Response 不可丢。

### 16.4 `src/protocol/status_publisher.h/.cpp`

职责：

- 根据 OperationState/SystemState 生成 Status；
- 状态变化立即发布；
- 周期心跳发布；
- 过滤过期 revision。

线程：由 EventLoop 调用。

### 16.5 `src/protocol/custom_ntp_service.h/.cpp`

职责：

- 实现 TCP 8801 上的 custom NTP JSON；
- 管理独立会话；
- 在收包点尽早记录 t2；
- 在发包前尽晚记录 t3；
- `_ns` 使用十进制字符串；
- 不开启 UDP 123 或新端口。

线程：ControlServer EventLoop。

### 16.6 `src/protocol/video_server.h/.cpp`

职责：

- TCP 8802 listen/accept；
- 新连接先发送缓存 VPS/SPS/PPS；
- 请求 RGB IDR；
- 发送 Annex-B；
- disconnect 不停止本地录制；
- 按当前 mode 决定是否允许重连。

线程：

- 一个 video accept/send thread，或集成 epoll；
- 建议独立 send thread，避免大视频 write 占用 control event loop；
- `shutdown(fd)` 用于停止时解除阻塞。

队列：

- 仅 preview 使用；
- 以字节数和帧数双重限制；
- 慢客户端丢旧 P-frame，保留最近 config/IDR 策略；
- 网络队列满不得阻塞 encoder output/file writer；
- 网络引用释放后立即归还 EncodedBufferPool。

---

## 17. Connectivity 文件

### 17.1 `src/connectivity/ble_service.h/.cpp`

职责：

- BlueZ GATT application；
- FFE0/FFE1～FFE4；
- time sync/control characteristics；
- notify subscription；
- manufacturer payload；
- 连接状态；
- 将命令投递 dispatcher/coordinator。

线程：

- 使用 system D-Bus event integration；
- 接入主 EventLoop；
- 不另建 Binder 风格 service thread。

### 17.2 `src/connectivity/ble_time_sync_session.h/.cpp`

职责：

- BLE sync/verify 状态机；
- session_id、phase、sample_index；
- timeout/retry/cancel/status；
- 使用 ExternalTimeSync 数学模块。

线程：主 EventLoop。

### 17.3 `src/connectivity/wifi_service.h/.cpp`

职责：

- 统一 Wi-Fi 接口；
- scan/connect/disconnect；
- SSID/IP/RSSI/channel；
- timeout 和 link event；
- 凭据不写日志。

backend：

- `network_manager_backend.h/.cpp`
- `wpa_supplicant_backend.h/.cpp`

线程：

- D-Bus backend 接 EventLoop；
- wpa_supplicant control fd 接 epoll；
- 不轮询 sleep。

---

## 18. Platform 文件

### 18.1 `src/platform/event_loop.h/.cpp`

职责：

- epoll；
- eventfd；
- timerfd；
- signalfd；
- fd callback；
- 延迟任务；
- 不执行媒体重活。

线程：main thread。

### 18.2 `src/platform/config_service.h/.cpp`

职责：

- TOML/YAML 加载；
- schema 校验；
- 默认值和范围；
- 生成 immutable ConfigSnapshot；
- reload 只允许修改运行期安全字段。

### 18.3 `src/platform/storage_service.h/.cpp`

职责：

- 数据根目录；
- `statvfs`；
- 1 GiB 和百分比阈值；
- inode、只读、短写；
- 原子目录/文件操作；
- 预估当前配置每分钟数据量。

### 18.4 `src/platform/system_state_service.h/.cpp`

职责：

- power_supply；
- thermal/hwmon；
- 内存、CPU、fd；
- Wi-Fi snapshot；
- peripheral health 汇总。

### 18.5 `src/platform/logger.h/.cpp`

职责：

- 统一结构化日志；
- journald；
- module/session/frame/error 字段；
- rate limit；
- dataset session 可选日志副本。

禁止任何 core 文件直接调用 syslog/journald。

### 18.6 `src/platform/systemd_notifier.h/.cpp`

职责：

- READY；
- WATCHDOG；
- STATUS；
- STOPPING。

### 18.7 `src/platform/thread_utils.h/.cpp`

职责：

- thread name；
- priority；
- CPU affinity 可配置；
- realtime scheduling 的能力检查；
- 统一 join deadline。

原则：

- camera/IMU 只有在测量证明需要时使用实时优先级；
- disk/network 不得与 capture 使用同等实时优先级。

### 18.8 `src/platform/local_input_service.h/.cpp`

职责：

- 通过 evdev/libinput 读取设备按键；
- 将录制键转换为统一 LocalStart/Stop toggle event；
- 实现 debounce 和 long-press；
- 不直接创建 DatasetSession 或控制 encoder；
- 按键事件必须进入 OperationCoordinator。

线程：

- input fd 接入主 EventLoop；
- 不需要独立线程。

### 18.9 `src/platform/removable_storage_monitor.h/.cpp`

职责：

- 监听 udev block device/mount 事件；
- 识别允许的导出目标；
- 提供挂载点、容量、只读状态；
- 设备拔出时取消 DatasetExportService；
- 不在 udev callback 中执行复制。

线程：udev monitor fd 接入主 EventLoop。

### 18.10 `src/platform/audio_prompt_service.h/.cpp`

职责：

- 播放开始、停止、存储不足、导出完成等提示音；
- 使用预录 WAV/OGG，不把 TTS 设为核心依赖；
- 与采集 PCM 设备协调，避免抢占 microphone；
- 提示失败只记录 WARN，不改变业务状态。

线程：

- 一个低优先级 prompt worker；
- 有界队列，容量建议 8；
- 相同低优先级提示可合并；
- fatal/stop 提示优先但不得阻塞 coordinator。

内存：启动时缓存小型提示音，或使用固定流式 decode buffer。

### 18.11 `src/platform/backend_factory.h/.cpp`

职责：

- 根据配置创建 Camera、Tracking、IMU、Audio、ImageProcessor、Encoder backend；
- 校验 backend capability；
- 明确链接静态 backend 或加载有版本的 plugin；
- 不允许业务代码中散布 backend `#ifdef`。

线程：只在 Application 初始化阶段使用。

---

## 19. CLI 和工具文件

### 19.1 `apps/egocollectctl/main.cpp`

职责：

- Unix domain socket 调用；
- `status/start/stop/preview/snapshot/reload/diagnose`；
- JSON 输出；
- 不直接访问设备。

### 19.2 `apps/replay/main.cpp`

职责：

- 从 recorded raw source 驱动 Camera/IMU/Audio/Tracking fake backend；
- 复现时钟、丢帧、乱序；
- 运行完整 coordinator/media/dataset/protocol；
- CI 核心入口。

### 19.3 `apps/calibrate/main.cpp`

职责：

- 相机内参、双目外参、Camera→Body、IMU→Body；
- 时间偏移；
- 输出 CalibrationRepository 格式；
- 不属于 daemon。

### 19.4 `apps/diagnose/main.cpp`

职责：

- 枚举设备；
- 检查 camera formats/dma-buf/modifier；
- 多相机同步；
- encoder 并发；
- ALSA/IIO；
- OpenXR tracking；
- 生成机器可读报告。

---

## 20. 完整线程模型

### 20.1 推荐线程清单

| 线程 | 数量 | Owner | 主要工作 |
|---|---:|---|---|
| Main EventLoop | 1 | Application | epoll、signal、timer、TCP control、D-Bus |
| Operation Coordinator | 1 | OperationCoordinator | 串行业务状态 |
| Camera Poll | 0 或 1 | Camera backend | poll/DQBUF；SDK callback 时为 0 |
| Frame Synchronizer | 1 | FrameSynchronizer | 组成 FrameSet |
| Time Aligner | 1 | FrameTimeAligner | pose/hand/controller/IMU 对齐 |
| OpenXR Owner | 1 | OpenXR backend | session、event、frame loop、locate |
| IMU Capture | 0 或 1 | IMU backend | 读取 IMU |
| Audio Capture | 1 | ALSA backend | 读取 PCM |
| Audio Encode | 1 | AAC encoder | AAC、audio writer |
| GPU Submit | 1 | Vulkan processor | import/process/submit |
| Video Encoder Output | 3 | 各 encoder | dequeue、IDR gate、file/network route |
| IMU Writer | 1 | DatasetSession | accel/gyro CSV |
| Tracking Writer | 1 | DatasetSession | pose/hand/controller CSV |
| Video Network Send | 1 | VideoServer | TCP 8802 |
| Snapshot Worker | 1 | SnapshotService | PNG/JPEG |
| Export Worker | 1 | DatasetExportService | 数据集分块导出 |
| Prompt Worker | 1 | AudioPromptService | 提示音 |
| Recovery/Low-priority | 1 | Platform worker | 恢复、慢速诊断 |

典型总数约 16～18。实际数量根据 camera SDK、是否有 ctrl stream、是否使用 OpenXR、是否启用导出调整。

### 20.2 为什么不使用万能线程池

以下资源需要线程亲和或单一 owner：

- OpenXR session；
- Vulkan queue；
- encoder handle；
- ALSA PCM；
- fMP4 writer 顺序；
- 业务状态机。

万能线程池会让资源所有权模糊，停止顺序困难。因此：

- 高频实时链路使用专用线程；
- 低频无状态任务可以使用一个 1～2 线程低优先级 worker；
- 不建立大型通用线程池。

### 20.3 线程间规则

- camera callback 不做文件 I/O、网络发送、OpenXR query、编码 drain；
- encoder output thread 不等待网络；
- coordinator 不在状态处理函数里 join；
- 状态锁内禁止系统调用和耗时操作；
- 同一 writer 只能由一个线程调用；
- stop 操作必须幂等；
- 所有线程有名称、owner 和最大停止时间；
- shutdown 时先关闭 admission，再 drain，最后销毁资源。

---

## 21. 队列和背压

| 通路 | 队列 | 建议初值 | 满时策略 |
|---|---|---:|---|
| Camera→Synchronizer | MPSC bounded | 每相机 4～8 帧 | 释放迟到帧；recording 连续超限报错 |
| Synchronizer→Aligner | SPSC bounded | 8 FrameSet | recording 停止；preview 丢旧 |
| Tracking query | MPSC bounded | 128 request | 超时标 invalid；不阻塞 camera |
| Aligner→GPU | SPSC bounded | 4～8 | recording backpressure fault |
| PCM→AAC | SPSC bounded | 16～32 blocks | audio fault |
| IMU→Writer | SPSC bounded | ≥2 秒样本 | 数据完整性 fault |
| Tracking→Writer | SPSC bounded | ≥2 秒样本 | fault，不静默丢 |
| Encoded→Network | bounded bytes+frames | 0.5～1 秒 | 丢旧网络帧 |
| Control Event | MPSC bounded | 256 | system fault |
| Snapshot | bounded | 1～2 | 拒绝新请求 |

所有队列必须暴露：

- current depth；
- capacity；
- high-water mark；
- push failure；
- dropped count；
- oldest item age。

禁止：

- 无界 `std::queue/deque`；
- 队列满后自动无限 heap allocation；
- 所有通路统一使用“丢最旧”。

---

## 22. 内存池设计

### 22.1 必须使用的池

1. 驱动 camera buffer pool；
2. FrameEnvelope/FrameSet object pool；
3. GPU output image pool；
4. AudioBlockPool；
5. EncodedBufferPool；
6. 固定 tracking/IMU ring buffer。

### 22.2 不建立全局万能内存池

原因：

- 图像、PCM、编码包大小和生命周期不同；
- 一个 pool 的碎片和锁会污染所有链路；
- 难以区分哪个模块泄漏；
- 回收线程不明确。

每个 pool 必须：

- 有唯一 owner；
- 固定最大字节数；
- 提供 RAII lease；
- 统计 outstanding/high-water；
- shutdown 时验证全部归还；
- debug 模式记录最后持有者。

### 22.3 零拷贝边界

理想路径：

```text
Camera dma-buf
→ Vulkan import
→ GPU output dma-buf
→ Encoder import
→ encoded buffer single copy/shared reference
→ file + network
```

允许的复制：

- driver encoded output 生命周期过短时，复制一次到 EncodedBufferPool；
- CPU reference backend；
- snapshot；
- 调试 dump。

禁止：

- 每个 consumer 各复制一份原始图像；
- TCP send 前重新拼接整个视频帧；
- 逐帧新建大 `std::vector`。

---

## 23. 时间同步完整设计

### 23.1 三个不同问题

不能把三者混为一谈：

1. **设备内部传感器同步**：camera、IMU、audio、tracking；
2. **媒体时间轴**：capture time→encoder PTS→fMP4；
3. **设备间同步**：BLE 四时间戳和 TCP custom NTP。

### 23.2 内部标准时间轴

内部标准使用 `CLOCK_BOOTTIME` ns：

```text
camera hardware tick ─┐
IMU hardware tick ────┼→ ClockMapper → BOOTTIME
audio hardware time ──┤
OpenXR XrTime ────────┘
```

写文件时：

```text
UTC ns = BOOTTIME ns + sessionBoottimeToRealtimeOffset
```

为了与当前 schema 兼容，每个 session 仍保存一个 mapping snapshot；同时 manifest 记录录制期间 offset 是否变化。

### 23.3 视频锚点

视频真实采样时间：

```text
midExposureBootNs =
    exposureStartBootNs + exposureDurationNs / 2
```

不是：

- callback 到达时间；
- GPU submit 时间；
- encoder output 时间；
- 文件写入时间。

文件 PTS：

```text
pts_us = (midExposureBootNs - firstWrittenIdrMidExposureBootNs) / 1000
```

### 23.4 Head Pose 对齐

```text
Camera mid-exposure BOOTTIME
→ TimeService BOOTTIME→XrTime
→ xrLocateSpace(head, root, targetXrTime)
→ Pose at exposure
```

如果 backend 只提供连续 sample：

- 保存 pose ring；
- position 插值；
- orientation SLERP；
- 超过最大 gap 标 invalid；
- 不使用“当前 pose”冒充曝光时 pose。

### 23.5 IMU 对齐

IMU 保留全部样本，不压成“一视频帧一个 IMU”：

```text
previous video mid-exposure
    < IMU samples >
current video mid-exposure
```

可以额外计算当前视频时刻插值值，但不能替代原始 CSV。

### 23.6 Audio 对齐

- PTS 由累计 PCM sample 数产生；
- capture UTC 由 ALSA hardware timestamp 映射；
- 不以视频帧切分音频；
- 定期验证 sample clock 与 boottime drift；
- 记录 xrun 和补偿策略。

### 23.7 外部设备同步

BLE 和 TCP NTP 对外继续使用 UTC ns：

- t1/t2/t3/t4 含义不变；
- TCP t2 尽量靠近接收，t3 尽量靠近发送；
- BLE sync 与 TCP custom NTP 维护独立 session；
- 外部 offset 不直接修改 session 内部历史 timestamp；
- 系统校时应优先 slew，录制期间避免 wall clock step。

---

## 24. 动态录制与预览的关键实现

### 24.1 Preview→Record

严格流程：

```text
PHONE_PREVIEW/STABLE
→ PHONE_RECORD/STARTING
→ encoder 和 8802 保持运行
→ 创建 DatasetSession
→ arm RGB/tracking/ctrl writer
→ 在下一输入边界 request IDR
→ IDR 前继续 preview，不落盘
→ 验证真实 IDR
→ 用缓存 VPS/SPS/PPS 创建 fMP4
→ IDR 作为文件第一帧和 PTS=0
→ PHONE_RECORD/STABLE
```

IDR 超时：

- 删除/标记未完成 session；
- 回滚 `PHONE_PREVIEW/STABLE`；
- 不断开 8802；
- 不重启 encoder。

### 24.2 Record 中开关 Preview

`LOCAL_RECORD → LOCAL_RECORD_WITH_PREVIEW`：

- writer 和文件时间轴不变；
- 启动 8802；
- 发送 VPS/SPS/PPS；
- 请求 IDR 供客户端起解；
- 新 IDR 仍正常写原文件。

反向：

- 停止网络 admission；
- 清网络 queue；
- shutdown socket；
- 不向 encoder 发 EOS；
- 不 finalize writer。

### 24.3 全局 Stop

顺序：

1. coordinator 设置 `STOPPING`、revision++、主动上报；
2. 关闭 preview admission，shutdown 8802；
3. 关闭新 camera frame 进入 encoder 的 admission；
4. 确定最后输入边界；
5. encoder signal EOS；
6. output thread drain 全部有效 sample；
7. output thread finalize video writer；
8. 停 audio/IMU/tracking writers；
9. 写 capture status/manifest；
10. 销毁 encoder/GPU resources；
11. 回收所有 lease，检查 pool；
12. 设置 `IDLE/STABLE`、revision++、主动上报。

---

## 25. 配置

示例：

```toml
[device]
id_source = "eeprom"
input_mode = "hand"

[storage]
root = "/var/lib/egocollect"
min_free_bytes = 1073741824
min_free_percent = 5

[camera]
backend = "own"
topology = "/etc/egocollect/cameras.yaml"
calibration_root = "/var/lib/egocollect/calibration"

[tracking]
backend = "openxr"
root_space = "vendor_root_or_local"
history_seconds = 5
query_timeout_ms = 5

[processing]
backend = "vulkan"
zero_copy = true
project_hand = false
project_controller = false

[video.rgb]
fps = 30
bitrate = 8000000

[video.tracking]
fps = 60
bitrate = 4000000

[video.ctrl]
fps = 60
bitrate = 4000000

[audio]
sample_rate = 44100
channels = 1
bitrate = 96000

[network]
control_port = 8801
video_port = 8802
```

启动后生成 `EffectiveConfig`，记录：

- 请求值；
- backend 实际值；
- 降级原因；
- buffer/queue 实际容量；
- codec profile；
- clock quality。

---

## 26. systemd 与权限

```ini
[Unit]
Description=EgoCollect Service
After=local-fs.target bluetooth.service network.target
Wants=bluetooth.service

[Service]
Type=notify
User=egocollect
Group=egocollect
ExecStart=/usr/bin/egocollectd --config /etc/egocollect/config.toml
Restart=on-failure
RestartSec=2
TimeoutStopSec=30
WatchdogSec=10
NoNewPrivileges=true
ProtectSystem=strict
ReadWritePaths=/var/lib/egocollect
SupplementaryGroups=video render audio input iio

[Install]
WantedBy=multi-user.target
```

原则：

- 默认不以 root 运行；
- udev 配置 camera/render/audio/IIO 权限；
- Wi-Fi/BLE 通过受控 D-Bus policy；
- 配网密码不进入日志；
- Unix control socket 由 group 控制；
- 数据目录 `0750`，文件 `0640`。

---

## 27. CMake Targets

```text
ego_types                 INTERFACE
ego_core                  STATIC
ego_time                  STATIC
ego_camera                STATIC
ego_tracking              STATIC
ego_imu                   STATIC
ego_audio                 STATIC
ego_media                 STATIC
ego_dataset               STATIC
ego_protocol              STATIC
ego_connectivity          STATIC
ego_platform_linux        STATIC

camera_backend_own        MODULE/STATIC
tracking_backend_openxr   MODULE/STATIC
tracking_backend_vio      MODULE/STATIC
image_backend_vulkan      MODULE/STATIC
encoder_backend_v4l2      MODULE/STATIC
audio_backend_alsa        MODULE/STATIC
imu_backend_own           MODULE/STATIC

egocollectd               EXECUTABLE
egocollectctl             EXECUTABLE
egocollect-replay         EXECUTABLE
egocollect-calib          EXECUTABLE
egocollect-diag           EXECUTABLE
```

要求：

- C++20；
- CMake 3.22+；
- 禁止全局 `file(GLOB *.cpp)`；
- core 不链接平台库；
- backend 通过 factory 显式创建；
- 支持 x86_64 host 和 aarch64 cross-build；
- replay backend 在无硬件 CI 可运行；
- Debug 支持 ASan/UBSan/TSan；
- 生成 build-id、debug symbols、SBOM。

---

## 28. 测试文件和验收

### 28.1 Unit

```text
test_operation_state.cpp
test_resource_plan.cpp
test_clock_mapper.cpp
test_frame_synchronizer.cpp
test_pose_interpolation.cpp
test_hevc_parser.cpp
test_idr_gate.cpp
test_fmp4_writer.cpp
test_packet_codec.cpp
test_external_time_sync.cpp
test_dataset_schema.cpp
```

### 28.2 Backend Contract

每个 backend 必须运行统一 contract：

```text
camera_backend_contract.cpp
tracking_backend_contract.cpp
video_encoder_contract.cpp
audio_backend_contract.cpp
imu_backend_contract.cpp
```

检查：

- start/stop 幂等；
- stop 后无 callback；
- timestamp domain；
- buffer lease；
- queue/backpressure；
- EOS/drain；
- 故障恢复。

### 28.3 Compatibility

建立黄金资产：

- 当前 Android SDK TCP pcap；
- BLE 广播/GATT trace；
- 正常/短时/长时/强杀数据集；
- 当前 HEVC config/IDR；
- 状态转移序列；
- 时间同步 JSON；
- 手机 App 联调脚本。

Linux 验收：

- 现有 App 不改协议即可配网、控制、预览；
- 当前分析脚本可直接读取；
- CSV 行数与媒体 packet 数一致；
- PTS 从 0 严格单调；
- fMP4 强杀后可读；
- Head/Hand/Controller 对齐 mid-exposure；
- IMU/audio/video 在同一 UTC 时间线；
- preview→record 不断流；
- record 中开关 preview 不重启 encoder；
- Stop 严格排空末帧。

### 28.4 Soak/Fault

- 12/24 小时录制；
- 1000 次 start/stop；
- TCP 8802 慢客户端；
- TCP 8801 断连重连；
- BLE 断连；
- Wi-Fi 丢失；
- camera 丢帧/reset；
- OpenXR session loss；
- encoder hang/EOS timeout；
- 磁盘满、短写、只读；
- audio xrun；
- IMU timestamp reset；
- wall clock step；
- SIGTERM/断电恢复；
- pool/queue 泄漏。

---

## 29. 实施阶段

### Phase 0：兼容规格冻结

- 从当前 SDK 生成 golden dataset、pcap、BLE trace；
- 冻结 OperationMode/Phase/revision；
- 冻结数据 schema；
- 测量当前性能、同步和稳定性；
- 明确自有相机 timestamp、同步和 calibration contract。

### Phase 1：无硬件框架

- 新建仓库和公共类型；
- OperationCoordinator；
- TimeService；
- replay backends；
- packet/protocol；
- DatasetSession/FMP4；
- 用 replay 跑通五模式。

### Phase 2：相机、IMU、Audio

- OwnCameraBackend；
- FrameSynchronizer；
- calibration；
- ClockMapper；
- IMU/ALSA；
- 原始数据正确性验证。

### Phase 3：Tracking 和 Media

- 最小 OpenXR TrackingService；
- Head/Hand/Controller；
- Vulkan processing；
- V4L2/厂商 HEVC；
- IDR gate；
- fMP4 + preview。

### Phase 4：连接与系统

- BlueZ；
- Wi-Fi；
- TCP 8801/8802；
- systemd、udev、health、fault、CLI。

### Phase 5：兼容和量产

- 手机端兼容；
- 数据质量；
- 长稳和故障注入；
- 性能、温度、功耗；
- 权限、安全、诊断、升级；
- 文档和配置冻结。

---

## 30. 最终技术结论

Linux 新版本应当是一个**以统一时间轴为核心的多传感器采集服务**：

```text
Drivers
→ Typed C++ Services
→ Clock Mapping
→ Frame Synchronization
→ Pose/Hand/Controller Alignment
→ Image Processing
→ Encoding
→ Dataset + TCP Preview
```

最重要的设计不是把所有数据“贴到最近的视频 PTS”，而是：

1. 每个 source 明确原始时钟域；
2. 映射到统一 boottime；
3. 以视频 mid-exposure 为视觉锚点；
4. Pose/Hand/Controller 在该时刻查询或插值；
5. IMU 保留完整时间窗口；
6. Audio 保持连续 sample clock；
7. encoder PTS 从 capture time 派生；
8. 写文件时再映射 UTC；
9. BLE/TCP 设备间同步与内部同步分开处理。

代码结构上，业务状态、资源计划、时间系统、设备 backend、媒体处理和兼容输出必须彼此独立。高频链路使用专用线程、有界队列和专用内存池；低频系统事件使用主 epoll loop。每个 encoder、OpenXR session、Vulkan queue 和 writer 都有唯一线程 owner。

按照这个框架实现，可以保留当前 SDK 的功能和对外行为，同时完全摆脱 Android 工程结构。后续更换相机、Tracking backend、编码器或 Linux 发行版时，只需要替换 backend，不需要再次重写整个采集服务。
