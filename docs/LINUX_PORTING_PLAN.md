# SXR EGO Linux 原生采集系统方案

## 1. 方案目标和边界

当前 SDK 运行在 Android。Linux 版本的目标是保留相同的采集、记录、控制和导出能力，但重新建立一套 Linux 原生数据链路。

Linux 版本必须支持：

- RGB 双目相机；
- Tracking 双目灰度相机；
- Ctrl 双目灰度相机；
- 加速度计和陀螺仪；
- Head Pose；
- 左右手关节；
- 麦克风和 AAC；
- RGB、Tracking、Ctrl 三路 HEVC；
- 相机曝光时刻与 Head Pose、双手、IMU 对齐；
- fMP4、CSV、JSON 数据集；
- TCP 8801 控制；
- TCP 8802 RGB 实时视频；
- BLE 配网和时间同步；
- Wi-Fi 管理；
- U 盘导出；
- 日志、故障恢复和 systemd 托管。

本方案有三个明确边界：

1. **Linux 完全不使用 OpenXR。**
2. 不移植 Android Activity、JNI、`AHardwareBuffer`、AAudio、MediaCodec、ASensorManager、Android BLE/Wi-Fi Service。
3. 本文只设计外设链路、线程、关键 API、线程间数据和内存生命周期，不规定 `.h/.cpp` 文件如何组织。

Head Pose 和双手必须来自以下二者之一：

- 厂商提供的 Linux 原生定位与手势 SDK；
- 自研 Linux 原生 VIO/SLAM 和 Hand Tracking 算法。

如果当前厂商只有 OpenXR 能输出 Head Pose 或 Hand Joints，则“提供 Linux 原生 Tracking SDK”是移植前置条件。不能在 Linux 服务中重新引入 OpenXR 作为替代。

---

## 2. 总体数据链路

```text
RGB / Tracking / Ctrl Camera
  → Camera IO
  → 三条 CameraIngressQueue
  → Camera Sync & Router
  ├→ ImageProcessQueue
  │   → GPU/CPU Image Process
  │   → 三条 EncoderInputQueue
  │   → RGB / Tracking / Ctrl HEVC Encoder
  │   ├→ fMP4 + camera metadata
  │   └→ RGB PreviewQueue → TCP 8802
  │
  ├→ TrackingImageQueue
  │   → Linux Native Tracking
  │   → TrackingRing
  │
  └→ FrameAnchorQueue
      → Sensor Aligner
      → AlignedSensorQueue
      → Sensor Writer

IMU
  → IMU Capture
  ├→ ImuRing
  ├→ TrackingImuQueue → Linux Native Tracking
  └→ ImuRecordQueue → Sensor Writer

Microphone
  → ALSA Capture
  → PcmQueue
  → AAC Encoder
  → audio.m4a + audio metadata
```

这几条链路互相通过时间戳和 `FrameId` 对齐，不把所有传感器数据塞入一个万能队列。

视频编码不等待 Head Pose 或双手结果。相机图像立即进入图像处理和编码；Sensor Aligner 稍后使用同一个 `FrameId` 生成对齐记录。这样 Tracking 算法的延迟不会长时间占用相机驱动 buffer，也不会阻塞视频。

文中的 `CameraGroupBlock`、`ImuBatch`、`ProcessedFrame` 等名称只表示“队列中传递的数据字段和内存所有权”，不是要求建立同名头文件或 C++ 类。

---

## 3. 进程和线程

第一版使用一个 `egocollectd` 进程。大图像使用 dma-buf 或进程内内存池，避免跨进程传 fd 和 fence。

### 3.1 应用线程总表

全功能版本固定创建 18 个应用线程：

| ID | 线程 | 数量 | 主要职责 |
|---:|---|---:|---|
| T00 | Main EventLoop | 1 | TCP 8801、BLE D-Bus、Wi-Fi D-Bus、udev、signal、timer |
| T01 | Operation Coordinator | 1 | 唯一业务状态机，决定开始、停止、预览、录制、导出 |
| T02 | Camera IO Owner | 1 | 相机 start/stop、V4L2 DQBUF/QBUF，或厂商 SDK buffer return |
| T03 | Camera Sync & Router | 1 | 双目 group 校验、三组 cadence 同步、图像和锚点分发 |
| T04 | IMU Capture | 1 | IIO/厂商 IMU 读取、时间转换、校准、分发 |
| T05 | Native Tracking | 1 | 唯一调用 Linux 定位/手势算法，产生 Head Pose 和 Hand Joints |
| T06 | Sensor Aligner | 1 | 按 RGB 曝光时刻查询 TrackingRing 和 ImuRing |
| T07 | Image Processor | 1 | dma-buf import、SBS/灰度/旋转等 GPU 或 CPU 处理 |
| T08 | RGB Encoder Owner | 1 | RGB V4L2 M2M、RGB fMP4、RGB metadata、预览分发 |
| T09 | Tracking Encoder Owner | 1 | Tracking V4L2 M2M、fMP4、metadata |
| T10 | Ctrl Encoder Owner | 1 | Ctrl V4L2 M2M、fMP4、metadata |
| T11 | Audio Capture | 1 | ALSA PCM 读取和硬件时间戳 |
| T12 | Audio Encoder | 1 | AAC 编码、audio.m4a 和 audio metadata |
| T13 | Sensor Writer | 1 | accel、gyro、head pose、hand joints CSV |
| T14 | Video Sender | 1 | TCP 8802 RGB 发送和慢客户端处理 |
| T15 | Snapshot Worker | 1 | PNG/JPEG 快照 |
| T16 | Export Worker | 1 | U 盘数据集复制、校验和提交 |
| T17 | Logger Worker | 1 | 文件日志和 journald |

如果厂商相机 SDK 自己创建 callback 线程，这些是 SDK 外部线程，不计入上述 18 个。callback 只能取得合法 buffer 所有权并投递队列，不能执行同步、Tracking、GPU、编码、文件和网络工作。

厂商 Tracking、AAC 或相机 SDK 如果内部还有线程，必须在启动日志中记录实际线程数。应用层仍然只能由表中的 owner thread 调用对应 SDK handle。

### 3.2 线程统一规则

- 一个硬件 fd 或 SDK handle 只有一个 owner thread。
- 其他线程不能直接调用该 fd/handle，只能向 owner 的 command queue 发命令。
- 高频队列全部有界，并且在启动时分配完成。
- 大图像和音频通过 lease 传递；小型 IMU、Pose、Hand、command 使用定长值对象。
- callback 和硬件采集线程不等待下游队列。
- 队列满时必须有明确的丢弃或故障策略。
- Stop 顺序固定为：停止新输入、drain 已接收数据、归还全部 lease、最后销毁硬件和内存池。

---

## 4. 统一时间和标识

### 4.1 内部时间

所有传感器进入系统后统一转换为：

```text
CLOCK_BOOTTIME，单位 ns
```

使用的 Linux API：

```text
clock_gettime(CLOCK_BOOTTIME)
clock_gettime(CLOCK_REALTIME)
clock_gettime(CLOCK_MONOTONIC_RAW)
```

每次录制开始时采样：

```text
sessionRealtimeOffsetNs = realtimeNs - boottimeNs
```

写 UTC 时使用：

```text
utcNs = bootNs + sessionRealtimeOffsetNs
```

录制过程中不能因为系统时间校准而修改已经建立的 session offset。

### 4.2 硬件时间转换

Camera、IMU、ALSA、Tracking 算法分别维护自己的时间映射状态：

```text
硬件 tick / MONOTONIC_RAW / driver timestamp
  → offset + drift 拟合
  → BOOTTIME ns
```

每个映射必须输出：

- 当前 offset；
- drift；
- 最近一次同步时间；
- residual；
- 是否发生 timestamp reset；
- 当前质量状态。

不能使用 callback 到达时间、`DQBUF` 返回时间、GPU 时间或文件写入时间代替真实采样时间。

### 4.3 相机视觉锚点

每只眼分别计算：

```text
midExposureBootNs =
    exposureStartBootNs + exposureDurationNs / 2
```

当前 Android 行为使用 RGB 左眼作为主要视觉锚点，因此 Linux 固定：

```text
visualAnchorBootNs = RGB left midExposureBootNs
```

右眼时间仍然单独保存，并记录左右眼曝光偏差。

### 4.4 全局标识

所有跨线程对象至少携带：

- `sessionGeneration`：区分不同录制 session；
- `streamId`：RGB、Tracking、Ctrl、Audio、IMU 等；
- `sequence`：数据源连续序号；
- `FrameId`：进入 Camera Sync 后分配的全局帧标识；
- `timestampBootNs`。

旧 session 的异步结果如果 `sessionGeneration` 不匹配，只能释放自己的内存，不能写入新 session。

---

## 5. 相机链路

### 5.1 当前 Android 语义和 Linux 变化

当前 Android 使用：

```text
sxr_camera_open_group(RGB)
sxr_camera_open_group(TRACKING)
sxr_camera_open_group(CTRL)
```

每次 callback 已经包含一个双目 group：

- RGB：左右眼两个 `AHardwareBuffer`；
- Tracking：一个左右拼接灰度 buffer；
- Ctrl：一个左右拼接灰度 buffer。

Linux 必须保留这个 group 语义：

```text
CameraGroupBlock
  group             RGB / TRACKING / CTRL
  groupSequence
  triggerId
  bufferCount
  buffers[]         dma-buf/CPU block 描述和 lease
  leftEye           曝光、crop、内参、外参
  rightEye          曝光、crop、内参、外参
```

左右眼不能先拆到两个异步队列再重新配对。若 Linux 驱动分别暴露左右眼 node，T02 必须先在相机 backend 内形成完整 `CameraGroupBlock`。

### 5.2 T02 Camera IO Owner

#### V4L2 模式使用的关键 API

```text
open(O_NONBLOCK | O_CLOEXEC)
VIDIOC_QUERYCAP
VIDIOC_ENUM_FMT
VIDIOC_S_FMT
VIDIOC_REQBUFS
VIDIOC_QUERYBUF
VIDIOC_EXPBUF
VIDIOC_QBUF
VIDIOC_STREAMON
epoll_wait
VIDIOC_DQBUF
VIDIOC_STREAMOFF
close
```

T02 同时监听：

- camera fd；
- CameraCommand eventfd；
- CameraRequeue eventfd。

T02 是所有 camera `DQBUF/QBUF/STREAMON/STREAMOFF` 的唯一调用线程。

每次 `DQBUF` 后：

1. 读取 group、sequence、trigger ID；
2. 读取左右眼曝光开始、曝光时长、gain、crop、内外参；
3. 将硬件时间转换成 BOOTTIME；
4. 建立 camera buffer lease；
5. 组成完整 `CameraGroupBlock`；
6. 投递到对应 group 的 CameraIngressQueue；
7. 返回 epoll。

#### 厂商 callback 模式

Android 的 `JavaVM*`、`jobject`、`AHardwareBuffer*` ABI 不能直接用于 Linux。厂商必须提供 Linux 原生 camera SDK。

SDK 必须明确提供以下语义，实际函数名由厂商文档确定：

- 枚举 camera group 和能力；
- 配置格式、分辨率和帧率；
- 启动、停止 group；
- frame callback；
- buffer retain/release；
- timestamp domain 和曝光单位；
- 获取标定参数；
- reset 和错误通知。

callback 内只允许：

1. 校验 frame descriptor；
2. retain 厂商 buffer，或者复制到预分配 CameraCopyPool；
3. 转换时间和填充 `CameraGroupBlock`；
4. `try_push` 到对应 CameraIngressQueue；
5. 返回。

只 `dup()` dma-buf fd 不能证明 buffer 在 callback 返回后仍然有效。必须有厂商 retain/release 合同，或者在 callback 返回前完成一次固定池复制。

### 5.3 CameraIngressQueue

使用三条独立队列：

| 队列 | Producer | Consumer | 类型 | 初始容量 |
|---|---|---|---|---:|
| RGB ingress | RGB callback/T02 | T03 | MPSC bounded | 8 group |
| Tracking ingress | Tracking callback/T02 | T03 | MPSC bounded | 8 group |
| Ctrl ingress | Ctrl callback/T02 | T03 | MPSC bounded | 8 group |

三条队列共用一个 eventfd 唤醒 T03。T03 使用 round-robin drain，防止 60 fps Tracking/Ctrl 挤占 30 fps RGB。

队列元素是 `CameraGroupBlock` 和 camera buffer lease，不复制像素。

满队列：

- 预览模式：丢弃本次新 group，立即释放 lease；
- 录制模式：记录 sequence gap；连续满超过阈值后通知 T01 有序停止；
- producer 不能反向 pop 队列中的旧元素。

### 5.4 Camera buffer 内存保护

camera buffer lease 内保存：

- buffer index/token；
- dma-buf fd 所有权；
- plane offset、stride、modifier；
- acquire fence；
- 原子引用计数；
- backend lifetime token；
- generation；
- 最后持有模块。

最后一个引用释放时：

```text
V4L2：
  释放线程 → CameraRequeueQueue
  → eventfd 唤醒 T02
  → T02 执行 VIDIOC_QBUF

厂商 SDK：
  释放线程 → 厂商声明为 thread-safe 的 release
  或 → CameraRequeueQueue → T02 调用厂商 release
```

不能从 T05、T07、T08～T10 直接 `QBUF`。

STREAMOFF 后 T02 保持 requeue-only 状态，等待全部 lease 归零后才能 unmap、close 或销毁 camera SDK。

### 5.5 T03 Camera Sync & Router

T03 从三条 ingress queue 取得 group 后执行：

1. 检查每组左右眼 sequence 和曝光偏差；
2. 放入 RGB、Tracking、Ctrl 三个固定 reorder ring；
3. 按 trigger ID、已验证的公共 sequence 或曝光中点匹配同步 epoch；
4. 生成 `VisualEpoch`；
5. 为每个 group 分配 `FrameId`；
6. 将图像发送到 T07；
7. 将 Tracking/Ctrl 图像发送到 T05；
8. 将不带图像的 RGB `FrameAnchor` 发送到 T06。

`VisualEpoch` 保存：

```text
epochId
optional RGB CameraGroupBlock
optional Tracking CameraGroupBlock
optional Ctrl CameraGroupBlock
sync method
group skew
quality
```

RGB、Tracking、Ctrl 不能假定同帧率。以 RGB 30 fps、Tracking/Ctrl 60 fps 为例：

```text
60 Hz epoch 100：RGB + Tracking + Ctrl
60 Hz epoch 101：      Tracking + Ctrl
60 Hz epoch 102：RGB + Tracking + Ctrl
```

有效的 group sample 必须恰好向下游发送一次，不能为凑齐三个 group 而丢掉一半 Tracking/Ctrl。

没有硬件同步时可以按时间相关形成 epoch，但必须标记 `timestamp_correlated`，不能写成 `hardware_synchronized`。

---

## 6. IMU 链路

### 6.1 T04 IMU Capture

优先使用 Linux IIO。

配置接口：

```text
/sys/bus/iio/devices/iio:deviceX/
scan_elements/*_en
scan_elements/*_index
scan_elements/*_type
buffer/length
buffer/enable
sampling_frequency
```

读取 API：

```text
open("/dev/iio:deviceX", O_NONBLOCK | O_CLOEXEC)
poll({iioFd, commandEventFd})
read
close
```

T04 每次读取后：

1. 按 scan element 的 index、type 和 alignment 解析，不能直接强转固定结构体；
2. 区分 accel、gyro 和硬件 timestamp；
3. 应用 scale、bias、轴向和量纲转换；
4. 转换成 BOOTTIME；
5. 分配连续 sequence；
6. 组成固定容量 `ImuBatch`；
7. 写入 ImuRing；
8. 把一份值对象投递 TrackingImuQueue；
9. 把一份值对象投递 ImuRecordQueue。

`ImuBatch` 是小型定长结构：

```text
count
samples[N]
  kind             accel / gyro
  sequence
  timestampBootNs
  x, y, z
  temperature
  validity
```

IMU 不需要大内存 lease。`ImuBatch` 直接复制到预分配 queue slot。

### 6.2 ImuRing

ImuRing 保存最近 2～5 秒所有 accel/gyro 原始样本。

用途：

- T06 查询两个 RGB 曝光锚点之间的完整 IMU window；
- 故障检测 sequence gap；
- Tracking reset 后短时间回放 IMU。

ImuRing 单 writer 为 T04。reader 使用 sequence snapshot，不能持有 ring slot 裸指针。

### 6.3 IMU 队列策略

| 队列 | Producer → Consumer | 类型 | 容量 |
|---|---|---|---:|
| TrackingImuQueue | T04 → T05 | SPSC | 至少 500 ms 最大 ODR |
| ImuRecordQueue | T04 → T13 | SPSC | 至少 2 秒最大 ODR |

TrackingImuQueue 满：

- 说明 Tracking 算法跟不上 IMU；
- 不能覆盖中间 IMU 后继续输出“正常 Pose”；
- T05 进入 degraded/reset，持续发生则停止录制。

ImuRecordQueue 满：

- 数据集已不完整；
- 通知 T01 停止录制；
- session 标记 incomplete。

---

## 7. Linux 原生 Head Pose 和双手链路

### 7.1 数据来源

T05 使用一个 Linux 原生 Tracking backend，输入：

- Tracking 左右灰度图；
- Ctrl 左右灰度图；
- 连续 accel/gyro；
- camera intrinsics/extrinsics；
- IMU calibration；
- camera 与 IMU 时间偏移。

输出：

- Head position 和 orientation；
- tracking state 和 confidence；
- 左右手每个关节的位置、方向、半径和 validity；
- 输出数据对应的 BOOTTIME。

厂商 Linux SDK 必须提供的操作语义：

1. 创建算法实例；
2. 加载 camera/IMU 标定；
3. 启动；
4. 提交 IMU batch；
5. 提交 Tracking stereo frame；
6. 提交 Ctrl stereo frame；
7. 取得 Pose/Hand 输出；
8. reset；
9. stop；
10. destroy。

这些不是本文虚构的函数名。最终实际符号、参数、返回值和线程约束必须来自厂商 Linux SDK 文档，并作为 Phase 0 交付物冻结。

### 7.2 TrackingImageQueue

T03 把 Tracking/Ctrl 图像交给 T05。

推荐的队列元素：

```text
TrackingImagePacket
  group
  frameId
  left/right timestamp
  left/right image descriptor
  calibration revision
  image lease
```

队列类型：

```text
T03 → T05
SPSC bounded
容量 8～16 个 group
```

两种内存模式必须在启动时二选一：

#### 模式 A：算法支持 dma-buf，并保证释放 deadline

- T03 增加 camera lease 引用；
- T05 将 dma-buf 提交给算法；
- 算法完成输入消费后 T05 释放 lease；
- CameraPool 必须覆盖算法最大在途帧数。

#### 模式 B：算法不能及时释放 camera buffer

- T03 从 TrackingImagePool 取得固定 block；
- 将灰度图复制一次；
- 立即释放 camera lease；
- T05 只持有 TrackingImagePool lease。

模式 B 内存更多，但不会因为 Tracking 算法延迟耗尽 camera driver buffer。厂商不能明确承诺输入生命周期时，必须使用模式 B。

TrackingImageQueue 满不能静默丢中间帧后继续宣称 tracking 正常。T05 必须收到 discontinuity/reset 事件，并将相应时间段 Pose/Hand 标记 invalid。

### 7.3 T05 Native Tracking

T05 是 Tracking SDK handle 的唯一 owner。

线程循环按时间顺序处理：

```text
poll TrackingCommand eventfd
  → drain TrackingImuQueue
  → drain TrackingImageQueue
  → 按 timestamp 提交 IMU 和图像
  → poll/drain Tracking 输出
  → 写 TrackingRing
  → 发布 health metrics
```

T05 不写 CSV、不编码视频、不发送网络。

### 7.4 TrackingRing

TrackingRing 是 T05 单 writer 的固定环：

```text
TrackingSample
  timestampBootNs
  head position/orientation
  head validity/confidence
  left hand joints[]
  right hand joints[]
  hand validity/confidence
  source sequence
  algorithm state
```

保存时间至少覆盖：

```text
最大算法输出延迟 + Sensor Aligner 最大等待 + 2 秒余量
```

T06 查询目标曝光时刻时：

- position 线性插值；
- orientation 使用 quaternion SLERP；
- Hand joints 使用相邻有效结果插值或最近有效结果；
- 超过最大 gap 返回 invalid；
- 不能用“当前最新 Pose”冒充历史曝光时刻 Pose。

---

## 8. 传感器对齐链路

### 8.1 FrameAnchorQueue

T03 对每个 RGB frame 产生一个不带图像的小对象：

```text
FrameAnchor
  sessionGeneration
  frameId
  rgb left/right sequence
  left/right exposure start
  left/right exposure duration
  visualAnchorBootNs
  sync quality
  calibration revision
```

通过 SPSC bounded FrameAnchorQueue 从 T03 传给 T06。容量覆盖：

```text
RGB fps × 最大 Tracking 输出延迟 + safety
```

该队列不持有相机图像，因此 T06 等待 Tracking 结果不会占用 camera buffer。

### 8.2 T06 Sensor Aligner

T06 每次处理一个 `FrameAnchor`：

1. 等待 TrackingRing 覆盖 `visualAnchorBootNs`，但不超过配置 deadline；
2. 查询或插值该时刻 Head Pose；
3. 查询或插值该时刻左右手；
4. 从 ImuRing 取得 `(previousRgbAnchor, currentRgbAnchor]` 的全部 IMU；
5. 计算 pose/hand 时间误差；
6. 记录 camera、IMU、Tracking clock quality；
7. 生成 `AlignedSensorRecord`；
8. 投递 AlignedSensorQueue 给 T13。

`AlignedSensorRecord` 是小型值对象：

```text
sessionGeneration
frameId
visualAnchorBootNs
head pose + validity
left/right hand joints + validity
imu window begin/end sequence
sync method
time error
clock quality
```

Tracking deadline 超时：

- 当前 frame 的 Pose/Hand 标记 invalid；
- 图像和视频仍然正常写入；
- 连续 invalid 超过阈值才触发 Tracking fault；
- 不能反向阻塞 Camera、GPU 或 Encoder。

---

## 9. 图像处理链路

### 9.1 ImageProcessQueue

T03 将每个有效 CameraGroupBlock 投递给 T07。

元素保存：

```text
streamId
frameId
capture timestamp
camera metadata
camera buffer lease
acquire fence
```

使用一条 SPSC bounded queue，或 RGB/Tracking/Ctrl 三条独立 SPSC queue。建议使用三条，避免 60 fps 灰度图阻塞 RGB。

### 9.2 T07 Image Processor

优先使用 Vulkan。

关键 API：

```text
vkCreateInstance
vkEnumeratePhysicalDevices
vkCreateDevice
vkGetDeviceQueue
vkCreateImage
vkGetMemoryFdPropertiesKHR
VkImportMemoryFdInfoKHR + vkAllocateMemory
vkBindImageMemory
vkCreateSemaphore
vkImportSemaphoreFdKHR
vkQueueSubmit2 / vkQueueSubmit
vkWaitSemaphores
```

T07 是 `VkQueue` 的唯一 submit 线程。

每个输入执行：

1. import 或查找已缓存的 dma-buf image；
2. 等待 acquire fence；
3. 根据 group layout 处理 SBS、crop、旋转、灰度和格式；
4. 从对应 GPU ImagePool 取得输出 image；
5. submit GPU command；
6. 产生 release fence；
7. 生成 `ProcessedFrame`；
8. 投递到对应 EncoderInputQueue；
9. GPU 不再读取 camera buffer 后释放 camera lease。

`ProcessedFrame` 保存：

```text
streamId
frameId
captureBootNs
camera metadata
GPU image lease
release fence
```

没有 Vulkan 时使用 CPU/libyuv fallback，但必须从固定 CPU ImagePool 取得输出，不能逐帧分配大 `std::vector`。

### 9.3 GPU ImagePool

RGB、Tracking、Ctrl 各自独立。

容量至少为：

```text
EncoderInputQueue 容量
+ encoder OUTPUT buffer 数
+ GPU 最大 in-flight
+ 1
```

最后一个 encoder input 引用释放后，image 才能回到对应 pool。

---

## 10. 视频编码和落盘

### 10.1 三个 Encoder Owner

T08、T09、T10 分别独占一个 V4L2 M2M encoder fd。

关键 API：

```text
VIDIOC_QUERYCAP
VIDIOC_S_FMT
VIDIOC_S_EXT_CTRLS
VIDIOC_REQBUFS
VIDIOC_QUERYBUF
VIDIOC_QBUF
VIDIOC_STREAMON
poll/epoll
VIDIOC_DQBUF
VIDIOC_STREAMOFF
```

每个 encoder 使用：

- OUTPUT queue：GPU/CPU 处理后的原始图像；
- CAPTURE queue：编码后的 HEVC；
- B-frame 关闭；
- 固定 GOP；
- 明确 bitrate/profile/level；
- 支持 request IDR；
- 支持 EOS drain。

T07 只向 EncoderInputQueue push，不能直接调用 encoder ioctl。

### 10.2 Encoder owner loop

每个 encoder owner 同时监听：

- input queue eventfd；
- encoder fd；
- command eventfd。

处理顺序：

```text
ProcessedFrame
  → QBUF OUTPUT
  → 保存 frameId/metadata 到固定 in-flight table
  → DQBUF OUTPUT
  → 释放 GPU image lease

DQBUF CAPTURE
  → 取得 bytesused、timestamp、flags
  → 从 EncodedBufferPool 取得 block
  → 将编码 payload 复制一次
  → 立即 QBUF CAPTURE
  → 解析 VPS/SPS/PPS、IDR、PTS
  → 按 timestamp/sequence 找回 frameId 和 camera metadata
  → 写对应 fMP4 和 camera metadata
```

V4L2 CAPTURE buffer 如果能在多个 sink 完成前保持有效，也可以直接 lease；否则必须复制一次到 EncodedBufferPool。文件和网络不能各复制一份。

### 10.3 编码数据结构

```text
EncodedPacket
  streamId
  frameId
  captureBootNs
  ptsUs
  codecConfig
  idr
  payload lease
  camera metadata
```

RGB `EncodedPacket` 有两个只读消费者：

- T08 自己的 FileSink；
- PreviewQueue/T14。

Tracking 和 Ctrl 只有 FileSink。

网络丢帧只释放网络引用，不能影响文件引用。

### 10.4 fMP4

每个 encoder owner 只写自己的视频和 metadata：

| 线程 | 文件 |
|---|---|
| T08 | `rgb.mp4`、`rgb_metainfo.csv` |
| T09 | `tracking.mp4`、`tracking_metainfo.csv` |
| T10 | `ctrl.mp4`、`ctrl_metainfo.csv` |

文件 API：

```text
open
write/writev
fdatasync
fsync
close
```

fMP4 使用：

```text
ftyp + moov
styp + moof + mdat
styp + moof + mdat
...
```

视频 sample 和对应 metadata 必须在同一 owner thread 中按同一 `FrameId` 提交，避免视频帧数和 CSV 行数不一致。

磁盘持续变慢时不能静默丢录制视频；应通知 T01 停止并 finalize 已经接收的数据。

---

## 11. 音频链路

### 11.1 T11 Audio Capture

使用 ALSA libasound。

关键 API：

```text
snd_pcm_open
snd_pcm_hw_params_any
snd_pcm_hw_params_set_access
snd_pcm_hw_params_set_format
snd_pcm_hw_params_set_channels
snd_pcm_hw_params_set_rate_near
snd_pcm_hw_params_set_period_size_near
snd_pcm_hw_params_set_buffer_size_near
snd_pcm_hw_params
snd_pcm_prepare
snd_pcm_start
snd_pcm_poll_descriptors
snd_pcm_readi / snd_pcm_mmap_readi
snd_pcm_status
snd_pcm_status_get_htstamp
snd_pcm_recover
snd_pcm_drop
snd_pcm_close
```

T11 是 ALSA handle 的唯一 owner。

每个 period：

1. 从 AudioBlockPool 取得 PCM block；
2. `snd_pcm_readi` 直接写入 block；
3. 取得硬件 timestamp 和 frame position；
4. 计算第一个 sample 的 BOOTTIME；
5. 生成 `AudioBlock`；
6. 投递 PcmQueue。

`AudioBlock` 保存：

```text
firstSampleIndex
firstSampleBootNs
frameCount
sampleRate
channels
PCM block lease
```

发生 xrun 时使用 `snd_pcm_recover`，记录 gap 和 gap 长度。超过阈值后停止录制。

### 11.2 AudioBlockPool 和 PcmQueue

```text
blockBytes = periodFrames × channels × bytesPerSample
blockCount = ceil(maxPipelineLatency / periodDuration) + safety
```

建议初始 16～32 blocks。pool 耗尽时不能临时 heap 分配。

PcmQueue：

```text
T11 → T12
SPSC bounded
容量不大于 AudioBlockPool blockCount
```

### 11.3 T12 Audio Encoder

T12 独占 AAC encoder。

Linux AAC backend 优先级：

1. SoC 厂商硬件 AAC API；
2. FFmpeg `libavcodec`；
3. 其他已确认许可的 AAC encoder。

厂商 API 的实际函数名必须在 Phase 0 冻结。需要具备 configure、submit PCM、drain output、EOS、flush、stop 语义。

AAC PTS 使用累计 sample 数：

```text
ptsUs = totalSamples × 1,000,000 / sampleRate
```

T12 直接写：

- `audio.m4a`；
- `audio_metainfo.csv`。

PCM 编码完成后释放 AudioBlock lease。

---

## 12. Sensor Writer 和数据集

### 12.1 T13 Sensor Writer

T13 独占：

- `accel.csv`；
- `gyro.csv`；
- `head_pose.csv`；
- `hand_tracking.csv`。

输入使用两条独立 SPSC queue：

| 队列 | Producer |
|---|---|
| ImuRecordQueue | T04 |
| AlignedSensorQueue | T06 |

T13 round-robin 批量 drain，防止高频 IMU 长期阻塞 Head/Hand。

写入 API：

```text
open
writev
fdatasync
close
```

不要求每行 fsync。按时间或数据量执行批量 `fdatasync`。

### 12.2 Session 文件

```text
rgb.mp4
rgb_metainfo.csv
tracking.mp4
tracking_metainfo.csv
ctrl.mp4
ctrl_metainfo.csv
audio.m4a
audio_metainfo.csv
accel.csv
gyro.csv
head_pose.csv
hand_tracking.csv
camera_params_rgb.json
camera_params_tracking.json
camera_params_ctrl.json
imu_calibration.json
tracking_calibration.json
capture_status.json
session_manifest.json
capture.log
```

录制先创建：

```text
<session>.partial
```

只有全部 writer finalize、文件同步和 manifest 完成后才：

```text
rename <session>.partial → <session>
fsync parent directory
```

发生数据丢失时 session 必须标记 incomplete，不能只保留日志而继续标记 complete。

---

## 13. TCP 8801 控制链路

T00 Main EventLoop 使用：

```text
epoll_create1
epoll_ctl
epoll_wait
eventfd
timerfd_create
signalfd
socket
bind
listen
accept4
recv
send
```

链路：

```text
TCP 8801
  → T00 connection read buffer
  → packet framing / protobuf decode
  → CoordinatorQueue
  → T01 Operation Coordinator
  → 模块 command queue
  → completion event
  → T00 response write queue
```

控制连接不使用“一连接一线程”。

每个连接使用固定最大 read/write buffer，必须处理半包、粘包、非法长度和超时。

T01 是唯一可以修改以下状态的线程：

- Idle/Preview/Recording/Export；
- Starting/Stable/Stopping/Fault；
- state revision；
- session generation；
- ResourcePlan。

T01 不执行 camera ioctl、encoder drain、文件 fsync 或 socket 大包发送。

---

## 14. TCP 8802 RGB 视频链路

```text
T08 RGB EncodedPacket
  → PreviewQueue
  → T14 Video Sender
  → TCP 8802
```

T14 使用：

```text
socket(SOCK_NONBLOCK | SOCK_CLOEXEC)
setsockopt
bind
listen
accept4
poll/epoll
sendmsg/writev
shutdown
close
```

PreviewQueue 同时限制：

- 最大帧数；
- 最大总字节数；
- 最大帧年龄。

队列元素只保存共享 `EncodedPacket` lease。

慢客户端策略：

1. 丢弃网络队列中的旧 P 帧；
2. 保留最新 VPS/SPS/PPS；
3. 请求 T08 产生新 IDR；
4. 从新 IDR 恢复；
5. 持续超时则断开客户端。

任何网络行为都不能阻塞 T08 写本地文件。

---

## 15. BLE、Wi-Fi 和时间同步

### 15.1 BLE

Linux 使用 BlueZ D-Bus GATT，不使用 Android `BluetoothGattServer`。

T00 使用 sd-bus：

```text
sd_bus_open_system
sd_bus_add_object_vtable
sd_bus_request_name
org.bluez.GattManager1.RegisterApplication
org.bluez.LEAdvertisingManager1.RegisterAdvertisement
org.freedesktop.DBus.ObjectManager.GetManagedObjects
sd_bus_match_signal
sd_bus_get_fd
sd_bus_get_events
sd_bus_get_timeout
sd_bus_process
UnregisterAdvertisement
UnregisterApplication
sd_bus_unref
```

BlueZ D-Bus fd 接入 T00 epoll，不增加 BLE 线程。

GATT callback 只执行：

1. 检查 characteristic 和 payload 长度；
2. 复制小型 request；
3. 记录接收 BOOTTIME；
4. 投递 CoordinatorQueue 或 TimeSyncSession；
5. 返回 D-Bus。

BLE 功能包括：

- SSID/Password 配网；
- Wi-Fi 状态和 IP notify；
- 控制命令和响应；
- 错误通知；
- 设备信息；
- 时间同步。

Wi-Fi 密码不能写入日志，D-Bus message 发送完成后清零临时内存。

### 15.2 Wi-Fi

优先使用 NetworkManager D-Bus：

```text
org.freedesktop.NetworkManager
AddAndActivateConnection
ActivateConnection
DeactivateConnection
PropertiesChanged
IP4Config
```

没有 NetworkManager 时才使用 wpa_supplicant D-Bus。不能执行 `nmcli`/`wpa_cli` 后解析 shell 文本。

Wi-Fi D-Bus fd 同样由 T00 epoll 管理。

状态变化：

```text
D-Bus signal
  → T00
  → WifiState event
  → T01
  → TCP/BLE status response
```

### 15.3 BLE 时间同步

每个 BLE peer 保存一个小型 TimeSyncSession：

```text
peer id
request id
t1/t2/t3/t4
RTT
offset
quality
state
```

接收入口立即采样 `t2 = CLOCK_BOOTTIME`，notify 前采样 `t3`。

BLE 同步结果只描述 Linux 设备和手机之间的 offset，不能反向修改 Camera、IMU、Audio 已采集的内部 BOOTTIME。

---

## 16. U 盘、快照和日志

### 16.1 U 盘检测与导出

T00 使用：

```text
udev_new
udev_monitor_new_from_netlink
udev_monitor_filter_add_match_subsystem_devtype
udev_monitor_enable_receiving
udev_monitor_get_fd
udev_monitor_receive_device
statvfs
```

udev fd 接入 epoll。T00 只检测设备并创建 ExportRequest，不复制文件。

T16 Export Worker：

```text
ExportRequest
  → 检查 session 为 complete
  → 检查目标空间
  → 创建 <session>.tmp
  → copy_file_range 或 read/write fallback
  → 校验大小和可选 SHA-256
  → fdatasync files
  → fsync directory
  → rename 到正式目录
  → fsync export 根目录
```

U 盘拔出时保留 `.tmp`，不修改源数据。默认导出成功后仍保留本地数据。

### 16.2 T15 Snapshot Worker

快照请求链路：

```text
T01
  → CameraRouterCommandQueue
  → T03 标记下一张完整 group
  → T07 生成 readback image lease
  → SnapshotImageQueue
  → T15 使用 libpng、libjpeg-turbo 或已确认的图片库编码
  → completion event
  → T01
```

快照不能在 camera callback、T03 或 T07 中执行 PNG/JPEG 压缩。

SnapshotImageQueue 容量 1～2；已有活动请求时拒绝新请求，不影响录制。

### 16.3 T17 Logger Worker

所有线程向有界 LoggerQueue 写定长 LogRecord：

```text
boottime
realtime
thread id/name
module
session generation
stream/frame/sequence
level
message
```

T17 写：

- stdout/stderr，由 systemd 收集；
- journald；
- app log；
- session `capture.log`。

LoggerQueue 满时 DEBUG/INFO 可以丢并计数，ERROR/FATAL 使用 stderr/journald fallback。日志不能阻塞 camera callback。

---

## 17. 队列和内存池总表

### 17.1 队列

| 队列 | Producer → Consumer | 类型 | 元素 | 满时处理 |
|---|---|---|---|---|
| CameraIngress[3] | callback/T02 → T03 | MPSC bounded | CameraGroupBlock lease | Preview 丢当前；Recording fault |
| CameraRequeue | 任意 lease releaser → T02 | MPSC bounded | buffer token/index | 不应满；满为生命周期错误 |
| ImageProcess[3] | T03 → T07 | SPSC bounded | CameraGroupBlock lease | Recording fault |
| TrackingImage | T03 → T05 | SPSC bounded | camera/copy-pool lease | Tracking discontinuity/reset |
| TrackingImu | T04 → T05 | SPSC bounded | ImuBatch 值 | Tracking reset/fault |
| FrameAnchor | T03 → T06 | SPSC bounded | FrameAnchor 值 | Recording incomplete/stop |
| EncoderInput[3] | T07 → T08/T09/T10 | SPSC bounded | ProcessedFrame lease | Recording fault |
| PcmQueue | T11 → T12 | SPSC bounded | AudioBlock lease | Audio fault |
| ImuRecord | T04 → T13 | SPSC bounded | ImuBatch 值 | Recording incomplete/stop |
| AlignedSensor | T06 → T13 | SPSC bounded | AlignedSensorRecord 值 | Recording incomplete/stop |
| Preview | T08 → T14 | SPSC/有界 deque | EncodedPacket lease | 丢网络旧 P 帧 |
| Coordinator | T00/Health/worker → T01 | MPSC bounded | OperationEvent | System fault |
| ModuleCommand | T01 → 各 owner | 每模块 SPSC | Command + revision | System fault |
| SnapshotImage | T07 → T15 | SPSC，容量 2 | CPU image lease | 拒绝新请求 |
| Export | T01 → T16 | SPSC，容量 2 | ExportRequest | 拒绝或合并 |
| Logger | 所有线程 → T17 | MPSC bounded | LogRecord | 低级日志可丢 |

### 17.2 内存池

| Pool | 保存内容 | 归还条件 |
|---|---|---|
| Camera driver pool | 原始 camera buffer | 所有 camera lease 释放 |
| CameraCopyPool | callback 无 retain 时的完整 group copy | T07/T05 不再使用 |
| TrackingImagePool | Tracking 算法专用灰度输入 | T05 完成提交/消费 |
| GPU ImagePool[3] | RGB/Tracking/Ctrl 处理结果 | encoder DQBUF OUTPUT |
| EncodedBufferPool | HEVC payload | FileSink 和 PreviewSink 都释放 |
| AudioBlockPool | PCM period | T12 编码完成 |
| ObjectPool | CameraGroupBlock、VisualEpoch 等描述对象 | 最后一个引用释放 |
| ImuRing | 最近 IMU 值 | 固定 slot 被 sequence 安全覆盖 |
| TrackingRing | 最近 Pose/Hand 值 | 固定 slot 被 sequence 安全覆盖 |

所有 pool 在硬件启动前建立。pool 耗尽后禁止回退到 `new/malloc`。

### 17.3 Lease 和数据安全

- pool handle 使用 `{slotIndex, generation}`，防止 slot 重用后的 ABA。
- lease control block 使用原子引用计数。
- 大 payload 发布后只读。
- dma-buf fd 由 lease control block 唯一拥有，业务对象只保存 fd index/plane 描述。
- fence 必须明确 move 所有权，不能把同一个整数 fd 交给多个析构者。
- ring writer 先写 payload，最后 release-store sequence；reader acquire-load sequence，复制后再次校验。
- queue push 成功后所有权转给 queue；push 失败后所有权仍归 producer。
- queue close 后拒绝新 push，consumer drain 完已有元素后返回 closed。

### 17.4 内存预算

启动时计算：

```text
camera driver/copy pool
+ tracking image pool
+ GPU image pool
+ encoder driver buffers
+ encoded payload pool
+ PCM pool
+ ring/queue/object pool
```

总量超过 `memory_budget_bytes` 时拒绝开始采集。

容量至少满足：

```text
camera buffers
  >= driver 最小 queued
   + ingress/reorder/image process in-flight
   + tracking in-flight
   + GPU in-flight

GPU images
  >= encoder input queue
   + encoder OUTPUT in-flight
   + GPU in-flight

PCM blocks
  >= 最大音频链路延迟 / period 时长 + safety
```

---

## 18. 启动顺序

```text
1. 读取配置
2. 启动 T17 Logger
3. 建立 CLOCK_BOOTTIME/REALTIME 基准
4. 初始化 T00 epoll、signalfd、timerfd
5. probe Camera、IIO、ALSA、Tracking SDK、GPU、HEVC、AAC
6. 校验 timestamp domain、buffer lifetime、格式和标定
7. 计算并创建全部 queue、ring 和 pool
8. 启动 T13 Sensor Writer 等等待型线程
9. 启动 T05 Native Tracking
10. 启动 T04 IMU Capture
11. 启动 T02 Camera IO 和 T03/T06/T07
12. 启动三个 encoder owner
13. 启动 Audio Capture/Encoder
14. 注册 BlueZ GATT 和 NetworkManager 订阅
15. 启动 TCP 8801/8802 listen
16. 启动 T01 Coordinator
17. sd_notify READY=1
```

任何硬件 capability 未确认时只能进入诊断模式，不能开始正式录制。

---

## 19. 录制开始顺序

```text
START command
  → T01 phase=STARTING，sessionGeneration++
  → 检查存储、硬件和 queue/pool health
  → 创建 <session>.partial
  → 记录 sessionRealtimeOffsetNs 和标定 revision
  → 打开所有 CSV/fMP4 writer
  → 清空旧 ring/query 状态
  → 启动 IMU、Tracking、Camera、Audio admission
  → 启动三个 encoder stream
  → 请求三个 encoder IDR
  → 第一张真实 IDR 建立各自 PTS 零点
  → capture_status=recording
  → T01 phase=STABLE
```

任一步失败都要关闭已经打开的资源，并把 partial session 标记为 startup_failed。

---

## 20. 停止和内存回收顺序

```text
STOP command
  → T01 phase=STOPPING，记录 stopBootNs

  → 关闭新 preview/snapshot/export admission

  → 同时通知硬件 producer 停止新数据：
       T02 stop camera / STREAMOFF
       T04 stop IIO
       T11 stop ALSA capture
     T02 保持 requeue-only 状态

  → 等待厂商 callback active count=0
  → close CameraIngress[3]、TrackingImu、PcmQueue、ImuRecordQueue

  → T03 drain camera，关闭 ImageProcess、TrackingImage、FrameAnchor
  → T05 drain tracking input，输出最后结果
  → T06 drain FrameAnchor，关闭 AlignedSensorQueue
  → T07 drain GPU，关闭 EncoderInput[3]

  → T08/T09/T10 drain encoder input
  → signal EOS
  → DQBUF 到真实 EOS
  → finalize 三路 fMP4 和 metadata

  → T12 drain PCM、AAC EOS、finalize audio
  → T13 drain IMU/AlignedSensor、flush CSV

  → 停止 T05 Tracking backend

  → 等待 Camera/GPU/Encoded/PCM/Object pool outstanding=0
  → T02 处理最后 CameraRequeue 后才能 close camera

  → 写 manifest/capture_status
  → fdatasync 文件
  → fsync session 目录
  → rename partial
  → fsync 父目录

  → T01 phase=IDLE
```

每一步都有 deadline。超时后记录具体未归还的 pool slot、线程和 frame/sequence，继续执行仍然安全的清理。

Stop 必须幂等。第二个 Stop 等待第一次 Stop 的结果，不能启动第二套 drain。

---

## 21. 故障和背压

| 故障 | 检测线程 | 处理 |
|---|---|---|
| Camera queue 持续满 | T03/Health | Preview 丢当前；Recording 停止 |
| camera timestamp reset | T02 | 当前 session incomplete，reset 映射 |
| 左右眼曝光偏差超限 | T03 | group invalid；持续则停止 |
| 30/60 fps cadence 错误 | T03 | 记录 sequence/trigger fault |
| TrackingImage/IMU 丢失 | T05 | Tracking reset；输出 invalid |
| Tracking 输出超时 | T06 | 当前 FrameId invalid；持续则 fault |
| ImuRecordQueue 满 | T04/T13 | session incomplete，停止 |
| Audio xrun | T11 | recover 并记录 gap；超阈值停止 |
| GPU pool 耗尽 | T07 | 停止录制 |
| Encoder hang | T08～T10 | EOS deadline 后故障清理 |
| 文件短写/磁盘慢 | writer owner | 停止录制并 finalize |
| TCP 慢客户端 | T14 | 丢网络 P 帧或断开 |
| BLE/Wi-Fi 断开 | T00 | 发布状态，不影响本地录制 |
| U 盘拔出 | T16 | 取消 export，保留 `.tmp` 和源数据 |
| pool lease 泄漏 | Stop/Health | 报告 slot/generation/owner，不销毁悬空 backend |

Health timer 每秒采集：

- 每个线程最后 progress 时间；
- queue depth/capacity/high-water；
- pool available/outstanding；
- sensor rate 和 sequence gap；
- clock mapping residual；
- Tracking latency/validity；
- encoder latency；
- writer latency；
- 磁盘空间；
- fd 数量。

---

## 22. systemd 和权限

systemd 服务使用非 root 用户。

需要的设备权限：

- `/dev/video*`；
- `/dev/media*`；
- `/dev/dri/renderD*`；
- `/dev/iio:device*`；
- ALSA capture device；
- BlueZ system D-Bus；
- NetworkManager system D-Bus。

服务配置至少包括：

```text
Type=notify
Restart=on-failure
WatchdogSec
TimeoutStopSec
NoNewPrivileges=true
ProtectSystem=strict
ReadWritePaths=/var/lib/egocollect
SupplementaryGroups=video render audio input iio
```

SIGTERM/SIGINT 通过 signalfd 进入 T00，不能在异步 signal handler 中调用 C++ 对象、文件或硬件 API。

---

## 23. 移植前必须由硬件/算法团队确认的内容

### 23.1 Camera

- Linux 实际 API 和 so；
- RGB/Tracking/Ctrl topology；
- 左右眼独立 buffer 还是 SBS；
- dma-buf 格式、plane、stride、modifier；
- acquire/release fence；
- callback buffer retain/release；
- timestamp domain；
- exposure 单位；
- trigger ID、sequence domain；
- RGB 30、Tracking/Ctrl 60 的 divisor/phase；
- 最大在途 buffer；
- 标定参数格式。

### 23.2 IMU

- Linux IIO node 或厂商 API；
- ODR；
- accel/gyro 单位；
- 轴向；
- timestamp domain；
- bias、scale、non-orthogonal calibration；
- camera-IMU time offset；
- reset 行为。

### 23.3 Native Tracking

- Linux 原生 Tracking SDK；
- 实际函数和 ABI；
- 所需 Camera/IMU 输入；
- dma-buf 支持；
- 输入 buffer 生命周期；
- 最大输入/输出延迟；
- Pose 坐标系；
- Hand joints 定义；
- reset 和 lost 状态；
- 内部线程数。

### 23.4 Codec/GPU/Audio

- Vulkan dma-buf/modifier 支持；
- V4L2 M2M HEVC 格式和 controls；
- IDR/EOS 行为；
- AAC backend；
- ALSA device、采样率、声道和硬件 timestamp；
- Android 侧软件增益/AGC 是否需要等价实现。

这些内容未冻结前，文档中的 Linux 标准 API 可以实现，但厂商链路不能标记为完成。

---

## 24. 验收测试

### 24.1 数据链路

- RGB 30 fps、Tracking/Ctrl 60 fps 全部有效 group 恰好处理一次；
- 左右眼 sequence、曝光和 calibration 正确；
- RGB FrameId 与 Head/Hand CSV 一一对应；
- Head/Hand 使用 RGB 曝光中点，不使用 callback 时间；
- 两个 RGB 锚点之间的 IMU window 无重复、无遗漏；
- Audio PTS 连续，xrun 有明确 gap；
- 三路视频 sample 和 metadata 行数一致；
- TCP 慢客户端不影响本地文件；
- Preview 转 Recording 从真实 IDR 开始；
- 强杀后 fMP4 可恢复到最后完整 fragment。

### 24.2 内存和并发

- callback 返回后立即复用底层 buffer，验证 retain/copy 正确；
- 一个 SBS buffer 被左右 eye view 引用时只 release 一次；
- 最后 lease 在任意线程释放，V4L2 QBUF 仍只发生在 T02；
- dma-buf/fence fd 无重复 close、无泄漏；
- pool slot generation 能检测 ABA；
- queue close 和 producer push 并发安全；
- pool 耗尽不回退 heap；
- Stop 后所有 outstanding lease 为零；
- 厂商 callback 晚到不会访问已销毁 backend。

### 24.3 压力和故障

- 12/24 小时录制；
- 1000 次 start/stop；
- camera 丢帧、reset、timestamp 跳变；
- IMU timestamp reset；
- Tracking 输入丢失、延迟和 reset；
- Audio xrun；
- GPU/encoder hang；
- 磁盘满、短写和只读；
- TCP/BLE/Wi-Fi 反复断连；
- U 盘复制中拔出；
- SIGTERM；
- 断电恢复；
- queue/pool 泄漏检测。

---

## 25. 实施顺序

### Phase 0：冻结硬件和算法合同

- Camera Linux ABI；
- IIO/IMU 参数；
- Native Tracking Linux SDK；
- timestamp 和 trigger cadence；
- dma-buf/fence/GPU/encoder 能力；
- ALSA/AAC；
- 真实数据 replay trace。

### Phase 1：时间、队列和内存

- Clock mapping；
- SPSC/MPSC bounded queue；
- lease/generation；
- Camera/Tracking/GPU/Audio pool；
- replay backend；
- Stop/drain 单元测试。

### Phase 2：Camera 和 IMU

- T02/T03/T04；
- 三条 CameraIngressQueue；
- 30/60 fps cadence；
- ImuRing；
- 原始 dump 和时间戳验证。

### Phase 3：Native Tracking 和对齐

- T05 Linux Native Tracking；
- TrackingImagePool；
- TrackingRing；
- T06 Sensor Aligner；
- RGB FrameId 对齐验收。

### Phase 4：GPU、HEVC、AAC 和数据集

- T07～T13；
- 三路 HEVC；
- AAC；
- fMP4/CSV；
- partial/finalize/recovery。

### Phase 5：TCP、BLE、Wi-Fi 和导出

- TCP 8801/8802；
- BlueZ GATT；
- NetworkManager；
- BLE time sync；
- snapshot；
- U 盘 export。

### Phase 6：长稳和故障注入

- systemd/watchdog；
- health metrics；
- 12/24 小时；
- 1000 次启停；
- 断电和存储故障；
- buffer/queue/pool 泄漏。

---

## 26. 最终设计原则

```text
每个外设由一个明确 owner thread 调用真实 Linux/厂商 API
  → 采样时间全部映射为 BOOTTIME
  → 大图像/PCM 通过专用 pool lease 传输
  → IMU/Pose/Hand 通过定长值对象和固定 ring 传输
  → Camera 图像编码与传感器对齐解耦
  → Head Pose 和双手由 Linux Native Tracking 产生
  → 文件完整性优先，网络预览允许丢帧
  → Stop 先停 producer，再逐级 drain，最后销毁 pool 和硬件
```

这份方案的实施重点不是类名和文件名，而是每条链路的 owner、API、队列、内存和生命周期必须严格一致。
