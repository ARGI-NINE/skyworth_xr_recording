# 当前 Android SDK 数据采集与同步全流程

> 本文描述 SDK 1.5.0 当前 Android 代码实际采用的数据采集、图像处理、编码、时间同步和落盘流程。  
> 本文只覆盖当前需要的数据：RGB、tracking、ctrl 相机、音频、加速度计、陀螺仪、Head Pose 和双手关节。

本文以“数据链路”为主线，并严格区分：

- **项目显式线程/容器**：源码中实际存在的 `std::thread`、回调上下文、queue、map、ring、mutex 和 condition variable；
- **平台内部机制**：Camera HAL、BufferQueue、MediaCodec、AAudio、SensorService 或 OpenXR runtime 内部管理，项目只通过 API 观察输入输出；
- **数据传递与复制**：说明传递的是硬件 buffer 引用、编码 packet，还是按值复制的结构体。

本文若广义称“队列”，表示生产者—消费者间的显式缓冲；具体 C++ 类型会单独写明。例如 Head Pose 使用 `std::map` 重排缓冲，不是 `std::queue`。

## 1. 总览

当前 SDK 不是单纯的相机录像程序，而是一条多传感器采集链：

```text
相机驱动/厂商相机服务
        │
        ▼
libsxr_camera_client.so
        │ FrameData + AHardwareBuffer + 曝光元数据
        ▼
三个相机组 callback
        │
        ├── RGB：双眼导入 EGL → OpenGL SBS → HEVC
        ├── Tracking：拼接 buffer 导入 EGL → 灰度处理 → HEVC
        └── Ctrl：拼接 buffer 导入 EGL → 灰度处理 → HEVC
                                      │
                                      ▼
                               CameraEncoder
                                      │
                         ┌────────────┴────────────┐
                         ▼                         ▼
                    FMP4Writer                TCP 8802
                    MP4 + CSV                RGB 实时预览

AAudio → PCM → AAC MediaCodec → audio.m4a + audio_metainfo.csv

Android SensorManager → accel/gyro → 异步 CSV writer

OpenXR → Head Pose/双手关节
           ▲
           │ 按 RGB 曝光中点查询历史状态
           │
      RGB 时间戳对齐线程
           │
           └── head_pose.csv + hand_tracking.csv
```

系统最终把所有流写到同一个数据集目录，并将原始 `CLOCK_BOOTTIME` 时间统一转换为 UTC。

---

## 2. 采集的数据源

### 2.1 相机

当前打开三个逻辑相机组：

| 相机组 | 数据形态 | 典型帧率 | 用途 | 输出 |
|---|---|---:|---|---|
| RGB | 左右眼各一个 `AHardwareBuffer` | 30，可配置 60 | 彩色双目、实时预览、视觉数据 | `rgb.mp4` |
| Tracking | 左右灰度画面位于一个拼接 buffer | 60 | 视觉跟踪数据 | `tracking.mp4` |
| Ctrl | 左右灰度画面位于一个拼接 buffer | 60 | 额外灰度视觉数据 | `ctrl.mp4` |

每个相机帧同时提供：

- `frameId`；
- 曝光开始时间 `timestamp`；
- 曝光时长 `exposure`；
- `gain`；
- width、height、stride、format；
- cropX、cropY；
- focalX、focalY、centerX、centerY；
- Kannala-Brandt 鱼眼畸变参数；
- Camera→Body 位置和四元数；
- 一个或两个 `AHardwareBuffer`。

### 2.2 IMU

使用 Android NDK Sensor API：

- `ASENSOR_TYPE_ACCELEROMETER`；
- `ASENSOR_TYPE_GYROSCOPE`；
- 请求 FASTEST rate；
- `ASensorEvent.timestamp` 属于 `CLOCK_BOOTTIME`；
- 加速度单位为 m/s²；
- 角速度单位为 rad/s。

### 2.3 音频

使用 AAudio：

- 输入方向；
- PCM 16-bit；
- 44.1 kHz；
- mono；
- shared mode；
- low-latency mode；
- AAC-LC 编码；
- 96 kbps。

### 2.4 Head Pose

使用 OpenXR：

- `xrViewSpace` 表示当前设备头部/Body；
- 优先在 Root Space 中定位；
- Root Space 不可用时回退 Local Space；
- 对每个 RGB 帧，按其曝光中点对应的 `XrTime` 查询历史 Head Pose。

### 2.5 双手关节

使用 OpenXR hand tracking：

- 左右手各创建一个 hand tracker；
- 每只手 26 个关节；
- 每个关节保存位置、方向和 radius；
- 数据坐标系与 Head Pose 使用相同的参考空间；
- 最终按 RGB 曝光中点对齐并写入 `hand_tracking.csv`；
- 可选将骨骼投影到 RGB 编码视频。

---

## 3. 主要代码组件

| 文件 | 当前职责 |
|---|---|
| `VrNativeActivity.java` | Android Activity、权限、生命周期、广播控制、平台状态、提示音 |
| `main.cpp` | OpenXR 主循环、相机回调、EGL 图像处理、传感器对齐、录制协调 |
| `sxr_camera.h` | 动态加载厂商相机客户端并封装函数表 |
| `sxr_common.h` | CameraGroup、FrameInfo、FrameData、标定结构 |
| `SharedTexture.*` | `AHardwareBuffer` 与 EGLImage/GL texture 的公共封装 |
| `EncoderSurface.*` | MediaCodec input Surface 的 EGL 封装和 presentation time |
| `CameraEncoder.*` | HEVC MediaCodec、输出线程、视频 metadata、fMP4 writer |
| `AudioEncoder.*` | AAudio、PCM 队列、AAC MediaCodec、音频 fMP4 |
| `ImuPoseCollector.*` | Android sensor 读取线程和 IMU CSV writer |
| `DatasetRecorder.*` | 创建 session、统一 UTC offset、启动音频/IMU/Head Pose writer |
| `RawDateSave.*` | 双手数据的异步 CSV 写入 |
| `FMP4Writer.*` | 视频和音频 fragmented MP4 |
| `HandOverlayRenderer.*` | 双手骨骼的鱼眼投影和 RGB 叠加 |
| `protocol_adapter.*` | TCP 8801 控制、TCP 8802 RGB 预览、状态和时间协议 |
| `SdkStateBridge.*` | SDK 运行状态和错误快照 |
| `platform_state_bridge.*` | 电池、温度、Wi-Fi 等 Android 平台状态 |

---

## 4. 应用启动流程

### 4.1 Java 层启动

Android 启动 `VrNativeActivity` 后：

1. 加载 `libmixedreality.so`；
2. 请求相机、麦克风、存储、蓝牙、网络等权限；
3. 初始化 Activity 生命周期和广播接收；
4. 启动 BLE 服务；
5. 将电池、温度、Wi-Fi、存储等状态传给 native；
6. Android `NativeActivity` 最终进入 `android_main()`。

### 4.2 Native 主入口

`android_main()` 主要完成：

1. 保存 `android_app`、JavaVM 和 Activity；
2. 初始化 native logger；
3. 启动 TCP 协议服务；
4. 初始化 OpenXR instance、system、session、spaces；
5. 初始化 EGL display/context；
6. 初始化 OpenXR 输入和双手 tracking；
7. 初始化 DatasetRecorder；
8. 启动传感器对齐线程；
9. 动态加载相机客户端；
10. 打开 RGB、tracking、ctrl 三组相机；
11. 进入 Android/OpenXR 事件和帧循环。

### 4.3 相机初始化

相机初始化链路：

```text
sxr_camera_api_init()
    │
    ├── dlopen("libsxr_camera_client.so")
    ├── dlsym("sxr_camera_create")
    ├── dlsym("sxr_camera_open_group")
    ├── dlsym("sxr_camera_close_group")
    ├── dlsym("sxr_camera_get_group_info")
    └── dlsym("sxr_camera_get_imu_calibration")
          │
          ▼
sxr_camera_create(JavaVM, Activity)
          │
          ├── open RGB callback
          ├── open TRACKING callback
          └── open CTRL callback
```

相机底层负责从驱动/厂商服务取得帧，应用不主动轮询相机。相机服务在帧到达时调用：

```text
onRGBFrame()
onTrackingFrame()
onCtrlFrame()
```

---

## 5. 相机回调的公共保护逻辑

三个 callback 都执行相似的入口保护：

1. 检查 `data` 和 `userData`；
2. 检查相机是否 pause；
3. 检查 callback admission 是否关闭；
4. `inFlightCallbacks++`；
5. 选择该相机组独立的 EGL context；
6. 执行具体帧处理；
7. release 当前 EGL context；
8. `inFlightCallbacks--`；
9. 当 in-flight 归零时唤醒停止等待者。

这套计数用于暂停和停止：

```text
关闭 callback admission
→ 不接受新帧
→ 等待 inFlightCallbacks == 0
→ 安全关闭相机组和 EGL/编码资源
```

相机 callback 本身就是厂商相机服务提供的工作线程。RGB、tracking、ctrl 各自使用独立 EGL context，避免三个 callback 争用同一个 GL context。

---

## 6. RGB 相机完整流程

### 6.1 输入数据

RGB 的 `FrameData` 通常包含：

```text
frames[0]       左眼 metadata
frames[1]       右眼 metadata
hwBuffer[0]     左眼图像
hwBuffer[1]     右眼图像
hwBufferCount   2
```

### 6.2 第一次有效帧

`handleRGBFrame()` 在第一次需要时完成：

- 保存 `camera_params_rgb.json`；
- 读取 `AHardwareBuffer_Desc`；
- 得到真实宽高；
- 初始化 RGB shader；
- 当业务状态需要编码时，懒初始化 RGB encoder；
- 创建输出尺寸 `2W × H` 的 HEVC encoder；
- 创建对应 MediaCodec input Surface；
- 创建 EncoderSurface 的 EGL surface/context。

编码器不在应用启动时固定创建，而是由实际帧尺寸和当前业务状态决定。

### 6.3 AHardwareBuffer 导入 OpenGL

每个眼睛执行：

```text
AHardwareBuffer
→ eglGetNativeClientBufferANDROID()
→ EGLClientBuffer
→ eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID)
→ EGLImageKHR
→ GL_TEXTURE_EXTERNAL_OES
```

当前实现：

- external OES texture 是持久对象；
- 每一帧重新把新的 EGLImage 绑定到持久 texture；
- 每帧 EGLImage 在处理完成后销毁；
- 不复制原始相机像素到普通 CPU buffer；
- 相机 buffer 只在 callback 有效期内被使用。

### 6.4 计算 RGB 视觉锚点

RGB 左眼 metadata 用于计算这一组帧的同步时刻：

```text
midExposureNs =
    frames[0].timestamp + frames[0].exposure / 2
```

其中：

- `frames[0].timestamp` 是曝光开始 `CLOCK_BOOTTIME` ns；
- `exposure` 是曝光持续时间；
- `midExposureNs` 是画面最合理的采样时间。

这个时间同时用于：

- MediaCodec presentation time；
- `rgb_metainfo.csv`；
- Head Pose 查询；
- 双手关节查询；
- 双手骨骼视频叠加；
- 后处理与 IMU 对齐。

### 6.5 RGB SBS 绘制

RGB 编码时切换到 EncoderSurface：

```text
MediaCodec input Surface
        │
        ▼
EncoderSurface EGL context
        │
        ├── viewport [0, W)      绘制左眼
        └── viewport [W, 2W)     绘制右眼
```

两眼使用同一个 shader 和全屏 quad：

```text
左眼 texture ──► 左半部分
右眼 texture ──► 右半部分
```

结果是一个 `2W × H` 的 SBS 帧。

### 6.6 双手骨骼叠加

如果开启 `persist.xr.project_hand=1`：

1. 传感器对齐线程准备与当前 RGB 曝光时刻匹配的 Head Pose 和双手关节；
2. `HandOverlayRenderer` 获取 RGB 内参、外参和 aligned snapshot；
3. 将关节从 Root/World Space 变换到 Camera Space；
4. 使用 Kannala-Brandt 鱼眼模型投影到像素；
5. 执行与画面方向一致的旋转；
6. 在左右眼对应区域绘制骨骼。

叠加只作用于编码视频，不改变保存的原始关节 CSV。

### 6.7 向 MediaCodec 提交

绘制完成后：

```text
EncoderSurface::setPresentationTime(midExposureNs)
→ CameraEncoder::submitFrameMeta(FrameMeta)
→ eglSwapBuffers()
→ MediaCodec 接收这一帧
```

FrameMeta 保存：

- exposure start；
- exposure duration；
- gain；
- frameId；
- mid-exposure；
- 必要的诊断时间。

Metadata 必须在 swap 前入队，使 encoder output thread 能按编码输出顺序取得对应 metadata。

### 6.8 RGB callback 结束

完成后：

- 切回 RGB camera EGL context；
- 如有快照请求，读取 RGBA 像素到 snapshot buffer；
- 恢复之前 FBO；
- 销毁本帧 EGLImage；
- 标记 RGB frame ready；
- 返回厂商相机 callback；
- 厂商相机服务可以回收/复用 `AHardwareBuffer`。

---

## 7. Tracking 相机完整流程

### 7.1 输入布局

Tracking 相机组通常使用：

```text
hwBufferCount = 1
hwBuffer[0] = 左右眼拼接的图像 buffer
frames[0] = 左眼 metadata
frames[1] = 右眼 metadata
```

### 7.2 EGL 和 GL 处理

流程：

```text
AHardwareBuffer
→ EGLClientBuffer
→ EGLImageKHR
→ GL_TEXTURE_EXTERNAL_OES
→ tracking 独立 EGL context
→ YUV/亮度采样 shader
→ MediaCodec input Surface
```

当前实现对 tracking 使用独立：

- `trackingCtx`；
- grayscale shader；
- encoder；
- EncoderSurface；
- metadata queue；
- fMP4 writer。

### 7.3 编码和 metadata

当数据集正在录制：

1. 计算 tracking 帧 mid-exposure；
2. 将 FrameMeta 放入 tracking encoder；
3. 设置 encoder surface presentation time；
4. shader 将相机 buffer 绘制到 encoder surface；
5. `eglSwapBuffers()`；
6. MediaCodec 输出 HEVC；
7. 写 `tracking.mp4`；
8. 同步写 `tracking_metainfo.csv`。

tracking 帧独立保持自己的 60 fps 时间轴，不强制与 RGB 一一对应。后处理使用双方的 `mid_exposure_utc_ns` 做时间匹配。

### 7.4 参数保存

第一次有效 tracking 帧保存：

```text
camera_params_tracking.json
```

内容来自两眼 `FrameInfo`，包括内参、畸变和 Camera→Body 外参。

---

## 8. Ctrl 相机完整流程

Ctrl 相机与 tracking 的处理结构基本相同：

```text
厂商相机帧
→ onCtrlFrame()
→ ctrlCtx
→ AHardwareBuffer 导入 EGLImage
→ external OES texture
→ 灰度 shader
→ ctrl EncoderSurface
→ MediaCodec HEVC
→ ctrl.mp4
→ ctrl_metainfo.csv
```

区别是：

- 使用独立 `ctrlCtx`；
- 使用独立 encoder 和 output thread；
- 保存独立的 `camera_params_ctrl.json`；
- metadata 来自 ctrl 相机组；
- 与 RGB/tracking 之间只通过统一 UTC 时间线关联。

---

## 9. CameraEncoder 与视频落盘

### 9.1 编码器创建

`CameraEncoder` 配置 MediaCodec：

- codec：`video/hevc`；
- RGB：2W×H；
- tracking/ctrl：各自 W×H；
- bitrate 按相机组配置；
- frame rate 按相机组配置；
- B-frame 关闭；
- Surface 或 Buffer 输入模式；
- 启动独立 output thread。

### 9.2 编码输出线程

每个视频 encoder 有自己的 `outputLoop()`：

```text
AMediaCodec_dequeueOutputBuffer()
        │
        ├── OUTPUT_FORMAT_CHANGED
        │       └── 取得 VPS/SPS/PPS，准备 fMP4 track
        │
        ├── CODEC_CONFIG
        │       └── 缓存 codec config
        │
        ├── 普通 access unit
        │       ├── 匹配 FrameMeta
        │       ├── 计算相对 PTS
        │       ├── 写 fMP4
        │       ├── 写 metainfo CSV
        │       └── RGB 可送 TCP 8802
        │
        └── EOS
                └── 完成 drain
```

### 9.3 动态挂载 writer

编码器可以为了手机预览已经在运行，但此时不落盘。

从预览进入录制时：

1. 不停止 RGB encoder；
2. 不断开 TCP 8802；
3. 创建新的 DatasetRecorder；
4. `armWriter(datasetDir)`；
5. 请求 HEVC IDR；
6. output thread 同时检查 codec flag 和 NAL 类型；
7. IDR 前的帧继续发送到 TCP；
8. IDR 前的帧不写入新文件；
9. 确认后的 IDR 成为文件第一帧；
10. 文件 PTS 从该 IDR 归零。

这样避免为了开始录制而重启预览 encoder。

### 9.4 视频 metadata 匹配

输入时 `submitFrameMeta()` 把 metadata 放入队列。

输出时：

- 由于 B-frame 被关闭，输入/输出呈现顺序保持一致；
- 每个编码 sample 消费一个 FrameMeta；
- 写一行 `*_metainfo.csv`；
- `pts_us` 与 fMP4 sample PTS 完全一致；
- exposure/gain 使用相机驱动 metadata；
- UTC 时间通过 session offset 计算。

### 9.5 FMP4Writer

视频文件结构：

```text
ftyp
moov
styp + moof + mdat   sample 0
styp + moof + mdat   sample 1
styp + moof + mdat   sample 2
...
```

特点：

- `moov` 在文件开头；
- 每一个编码 sample 独立 fragment；
- 强杀或掉电时，最后一个完整 fragment 之前的数据仍可读取；
- 不依赖正常 stop 才能生成整个索引。

### 9.6 RGB 实时预览

RGB encoder output 同时实现 `IRgbEncodedSink`：

```text
RGB EncodedAccessUnit
        │
        └── protocol_adapter
                │
                └── videoQueue
                        │
                        └── TCP 8802 send thread
```

网络发送与文件写入分离：

- 网络慢不允许阻塞 fMP4 writer；
- TCP 8802 断开不停止本地录制；
- 新连接需要 codec config 和新的 IDR 才能正常起解；
- tracking 和 ctrl 不进入 TCP 8802。

---

## 10. 音频完整流程

### 10.1 启动

`DatasetRecorder.start()`：

1. 创建 `audio.m4a` 路径；
2. 把 session 的 BOOTTIME→REALTIME offset 设置给 AudioEncoder；
3. 创建 AAC MediaCodec；
4. 打开 `audio_metainfo.csv`；
5. 创建 AAudio input stream；
6. 启动三个线程。

### 10.2 三线程结构

```text
Recording Thread
    AAudioStream_read()
    → PCM + AAudio timestamp
    → 软件 gain
    → PcmFrame queue

Input Thread
    PcmFrame queue
    → 按 1024 samples 切 AAC frame
    → AMediaCodec input buffer

Output Thread
    AMediaCodec output
    → AAC access unit
    → FMP4Writer
    → audio_metainfo.csv
```

### 10.3 PCM 采集时间

Recording thread：

- 从 AAudio 读取 PCM；
- 获取 stream timestamp；
- 建立音频帧位置和 `CLOCK_BOOTTIME` 的关系；
- 丢弃约 200 ms cold-start warm-up；
- 对跨过 warm-up 边界的 block 修剪前部；
- 调整该 block PTS；
- 应用软件增益；
- 放入有界 PCM queue。

PCM queue 满时丢最旧 block，以限制实时延迟，并增加 dropped 计数。

### 10.4 AAC 编码时间

Input thread：

- AAC 每帧 1024 samples；
- 每块 PCM 拆成若干 AAC input；
- PTS 根据音频 sample 时间递增；
- 结束时向 MediaCodec 输入 EOS。

### 10.5 音频输出和落盘

Output thread：

1. 从 codec output format 取得 AAC `csd-0`；
2. 生成 fMP4 audio track；
3. 跳过 codec-config buffer；
4. 第一包 AAC 的 PTS 作为音频文件零点；
5. 每包写一个 fMP4 fragment；
6. 每包写一行 `audio_metainfo.csv`。

```text
capture_utc_ns =
    audioBoottimeBaseNs
    + absoluteCodecPtsUs × 1000
    + boottimeToRealtimeOffsetNs
```

### 10.6 音频停止

严格顺序：

```text
requestStop AAudio
→ join Recording Thread
→ Input Thread drain queue 并送 EOS
→ join Input Thread
→ Output Thread drain 到 EOS
→ join Output Thread
→ close AAudio
→ stop/delete MediaCodec
→ close FMP4Writer
→ close audio metadata CSV
```

---

## 11. IMU 完整流程

### 11.1 启动

`DatasetRecorder.start()`：

1. 创建 `accel.csv`；
2. 创建 `gyro.csv`；
3. 写 CSV header；
4. 设置 BOOTTIME→REALTIME offset；
5. 获取 `ASensorManager`；
6. 启动 sensor thread；
7. 启动 writer thread。

### 11.2 Sensor thread

```text
ALooper_prepare()
→ ASensorManager_createEventQueue()
→ enable accelerometer
→ enable gyroscope
→ request FASTEST
→ ALooper_pollOnce()
→ ASensorEventQueue_getEvents()
```

每个 `ASensorEvent` 转为内部 `ImuEvent`：

```text
timestamp
x
y
z
isAccel
```

然后放入 `mWriteQueue`，通知 writer thread。

Sensor thread 不直接操作 CSV，避免存储延迟阻塞 Android sensor event queue。

### 11.3 Writer thread

Writer thread：

1. 等待 queue；
2. 取出一个 ImuEvent；
3. `utcNs = event.timestamp + sessionOffset`；
4. 根据类型写 accel 或 gyro；
5. 每 200 个 sample flush；
6. 更新计数。

输出：

```text
accel.csv: timestamp_ns,x,y,z
gyro.csv:  timestamp_ns,x,y,z
```

### 11.4 IMU 与视频的关系

IMU 不在采集时压缩成“一帧视频对应一个 IMU”。

它保存完整高速流：

```text
RGB frame N-1 mid-exposure
    ├── 多个 accel sample
    ├── 多个 gyro sample
    └── ...
RGB frame N mid-exposure
```

后处理以视频的 `mid_exposure_utc_ns` 为目标，从 accel/gyro CSV 中查找最近样本或执行插值。

### 11.5 IMU 标定

录制开始时通过厂商相机客户端的可选接口获取 IMU 标定，写：

```text
imu_calibration.json
```

包括：

- accel bias；
- gyro bias；
- scale factor；
- nonorthogonality；
- noise；
- IMU 与 pose 时间偏移；
- 每个相机与 IMU 的时间偏移；
- device UID。

CSV 保存的是采集样本；标定值独立保存在 sidecar，供后处理选择是否校正。

---

## 12. Head Pose 与双手采集

### 12.1 完整线程和容器关系

```text
OpenXR 主帧循环/渲染线程
  │ 当前时刻 xrLocateSpace + xrLocateHandJointsEXT
  │ 构造 PoseHandSampleRing::Sample
  ▼
PoseHandSampleRing[64]（显式固定 ring，mutex 保护）
  │
  ├───────────────────────────────────────────┐
  │                                           │
RGB 厂商回调执行上下文                        │
  │ 只提交 midExposureNs                      │
  ▼                                           │
s_alignTs 原子单槽 + s_alignCv                │
  │ 不是 FIFO；新值可能覆盖未处理旧值         │
  ▼                                           ▼
sensorAlignWorker 线程 ───────────────────────┘
  │ RGB BOOTTIME → XrTime
  │ direct OpenXR query，失败才从 ring fallback
  │ 插值也在本线程内完成
  ├─ Head → mPoseMap → Pose writer → head_pose.csv
  ├─ Hand → m_frameQueue → Hand writer → hand_tracking.csv
  └─ overlaySnap 最新值 → RGB callback 绘制骨骼
```

因此不存在另一个独立“插值线程”。`sensorAlignWorker` 同时负责目标时刻查询、fallback/插值、构造落盘对象和更新叠加 snapshot。

### 12.2 OpenXR 内部与项目代码的边界

原始 tracking 传感器、VIO/SLAM、融合、预测和手关节识别由 OpenXR runtime 内部完成，内部线程与队列不在项目源码中。项目显式调用：

```text
xrLocateSpace(..., targetXrTime, &location)
xrLocateHandJointsEXT(..., targetXrTime, &locations)
```

这两个 API 同步填充调用者提供的结构体/数组。Head Pose 中：

- `position` 是 3 个 `float`；
- `orientation` 是 4 个 `float` 四元数，项目顺序 `x, y, z, w`；
- `locationFlags` 是 position/orientation 的 valid/tracked 位标志，不是方向值，也不参与插值。

不能因为应用只看到一个输出变量，就推断 runtime 内部没有 buffer；只能确认项目没有直接管理其内部 buffer。

### 12.3 OpenXR 主循环到 align worker：显式 ring

主循环先在栈上构造完整 `PoseHandSampleRing::Sample`，包括时间、Head Pose/有效性，以及左右手各 26 个关节的位置、半径、四元数和 active 状态，然后调用：

```text
poseHandRing.push(rs)
→ mutex 加锁
→ buffer[writeIdx] = rs
→ 更新 writeIdx/count
→ mutex 解锁
```

Ring 位于：

```text
生产者：OpenXR 主循环线程
消费者：sensorAlignWorker
```

容量固定为 64，满后覆盖最老样本，不阻塞生产者。`buffer[writeIdx] = rs` 是一次完整结构体复制；mutex 保证消费者不会读到写了一半的槽位。

### 12.4 RGB callback 到 align worker：只有时间戳单槽

RGB 图像不传给 align worker：

```text
RGB callback
→ s_alignTs.store(midExposureNs)
→ s_alignCv.notify_one()
→ align worker 被唤醒
→ s_alignTs.exchange(-1)
```

这不是 queue。若 align worker 落后，新时间戳可能覆盖旧时间戳，因此不能保证每个 RGB 帧都产生一条 Pose/Hand 记录；这样设计是为了不让 OpenXR 查询阻塞 Camera callback。

### 12.5 align worker 如何取得目标时刻结果

`sensorAlignWorker` 读取 RGB 曝光中点后：

1. 把 `CLOCK_BOOTTIME` 转为目标 `XrTime`；
2. 优先让 runtime 在该目标时刻执行 Head/Hand direct query；
3. direct query 不可用时调用 `PoseHandSampleRing::sample()`；
4. `sample()` 持有 ring mutex，寻找目标时刻前后样本；
5. Head position 线性插值，orientation 四元数 SLERP；
6. flags/validity 只作有效性判断；
7. Hand joints 当前使用最近邻，不对每个关节做线性插值；
8. ring 也不可用时再退回最近共享 snapshot。

查找、插值和结果写出均在 align worker 内，不存在 ring 到“插值线程”的再次传输。

### 12.6 align worker 到 Pose writer：显式 map 缓冲

二者之间有项目代码显式缓冲，但严格类型不是 `std::queue`：

```text
sensorAlignWorker
→ DatasetRecorder::saveHeadPose()
→ 构造 PoseEntry(timestamp + 3 position + 4 quaternion)
→ mutex 内 mPoseMap[timestamp] = entry
→ mPoseCV.notify_one()
→ mPoseWriterThread
→ head_pose.csv
```

`mPoseMap` 是 `std::map<int64_t, PoseEntry>`。它按 timestamp 排序，writer 使用 `mMaxSeenPoseTimestamp - 100 ms` 的重排窗口，只写安全阈值以前的数据；停止时排空剩余项。插入 map 时发生一次小结构体按值复制。

### 12.7 align worker 到 Hand writer：显式 FIFO

```text
sensorAlignWorker
→ 构造 FrameData
→ RawDateSave::SaveFrame(const FrameData&)
→ mutex 内 m_frameQueue.push(frameData)
→ condition_variable 通知
→ m_saveThread
→ frameData = queue.front(); queue.pop()
→ FrameToCsv()
→ hand_tracking.csv
```

`m_frameQueue` 是项目显式 `std::queue<FrameData>`。`push(frameData)` 完整复制一次，writer 从 `front()` 复制到线程局部对象一次。当前队列没有容量上限，磁盘持续变慢时可能增长。

### 12.8 内存传递与复制汇总

| 阶段 | 内存/所有者 | 项目可见复制 |
|---|---|---|
| OpenXR 输出 | runtime 填充调用者 location/关节数组 | runtime 内部不可见 |
| 主循环构造 `Sample` | 主循环栈对象 | 提取并写入字段 |
| `ring.push(rs)` | ring 固定槽 | 1 次完整 `Sample` 赋值 |
| `ring.sample()` | align worker 结果对象 | 插值写出；Hand 最近邻数组复制 |
| Head `saveHeadPose()` | map 节点 | 构造并复制 `PoseEntry` |
| Hand `SaveFrame()` | queue 节点/线程局部对象 | push 1 次，writer 取出 1 次 |
| CSV | writer 局部字符串/文件流 | 数值格式化成文本 |

这条链路不传 RGB 像素，也不需要图像内存池。双手样本约为 KiB 量级；64 槽 ring 约百 KiB 量级，复制开销远小于视频帧，但无界 Hand queue 仍需监控背压。

### 12.9 RGB 骨骼叠加分支

align worker 还会写 mutex 保护的 `overlaySnap` 并预计算投影。它是 latest-value snapshot，不是 FIFO：

```text
sensorAlignWorker 写最新 aligned Pose/Hand
→ RGB callback 加同一 mutex 读取
→ OpenGL 绘制骨骼
```

CSV writer 与 overlay 消费者彼此独立，写 CSV 不占用 RGB 图像 buffer。原则上 CSV 关节、叠加关节和时间戳来自同一个 RGB 曝光目标时刻。

---

## 13. 时间戳系统

### 13.1 内部时间域

当前采集链主要涉及：

| 来源 | 原始时间域 |
|---|---|
| 相机曝光开始 | `CLOCK_BOOTTIME` |
| Android IMU event | `CLOCK_BOOTTIME` |
| 音频基准 | 映射到 `CLOCK_BOOTTIME` |
| Head Pose/双手 | OpenXR `XrTime` |
| 数据集对外时间 | UTC ns |
| fMP4 PTS | session 内零基时间 |

### 13.2 Session UTC offset

录制开始时 `DatasetRecorder` 紧邻采样：

```text
boottime = clock_gettime(CLOCK_BOOTTIME)
realtime = clock_gettime(CLOCK_REALTIME)

offset = realtime - boottime
```

整个 session 使用同一个 offset：

```text
utcNs = boottimeNs + offset
```

这让 video、audio、IMU、Head Pose、双手进入同一 UTC 时间线。

### 13.3 OpenXR 时间转换

相机时间不能直接当作 `XrTime` 使用。

流程：

```text
camera BOOTTIME ns
→ xrConvertTimespecTimeToTimeKHR
→ XrTime
→ xrLocateSpace / xrLocateHandJointsEXT
```

转换函数在 OpenXR 初始化时加载，并通过全局函数指针提供给 sensor-align worker。

### 13.4 视频 PTS 和绝对时间是两套值

视频 metadata 同时保存：

- `pts_us`：媒体内零基时间；
- `exposure_start_utc_ns`：绝对曝光开始；
- `mid_exposure_utc_ns`：绝对曝光中心。

用途：

- 播放和解码使用 `pts_us`；
- 多传感器对齐使用 `mid_exposure_utc_ns`；
- 追查相机曝光使用 `exposure_start_utc_ns` 和 exposure duration。

不要用编码输出时间代替曝光时间。

### 13.5 三类同步不能混淆

当前系统存在三类时间问题：

1. 相机、IMU、音频、OpenXR 的设备内部同步；
2. capture timestamp 到 MediaCodec/fMP4 PTS；
3. TCP custom NTP 和 BLE 四时间戳的设备间同步。

外部时间协议用于估计设备之间的 UTC offset/RTT，不负责修改已经采集的数据流内部对应关系。

---

## 14. 数据集启动流程

### 14.1 从空闲开始录制

```text
OperationCoordinator
→ target mode / STARTING
→ DatasetRecorder.start()
    ├── 创建 dataset/<time>/
    ├── 采样 session UTC offset
    ├── 启动 AudioEncoder
    ├── 启动 ImuPoseCollector
    ├── 打开 head_pose writer
    ├── 启动 hand CSV session
    └── 写 capture_status.json = recording
→ 准备 video writers
→ 请求 RGB IDR
→ 第一个有效 IDR 写入
→ STABLE
```

### 14.2 相机参数和标定

录制开始后，每个相机组第一次有效帧触发：

```text
saveCameraParams()
```

输出：

- `camera_params_rgb.json`；
- `camera_params_tracking.json`；
- `camera_params_ctrl.json`。

IMU 标定在 session 初始化阶段写入。

### 14.3 capture_status

状态：

- `recording`；
- `finalizing`；
- `complete`。

只有音频、IMU、Head Pose、双手和媒体链都结束后，才能标记完整。

---

## 15. 快照流程

快照由广播或本地输入触发：

```text
snapshotRequested = true
```

随后各相机 callback 在自己的 EGL context：

1. 将当前相机图像绘制到 FBO；
2. `glReadPixels()` 得到 RGBA；
3. 垂直翻转；
4. 写入线程安全 snapshot buffer；
5. 设置 ready。

主循环等待目标相机数据 ready 后：

- 将左右眼组合；
- 使用 stb image writer 写 PNG；
- 输出到 images 目录；
- 清除请求标志。

快照涉及 GPU→CPU readback，因此不是零拷贝路径；只在请求时执行。

---

## 16. 录制与预览状态关系

当前编码、落盘、网络并非永远同时开启：

| 状态 | RGB 编码 | tracking/ctrl 编码 | 文件 | TCP 8802 |
|---|---:|---:|---:|---:|
| 空闲 | 关 | 关 | 关 | 关 |
| 仅手机预览 | 开 | 关 | 关 | 开 |
| 本地录制 | 开 | 开 | 开 | 关 |
| 本地录制并预览 | 开 | 开 | 开 | 开 |
| 手机录制 | 开 | 开 | 开 | 开 |

重要关系：

- 预览只需要 RGB；
- tracking/ctrl 只在实际数据集录制时编码；
- 从预览进入录制时复用 RGB encoder；
- 录制中打开或关闭预览不改变文件时间轴；
- TCP 8802 失败不应中断本地文件；
- 文件写入失败属于录制故障。

---

## 17. 停止流程

完整停止顺序：

```text
1. OperationCoordinator → STOPPING
2. 禁止新网络帧，shutdown TCP 8802
3. 关闭新的相机编码提交
4. 等待在途相机 callback
5. 向视频 MediaCodec 发送 EOS
6. 每个 output thread drain 到 EOS
7. 写完 EOS 之前的最后有效视频 sample
8. finalize/close video FMP4Writer 和 metadata CSV
9. 停 AAudio，drain AAC，关闭 audio writer
10. 停 IMU sensor thread
11. drain IMU writer queue
12. 停 Head Pose writer，输出 reorder buffer 剩余内容
13. 停 hand writer
14. 写 capture_status/完成状态
15. 销毁 encoder surface、MediaCodec 和 EGL 资源
16. 状态回到 IDLE/STABLE
```

关键原则：

- 第一个 Stop 生效；
- 后续 Stop 复用同一停止过程；
- 一个 MediaCodec 只由一个 owner stop/delete；
- 一个 FMP4Writer 只由对应 output thread finalize；
- 状态锁内不执行 join、drain、flush 或 socket send；
- EOS 本身不是媒体帧；
- 末帧是 EOS 前最后成功写入的有效 sample。

---

## 18. 当前线程模型

| 线程/来源 | 主要职责 |
|---|---|
| Android main/OpenXR loop | Activity event、OpenXR frame、连续 pose/hand snapshot、快照协调 |
| RGB camera callback thread | RGB buffer 导入、SBS 绘制、视频提交 |
| Tracking camera callback thread | tracking 导入、灰度处理、视频提交 |
| Ctrl camera callback thread | ctrl 导入、灰度处理、视频提交 |
| Sensor-align worker | 按 RGB 曝光中点查询 Head Pose/双手 |
| RGB encoder output thread | HEVC drain、RGB fMP4、metadata、TCP sink |
| Tracking encoder output thread | HEVC drain、tracking fMP4、metadata |
| Ctrl encoder output thread | HEVC drain、ctrl fMP4、metadata |
| Audio recording thread | AAudio PCM |
| Audio input thread | PCM→AAC input |
| Audio output thread | AAC drain、audio fMP4、metadata |
| IMU sensor thread | Android accel/gyro |
| IMU writer thread | accel/gyro CSV |
| Head Pose writer thread | 排序和 CSV |
| Hand writer thread | hand tracking CSV |
| TCP control thread | 8801 控制 |
| Status thread | 周期状态 |
| TCP video accept thread | 8802 连接 |
| TCP video send thread | RGB HEVC 发送 |

线程之间通过：

- mutex；
- condition_variable；
- atomic flag；
- queue/deque/map；
- OpenGL shared context；
- encoder output callback；

完成协作。

---

## 19. 当前缓冲和队列

### 19.1 相机图像

- 像素主要保留在厂商 `AHardwareBuffer`；
- 通过 EGLImage/GL texture 零拷贝访问；
- RGB texture 对象持久；
- RGB EGLImage 每帧创建/销毁；
- callback 返回前完成对 buffer 的使用。

### 19.2 视频 metadata

- 每个 CameraEncoder 一个 metadata deque；
- 输入帧提交时 push；
- 编码 sample 输出时 pop；
- B-frame 关闭是保持匹配关系的重要条件。

### 19.3 音频

- PCM queue 有上限；
- 满时丢最旧 block；
- 每个 PcmFrame 持有自己的 byte vector；
- AAC output 直接送 FMP4Writer。

### 19.4 IMU

- sensor thread 将 ImuEvent 放入 write queue；
- writer thread逐项写出；
- 当前 queue 是普通队列，主要依靠 writer 持续跟上采集速率。

### 19.5 Pose 和双手

- 连续 OpenXR sample 存入固定历史 ring；
- Head Pose writer 使用 timestamp→Pose 的排序 map；
- 100 ms reorder window；
- 双手数据由异步 writer queue 写出。

### 19.6 TCP 视频

- RGB 编码数据复制/封装后进入网络 deque；
- send thread 独立发送；
- 网络队列和文件 writer 分离；
- 网络慢不能阻塞 encoder 文件输出。

---

## 20. 坐标关系

内部主要坐标链：

```text
World/Root Space
      │ Head Pose
      ▼
Body
      │ Camera→Body 外参
      ▼
Camera
      │ Kannala-Brandt 投影
      ▼
Pixel
```

相机外参：

```text
X_body = R_BC × X_camera + t_BC
```

Head Pose：

```text
T_WB = Body 在 World 中的位姿
```

相机在 World 中：

```text
T_WC = T_WB × T_BC
```

双手骨骼投影需要：

1. Root/World 中关节点；
2. 同一时刻 Head Pose；
3. Camera→Body 外参；
4. 相机内参；
5. 鱼眼畸变；
6. 图像方向变换。

这些参数必须属于同一 RGB 帧时刻，才能避免运动时骨骼漂移。

---

## 21. 最终落盘结果

```text
RGB Camera
→ AHardwareBuffer×2
→ EGL/OpenGL SBS
→ MediaCodec HEVC
→ rgb.mp4
→ rgb_metainfo.csv

Tracking Camera
→ AHardwareBuffer
→ EGL/OpenGL gray
→ MediaCodec HEVC
→ tracking.mp4
→ tracking_metainfo.csv

Ctrl Camera
→ AHardwareBuffer
→ EGL/OpenGL gray
→ MediaCodec HEVC
→ ctrl.mp4
→ ctrl_metainfo.csv

Microphone
→ AAudio PCM
→ AAC MediaCodec
→ audio.m4a
→ audio_metainfo.csv

Accelerometer
→ Android SensorManager
→ accel.csv

Gyroscope
→ Android SensorManager
→ gyro.csv

OpenXR Head Pose
→ RGB mid-exposure query/interpolation
→ head_pose.csv

OpenXR Hand Joints
→ RGB mid-exposure query/interpolation
→ hand_tracking.csv

Camera Calibration
→ camera_params_rgb/tracking/ctrl.json

IMU Calibration
→ imu_calibration.json
```

---

## 22. 当前实现最需要注意的点

### 22.1 相机时间才是视觉时间

视频同步必须使用驱动给出的曝光开始和曝光中点，不能使用 callback、渲染、编码或写盘时间。

### 22.2 RGB 是同步主锚点

Head Pose 和双手按 RGB mid-exposure 对齐。tracking 和 ctrl 保持独立帧率，通过各自 metadata 的 UTC 时间在后处理中与 RGB 匹配。

### 22.3 OpenXR 仍参与采集

即使不关心 XR 场景显示，当前 Head Pose 和双手数据仍来自 OpenXR。删除 OpenXR 会同时失去这些数据，除非另有 tracking 来源。

### 22.4 EGL 不只负责头显显示

当前 EGL/OpenGL 还负责：

- `AHardwareBuffer` 导入；
- RGB SBS；
- 灰度转换；
- 双手骨骼叠加；
- 向 MediaCodec Surface 提交帧。

因此删除头显场景渲染，不等于能直接删除相机编码所用 EGL。

### 22.5 Sensor-align worker 只保存最新待处理时间

当前 RGB callback 通过一个 atomic timestamp 通知对齐线程。若 RGB 到达速度高于对齐线程处理速度，新时间可能覆盖旧时间。这保护了相机 callback 不被阻塞，但极端负载下可能减少 aligned pose/hand 记录数量，需要通过最终 CSV 行数和分析工具检查。

### 22.6 Metadata 与视频数量必须一致

视频文件和 `*_metainfo.csv` 的对应关系依赖：

- 输入 metadata queue；
- B-frame 关闭；
- output thread 串行消费；
- IDR gate；
- stop 时完整 drain。

任何一个环节丢失，都可能造成 MP4 packet 数与 CSV 行数不一致。

### 22.7 内部同步与设备间同步不同

BLE/TCP 对时解决不同设备之间的 UTC 差异；相机、IMU、音频、OpenXR 的内部对齐仍依赖它们自己的时钟域和 session offset。

---

## 23. 显式线程、平台内部队列与内存复制统计

本章专门回答三个实现层问题：

1. 每条数据链路至少经过几个代码可见的线程；
2. 项目代码建立了几个显式队列或缓冲结构；
3. 项目代码中发生了几次能够明确确认的数据复制。

统计时必须区分：

- **项目显式线程**：源码中能够看到 callback 执行上下文或 `std::thread`；
- **平台内部线程**：Camera HAL、厂商相机服务、EGL、BufferQueue、MediaCodec、AAudio、SensorService 内部线程；
- **显式队列**：项目源码中的 `queue`、`deque`、ring、map 或单槽 mailbox；
- **平台内部队列**：厂商 camera buffer pool、`ANativeWindow/BufferQueue`、MediaCodec input/output buffers；
- **视图转换**：`AHardwareBuffer → EGLImage → GL texture`，通常不是整帧像素复制；
- **明确复制**：源码中能够确认的 `memcpy`、`vector::assign`、`std::string::assign`、`glReadPixels` 或小结构体按值复制。

不能仅根据项目代码没有 `std::queue<CameraFrame>`，就认为 Camera 链路没有缓冲。Camera 链路的大部分图像缓冲和排队由厂商相机、Android 图形栈和 MediaCodec 内部管理。

### 23.1 Camera 公共执行阶段

三组 Camera 都经过以下逻辑阶段：

```text
Camera Sensor / ISP
→ Camera Driver
→ 厂商 Camera HAL/Service
→ 厂商内部 buffer pool
→ libsxr_camera_client.so
→ 项目 Camera callback
→ EGLImage / OpenGL
→ MediaCodec Input Surface
→ Android Surface BufferQueue
→ MediaCodec 内部编码
→ 项目 CameraEncoder output thread
→ 文件或网络
```

项目没有直接调用 V4L2，也没有直接执行 Camera `DQBUF/QBUF`。驱动、HAL 和厂商相机库之间的线程数、buffer 数量和队列实现不在当前源码中。

callback 接收到：

```cpp
const SXR::FrameData* data
```

其中包含：

- `AHardwareBuffer*`；
- frame ID；
- 曝光开始时间；
- 曝光时长；
- gain；
- 宽高等描述。

当前代码不会把 `FrameData*` 或 `AHardwareBuffer*` 放入项目图像队列供其他线程长期使用，而是在 callback 返回前完成 EGL 导入、GL 绘制和 Surface 提交。

### 23.2 RGB 显式线程

只考虑 RGB 本地录制主链路，至少有两个项目可见的执行线程：

```text
1. 厂商 RGB Camera callback thread
2. RGB CameraEncoder output thread
```

其中：

- callback thread 由厂商相机库调用，不一定由项目创建；
- output thread 由 `CameraEncoder` 使用 `std::thread` 创建；
- 两者之间还存在 MediaCodec 内部线程/硬件执行阶段，但数量对项目不可见。

RGB callback thread执行：

```text
onRGBFrame()
→ callback admission/in-flight 检查
→ rgbCtx.makeCurrent()
→ AHardwareBuffer 导入 EGLImage
→ 左右眼外部 texture
→ SBS OpenGL 绘制
→ 可选双手叠加
→ setPresentationTime(mid-exposure)
→ submitFrameMeta()
→ eglSwapBuffers()
→ callback 返回
```

RGB output thread执行：

```text
AMediaCodec_dequeueOutputBuffer()
→ 获取 HEVC output buffer
→ 根据 PTS 消费 FrameMeta
→ 写 rgb.mp4
→ 写 rgb_metainfo.csv
→ 可选投递 TCP 8802
→ AMediaCodec_releaseOutputBuffer()
```

开启手机预览后增加一个逐帧消费者线程：

```text
3. TCP video send thread
```

因此：

| RGB 模式 | 项目可见的主要数据线程 |
|---|---:|
| 只录制 | 至少 2 个 |
| 只预览 | 至少 3 个：callback、encoder output、TCP send |
| 录制并预览 | 至少 3 个 |

另有：

- TCP video accept thread：只负责 `listen/accept`，不持续处理每帧图像；
- Sensor-align worker：只消费 RGB 曝光中点时间戳，不消费 RGB 图像；
- Android/MediaCodec 内部线程：存在但数量不可见。

### 23.3 RGB 图像内存

正常RGB编码路径：

```text
厂商拥有的 AHardwareBuffer
→ EGLClientBuffer
→ EGLImageKHR
→ GL_TEXTURE_EXTERNAL_OES
→ 编码器 EGLSurface
→ ANativeWindow/BufferQueue GraphicBuffer
→ MediaCodec
```

对象生命周期：

| 对象 | Owner | 生命周期 |
|---|---|---|
| Camera 原始像素 buffer | 厂商相机/HAL | 厂商 buffer pool 管理 |
| `FrameData*` | 厂商 callback | 通常只保证 callback 期间 |
| `AHardwareBuffer*` | 厂商/Android | 按厂商接口约定 |
| `EGLImageKHR` | 当前 Camera callback | 每帧创建和销毁 |
| RGB GL texture name | 项目/OpenGL | 持久存在，每帧重新绑定 EGLImage |
| Encoder Surface buffer | Android BufferQueue | EGL/ANativeWindow/MediaCodec 管理 |
| Codec output buffer | MediaCodec | `dequeueOutputBuffer` 到 `releaseOutputBuffer` |

`AHardwareBuffer → EGLImage → GL texture`主要是共享内存句柄和GPU视图转换，当前源码没有把两眼原始图像逐帧复制到项目自己的CPU大buffer。

项目也没有显式：

```cpp
std::queue<AHardwareBuffer*>
std::queue<CameraFrame>
```

因此RGB原始图像的项目显式队列数量为：

```text
0
```

但平台内部至少存在：

- 厂商 Camera buffer pool/队列；
- `ANativeWindow/BufferQueue`；
- MediaCodec input/output buffer管理。

### 23.4 RGB 明确复制次数

只统计项目代码中可以确认的媒体数据复制。

#### 本地录制主路径

```text
AHardwareBuffer
→ EGLImage
→ OpenGL
→ MediaCodec Surface
→ Codec output
→ FMP4Writer
```

明确的整帧原始图像CPU复制：

```text
0 次
```

MediaCodec output写文件时，output thread直接将当前 `outBuf` 交给 `FMP4Writer::writeSample()`，没有先复制进项目视频写盘队列。底层文件系统写入内核页缓存不计为项目媒体队列复制。

#### TCP 8802推流

```text
MediaCodec output buffer
→ Annex-B std::string
→ videoQueue_
→ TCP send thread
```

这里至少有：

```text
1 次明确的编码数据复制
```

复制是必要的，因为调用 `AMediaCodec_releaseOutputBuffer()` 后，原 `outBuf` 可能被MediaCodec立即复用，TCP线程不能继续持有该裸指针。

如果AVCC到Annex-B转换需要重新组织NAL前缀，其内存构造也发生在这次网络payload生成过程中。统计上仍视为“从Codec buffer生成一份项目拥有的网络副本”。

#### RGB metadata

`FrameMeta` 是小结构体：

```text
Camera callback构造FrameMeta
→ 按值push到mMetaQueue
→ output thread取出
```

这里有小结构体复制/移动，但不是图像内存复制。

#### 快照

快照路径执行：

```text
GPU framebuffer
→ glReadPixels()
→ CPU RGBA buffer
```

因此快照存在至少一次明确的整帧GPU到CPU复制，不能归入正常零CPU拷贝的视频主路径。

### 23.5 RGB 显式队列和同步结构

| 结构 | 类型 | 生产者 | 消费者 | 容量/策略 |
|---|---|---|---|---|
| `mMetaQueue` | `deque<FrameMeta>` | RGB callback | RGB output thread | 异常超过240项时清理旧项 |
| `videoQueue_` | `deque<string>` | RGB output thread | TCP send thread | 24帧，满时丢最旧 |
| `s_alignTs` | `atomic<int64_t>`单槽 | RGB callback | Sensor-align worker | 新时间戳可覆盖旧时间戳 |
| Pose/Hand历史ring | 固定ring | OpenXR主循环 | Sensor-align worker | 固定历史窗口 |

`mMetaQueue` 使用 `mMetaMutex`：

```text
callback完整构造FrameMeta
→ lock
→ push_back
→ unlock

output thread
→ lock
→ 按PTS查找并erase
→ unlock
```

mutex既保护deque结构，也建立生产者写入与消费者读取之间的内存可见性。

`s_alignTs`不是FIFO队列，只保存一个最新待处理RGB时间戳：

```text
store(release)
→ worker exchange(acq_rel)
```

如果worker未处理上一帧，新帧可能覆盖旧值。

### 23.6 Tracking 显式线程、队列和复制

Tracking本地录制至少有两个项目可见执行线程：

```text
1. 厂商 Tracking Camera callback thread
2. Tracking CameraEncoder output thread
```

中间有不可见的MediaCodec内部阶段。

数据路径：

```text
Tracking AHardwareBuffer
→ EGLImage
→ 外部 texture
→ grayscale shader
→ Tracking Encoder Surface
→ Android BufferQueue
→ MediaCodec HEVC
→ Tracking output thread
→ tracking.mp4
→ tracking_metainfo.csv
```

项目显式原始图像队列：

```text
0 个
```

项目显式metadata队列：

```text
1 个：Tracking CameraEncoder::mMetaQueue
```

正常直接Surface编码路径中的明确整帧原始图像CPU复制：

```text
0 次
```

Tracking不进入TCP 8802，因此没有RGB网络payload那次编码数据复制。

如果走备用 `CameraEncoder::feedFrame()` Buffer模式，则会把Y平面 `memcpy` 到MediaCodec input buffer，并填充UV平面。当前直接编码主路径应与该备用路径分开统计。

### 23.7 Ctrl 显式线程、队列和复制

Ctrl与Tracking相同，至少有：

```text
1. 厂商 Ctrl Camera callback thread
2. Ctrl CameraEncoder output thread
```

数据路径：

```text
Ctrl AHardwareBuffer
→ EGLImage
→ grayscale shader
→ Ctrl Encoder Surface
→ MediaCodec
→ Ctrl output thread
→ ctrl.mp4
→ ctrl_metainfo.csv
```

统计：

| 项目 | 数量 |
|---|---:|
| 项目显式原始图像队列 | 0 |
| Ctrl metadata deque | 1 |
| TCP视频队列 | 0 |
| 直接Surface路径整帧CPU复制 | 0 |
| 项目可见主要数据线程 | 至少2 |

### 23.8 三路Camera总体线程

录制三路Camera时，项目可见的Camera核心执行上下文至少包括：

```text
RGB callback
Tracking callback
Ctrl callback

RGB encoder output
Tracking encoder output
Ctrl encoder output
```

合计至少：

```text
6个执行上下文
```

但必须注意：

- 三个callback由厂商相机库调用；
- 厂商可能一组一线程，也可能使用线程池或串行调度；
- 因而不能仅从当前项目源码证明厂商一定创建了三条固定线程；
- 三个encoder output thread由项目明确创建；
- MediaCodec内部还存在不可见的编码线程或Codec服务执行上下文。

### 23.9 音频线程、内存和复制

音频明确创建三个项目线程：

```text
1. Audio recording thread
2. Audio input thread
3. Audio output thread
```

完整路径：

```text
麦克风驱动/Audio HAL
→ AAudio内部buffer
→ Recording thread
→ recordingLoop临时vector
→ PcmFrame::data
→ mPcmQueue
→ Input thread
→ MediaCodec AAC input buffer
→ MediaCodec内部编码
→ Output thread
→ audio.m4a
→ audio_metainfo.csv
```

显式PCM队列：

```cpp
std::queue<PcmFrame> mPcmQueue;
```

容量：

```text
64个PcmFrame
```

同步：

- `mQueueMutex`；
- `mQueueCV`。

满时：

```text
丢最旧PcmFrame
→ 记录drop
→ 放入最新PcmFrame
```

能够在项目源码中明确确认的PCM复制：

```text
复制1：
AAudioStream_read写入recordingLoop临时buffer后
→ frame.data.assign()
→ PcmFrame::data

复制2：
PcmFrame::data
→ memcpy()
→ MediaCodec input buffer
```

因此，正常音频输入链路至少有：

```text
2次项目级PCM内存复制
```

AAC output thread直接使用MediaCodec output buffer写 `FMP4Writer`，没有再建立AAC磁盘队列。

### 23.10 IMU线程、内存和队列

IMU明确有两个项目线程：

```text
1. IMU sensor thread
2. IMU writer thread
```

完整路径：

```text
IMU硬件
→ Android Sensor HAL/Service
→ ASensorEventQueue
→ IMU sensor thread
→ ImuEvent
→ mWriteQueue
→ IMU writer thread
→ accel.csv / gyro.csv
```

这里存在两级队列：

| 队列 | Owner | 是否项目显式 |
|---|---|---:|
| `ASensorEventQueue` | Android SensorManager | API可见、实现内部 |
| `mWriteQueue` | 项目 | 是 |

项目队列：

```cpp
std::queue<ImuEvent> mWriteQueue;
```

生产者：

```text
IMU sensor thread
```

消费者：

```text
IMU writer thread
```

使用mutex和condition variable保护。

`ImuEvent`为小结构体，sensor thread构造后按值push到queue，因此至少发生一次小结构体复制/移动。没有大型媒体buffer，也没有图像式内存池。

当前 `mWriteQueue`没有明确容量上限，主要依赖writer持续跟上采集速度。如果磁盘长期变慢，queue可能增长。

### 23.11 Head Pose线程、ring、队列和复制

逐步权威说明见 12.1～12.9。统计结论：

- 执行上下文：OpenXR 主循环、RGB callback、`sensorAlignWorker`、Pose writer；
- 主循环到 align worker：显式 `PoseHandSampleRing[64]`，mutex 保护；
- RGB callback 到 align worker：原子单槽 `s_alignTs` + CV，不是 FIFO；
- align worker 到 writer：显式 `std::map<int64_t, PoseEntry>` + mutex/CV，不是普通 queue；
- 插值就在 `sensorAlignWorker` 中，没有独立插值线程；
- position 为 3 个 float，orientation 为 `x,y,z,w` 四元数，flags 只表示 valid/tracked；
- map 插入复制小型 `PoseEntry`，不复制 RGB 图像。

### 23.12 双手线程、ring、队列和复制

逐步权威说明见 12.1～12.9。统计结论：

- 执行上下文：OpenXR 主循环、RGB callback、`sensorAlignWorker`、Hand writer；
- 主循环到 align worker：与 Head 共用显式 64 槽 ring；
- align worker 到 writer：显式无界 `std::queue<FrameData>` + mutex/CV；
- Hand 当前使用最近邻样本，不应描述为所有关节都插值；
- `push(frameData)` 完整复制一次，writer 从 `front()` 复制到局部对象一次，再序列化为 CSV；
- align worker 到 RGB overlay：mutex 保护的 latest snapshot，不是 queue；
- 复制的是 KiB 级关节结构，不是相机像素。

### 23.13 快照线程和复制

快照是Camera正常零CPU拷贝路径的例外。

```text
snapshotRequested
→ 各Camera callback
→ FBO/texture
→ glReadPixels()
→ CPU RGBA buffer
→ snapshot ready状态
→ ImageSaver/协调逻辑
→ PNG
```

明确复制：

```text
至少1次GPU framebuffer到CPU RGBA的整帧读回
```

随后PNG编码器还会读取CPU RGBA并生成压缩输出。快照是低频请求，不应以其内存行为代表正常视频录制主链路。

### 23.14 视频文件和网络的线程所有权

视频文件没有独立的项目磁盘线程：

```text
RGB output thread      独占 RGB FMP4Writer
Tracking output thread 独占 Tracking FMP4Writer
Ctrl output thread     独占 Ctrl FMP4Writer
```

同一个output thread按顺序执行：

```text
取Codec output
→ 匹配metadata
→ 写fMP4 sample
→ 写metadata CSV
→ release Codec buffer
```

优点：

- 视频sample和metadata容易保持一一对应；
- writer不需要额外mutex；
- 不会出现视频和metadata两个写盘queue不同步。

风险：

- 文件系统写入变慢会拖慢encoder output thread；
- output buffer归还变慢；
- MediaCodec内部输出压力增大；
- 最终可能反向影响Surface/Camera callback。

网络发送通过独立queue和send thread隔离：

```text
RGB output thread
→ 复制网络payload
→ videoQueue_
→ TCP send thread
```

因此网络慢主要导致预览旧帧被丢弃，不应该阻塞本地文件writer。

### 23.15 当前Android各链路统计总表

以下“复制次数”只统计项目源码中可以明确确认的数据复制，不包含驱动、GPU、Codec或内核内部可能发生的实现复制。

| 链路 | 项目可见主要线程 | 项目显式队列/缓冲 | 明确媒体复制 |
|---|---:|---|---:|
| RGB本地录制 | 至少2 | metadata deque；对齐单槽 | 原始图像0 |
| RGB推流 | 至少3 | metadata deque、video deque、对齐单槽 | 编码payload至少1 |
| Tracking录制 | 至少2 | metadata deque | 直接Surface路径原始图像0 |
| Ctrl录制 | 至少2 | metadata deque | 直接Surface路径原始图像0 |
| 音频 | 3 | PCM queue | PCM至少2 |
| IMU | 2 | Android event queue + 项目write queue | 小结构体至少1次入队 |
| Head Pose | 至少3个执行上下文 | history ring、对齐单槽、reorder map | 小结构体多次传递 |
| 双手 | 至少3个生产/写入上下文，另有RGB消费者 | history ring、writer queue、overlay snapshot | 关节数组多次传递 |
| 快照 | Camera callback + saver/协调执行上下文 | snapshot CPU buffer | 整帧GPU→CPU至少1 |

### 23.16 “没有显式queue”时的准确理解

Android Camera链路不能描述为：

```text
一个普通变量直接交给另一个线程
```

准确描述是：

```text
callback局部变量用于操作平台对象句柄
→ EGL/ANativeWindow API接管当前Surface buffer
→ Android BufferQueue把GraphicBuffer交给MediaCodec
→ MediaCodec output queue把编码buffer交给output thread
```

例如，局部变量：

```cpp
EGLImageKHR eyeEglImage[2];
```

不会被MediaCodec线程直接读取。它只在callback中帮助GL采样Camera `AHardwareBuffer`。`eglSwapBuffers()`之后，MediaCodec消费的是Android BufferQueue中的Surface GraphicBuffer。

同样：

```cpp
uint8_t* outBuf
```

只在 `dequeueOutputBuffer()` 到 `releaseOutputBuffer()` 之间有效。文件writer在该窗口内同步使用；网络必须先复制出自己的payload。

因此，当前Android Camera实现的核心是：

```text
原始图像大buffer和队列由平台管理；
项目显式管理metadata、同步时间戳和网络副本；
项目显式创建encoder output和网络发送线程；
Camera callback承担EGL/OpenGL和编码输入提交。
```

---

## 24. 一句话总结

当前 Android SDK 的核心采集过程是：

```text
厂商相机驱动输出带曝光时间的 AHardwareBuffer
→ EGL/OpenGL 完成图像导入、SBS/灰度和可选双手叠加
→ MediaCodec 硬件编码
→ FMP4Writer 和 TCP 预览

同时：
AAudio、Android IMU、OpenXR Head Pose 和双手持续采集
→ 所有数据转换到统一 BOOTTIME/UTC 时间轴
→ Head Pose 和双手按 RGB 曝光中点查询
→ 最终写成可逐帧对齐的数据集
```

真正把所有采集链连接起来的不是渲染帧，也不是编码时间，而是相机曝光时间和统一时钟映射。
