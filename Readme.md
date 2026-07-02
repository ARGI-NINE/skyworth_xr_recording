# SXR EGO SDK 使用说明

XR 设备多传感器数据采集 SDK：通过 OpenXR + 相机扩展采集 RGB / 灰度相机、IMU、音频、位姿、手势，同步录制为带时间戳的数据集。

## 目录

- [1. 快速开始](#1-快速开始) — 系统属性 / ADB 命令 / 按键 / 路径
- [2. 数据集](#2-数据集) — 目录结构 / 文件格式 / 时间戳体系 / 录制流程
- [3. 相机与坐标系](#3-相机与坐标系) — 坐标系约定 / 内外参 / 位姿变换
- [4. 编码管线](#4-编码管线) — 硬件编码 / SBS / 时间戳 / 格式
- [5. 开发参考](#5-开发参考) — SxrCameraApi / 核心结构 / 手势
- [附录](#附录) — 设备硬件 / 截图录像 / MP4 分析 / 注意事项

---

# 1. 快速开始

### 系统属性

| 属性 | 值 | 说明 | 生效 |
|------|------|------|------|
| `persist.xr.usecontroller` | `true`/`false` | 手柄模式 / 手势模式 | 重启应用 |
| `persist.xr.project_hand` | `1`/`0` | 手势骨骼投影到编码视频（不影响头戴预览，需手势模式） | 重启应用 |
| `persist.xr.project_controller` | `1`/`0` | 手柄坐标系投影到编码视频（不影响头戴预览，需手柄模式） | 重启应用 |
| `persist.sxr.cam.rgb.fps` | `30`/`60` | RGB 相机帧率（默认 30） | 重启应用 |

```bash
adb shell getprop persist.xr.usecontroller
adb shell setprop persist.xr.usecontroller false   # 切到手势模式
adb shell setprop persist.xr.project_hand 1        # 开启骨骼投影
adb shell setprop persist.xr.project_controller 1   # 手柄模式：开启坐标系投影
adb shell setprop persist.sxr.cam.rgb.fps 60       # RGB 切到 60fps
```

### ADB 远程命令

```bash
# 截图（所有相机 → .../files/images/）
adb shell am broadcast -a com.ssnwt.helloxr.SAVE_IMAGE

# 数据集录制（→ .../files/dataset/<YYYYMMDD_HHMMSS>/）
adb shell am broadcast -a com.ssnwt.helloxr.START_RECORDING
adb shell am broadcast -a com.ssnwt.helloxr.STOP_RECORDING

# 拉取数据集
adb pull /sdcard/Android/data/com.ssnwt.helloxr/files/dataset/ ./dataset/

# 重启应用 / 查看日志
adb shell am force-stop com.ssnwt.helloxr && sleep 1 \
  && adb shell am start -n com.ssnwt.helloxr/com.ssnwt.helloxr.VrNativeActivity
adb logcat | grep -E "HelloXr|sxrcam|DatasetRecorder|Encoder"
```

### 手柄按键

| 按键 | 功能 |
|------|------|
| VOLUME_UP | 截图（所有相机） |
| Right B / DPAD_CENTER | 切换录制开始/停止 |

### 数据路径

```
/sdcard/Android/data/com.ssnwt.helloxr/files/
├── dataset/<YYYYMMDD_HHMMSS>/   # 数据集
└── images/                      # 截图（VOLUME_UP 触发）
```

---

# 2. 数据集

## 2.1 目录结构

```
dataset/<YYYYMMDD_HHMMSS>/
├── rgb.mp4 / rgb_metainfo.csv           # RGB SBS 视频 (2W×H, H.265, 30fps) + 每帧元数据
├── tracking.mp4 / tracking_metainfo.csv # Tracking 灰度 (W×H, H.265, 60fps) + 元数据
├── ctrl.mp4 / ctrl_metainfo.csv         # Ctrl 灰度 (W×H, H.265, 60fps) + 元数据
├── audio.m4a / audio_metainfo.csv       # AAC 音频 (44.1kHz, mono, 96kbps) + 元数据
├── accel.csv / gyro.csv                 # IMU 加速度计/陀螺仪
├── head_pose.csv                        # 6DOF 头部位姿
├── hand_tracking.csv                    # 手势关节（手势模式）
├── controller_poses.csv                 # 手柄位姿（手柄模式，二选一）
├── imu_calibration.json                 # IMU 标定 sidecar
└── camera_params_{rgb,tracking,ctrl}.json  # 相机内参/外参
```

**文件大小参考（约 10 秒）**：rgb.mp4 ~10MB · tracking/ctrl.mp4 ~5MB · audio.m4a ~120KB · accel/gyro.csv ~300KB · head_pose.csv ~50KB · hand_tracking.csv ~100KB。

## 2.2 文件格式

**媒体 + 元数据** — 视频/音频为 **fragmented MP4**（`FMP4Writer`，崩溃安全）。逐帧时间戳/曝光/增益不进媒体文件，写入 `*_metainfo.csv`（首行表头，逐 sample 追加）：

| 文件 | schema |
|------|--------|
| `rgb/tracking/ctrl_metainfo.csv` | `frame_index,frame_id,pts_us,exposure_start_utc_ns,exposure_duration_ns,gain,mid_exposure_utc_ns` |
| `audio_metainfo.csv` | `packet_index,pts_us,capture_utc_ns` |

- `pts_us`：零基微秒，与 mp4 sample 呈现时间一致。
- `*_utc_ns`：绝对 UTC（见 [2.3 时间戳体系](#23-时间戳体系)）。`mid_exposure_utc_ns` = 曝光中心，推荐作图像采样时间基准。

**IMU** — `accel.csv` / `gyro.csv`：`timestamp_ns,x,y,z`（加速度计 m/s²，陀螺仪 rad/s，timestamp 为 UTC ns，见 2.3）。

**位姿**：
- `head_pose.csv`：`timestamp_ns,pos_x,pos_y,pos_z,quat_x,quat_y,quat_z,quat_w`（Body 坐标系在 World 中的位姿）。
- `hand_tracking.csv`：`frame_number,timestamp,left_active,right_active,{left|right}_joint{i}_{id,name,radius,pos_x,pos_y,pos_z,orientation_x,y,z,w}`（每只手 26 关节，每关节 10 字段；Root Space，米）。手势模式产物。
- `controller_poses.csv`：`frame_number,timestamp_ns,left_active,left_px..qw,right_active,right_px..qw`。手柄模式产物。

**IMU 标定 `imu_calibration.json`** — 录制开始时写入，记录设备 IMU 标定参数（原始 accel/gyro 见 CSV，相机外参见 `camera_params_*.json`）。严格 JSON。

| 字段 | 含义 | 单位 |
|------|------|------|
| `device_uid` | 设备唯一 ID | — |
| `imu.imu_id` / `imu.is_primary` | IMU 编号 / 是否为主 IMU | — / bool |
| `imu.bias.accelerometer_mps2` | 加速度计三轴零偏（恒定偏移量） | m/s² |
| `imu.bias.gyroscope_rads` | 陀螺仪三轴零偏 | rad/s |
| `imu.scale_factor.accelerometer` / `.gyroscope` | 三轴比例因子（相对 1.0 的偏差） | 无量纲 |
| `imu.nonorthogonality.accelerometer` / `.gyroscope` | 三轴非正交性（轴间串扰） | 无量纲 |
| `imu.time_alignment_s.imu_to_pose` | IMU↔pose（tracking）时间偏移，来自标定文件 `<Stateinit delta>`（秒级相对偏移，两端时间戳同域时直接相加） | s |
| `imu.time_alignment_s.cameras.<cam_name>` | 每个相机各自的 IMU↔相机时间偏移（`trackingA`/`trackingB`/`ctrl-trackingA`/`ctrl-trackingB`/`rgb-left`/`rgb-right`） | s |
| `imu.time_alignment_s.accel` | 加速度计相对陀螺仪的额外时间偏移，来自 `<Stateinit accelDelta>` | s |
| `noise.accel_noise_std_mps2` | 加速度计测量噪声标准差 σ_a | m/s² |
| `noise.gyro_noise_std_rads` | 陀螺仪测量噪声标准差 σ_g | rad/s |
| `noise.accel_bias_std_mps2` | 加速度计零偏随机游走噪声 σ_ba（经验默认值，标定文件未提供） | m/s² |
| `noise.gyro_bias_std_rads` | 陀螺仪零偏随机游走噪声 σ_bg（经验默认值，标定文件未提供） | rad/s |

**相机参数 `camera_params_*.json`** — 首次有效帧时保存，静态参数：

```json
{
  "group": "rgb",
  "cameras": [
    { "eye": "left", "width": 2328, "height": 1748,
      "intrinsics": {
        "focalX": 1374.56, "focalY": 1369.12,
        "centerX": 1159.37, "centerY": 878.45,
        "radialDistortion": [0.0471,-0.0213,0.0038,-0.0002, 0,0,0,0]
      },
      "extrinsics": {
        "position": [0.032,-0.015,0.002],   // 相机光心在 Body 坐标系 (m)
        "rotation": [0.0,0.0,0.0,1.0]        // Camera→Body 四元数 [x,y,z,w]
      }
    },
    { "eye": "right", /* ... */ }
  ]
}
```

内参为 OpenCV 标准约定（可直接作 K 矩阵），畸变为 Kannala-Brandt 鱼眼（前 4 系数 k1..k4）。外参为 OpenXR 约定（Camera→Body, Tbc），转 OpenCV 见 [3.3](#33-外参--openxr--opencv-转换)。

## 2.3 时间戳体系

**所有数据流在同一条绝对 UTC 时间线上，互相一致。** 录制开始时一次性采样 `BOOTTIME→REALTIME` 偏移（`offset = CLOCK_REALTIME - CLOCK_BOOTTIME`），每个流的原始 `CLOCK_BOOTTIME` 时间戳统一加该偏移写入：

- **视频**（`*_metainfo.csv` 的 `exposure_start_utc_ns` / `mid_exposure_utc_ns`）：原始来源是相机帧 `start_of_exposure_ts`（kernel `CLOCK_BOOTTIME`），`+ offset → UTC`。
- **音频**（`capture_utc_ns`）：由 `CLOCK_BOOTTIME` 基 + 编解码 PTS 重构，`+ offset → UTC`。
- **IMU**（`accel/gyro.csv` `timestamp_ns`）：Android `ASensorEvent.timestamp`（`CLOCK_BOOTTIME`），`+ offset → UTC`。
- **位姿**（`head_pose.csv` 等 `timestamp_ns`）：`CLOCK_BOOTTIME`，`+ offset → UTC`。

**对齐到 RGB**：pose / 手势 / 手柄在 RGB 帧回调里，于 RGB **mid-exposure** 时刻直接查 OpenXR 历史位姿（`xrLocateSpace(camXrTime)`），并重打戳为 RGB mid-exposure —— 实测残差亚微秒。IMU 是独立高速流（~1kHz，逐样本 UTC），后处理按 `mid_exposure_utc_ns` 插值对齐。tracking/ctrl 是 60fps 独立流（RGB 30fps），按各自 mid-exposure 做时间戳匹配。

```bash
# 量化逐帧对齐误差（pose/hand 最近邻、IMU 残差、跨相机同步）
python3 dataset_analysis/analyze_alignment.py <dataset_dir>
```

## 2.4 录制流程

```
START_RECORDING (按键/Intent)
  └─ DatasetRecorder.start()
       ├─ 创建 dataset/<YYYYMMDD_HHMMSS>/
       ├─ AudioEncoder → audio.m4a ; ImuPoseCollector → accel/gyro.csv
       ├─ 写 imu_calibration.json（调用 qxr 的 IMU 标定 getter）
       └─ head_pose writer 线程 → head_pose.csv
  └─ 手势模式: RawDateSave → hand_tracking.csv ；手柄模式: ControllerPoseSaver → controller_poses.csv

每帧渲染循环:
  ├─ saveHeadPose(devicePose)           # xrLocateSpace(viewSpace) → boottime → head_pose.csv
  ├─ 相机回调 → 编码器 → rgb/tracking/ctrl.mp4
  ├─ 相机回调 → saveAlignedSensorData()  # pose/手势按 RGB mid-exposure 对齐写入
  ├─ 相机回调 → saveCameraParams()       # 首次有效帧 → camera_params_*.json
  └─ ControllerPoseSaver/RawDateSave.SaveFrame()

STOP_RECORDING → flush 各编码器/采集器 → stopEncoder()（异步）
```

核心组件（详见源码）：`DatasetRecorder`（中心协调）、`ImuPoseCollector`（IMU，sensor 线程 + writer 线程）、`AudioEncoder`（AAudio→AMediaCodec AAC）、`ControllerPoseSaver` / `RawDateSave`（异步线程写 CSV）。

---

# 3. 相机与坐标系

## 3.1 坐标系约定

相机外参统一使用 **Body（IMU/设备机体）坐标系**作为参考。OpenXR 的 `xrViewSpace` 在本设备上即代表 Body/head 系。

```
OpenXR / Body 坐标系（外参使用）：X 右、Y 上、Z 后（指向用户身后）

        Y (上)
        ↑
        |
        o————► X (右)
         \
          ↘ Z (后)

OpenCV 标准相机坐标系（后处理）：X 右、Y 下、Z 前（拍摄方向）

          Z (前)
          ↗
         /
        o————► X (右)
        |
        ↓
        Y (下)
```
- Pitch 绕 X、Yaw 绕 Y、Roll 绕 Z。
- 位姿单位：米；四元数顺序 `(x, y, z, w)`。

| 库/结构 | 四元数顺序 |
|---------|-----------|
| `XrQuaternionf` / `SxrPose.rotation` | (x, y, z, w) |
| `glm::quat` | (w, x, y, z) |

```cpp
// SxrPose(x,y,z,w) → GLM(w,x,y,z)
glm::quat q_glm(rot[3], rot[0], rot[1], rot[2]);
```

变换矩阵 `T_AB` = 从 B 系到 A 系的 4×4 齐次变换（`R_AB` 旋转 + `t_AB` 平移）。组合：`q_ac = q_ab * q_bc`，`p_ac = rotate(q_ab, p_bc) + p_ab`。

## 3.2 内参

OpenCV 标准约定，直接作 K 矩阵；畸变 Kannala-Brandt 鱼眼（前 4 系数）。

```python
import numpy as np, cv2
K = np.array([[focalX,0,centerX],[0,focalY,centerY],[0,0,1]], dtype=np.float64)
D = np.array(radialDistortion[:4], dtype=np.float64).reshape(1,4)
map1,map2 = cv2.fisheye.initUndistortRectifyMap(K,D,np.eye(3),K,(w,h),cv2.CV_32FC1)
undistorted = cv2.remap(img,map1,map2,cv2.INTER_LINEAR)
```

## 3.3 外参 + OpenXR → OpenCV 转换

外参表示 **Camera → Body**（Tbc）：`X_body = R @ X_camera + t`。`position`=相机光心在 Body 系的位置(m)，`rotation`=Camera→Body 四元数。

内参已兼容 OpenCV，**仅外参需转换**（H 矩阵翻转 Y/Z 轴）：

```python
import json, numpy as np
H = np.array([[0,1,0],[-1,0,0],[0,0,1]], dtype=np.float64)

def quat_to_rotmat(q):  # q = [x,y,z,w]
    x,y,z,w = q
    return np.array([
        [1-2*(y*y+z*z), 2*(x*y-w*z),   2*(x*z+w*y)],
        [2*(x*y+w*z),   1-2*(x*x+z*z), 2*(y*z-w*x)],
        [2*(x*z-w*y),   2*(y*z+w*x),   1-2*(x*x+y*y)]])

def load_camera_params(json_path):
    with open(json_path) as f: data = json.load(f)
    out = {}
    for cam in data['cameras']:
        intr, extr = cam['intrinsics'], cam['extrinsics']
        K = np.array([[intr['focalX'],0,intr['centerX']],
                      [0,intr['focalY'],intr['centerY']],
                      [0,0,1]], dtype=np.float64)
        D = np.array(intr['radialDistortion'][:4], dtype=np.float64).reshape(1,4)
        R = H @ quat_to_rotmat(extr['rotation']) @ H.T      # OpenXR → OpenCV
        t = H @ np.array(extr['position'], dtype=np.float64)
        out[f"{data['group']}-{cam['eye']}"] = {'width':cam['width'],'height':cam['height'],'K':K,'D':D,'R':R,'t':t}
    return out
```

**相机对相对变换**（A→B，均在 Body 系）：`R_BA = R_Bᵀ @ R_A`，`t_BA = R_Bᵀ @ (t_A - t_B)`。

## 3.4 位姿变换（World → Body → Camera）

计算相机在 World 中的位姿，变换链统一为 **World → Body → Camera**（`xrViewSpace` = Body/head 系）：

```
World (W) ──T_WB──► Body (B) ──T_BC──► Camera (C)      T_WC = T_WB × T_BC
```

```cpp
// 1. Body 在 World 中的位姿 T_WB（OpenXR view space = body/head frame）
xrLocateSpace(xrViewSpace, xrRootSpace, timestamp, &location);
glm::quat q_wb(location.pose.orientation.w, location.pose.orientation.x,
               location.pose.orientation.y, location.pose.orientation.z);
glm::vec3 p_wb(location.pose.position.x, location.pose.position.y, location.pose.position.z);

// 2. 外参 T_BC：Camera 在 Body 中的位姿（SxrPose.rotation 为 x,y,z,w）
glm::quat q_bc(frameInfo.rotation[3], frameInfo.rotation[0],
               frameInfo.rotation[1], frameInfo.rotation[2]);
glm::vec3  p_bc(frameInfo.position[0], frameInfo.position[1], frameInfo.position[2]);

// 3. 组合 T_WC
glm::quat q_wc = q_wb * q_bc;
glm::vec3  p_wc = glm::rotate(q_wb, p_bc) + p_wb;
```

灰度相机（4 个：GRAY_LEFT/RIGHT/LEFT_UP/RIGHT_UP）的外参同样来自 `SXR::FrameInfo.position/rotation`（Body 系）。

## 3.5 验证工具

```bash
python3 camera_analysis.py <dataset_dir>   # 输出 camera_analysis_report.html
```

分析：6 相机两两重投影误差、立体矫正极线误差、外参精度（E_sim）、RGB 双目基线。

---

# 4. 编码管线

## 4.1 架构

硬件加速 **Surface 模式**编码（零拷贝）。RGB 把左右眼经 FBO 拼成 **SBS** 编码为单个 mp4；灰度相机 YUV→灰度着色器后编码。每个相机组用**独立 EGL 上下文**（无锁竞争）。

```
相机回调 ──► handleRGBFrame: AHardwareBuffer×2 → GL_TEXTURE_EXTERNAL_OES
            → SBS Stitch FBO(左眼viewport=0,右眼=W) → encoder surface
            → eglPresentationTimeANDROID → MediaCodec → rgb.mp4 (2W×H SBS)
          └► handleCVFrame: AHardwareBuffer→GL Texture → YUV→灰度着色器
            → encoder surface → MediaCodec → tracking.mp4 / ctrl.mp4
```

时间戳单位：相机帧 ns、MediaCodec µs、OpenXR XrTime ns（`us = ns/1000`）。`CameraEncoder::submitFrameMeta()` 把逐帧元数据写入 `*_metainfo.csv`。

## 4.2 fragmented MP4 + 时间戳

`FMP4Writer`（纯 POSIX 文件 I/O，`CameraEncoder`/`AudioEncoder` 共用）：`open()`→`setVideoTrack/setAudioTrack`→`start()` 写出 `ftyp + moov`（前置），之后每次 `writeSample(data,size,ptsUs,isSync)` 追加一个 `styp+moof+mdat` fragment（每 sample 一个）。

```
ftyp + moov（前置：track 配置 + SPS/PPS/VPS 或 AAC csd）
styp + moof + mdat  ← sample 0（首帧 I 帧，PTS=0）
styp + moof + mdat  ← sample 1 ...
```

- **崩溃安全**：`moov` 前置 + 每 sample 独立 fragment，截断/强杀仍可播到最后一个完整 fragment。
- 视频 PTS **零基**（首帧=0）；HEVC `max-bframes=0`（仅 I/P 帧），解码序==显示序，PTS 严格单调。

Surface 模式 PTS 设置（swapBuffers 前调 `eglPresentationTimeANDROID`，编码器内部转 µs 作 PTS）。

## 4.3 编码格式

| 流 | 编码 | 分辨率 | 帧率 | 码率 |
|----|------|--------|------|------|
| RGB (SBS) | H.265 | 2W×H | 30（`persist.sxr.cam.rgb.fps` 可设 60） | 8 Mbps |
| Tracking / Ctrl | H.265 | W×H | 60 | 4 Mbps |
| Audio | AAC-LC | — | 44.1kHz mono | 96 kbps |

H.264/H.265 可切换（见 `CameraEncoder` 构造，默认 H.265）。关键技术点（EGL 上下文共享、AHardwareBuffer→GL Texture、VAO 跨上下文、纹理坐标翻转、灰度着色器、SBS 渲染管线）实现见 `app/src/main/cpp/` 源码。

---

# 5. 开发参考

## 5.1 SxrCameraApi（动态加载）

通过 `dlopen("libsxr_camera_client.so")` 加载，函数指针表：

```cpp
struct SxrCameraApi {
    void* libHandle;
    SxrCameraCreateFunc         create;
    SxrCameraDestroyFunc        destroy;
    SxrCameraOpenGroupFunc      open_group;
    SxrCameraCloseGroupFunc     close_group;
    SxrCameraIsGroupOpenFunc    is_group_open;
    SxrCameraGetGroupInfoFunc   get_group_info;
    SxrCameraGetImuCalibrationFunc get_imu_calibration;  // 可选符号，缺失返回 -1
};
int  sxr_camera_api_init(SxrCameraApi* api, const char* libPath);  // NULL 用默认 lib
bool sxr_camera_api_is_valid(const SxrCameraApi* api);
```

## 5.2 核心结构

```cpp
enum class CameraGroup : uint8_t { TRACKING=0, CTRL, RGB, DEPTH };

struct FrameInfo {
    uint32_t frameId; uint64_t timestamp;   // start_of_exposure_ts (CLOCK_BOOTTIME, ns)
    uint32_t exposure, gain;
    uint32_t width, height, stride, format, cropX, cropY;
    float focalX, focalY, centerX, centerY;            // OpenCV 标准内参
    float radialDistortion[8];                          // KB 鱼眼（前4非零）
    float position[3];   // 相机光心在 Body 系位置 (m)
    float rotation[4];   // Camera→Body 四元数 (x,y,z,w)
};
struct FrameData { CameraGroup group; FrameInfo frames[2]; /* [0]=left,[1]=right */
                   uint8_t hwBufferCount; AHardwareBuffer* hwBuffer[2]; };
typedef void (*FrameCallback)(void* userData, const FrameData* data);
```

## 5.3 手势追踪

依赖 OpenXR `XR_EXT_hand_tracking` 等扩展（API layer `XR_APILAYER_QCOM_handtracking`）。每只手 26 关节（PALM/WRIST/THUMB_*…/LITTLE_TIP），数据在 **Root Space**（全局 SLAM 坐标系，应用启动时尝试 `xrCreateRootSpaceQCOM`，失败则回退 `LOCAL` space）。

核心组件：`HandTrackerLogic`（左右手 tracker + 关节定位）、`Input`（OpenXR action 输入）、`RawDateSave`（异步线程写 `hand_tracking.csv`，Session 制）。仅手势模式（`persist.xr.usecontroller=false`）启用；手柄模式用 `ControllerPoseSaver` 写 `controller_poses.csv`。

## 5.4 叠加投影到 RGB

手势骨骼（`persist.xr.project_hand=1`，手势模式）和手柄坐标系（`persist.xr.project_controller=1`，手柄模式）均可叠加投影到编码视频。均不影响头戴预览，需重启应用，互斥（择一模式启用）。

**手势骨骼链路**：

```
HandJoint(RootSpace) → World → Camera → KB 鱼眼投影 → 2D 像素 → 绕中心顺时针 90° → 最终像素
```

```cpp
// World→Camera：外参 + 头部位姿四元数链（body→world + camera→body）
R_WC = conj(extQuat) * conj(headQuat);
cam   = quatRotate(R_WC, joint - wcPos);        // wcPos = devicePos + quatRotate(deviceQuat, extPos)
// KB 投影后 90° 顺时针旋转（匹配 Python 参考实现）
u_rot = centerX + (v - centerY);
v_rot = centerY - (u - centerX);
```

**手柄坐标系链路**：

```
ControllerPose(RootSpace) → KB 鱼眼原点投影 → 定长像素轴线段（25px）
    红 X 右 → 、绿 Y 上 ↑ 、蓝 Z 右下 ↘（屏幕像素空间）
```

手柄原点经 World→Camera→KB 鱼眼投影到 2D 像素坐标，轴臂为 25px 定长线段（不随距离缩放），直接画在编码帧上。投影逻辑复用 `HandOverlayRenderer` 的 KB 投影 + 90° CW 旋转，确保与手势投影坐标一致。

---

# 附录

## A. 设备硬件功能

集成的硬件测试能力（`svr_plugin_android_api.aar`）：

- **LED**：`DeviceUtils.flashLed(type)` / `blinkLed(type,on,off)`（1=红,2=绿,3=蓝）。
- **音频录制**：`MediaRecorder`（MIC → AAC/M4A，44.1kHz/96kbps）。
- **电池**：`BroadcastReceiver`(ACTION_BATTERY_CHANGED) + `BatteryManager`（level/voltage/temperature/capacity/current 等）。
- **系统事件**：`SystemEventUtils.Listener`（onStartHome / onRecenter / onKeyEvent 等）。
- **AndroidInterface**：`AndroidInterface.getInstance().init(...)` 初始化。

## B. 截图与录像

- 触发：`VOLUME_UP` 截图；`Right B`/`DPAD_CENTER` 切换录制；也可 `am broadcast`（见 [1. ADB](#adb-远程命令)）。
- 截图保存到 `.../files/images/`：`rgb_*.png`（RGB 左右眼拼接）、`tracking_*.png`、`ctrl_*.png`。
- 录制参数见 [4.3](#43-编码格式)。语音提示（TTS）+ Intent 远程控制（含 WiFi ADB 跨设备）。

## C. MP4 分析命令

```bash
ffprobe -v quiet -show_format -show_streams rgb.mp4                      # 结构（fragmented，单媒体 track）
ffprobe -v quiet -select_streams 0 -count_packets -show_entries stream=nb_read_packets -of csv=p=0 rgb.mp4  # 帧数
ffprobe -v quiet -select_streams v -show_entries packet=pts_time -of csv=p=0 rgb.mp4 | head                # PTS（首帧 0.0）
# 用 *_metainfo.csv 行数与 mp4 packet 数做一致性校验
```

## D. 注意事项

- **权限**：`CAMERA` / `RECORD_AUDIO` / `WRITE_EXTERNAL_STORAGE`。卸载重装会清除权限与数据集。
- **存储**：长时间录制产生大量数据，注意空间。
- **OpenXR runtime**：需 `com.qualcomm.qti.openxrruntime` / `spaces.services`；手势/手柄模式切换、骨骼投影开关需重启应用。
- **停止录制**：`stopEncoder` 异步，再次录制前确认其完成（`stopInProgress` 标志）。
- **手势/手柄投影**：仅对应模式 + 对应属性 `=1` 生效，只影响编码视频，头戴预览不画。

## 相关文件

```
app/src/main/cpp/
├── main.cpp                    # 应用主逻辑、相机回调、录制编排、对齐
├── DatasetRecorder.{h,cpp}     # 数据集录制协调器
├── CameraEncoder.{h,cpp}       # MediaCodec 视频编码（Surface 模式）
├── AudioEncoder.{h,cpp}        # AAC 音频编码
├── FMP4Writer.{h,cpp}          # fragmented MP4 写入器
├── ImuPoseCollector.{h,cpp}    # IMU 采集
├── HandOverlayRenderer.{h,cpp} # 手势骨骼 + 手柄坐标系投影
├── sxr_camera.h / sxr_common.h # 相机 API（动态加载）+ 共享结构
└── camera_analysis.py          # 内外参验证（生成 HTML 报告）
dataset_analysis/
├── analyze_alignment.py        # 时间戳对齐量化
└── analyze_all_timestamps.py   # 时间戳分析
```
