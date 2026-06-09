# SXR EGO SDK 使用说明

## 快速参考：系统属性与命令

### 系统属性

| 属性 | 值 | 说明 | 生效方式 |
|------|------|------|----------|
| `persist.xr.usecontroller` | `true` / `false` | 手柄模式 / 手势模式 | 重启应用 |
| `persist.xr.project_hand` | `1` / `0` | 开启 / 关闭手势骨骼投影（仅影响编码录制视频，不影响头戴预览） | 重启应用 |

```bash
# 查看当前设置
adb shell getprop persist.xr.usecontroller
adb shell getprop persist.xr.project_hand

# 设置手势模式
adb shell setprop persist.xr.usecontroller false

# 开启手势骨骼投影（需手势模式下生效）
adb shell setprop persist.xr.project_hand 1

# 关闭手势骨骼投影
adb shell setprop persist.xr.project_hand 0
```

### ADB 远程控制命令

```bash
# 截图（所有相机）
adb shell am broadcast -a com.ssnwt.helloxr.SAVE_IMAGE

# 数据集录制
adb shell am broadcast -a com.ssnwt.helloxr.START_RECORDING   # 开始录制
adb shell am broadcast -a com.ssnwt.helloxr.STOP_RECORDING    # 停止录制
```

### 手柄按键

| 按键 | 功能 |
|------|------|
| VOLUME_UP | 截图（所有相机） |
| Right B / DPAD_CENTER | 切换录制开始/停止 |

### 数据路径

```bash
# 数据集目录
/sdcard/Android/data/com.ssnwt.helloxr/files/dataset/<YYYYMMDD_HHMMSS>/

# 截图目录
/sdcard/Android/data/com.ssnwt.helloxr/files/images/

# 拉取数据到本地
adb pull /sdcard/Android/data/com.ssnwt.helloxr/files/dataset/ ./dataset/
```

### 应用管理

```bash
# 重启应用
adb shell am force-stop com.ssnwt.helloxr && sleep 1 && adb shell am start -n com.ssnwt.helloxr/com.ssnwt.helloxr.VrNativeActivity

# 查看日志
adb logcat | grep "HelloXr"
```

---

## 概述

SXR Camera API 是一套用于访问 XR 设备摄像头的 Native API，通过动态加载 `libsxr_camera_client.so` 库使用。支持三种摄像头组：
- **TRACKING**：灰度跟踪摄像头（左右眼）
- **CTRL**：灰度控制摄像头（左右眼）
- **RGB**：RGB 彩色摄像头（左右眼）

## 核心数据结构

### CameraGroup 枚举

```cpp
enum class CameraGroup : uint8_t {
    TRACKING = 0,   // 灰度跟踪摄像头（GRAY_LEFT + GRAY_RIGHT）
    CTRL,           // 灰度控制摄像头（GRAY_LEFT_UP + GRAY_RIGHT_UP）
    RGB,            // RGB 彩色摄像头（RGB_LEFT + RGB_RIGHT）
};
```

### FrameInfo 结构体

每帧的元数据信息：

```cpp
struct FrameInfo {
    // 帧元数据
    uint32_t frameId;           // 帧序号
    uint64_t timestamp;         // 曝光开始时间戳（boottime）
    uint32_t exposure;          // 曝光时间
    uint32_t gain;              // 增益值

    // 帧尺寸（裁剪后）
    uint32_t width;             // 图像宽度
    uint32_t height;            // 图像高度
    uint32_t stride;            // 行步长
    uint32_t format;            // 像素格式

    // 裁剪区域（在 HardwareBuffer 中的位置）
    uint32_t cropX;
    uint32_t cropY;

    // 内参（静态参数，每帧附带便于使用）
    float focalX, focalY;       // 焦距
    float centerX, centerY;     // 主点
    float radialDistortion[8];  // Kannala-Brandt 鱼眼畸变参数

    // 外参（静态参数，相机相对设备的位姿）
    float position[3];          // 相机位置
    float rotation[4];          // 相机旋转（四元数：x, y, z, w）
};
```

### FrameData 结构体

帧回调函数接收的数据：

```cpp
struct FrameData {
    CameraGroup group;          // 摄像头组类型

    FrameInfo frames[2];        // [0]=左眼, [1]=右眼

    uint8_t hwBufferCount;      // HardwareBuffer 数量：1=灰度, 2=RGB
    AHardwareBuffer* hwBuffer[2];
    // 灰度摄像头：hwBuffer[0] 包含左右眼拼接数据
    // RGB 摄像头：hwBuffer[0]=左眼, hwBuffer[1]=右眼
};
```

### FrameCallback 回调类型

```cpp
typedef void (*FrameCallback)(void* userData, const FrameData* data);
```

## API 函数

### SxrCameraApi 结构体

动态加载的函数指针表：

```cpp
typedef struct SxrCameraApi {
    void* libHandle;                    // 动态库句柄

    // 函数指针
    SxrCameraCreateFunc create;         // 创建上下文
    SxrCameraDestroyFunc destroy;       // 销毁上下文
    SxrCameraOpenGroupFunc open_group;  // 打开摄像头组
    SxrCameraCloseGroupFunc close_group;// 关闭摄像头组
    SxrCameraIsGroupOpenFunc is_group_open;     // 检查组是否打开
    SxrCameraGetGroupInfoFunc get_group_info;   // 获取组信息
} SxrCameraApi;
```

### 初始化/反初始化

```cpp
// 初始化 API（加载动态库）
int sxr_camera_api_init(SxrCameraApi* api, const char* libPath);
// libPath 为 NULL 时使用默认路径 "libsxr_camera_client.so"

// 反初始化 API（卸载动态库）
void sxr_camera_api_deinit(SxrCameraApi* api);

// 检查 API 是否有效
bool sxr_camera_api_is_valid(const SxrCameraApi* api);
```

### 上下文管理

```cpp
// 创建摄像头上下文
SxrCameraContext* sxr_camera_create(SxrCameraApi* api, JavaVM* vm, jobject activity);

// 销毁摄像头上下文
void sxr_camera_destroy(SxrCameraApi* api, SxrCameraContext* ctx);
```

### 摄像头组操作

```cpp
// 打开摄像头组（开始接收帧回调）
int sxr_camera_open_group(SxrCameraApi* api,
                          SxrCameraContext* ctx,
                          SXR::CameraGroup group,
                          SXR::FrameCallback callback,
                          void* userData);

// 关闭摄像头组（停止帧回调）
int sxr_camera_close_group(SxrCameraApi* api, SxrCameraContext* ctx, SXR::CameraGroup group);

// 检查摄像头组是否已打开
bool sxr_camera_is_group_open(SxrCameraApi* api, SxrCameraContext* ctx, SXR::CameraGroup group);

// 获取摄像头组信息
int sxr_camera_get_group_info(SxrCameraApi* api,
                              SxrCameraContext* ctx,
                              SXR::CameraGroup group,
                              uint32_t* maxWidth,
                              uint32_t* maxHeight,
                              uint32_t* format);
```

## 使用示例

### 完整示例代码

```cpp
#include "sxr_camera.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "CameraDemo", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "CameraDemo", __VA_ARGS__)

// 用户数据结构
struct MyCameraData {
    int frameCount;
    // ... 其他用户数据 ...
};

// 帧回调函数
void onCameraFrame(void* userData, const SXR::FrameData* data) {
    auto* myData = static_cast<MyCameraData*>(userData);
    myData->frameCount++;

    const char* groupName;
    switch (data->group) {
        case SXR::CameraGroup::TRACKING: groupName = "TRACKING"; break;
        case SXR::CameraGroup::CTRL:     groupName = "CTRL";     break;
        case SXR::CameraGroup::RGB:      groupName = "RGB";      break;
        default:                         groupName = "UNKNOWN";  break;
    }

    // 访问帧信息
    const SXR::FrameInfo* left = &data->frames[0];
    const SXR::FrameInfo* right = &data->frames[1];

    LOGI("Frame %d: group=%s, left=%dx%d, right=%dx%d, timestamp=%lu",
         myData->frameCount, groupName,
         left->width, left->height,
         right->width, right->height,
         left->timestamp);

    // 访问 AHardwareBuffer（仅在回调期间有效）
    if (data->hwBuffer[0]) {
        // 灰度摄像头：hwBuffer[0] 包含左右眼拼接数据
        // RGB 摄像头：hwBuffer[0]=左眼, hwBuffer[1]=右眼

        // 获取 buffer 描述信息
        AHardwareBuffer_Desc desc;
        AHardwareBuffer_describe(data->hwBuffer[0], &desc);
        LOGI("  Buffer: %ux%u, format=%u, stride=%u",
             desc.width, desc.height, desc.format, desc.stride);

        // 使用 buffer（上传到纹理、编码等）
        // 注意：buffer 只在回调期间有效，如需长期使用需要拷贝或增加引用
    }

    // 访问相机外参
    LOGI("  Left camera position: (%.3f, %.3f, %.3f)",
         left->position[0], left->position[1], left->position[2]);
}

class CameraManager {
private:
    SxrCameraApi mApi;
    SxrCameraContext* mContext = nullptr;
    MyCameraData mUserData;

public:
    bool init(JavaVM* vm, jobject activity) {
        // 1. 初始化 API
        if (sxr_camera_api_init(&mApi, NULL) != 0) {
            LOGE("Failed to load sxr_camera library");
            return false;
        }

        // 2. 创建上下文
        mContext = sxr_camera_create(&mApi, vm, activity);
        if (!mContext) {
            LOGE("Failed to create camera context");
            sxr_camera_api_deinit(&mApi);
            return false;
        }

        // 3. 打开摄像头组
        mUserData.frameCount = 0;

        // 打开灰度跟踪摄像头
        if (sxr_camera_open_group(&mApi, mContext, SXR::CameraGroup::TRACKING,
                                   onCameraFrame, &mUserData) != 0) {
            LOGE("Failed to open TRACKING group");
        }

        // 打开灰度控制摄像头
        if (sxr_camera_open_group(&mApi, mContext, SXR::CameraGroup::CTRL,
                                   onCameraFrame, &mUserData) != 0) {
            LOGE("Failed to open CTRL group");
        }

        // 打开 RGB 摄像头
        if (sxr_camera_open_group(&mApi, mContext, SXR::CameraGroup::RGB,
                                   onCameraFrame, &mUserData) != 0) {
            LOGE("Failed to open RGB group");
        }

        return true;
    }

    void cleanup() {
        if (mContext) {
            // 关闭所有摄像头组
            sxr_camera_close_group(&mApi, mContext, SXR::CameraGroup::TRACKING);
            sxr_camera_close_group(&mApi, mContext, SXR::CameraGroup::CTRL);
            sxr_camera_close_group(&mApi, mContext, SXR::CameraGroup::RGB);

            // 销毁上下文
            sxr_camera_destroy(&mApi, mContext);
            mContext = nullptr;
        }

        // 反初始化 API
        sxr_camera_api_deinit(&mApi);
    }
};
```

### 线程安全说明

```
┌─────────────────────────────────────────────────────────────────┐
│  线程        │  职责                    │  调用的 API           │
├─────────────────────────────────────────────────────────────────┤
│  主线程      │  初始化、销毁             │  api_init, create    │
│              │                          │  destroy, api_deinit │
├─────────────────────────────────────────────────────────────────┤
│  相机线程1   │  TRACKING 组帧回调       │  (回调中处理帧)       │
├─────────────────────────────────────────────────────────────────┤
│  相机线程2   │  CTRL 组帧回调           │  (回调中处理帧)       │
├─────────────────────────────────────────────────────────────────┤
│  相机线程3   │  RGB 组帧回调            │  (回调中处理帧)       │
└─────────────────────────────────────────────────────────────────┘

注意：
- 每个摄像头组的回调在独立线程中执行
- 回调函数必须线程安全
- AHardwareBuffer 只在回调期间有效
```

### AHardwareBuffer 使用注意事项

1. **生命周期**：`AHardwareBuffer` 只在帧回调期间有效，回调返回后 buffer 可能被复用
2. **不要释放**：buffer 由相机服务管理，不要调用 `AHardwareBuffer_release()`
3. **异步处理**：如需异步处理，需要：
   - 使用 `AHardwareBuffer_acquire()` 增加引用计数
   - 处理完后调用 `AHardwareBuffer_release()`
4. **OpenGL 绑定**：使用 `eglGetNativeClientBufferANDROID()` 和 `glEGLImageTargetTexture2DOES()` 绑定到纹理

---

# 相机姿态变换原理

## XR 坐标系约定

```
        Y (up)
        |
        |
        |_______ X (right)
       /
      /
     Z (backward)
```

- **Pitch** (绕X轴)：抬头/低头
- **Yaw** (绕Y轴)：向左/向右转
- **Roll** (绕Z轴)：侧向翻滚

## 四元数顺序说明

不同库的四元数存储顺序：

| 库/结构 | 顺序 | 示例 |
|---------|------|------|
| XrQuaternionf | (x, y, z, w) | `q.x, q.y, q.z, q.w` |
| SxrPose.rotation | (x, y, z, w) | `rot[0], rot[1], rot[2], rot[3]` |
| GLM glm::quat | (w, x, y, z) | `q.w, q.x, q.y, q.z` |

**转换示例**：
```cpp
// XrPosef → GLM
glm::quat q_glm(xrPose.orientation.w,   // w 在前
                xrPose.orientation.x,   // x 在后
                xrPose.orientation.y,
                xrPose.orientation.z);

// SxrPose → GLM
glm::quat q_glm(sxrPose.rotation[3],    // w = rot[3]
                sxrPose.rotation[0],    // x = rot[0]
                sxrPose.rotation[1],
                sxrPose.rotation[2]);
```

## 变换矩阵定义

使用4x4齐次变换矩阵表示位姿：T_AB 表示从坐标系B到坐标系A的变换

```
T_AB = | R_AB  t_AB |    其中:
       |  0     1   |    R_AB = 3x3旋转矩阵
                        t_AB = 3x1平移向量
```


---

## 变换组合公式

给定两个坐标系 A 和 B，已知 B 在 A 中的位姿 (q_ab, p_ab)，以及 C 在 B 中的位姿 (q_bc, p_bc)，计算 C 在 A 中的位姿 (q_ac, p_ac)：

```cpp
// 旋转组合
glm::quat q_ac = q_ab * q_bc;

// 平移组合
glm::vec3 p_ac = glm::rotate(q_ab, p_bc) + p_ab;
```

---

### 变换公式

**目标**：计算 Sensor 在 World 坐标系中的位姿 **T_WS**

**变换链**：`World (W) ──T_WV──► View (V) ──T_VS──► Sensor (S)`

**公式**：`T_WS = T_WV × T_VS`

### 代码实现

```cpp
// 1. 获取 View 在 World 中的位姿 (T_WV)
xrLocateSpace(xrViewSpace, xrRootSpace, timestamp, &location);
glm::quat q_wv(location.pose.orientation.w,
               location.pose.orientation.x,
               location.pose.orientation.y,
               location.pose.orientation.z);
glm::vec3 p_wv(location.pose.position.x,
               location.pose.position.y,
               location.pose.position.z);

// 2. 获取外参 - Sensor 在 View 中的位姿 (T_VS)
glm::quat q_vs(extrinsic.orientation.w,
               extrinsic.orientation.x,
               extrinsic.orientation.y,
               extrinsic.orientation.z);
glm::vec3 p_vs(extrinsic.position.x,
               extrinsic.position.y,
               extrinsic.position.z);

// 3. 组合变换：计算 Sensor 在 World 中的位姿 (T_WS)
glm::quat q_ws = q_wv * q_vs;
glm::vec3 p_ws = glm::rotate(q_wv, p_vs) + p_wv;
```

### 坐标系关系图

```
      World坐标系 (W)
           │
           │ T_WV (xrLocateSpace)
           ▼
      View坐标系 (V)
           │
           │ T_VC (标定外参)
           ▼
     Camera坐标系 (C)
```

### 外参数据来源

灰度相机的外参来自 `SXR::FrameInfo`：

```cpp
struct FrameInfo {
    // 外参 - Camera在设备坐标系中的位姿 (T_VC)
    float position[3];          // 相机位置
    float rotation[4];          // 相机旋转（四元数：x, y, z, w）
    // ...
};
```

### 变换公式

**目标**：计算 Camera 在 World 坐标系中的位姿 **T_WC**

**变换链**：`World (W) ──T_WV──► View (V) ──T_VC──► Camera (C)`

**公式**：`T_WC = T_WV × T_VC`

### 代码实现

```cpp
// 1. 获取 View 在 World 中的位姿 (T_WV)
xrLocateSpace(xrViewSpace, xrRootSpace, timestamp, &location);
glm::quat q_wv(location.pose.orientation.w,
               location.pose.orientation.x,
               location.pose.orientation.y,
               location.pose.orientation.z);
glm::vec3 p_wv(location.pose.position.x,
               location.pose.position.y,
               location.pose.position.z);

// 2. 获取外参 - Camera 在 View 中的位姿 (T_VC)
// 注意：SxrPose.rotation 顺序为 (x, y, z, w)，GLM 为 (w, x, y, z)
glm::quat q_vc(frameInfo.rotation[3],  // w
               frameInfo.rotation[0],  // x
               frameInfo.rotation[1],  // y
               frameInfo.rotation[2]); // z
glm::vec3 p_vc(frameInfo.position[0],
               frameInfo.position[1],
               frameInfo.position[2]);

// 3. 组合变换：计算 Camera 在 World 中的位姿 (T_WC)
glm::quat q_wc = q_wv * q_vc;
glm::vec3 p_wc = glm::rotate(q_wv, p_vc) + p_wv;
```

### 灰度相机配置

系统支持4个灰度相机（SXR::CAME_MAX = 4）：

```
┌─────────────────┬──────────────┐
│  枚举值         │  位置        │
├─────────────────┼──────────────┤
│  GRAY_LEFT (0)  │  左侧灰度相机 │
│  GRAY_RIGHT (1) │  右侧灰度相机 │
│  GRAY_LEFT_UP(2)│  左上灰度相机 │
│  GRAY_RIGHT_UP(3)│ 右上灰度相机 │
└─────────────────┴──────────────┘
```

每个相机的标定信息包括：
- `position[3]` - 相机在设备坐标系中的位置
- `rotation[4]` - 相机在设备坐标系中的旋转（四元数）
- `width`, `height` - 图像分辨率

### 时间戳转换

灰度相机使用 `boottime` 时间戳，需要转换为 OpenXR 的 `XrTime`：

```cpp
XrTime boottimeToXrTime(uint64_t boottime_ns) {
    // 1. 计算 CLOCK_BOOTTIME 和 CLOCK_MONOTONIC 的偏移
    // 2. 将 boottime 转换为 monotonic time
    // 3. 使用 xrConvertTimespecTimeToTimeKHR 转换为 XrTime
}
```

原因：
- 相机帧时间戳：`CLOCK_BOOTTIME`（包含休眠时间）
- OpenXR定位：需要 `XrTime`（基于 `CLOCK_MONOTONIC`）

---

## 内参说明

相机内参用于图像去畸变和3D投影计算：

FrameInfo 中的内参：
```cpp
float focalX, focalY;       // 焦距
float centerX, centerY;     // 主点
float radialDistortion[8];  // Kannala-Brandt 鱼眼畸变参数
```

---

## 手势投影到 RGB 图像

### 投影链路

将手势关节 (Root Space) 投影到 RGB 相机图像的 2D 像素坐标：

```
HandJoint(RootSpace) → World frame
    │
    │ offset = joint - wcPos
    ▼
R_WC = conj(extQuat) × conj(headQuat)  
    │
    ▼
Camera frame
    │
    │ KB fisheye projection
    ▼
2D pixel (u, v)
    │
    │ rotate_uv_90cw (绕图像中心顺时针 90°)
    ▼
Final pixel (u', v')
```

### 坐标变换

**World → Camera** 变换使用外参旋转的四元数链：

```
R_WC = conj(extQuat) × conj(headQuat)
cam   = quatRotate(R_WC, joint - wcPos)
```

其中 `headQuat` 是 view→world 旋转 (OpenXR view space: X=right, Y=up, Z=backward)，`extQuat` 是 camera→view 外参旋转。

**相机世界位置** 使用 device/IMU pose + 外参平移：

```
wcPos = devicePos + quatRotate(deviceQuat, extPos)
```

**KB 投影后**，应用 90° 顺时针 2D 旋转匹配 Python 参考实现：

```cpp
u_rot = centerX + (v - centerY);
v_rot = centerY - (u - centerX);
```


### UV 像素偏移

投影完成后可施加 per-eye UV 偏移进行微调校准（默认为 0）：

```cpp
handOverlay.setUVOffset(0, 0.0f, 0.0f);  // 左眼
handOverlay.setUVOffset(1, 0.0f, 0.0f);  // 右眼
```

### 开启/关闭

手势投影通过系统属性 `persist.xr.project_hand` 控制，**默认关闭**，仅在编码录制时生效（不影响头戴预览画面）：

```bash
# 查看当前状态
adb shell getprop persist.xr.project_hand

# 开启投影（编码视频中将绘制骨骼 overlay）
adb shell setprop persist.xr.project_hand 1

# 关闭投影
adb shell setprop persist.xr.project_hand 0
```

仅在**手势模式** (`persist.xr.usecontroller=false`) 下生效，手柄模式下不绘制。设置后需重启应用生效。

---

# 手势数据保存功能

## 概述

本应用支持实时捕获和保存手势关节数据，通过 OpenXR Hand Tracking 扩展获取双手的 26 个关节点位姿信息，并以 CSV 格式保存到设备存储中。

## 依赖的 OpenXR 扩展

```
┌───────────────────────────────────────────────────────────────┐
│  扩展名称                                    │  功能          │
├───────────────────────────────────────────────────────────────┤
│  XR_EXT_HAND_TRACKING_EXTENSION_NAME         │  手势跟踪基础  │
│  XR_MSFT_HAND_TRACKING_MESH_EXTENSION_NAME   │  手势网格      │
│  XR_QCOM_HAND_TRACKING_GESTURE_EXTENSION_NAME│  QCOM手势识别  │
│  XR_EXT_hand_interaction                     │  手势交互      │
│  XR_EXT_palm_pose                            │  手掌姿态      │
│  XR_MSFT_hand_interaction                    │  MSFT手势交互  │
└───────────────────────────────────────────────────────────────┘
```

API Layer: `XR_APILAYER_QCOM_handtracking`

## 核心组件

### HandTrackerLogic 结构体

负责手势跟踪的核心逻辑：

```cpp
struct HandTrackerLogic {
    // 手势跟踪句柄
    XrHandTrackerEXT LeftHandTrackerHandle;   // 左手跟踪器
    XrHandTrackerEXT RightHandTrackerHandle;  // 右手跟踪器

    // 关节位置数据（每只手26个关节）
    XrHandJointLocationEXT LeftHandJointLocations[XR_HAND_JOINT_COUNT_EXT];
    XrHandJointLocationEXT RightHandJointLocations[XR_HAND_JOINT_COUNT_EXT];

    // 状态标志
    bool LeftHandIsActive;
    bool RightHandIsActive;

    // 数据保存器
    RawDateSave* rawDateSave;
};
```

### Input 类

处理 OpenXR Action 输入和手势更新：

```cpp
class Input {
    void Init(XrInstance instance, XrSession session, XrSpace space);
    void UpdateInput(const XrFrameState& frameState);
};
```

### RawDateSave 类

负责将手势数据异步保存到文件，支持基于录制会话（Session）的 CSV 输出：

```cpp
class RawDateSave {
    void Init(const std::string& savePath);
    void Shutdown();
    void Resume();  // 开始/恢复保存
    void Pause();   // 暂停保存
    void SaveFrame(const FrameData& frameData);  // 保存一帧数据（仅在 Session 活跃时写入）

    // 录制会话管理
    bool StartNewSession(const std::string& csvPath);  // 开始新会话，指定 CSV 路径
    void StopSession();                                 // 停止会话，flush 并关闭 CSV
    bool IsSessionActive() const;                       // 检查会话是否活跃
};
```

## 手势关节定义

每只手包含 26 个关节点（XR_HAND_JOINT_COUNT_EXT = 26）：

```
┌────────────────────────────────────────────────────────────────┐
│  索引 │  关节名称              │  说明                         │
├───────┼────────────────────────┼───────────────────────────────┤
│   0   │  PALM                  │  手掌中心                     │
│   1   │  WRIST                 │  手腕                         │
│   2   │  THUMB_METACARPAL      │  拇指掌骨                     │
│   3   │  THUMB_PROXIMAL        │  拇指近节骨                   │
│   4   │  THUMB_DISTAL          │  拇指远节骨                   │
│   5   │  THUMB_TIP             │  拇指尖端                     │
│   6   │  INDEX_METACARPAL      │  食指掌骨                     │
│   7   │  INDEX_PROXIMAL        │  食指近节骨                   │
│   8   │  INDEX_INTERMEDIATE    │  食指中节骨                   │
│   9   │  INDEX_DISTAL          │  食指远节骨                   │
│  10   │  INDEX_TIP             │  食指尖端                     │
│  11   │  MIDDLE_METACARPAL     │  中指掌骨                     │
│  12   │  MIDDLE_PROXIMAL       │  中指近节骨                   │
│  13   │  MIDDLE_INTERMEDIATE   │  中指中节骨                   │
│  14   │  MIDDLE_DISTAL         │  中指远节骨                   │
│  15   │  MIDDLE_TIP            │  中指尖端                     │
│  16   │  RING_METACARPAL       │  无名指掌骨                   │
│  17   │  RING_PROXIMAL         │  无名指近节骨                 │
│  18   │  RING_INTERMEDIATE     │  无名指中节骨                 │
│  19   │  RING_DISTAL           │  无名指远节骨                 │
│  20   │  RING_TIP              │  无名指尖端                   │
│  21   │  LITTLE_METACARPAL     │  小指掌骨                     │
│  22   │  LITTLE_PROXIMAL       │  小指近节骨                   │
│  23   │  LITTLE_INTERMEDIATE   │  小指中节骨                   │
│  24   │  LITTLE_DISTAL         │  小指远节骨                   │
│  25   │  LITTLE_TIP            │  小指尖端                     │
└───────┴────────────────────────┴───────────────────────────────┘
```

## 关节数据结构

每个关节包含以下信息：

```cpp
struct HandJointPosition {
    float position[3];       // 位置 (x, y, z) - Root Space 坐标系
    float orientation[4];    // 姿态四元数 (x, y, z, w)
    float radius;            // 关节半径（用于碰撞检测）
};

struct HandFrameData {
    bool isActive;           // 手是否被检测到
    uint32_t jointCount;     // 关节数量 (26)
    HandJointPosition joints[26]; // 所有关节数据
};

struct FrameData {
    uint32_t frameNumber;    // 帧序号
    XrTime timestamp;        // boottime 时间戳（纳秒）
    bool hasLeftHand;        // 是否有左手数据
    bool hasRightHand;       // 是否有右手数据
    HandFrameData leftHand;  // 左手数据
    HandFrameData rightHand; // 右手数据
};
```

## 数据保存格式

### 存储路径

手势数据保存在录制数据集目录中：

```
/storage/emulated/0/Android/data/com.ssnwt.helloxr/files/dataset/<YYYYMMDD_HHMMSS>/
├── hand_tracking.csv       # 手势关节数据
└── ...
```

### CSV 文件格式

每帧一行，左右手所有关节数据平铺为列：

```csv
frame_number,timestamp,left_active,right_active,left_joint0_id,left_joint0_name,left_joint0_radius,left_joint0_pos_x,left_joint0_pos_y,left_joint0_pos_z,left_joint0_orientation_x,...,right_joint25_orientation_w
0,123456789012345678,1,1,0,PALM,0.01,0.1,0.2,0.3,0.0,...,0.35
...
```

**字段说明**：
- `frame_number`: 帧序号（uint32）
- `timestamp`: boottime 时间戳（纳秒，int64）
- `left_active` / `right_active`: 手是否活跃（1=活跃，0=不活跃）
- 每只手 26 个关节，每个关节 10 个字段：`{left|right}_joint{i}_id`, `_name`, `_radius`, `_pos_x`, `_pos_y`, `_pos_z`, `_orientation_x`, `_orientation_y`, `_orientation_z`, `_orientation_w`
- 手部不活跃时关节字段填充：`-1,"",0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0`

## 坐标系说明

手势关节数据使用 **Root Space（世界坐标系）**：

```
        Y (up)
        |
        |
        |_______ X (right)
       /
      /
     Z (backward)
```

- 位置单位：米（meters）
- 四元数顺序：(x, y, z, w)

### 全局姿态修正（坐标空间一致性）

应用启动时自动尝试创建 QCOM Root Space（`xrCreateRootSpaceQCOM`），如果创建成功则全局启用。

```
┌─────────────────────────────────────────────────────────────────┐
│  空间              │  类型                        │  用途       │
├─────────────────────────────────────────────────────────────────┤
│  xrRootSpace       │  QCOM 扩展 Root Space        │  全局SLAM 定位  │
│  xrLocalSpace      │  XR_REFERENCE_SPACE_TYPE_LOCAL │  渲染场景  │
│  xrViewSpace       │  XR_REFERENCE_SPACE_TYPE_VIEW  │  视角锁定  │
└─────────────────────────────────────────────────────────────────┘
```

渲染管线的空间链路：

```
xrLocateViews(space=xrRootSpace 或 xrLocalSpace，取决于 useRootSpace)
    → view 矩阵
    → 渲染时 model/view/projection 变换
    → projection layer 提交(space=xrRootSpace 或 xrLocalSpace)
```

#### 自动 Root Space 检测

在 `engine_init_openxr` 中，应用尝试通过 QCOM 扩展创建 Root Space：

```cpp
PFN_xrCreateRootSpaceQCOM createRootSpaceQCOM{};
XrResult result = xrGetInstanceProcAddr(engine->state.xrInstance,
    "xrCreateRootSpaceQCOM", reinterpret_cast<PFN_xrVoidFunction*>(&createRootSpaceQCOM));
if (result == XR_SUCCESS) {
    XrRootSpaceCreateInfoQCOM rootSpaceCreateInfo{};
    rootSpaceCreateInfo.poseInSpace.orientation.w = 1.0f;
    result = createRootSpaceQCOM(engine->state.xrSession, &rootSpaceCreateInfo, &engine->state.xrRootSpace);
    if (result == XR_SUCCESS) {
        engine->useRootSpace = true;
    }
}
```

#### baseSpace 动态选择

`HandTrackerLogic` 和渲染管线根据 `useRootSpace` 标志动态选择参考空间：

```cpp
// HandTrackerLogic::UpdateLeftHand / UpdateRightHand
HandJointsLocateInfo.baseSpace = engine->useRootSpace
    ? engine->state.xrRootSpace    // Root Space（全局 SLAM 坐标系）
    : engine->state.xrLocalSpace;  // Local Space（本地坐标系）

// xrLocateViews
viewLocateInfo.space = engine->useRootSpace
    ? engine->state.xrRootSpace
    : engine->state.xrLocalSpace;
```

**关键点**：手势关节定位与渲染管线始终使用相同的参考空间，确保显示位置正确。当 Root Space 可用时，所有数据（手势、IMU 姿态等）都在全局坐标系中，适合数据集采集。

## 渲染可视化

头戴预览不渲染骨骼。编码录制时，若 `persist.xr.project_hand=1`，会在 SBS 视频画面上叠加 2D 骨骼 overlay（蓝色左手、红橙色右手，含骨骼连线和关节点）。

## 异步保存机制

为避免阻塞渲染线程，数据保存使用独立的工作线程：

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│  渲染线程       │     │  数据队列       │     │  保存线程       │
│                 │     │                 │     │                 │
│  Update()       │────►│  FrameQueue     │────►│  SaveWorker     │
│  SaveFrame()    │     │  (线程安全)     │     │  写入CSV        │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

## 使用示例

### 初始化

```cpp
// 在 engine_init_openxr 中
engine->inputPtr = std::make_unique<Input>();
engine->inputPtr->Init(instance, session,
    engine->useRootSpace ? engine->state.xrRootSpace : engine->state.xrLocalSpace);
engine->mHandTrackerLogic.Init();  // 内部创建 RawDateSave 并初始化
```

### 输入模式选择

应用通过系统属性 `persist.xr.usecontroller` 决定使用手柄模式还是手势模式：

```cpp
engine.useControllerMode = readUseControllerProperty();  // 读取 persist.xr.usecontroller
if (!engine.useControllerMode) {
    engine.mHandTrackerLogic.Init();       // 手势跟踪模式
} else {
    engine.mControllerPoseSaver.Init(storagePath);  // 手柄姿态模式
}
```

### 每帧更新

```cpp
// 在主循环中
engine.inputPtr->UpdateInput(frameState);
if (!engine.useControllerMode) {
    engine.mHandTrackerLogic.Update(frameState);  // 手势跟踪模式
} else if (engine.mDatasetRecorder.isRecording()) {
    // 手柄模式下手动保存手柄姿态
    ControllerPoseRecord rec;
    // ... 填充 rec ...
    engine.mControllerPoseSaver.SaveFrame(rec);
}
```

### 录制会话管理

```cpp
// 开始录制（按键或 Intent 触发）
engine.mDatasetRecorder.start();
engine.mHandTrackerLogic.rawDateSave->StartNewSession(
    engine.mDatasetRecorder.getHandTrackingCsvPath());  // hand_tracking.csv

// 停止录制
engine.mHandTrackerLogic.rawDateSave->StopSession();
engine.mDatasetRecorder.stop();
```

## 注意事项

1. **权限要求**：需要 `android.permission.WRITE_EXTERNAL_STORAGE` 权限
2. **存储空间**：长时间运行会产生大量数据，注意管理存储空间
3. **性能影响**：异步保存机制确保不影响 XR 渲染性能
4. **数据有效性**：检测 `isActive` 标志判断手势是否被正确识别


---

# 数据集录制系统

## 概述

数据集录制系统将所有传感器和视频数据同步采集到一个带时间戳的目录中，用于离线数据处理和 SLAM 算法开发。

## 数据集目录结构

```
/storage/emulated/0/Android/data/com.ssnwt.helloxr/files/
├── dataset/
│   ├── 20260518_143025/                          # 录制会话 1
│   │   ├── rgb.mp4                               # RGB 相机 SBS 视频 (2W×H, H.265)
│   │   │   ├── Track 1: TimedText (帧级 boottime ns 时间戳)
│   │   │   └── Track 2: Video (H.265, 左眼左半 + 右眼右半)
│   │   ├── tracking.mp4                          # Tracking 灰度视频 (W×H, H.265)
│   │   │   ├── Track 1: TimedText
│   │   │   └── Track 2: Video
│   │   ├── ctrl.mp4                              # Ctrl 灰度视频 (W×H, H.265)
│   │   │   ├── Track 1: TimedText
│   │   │   └── Track 2: Video
│   │   ├── audio.m4a                             # AAC 音频 (44.1kHz, 单声道, 96kbps)
│   │   │   ├── Track 1: Audio (AAC-LC)
│   │   │   └── Track 2: TimedText (音频帧 boottime ns 时间戳)
│   │   ├── accel.csv                             # 加速度计 (timestamp_ns, x, y, z)  [m/s²]
│   │   ├── gyro.csv                              # 陀螺仪 (timestamp_ns, x, y, z)    [rad/s]
│   │   ├── head_pose.csv                         # 头部 6DOF (timestamp_ns, pos_x/y/z, quat_x/y/z/w)
│   │   ├── hand_tracking.csv                     # 手势关节数据 (手势模式，与下一行二选一)
│   │   ├── controller_poses.csv                  # 手柄姿态数据 (手柄模式)
│   │   ├── camera_params_rgb.json                # RGB 相机内参/外参
│   │   ├── camera_params_tracking.json           # Tracking 相机内参/外参
│   │   ├── camera_params_ctrl.json               # Ctrl 相机内参/外参
│   │   └── time_offset.json                      # BOOTTIME → REALTIME(UTC) 时间偏移采样
│   │
│   ├── 20260518_150312/                          # 录制会话 2
│   │   └── ...                                   # 同上结构
│   └── 20260518_161027/                          # 录制会话 3
│       └── ...
│
└── images/                                       # 截图目录 (VOLUME_UP 触发)
    ├── rgb_20260428_101208.png                   # RGB 左右眼拼接 (4656×1748)
    ├── tracking_20260428_101208.png              # Tracking 左右眼拼接 (1280×480)
    └── ctrl_20260428_101208.png                  # Ctrl 左右眼拼接 (1280×480)
```

### 数据流与时间戳体系

```
                    boottime (CLOCK_BOOTTIME, 纳秒)
                           │
          ┌────────────────┼────────────────────────┐
          │                │                        │
     IMU 传感器        相机帧                  渲染帧 (XrTime)
     accel/gyro.csv    *.mp4 TimedText         head_pose/hand
     timestamp_ns      (ns 原始值)             快照缓存
          │                │                        │
          │                └──────┬─────────────────┘
          │                       │ RGB回调读取快照
          │                       ▼
          │               head_pose.csv   (与RGB同时间戳, 30fps)
          │               hand_tracking.csv (与RGB同时间戳, 30fps)
          │
          └── 独立采集，不受RGB帧率约束
```

> **时间戳对齐**: head_pose.csv 和 hand_tracking.csv 的 timestamp 与 rgb.mp4 的 TimedText Track
> 时间戳完全一致（均为相机驱动提供的 boottime 纳秒）。渲染线程更新最新传感器快照，
> RGB相机回调线程在编码每一帧时读取快照并写入CSV，确保三路数据时间戳严格对齐。
>
> **时间戳转换**: 所有文件中的时间戳均为 `CLOCK_BOOTTIME`（设备开机后的纳秒数）。录制时 `DatasetRecorder`
> 同时以 1 Hz 频率采样 `CLOCK_BOOTTIME` 和 `CLOCK_REALTIME`（NTP 同步的 UTC 时间），计算 offset 写入
> `time_offset.json`。后处理通过公式 `utc_ns = boottime_ns + offset_ns` 转换为绝对 UTC 时间戳，
> 便于多设备间的时间同步。相邻 offset 采样点之间取最近点的 offset 值。

### time_offset.json 格式

```json
{
  "description": "CLOCK_BOOTTIME to CLOCK_REALTIME (UTC) offset samples",
  "unit": "nanoseconds",
  "formula": "utc_timestamp_ns = boottime_timestamp_ns + offset_ns",
  "offsets": [
    {"boottime_ns": 56421006350402, "realtime_ns": 1780024781831764266, "offset_ns": 1779968360825413864},
    {"boottime_ns": 56422008498943, "realtime_ns": 1780024782833912807, "offset_ns": 1779968360825413864}
  ]
}
```

- **采样频率**: 1 Hz（每秒一次，录制开始时立即采样第一个点）
- **转换方法**: 找到 `boottime_ns` 最接近的 offset 采样点，使用其 `offset_ns` 计算 `utc_ns = boottime_ns + offset_ns`
- **稳定性**: `offset_ns` 在正常录制期间波动 < 100 ns（仅两次 `clock_gettime` 调用之间的固有抖动）

### 文件大小参考（约 10 秒录制）

| 文件 | 大小估算 | 说明 |
|------|----------|------|
| rgb.mp4 | ~10 MB | 8Mbps, 30fps, SBS |
| tracking.mp4 | ~5 MB | 4Mbps, 60fps |
| ctrl.mp4 | ~5 MB | 4Mbps, 60fps |
| audio.m4a | ~120 KB | 96kbps, 单声道 |
| accel.csv | ~300 KB | ~2kHz 采样 |
| gyro.csv | ~300 KB | ~2kHz 采样 |
| head_pose.csv | ~50 KB | 30fps |
| hand_tracking.csv | ~100 KB | 30fps, 52关节×10字段 |
| camera_params_*.json | ~1 KB | 仅首帧，静态参数 |
| time_offset.json | ~0.5 KB | 1Hz offset 采样 |

## 核心组件

### DatasetRecorder

中心协调器，管理录制数据集目录的创建和数据采集的启停：

```cpp
class DatasetRecorder {
    void init(const std::string& basePath);
    bool start();     // 创建 dataset/<YYYYMMDD_HHMMSS>/ 目录，启动 IMU、音频、头部姿态采集
    void stop();      // 停止所有采集器，flush 数据

    std::string getDatasetDir() const;           // 数据集目录路径
    std::string getHandTrackingCsvPath() const;  // hand_tracking.csv 路径
    std::string getControllerPoseCsvPath() const;// controller_poses.csv 路径
    std::string getAudioPath() const;            // audio.m4a 路径

    // 从渲染线程异步保存头部姿态（非阻塞）
    void saveHeadPose(int64_t boottimeNs, const XrPosef& pose);
};
```

`saveHeadPose()` 在每帧渲染循环中调用，将 `xrLocateSpace(viewSpace)` 返回的 device/IMU pose 以 boottime 时间戳异步写入 `head_pose.csv`。相机外参在 device 坐标系中，使用 device pose 确保 `T_WC = T_WD × T_DC` 变换链正确。时间戳通过 `xrTimeToBoottime` 转换（如扩展可用），确保与相机帧时间戳在同一时钟域。

### ImuPoseCollector

采集 IMU 传感器数据（加速度计和陀螺仪），分别输出到 `accel.csv` 和 `gyro.csv`：

```cpp
class ImuPoseCollector {
    bool start(const std::string& accelCsvPath, const std::string& gyroCsvPath);
    void stop();
};
```

**线程模型**：
- `sensorThreadFunc`：通过 ALooper 监听加速度计和陀螺仪事件（最快采样率），事件入队
- `writerThreadFunc`：从队列取出事件，按类型分别写入 `accel.csv` 或 `gyro.csv`

**accel.csv 格式**：
```csv
timestamp_ns,x,y,z
```

**gyro.csv 格式**：
```csv
timestamp_ns,x,y,z
```

- 加速度计单位：m/s²
- 陀螺仪单位：rad/s
- 时间戳：sensor event timestamp（boottime 纳秒）

### AudioEncoder

Native 层音频编码器，使用 AAudio 采集 PCM 数据并通过 AMediaCodec 编码为 AAC，同时保存 boottime 纳秒时间戳到 TimedText Track：

```cpp
class AudioEncoder {
    AudioEncoder(int sampleRate = 44100, int bitRate = 96000, int channelCount = 1);
    bool start(const std::string& outputPath);
    void stop();
};
```

**音频参数**：

```
┌─────────────────────┬─────────────────┐
│  参数               │  值             │
├─────────────────────┼─────────────────┤
│  采样率             │  44100 Hz       │
│  比特率             │  96000 bps      │
│  声道数             │  1（单声道）    │
│  编码器             │  AAC-LC         │
│  MIME               │  audio/mp4a-latm│
│  输入               │  AAudio PCM_I16 │
└─────────────────────┴─────────────────┘
```

**时间戳方案**：与视频编码器相同的 TimedText Track 方案，每个音频帧的绝对 boottime 时间戳（`mBoottimeBaseNs + ptsUs * 1000`）作为纯数字字符串写入 TimedText。

### ControllerPoseSaver

手柄模式下保存左右手柄的位置和旋转数据：

```cpp
class ControllerPoseSaver {
    void Init(const std::string& savePath);
    bool StartSession(const std::string& csvPath);
    void StopSession();
    bool SaveFrame(const ControllerPoseRecord& record);
};
```

**CSV 格式**：
```csv
frame_number,timestamp_ns,left_active,left_px,left_py,left_pz,left_qx,left_qy,left_qz,left_qw,right_active,right_px,right_py,right_pz,right_qx,right_qy,right_qz,right_qw
```

### RootSpaceQCOM

QCOM 扩展 Root Space 的类型定义和函数指针：

```cpp
typedef struct XrRootSpaceCreateInfoQCOM {
    XrStructureType type;
    const void *XR_MAY_ALIAS next;
    XrPosef poseInSpace;
} XrRootSpaceCreateInfoQCOM;

typedef XrResult(XRAPI_PTR *PFN_xrCreateRootSpaceQCOM)(
    XrSession session,
    const XrRootSpaceCreateInfoQCOM *createInfo,
    XrSpace *space);
```

Root Space 提供全局 SLAM 定位，姿态数据在全局坐标系中不会随 Recenter 重置。

### Camera Params 保存

每次录制开始后，相机帧回调中首次收到有效帧时自动保存内参/外参到 JSON：

```
dataset/<YYYYMMDD_HHMMSS>/camera_params_rgb.json
dataset/<YYYYMMDD_HHMMSS>/camera_params_tracking.json
dataset/<YYYYMMDD_HHMMSS>/camera_params_ctrl.json
```

JSON 格式：
```json
{
  "group": "rgb",
  "cameras": [
    {
      "eye": "left",
      "width": 2328,
      "height": 1748,
      "intrinsics": {
        "focalX": ..., "focalY": ...,
        "centerX": ..., "centerY": ...,
        "radialDistortion": [...]
      },
      "extrinsics": {
        "position": [x, y, z],
        "rotation": [x, y, z, w]
      }
    },
    { "eye": "right", ... }
  ]
}
```

## 录制流程

```
按键/Intent → nativeStartRecording()
    │
    ├─ DatasetRecorder.start()
    │   ├─ 创建 dataset/<YYYYMMDD_HHMMSS>/ 目录
    │   ├─ AudioEncoder.start() → audio.m4a
    │   ├─ ImuPoseCollector.start() → accel.csv + gyro.csv
    │   └─ head_pose writer 线程 → head_pose.csv
    │
    ├─ encodingEnabled = true, encodersStopped = false
    ├─ cameraParamsSavedRgb/Tracking/Ctrl = false
    ├─ encoderBaseDir = getDatasetDir()
    │
    ├─ if (controllerMode)
    │   └─ ControllerPoseSaver.StartSession() → controller_poses.csv
    │   else
    │   └─ RawDateSave.StartNewSession() → hand_tracking.csv
    │
    └─ ttsSpeak("开始录制")

渲染循环中（每帧）:
    │
    ├─ DatasetRecorder.saveHeadPose(boottimeNs, devicePose) → head_pose.csv
    │   └─ devicePose = xrLocateSpace(viewSpace, rootSpace, predictedDisplayTime)
    │   └─ boottimeNs = xrTimeToBoottime(predictedDisplayTime)
    ├─ 相机回调 → 编码器 → rgb.mp4 / tracking.mp4 / ctrl.mp4
    ├─ 相机回调 → saveCameraParams() → camera_params_*.json（首次有效帧）
    └─ ControllerPoseSaver.SaveFrame() / RawDateSave.SaveFrame()

按键/Intent → nativeStopRecording()
    │
    ├─ encodingEnabled = false
    ├─ ControllerPoseSaver/RawDateSave.StopSession()
    ├─ DatasetRecorder.stop()
    │   ├─ AudioEncoder.stop()
    │   ├─ ImuPoseCollector.stop()
    │   └─ head_pose writer 停止
    ├─ stopEncoder() (异步线程), encoderBaseDir.clear()
    └─ ttsSpeak("录制已保存")
```

---

# 设备硬件测试功能

## 概述

本应用集成了XR设备的硬件测试功能，包括LED指示灯控制、音频录制、电池状态监控和按键事件监听。

## 依赖库

```
┌───────────────────────────────────────────────────────────────┐
│  库文件                              │  功能                  │
├───────────────────────────────────────────────────────────────┤
│  svr_plugin_android_api.aar          │  SVR设备管理API        │
│  gson-2.8.0.jar                      │  JSON序列化/反序列化   │
└───────────────────────────────────────────────────────────────┘
```

## 核心组件

### AndroidInterface 初始化

使用 SVR Android SDK 初始化设备接口：

```cpp
private void initSvrApi() {
    if (!AndroidInterface.getInstance().isInitialized()) {
        AndroidInterface.getInstance().init(getApplication(), new AndroidInterface.InitListener() {
            @Override
            public void onInitialized() {
                onSvrApiInitialized();
            }

            @Override public void onReleased() {}
            @Override public void onInitError() {}
        });
    }
}

private void onSvrApiInitialized() {
    AndroidInterface.getInstance().getSystemEventUtils().setListener(this);
}
```

## LED 指示灯控制

### 功能说明

通过 `DeviceUtils` 控制设备上的 RGB LED 指示灯：

| 方法 | LED颜色 | 效果 |
|------|---------|------|
| `flashRedLight()` | 红色 | 常亮 |
| `flashGreenLight()` | 绿色 | 常亮 |
| `flashBlueLight()` | 蓝色 | 常亮 |
| `blinkRedLed()` | 红色 | 闪烁 |
| `blinkGreenLed()` | 绿色 | 闪烁 |
| `blinkBlueLed()` | 蓝色 | 闪烁 |

### 实现代码

```java
/**
 * LED类型定义
 * 1 = 红灯
 * 2 = 绿灯
 * 3 = 蓝灯
 */
private void flashLed(int type) {
    AndroidInterface.getInstance().getDeviceUtils().flashLed(type);
}

private void blinkLed(int type) {
    // 闪烁参数：亮100ms，灭100ms
    AndroidInterface.getInstance().getDeviceUtils().blinkLed(type, 100, 100);
}
```

## 音频录制功能

### 功能说明

使用 `MediaRecorder` 实现麦克风音频录制，输出格式为 AAC 编码的 M4A 文件。

### 实现代码

```java
private MediaRecorder mRecorder;
private boolean isRecording = false;

private void startAudioRecord() {
    if (isRecording) {
        return;
    }
    mRecorder = new MediaRecorder();

    mRecorder.setAudioSource(MediaRecorder.AudioSource.MIC);
    mRecorder.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4);
    mRecorder.setAudioEncoder(MediaRecorder.AudioEncoder.AAC);

    // 输出文件路径
    String filepath = getExternalCacheDir().getAbsolutePath() + File.separator
        + System.currentTimeMillis() + ".m4a";
    mRecorder.setOutputFile(filepath);

    // 音频参数
    mRecorder.setAudioSamplingRate(44100);      // 采样率：44.1kHz
    mRecorder.setAudioEncodingBitRate(96000);   // 比特率：96kbps

    try {
        mRecorder.prepare();
        mRecorder.start();
        isRecording = true;
    } catch (IOException e) {
        e.printStackTrace();
    }
}

private void stopAudioRecord() {
    if (isRecording && mRecorder != null) {
        mRecorder.stop();
        mRecorder.release();
        mRecorder = null;
        isRecording = false;
    }
}
```

### 录制参数

```
┌─────────────────────┬─────────────────┐
│  参数               │  值             │
├─────────────────────┼─────────────────┤
│  音频源             │  MIC            │
│  输出格式           │  MPEG_4         │
│  编码器             │  AAC            │
│  采样率             │  44100 Hz       │
│  比特率             │  96000 bps      │
│  文件扩展名         │  .m4a           │
└─────────────────────┴─────────────────┘
```

## 电池状态监控

### 功能说明

通过 `BroadcastReceiver` 监听系统电池状态变化，获取详细的电池信息。

### BatteryInfo 数据结构

```java
private class BatteryInfo {
    private int status;          // 电池状态（充电/放电/满电等）
    private int health;          // 健康状况（良好/过热/过冷等）
    private boolean present;     // 电池是否存在
    private int level;           // 当前电量（百分比）
    private int scale;           // 最大电量
    private int plugged;         // 充电类型（USB/交流电）
    private int voltage;         // 电压（mV）
    private int temperature;     // 温度（0.1°C）
    private String technology;   // 电池技术（如Li-ion）
    private int capacity;        // 容量（mAh）
    private int current;         // 电流（mA）
}
```

### 电池状态枚举

```
┌───────────────────────────────────────────────────────────────┐
│  状态类型                              │  值                  │
├───────────────────────────────────────────────────────────────┤
│  BATTERY_STATUS_UNKNOWN                │  1 (未知)            │
│  BATTERY_STATUS_CHARGING               │  2 (充电中)          │
│  BATTERY_STATUS_DISCHARGING            │  3 (放电中)          │
│  BATTERY_STATUS_FULL                   │  4 (已充满)          │
│  BATTERY_STATUS_NOT_CHARGING           │  5 (未充电)          │
├───────────────────────────────────────────────────────────────┤
│  BATTERY_HEALTH_UNKNOWN                │  1 (未知)            │
│  BATTERY_HEALTH_GOOD                   │  2 (良好)            │
│  BATTERY_HEALTH_OVERHEAT               │  3 (过热)            │
│  BATTERY_HEALTH_DEAD                   │  4 (损坏)            │
│  BATTERY_HEALTH_OVER_VOLTAGE           │  5 (电压过高)        │
│  BATTERY_HEALTH_UNSPECIFIED_FAILURE    │  6 (获取失败)        │
│  BATTERY_HEALTH_COLD                   │  7 (过冷)            │
├───────────────────────────────────────────────────────────────┤
│  PLUGGED_NONE                          │  0 (未插入)          │
│  PLUGGED_AC                            │  1 (交流电)          │
│  PLUGGED_USB                           │  2 (USB)             │
└───────────────────────────────────────────────────────────────┘
```

### 广播接收器实现

```java
private BroadcastReceiver mBroadcastReceiver = new BroadcastReceiver() {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (Intent.ACTION_BATTERY_CHANGED.equals(intent.getAction())) {
            mBatteryInfo.status = intent.getIntExtra("status", 0);
            mBatteryInfo.plugged = intent.getIntExtra("plugged", 0);
            mBatteryInfo.health = intent.getIntExtra("health", 0);
            mBatteryInfo.present = intent.getBooleanExtra("present", false);
            mBatteryInfo.level = intent.getIntExtra("level", 0);
            mBatteryInfo.scale = intent.getIntExtra("scale", 0);
            mBatteryInfo.voltage = intent.getIntExtra("voltage", 0);
            mBatteryInfo.temperature = intent.getIntExtra("temperature", 0);

            // 通过 BatteryManager 获取额外属性
            mBatteryInfo.capacity =
                mBatteryManager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CHARGE_COUNTER);
            mBatteryInfo.current =
                mBatteryManager.getIntProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW);
            mBatteryInfo.technology = intent.getStringExtra("technology");

            Log.d(TAG, Arrays.toString(mBatteryInfo.toArrayString()));
        }
    }
};
```

### 生命周期管理

```java
@Override
protected void onResume() {
    super.onResume();
    if (!isRegisterReceiver) {
        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_BATTERY_CHANGED);
        registerReceiver(mBroadcastReceiver, filter);
        isRegisterReceiver = true;
    }
}

@Override
protected void onStop() {
    super.onStop();
    if (isRegisterReceiver) {
        unregisterReceiver(mBroadcastReceiver);
        isRegisterReceiver = false;
    }
}
```

## 系统事件监听

### SystemEventUtils.Listener 接口

实现 `SystemEventUtils.Listener` 接口以接收系统事件回调：

```java
public class VrNativeActivity extends NativeActivity implements SystemEventUtils.Listener {

    @Override
    public void onStartHome() {
        // Home 键按下
    }

    @Override
    public void onStartQuickMenu(String s) {
        // 快捷菜单打开
    }

    @Override
    public void onRecenter() {
        // 重新定位（Recenter）事件
    }

    @Override
    public void onOtherCommand(int i, int i1, String s) {
        // 其他命令
    }

    @Override
    public void onKeyEvent(int keycode, KeyEvent event) {
        // 按键事件
        Log.d(TAG, "onKeyEvent code:" + keycode + ", action:" + event.getAction());
    }
}
```

## 字符串资源

电池信息显示的本地化字符串定义在 `app/src/main/res/values/arrays.xml`：

```xml
<!-- 电池信息标题 -->
<string-array name="case_battery_info">
    <item>电池状态:</item>
    <item>健康情况:</item>
    <item>是否是当前信息:</item>
    <item>电量水平:</item>
    <item>最大电量:</item>
    <item>插座状态:</item>
    <item>电压:</item>
    <item>温度:</item>
    <item>容量:</item>
    <item>电流:</item>
    <item>电量状态:</item>
    <item>制作工艺:</item>
</string-array>

<!-- 电池状态 -->
<string-array name="case_battery_status">
    <item>未知</item>
    <item>充电</item>
    <item>放电</item>
    <item>电量已满</item>
    <item>未充电</item>
</string-array>

<!-- 电池健康状态 -->
<string-array name="case_battery_health">
    <item>未知</item>
    <item>良好</item>
    <item>过热</item>
    <item>糟糕</item>
    <item>电压过高</item>
    <item>获取信息失败</item>
    <item>过冷</item>
</string-array>

<!-- 电量水平 -->
<string-array name="case_battery_level">
    <item>正常</item>
    <item>低电</item>
    <item>极低</item>
</string-array>

<!-- 充电类型 -->
<string-array name="case_battery_plugged">
    <item>没有插入插座</item>
    <item>插入交流电</item>
    <item>接入USB插座</item>
</string-array>
```

## 权限要求

使用这些功能需要以下权限：

```xml
<!-- 麦克风录音权限 -->
<uses-permission android:name="android.permission.RECORD_AUDIO" />

<!-- 读取电池状态（无需声明，系统广播） -->
```

## 注意事项

1. **API 初始化顺序**：必须等待 `AndroidInterface` 初始化完成后才能调用设备控制 API
2. **生命周期管理**：`BroadcastReceiver` 必须在 Activity 暂停时注销，避免内存泄漏
3. **音频录制**：需要在实际使用前请求 `RECORD_AUDIO` 运行时权限
4. **电池温度单位**：系统返回的温度值为 0.1°C 单位，需要除以 10 转换为摄氏度
5. **LED 控制**：不同设备可能支持的 LED 颜色不同，需要根据实际硬件测试


---

# 摄像头硬件编码方案

## 概述

本应用实现了摄像头视频流的硬件加速编码，支持 RGB 摄像头和 CV 灰度摄像头。RGB 摄像头将左右眼画面通过 FBO 拼接为 **Side-by-Side (SBS)** 格式编码为单个 MP4 文件。编码采用 **Surface 模式**，通过 OpenGL 渲染到 `EncoderSurface`，实现零拷贝的硬件编码，大幅降低 CPU 消耗。

## 编码架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Camera Callback                               │
│                    onCameraFrame(group, data)                        │
└─────────────────────────────────────────────────────────────────────┘
                              │
          ┌───────────────────┴───────────────────┐
          ▼                                       ▼
   ┌──────────────┐                      ┌──────────────┐
   │  RGB Group   │                      │ CV Group     │
   │              │                      │ (TRACKING/   │
   │              │                      │  CTRL)       │
   └──────────────┘                      └──────────────┘
          │                                       │
          ▼                                       ▼
   ┌──────────────┐                      ┌──────────────┐
   │handleRGBFrame│                      │handleCVFrame │
   └──────────────┘                      └──────────────┘
          │                                       │
          ▼                                       ▼
   ┌──────────────────────────────┐  ┌─────────────────────────────────┐
   │ 1. AHardwareBuffer × 2       │  │ 1. AHardwareBuffer → GL Texture │
   │    → GL_TEXTURE_EXTERNAL_OES │  │ 2. YUV→Grayscale Shader 转换    │
   │ 2. 渲染到 SBS Stitch FBO     │  │ 3. 渲染到 EncoderSurface        │
   │    (左眼 viewport=0, 右眼=W) │  └─────────────────────────────────┘
   │ 3. FBO → encoder surface     │           │
   │    (sbsCopyShader, sampler2D)│           ▼
   │ 4. eglPresentationTimeANDROID│  ┌──────────────┐
   └──────────────────────────────┘  │ MediaCodec   │
          │                          │ (Surface)    │
          ▼                          └──────────────┘
   ┌──────────────┐                         │
   │ MediaCodec   │                         ▼
   │ (Surface)    │                 ┌──────────────┐
   └──────────────┘                 │tracking.mp4 /│
          │                         │  ctrl.mp4    │
          ▼                         └──────────────┘
   ┌──────────────┐
   │   rgb.mp4    │  (2W×H SBS, 左眼左半, 右眼右半)
   └──────────────┘
```

## 核心组件

### EncoderSurface

封装了 EGL Surface 和 MediaCodec 的输入 Surface：

```cpp
class EncoderSurface {
    ANativeWindow* mWindow;      // MediaCodec 输入 Surface
    EGLDisplay mDisplay;         // EGL 显示
    EGLSurface mSurface;         // EGL Surface
    EGLContext mContext;         // EGL 上下文（与主上下文共享）
    EGLConfig mConfig;           // EGL 配置

    bool init(ANativeWindow* window, EGLDisplay sharedDisplay, EGLContext sharedContext);
    bool makeCurrent();
    bool swapBuffers();
    void setPresentationTime(int64_t timeNs);  // 设置帧时间戳（swapBuffers 前调用）
    void release();
};
```

### CameraEncoder

封装了 MediaCodec 编码器：

```cpp
class CameraEncoder {
    // RGB 摄像头：Surface 模式（编码器不感知 SBS，由调用方传入宽高和文件名）
    CameraEncoder(int width, int height, int frameRate, int bitRate,
                  const std::string& outputName, const std::string& baseDir = "");

    // 灰度摄像头：Buffer 模式（已弃用）
    CameraEncoder(const std::string& groupName, int width, int height,
                  int frameRate = 60, const std::string& baseDir = "");

    // 灰度摄像头：Surface 模式（推荐）
    CameraEncoder(const std::string& groupName, int width, int height,
                  int frameRate, EncoderMode mode, const std::string& baseDir = "");

    ANativeWindow* getInputSurface();  // 获取编码器输入 Surface
    void submitNsTimestamp(int64_t timestampNs);  // 提交办秒时间戳（写入 TimedText）
    void signalEndOfInputStream();  // Surface 模式结束信号
    bool feedFrame(const uint8_t* data, size_t size, int64_t timestampNs);  // Buffer 模式输入
    bool start();
    void stop();
};
```

### 独立 EGL 上下文（每个相机组一个）

为了在相机回调线程中进行 GL 渲染，每个相机组（RGB、Tracking、Ctrl）拥有独立的 EGL 上下文，避免锁竞争：

```cpp
struct CameraGLContext {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;  // Pbuffer for offscreen rendering
    std::atomic<bool> initialized{false};

    bool init(const AppCommon::base_engine* engine);
    void cleanup();
    bool makeCurrent();
    void releaseCurrent();
};

struct CameraAccessExtension {
    // 每个相机组独立上下文（无锁竞争）
    CameraGLContext rgbCtx;
    CameraGLContext trackingCtx;
    CameraGLContext ctrlCtx;
};
```

**相比共享上下文的改进**：每个相机回调线程使用独立上下文，无需互斥锁保护，消除了相机回调线程之间的上下文切换等待。

### 时间戳处理方案

#### 时间戳单位差异

```
┌─────────────────────────────────────────────────────────────────┐
│  来源              │  单位          │  说明                      │
├───────────────────┼────────────────┼────────────────────────────┤
│  相机帧时间戳      │  纳秒 (ns)     │  FrameInfo.timestamp       │
│  MediaCodec       │  微秒 (µs)     │  presentationTimeUs        │
│  OpenXR XrTime    │  纳秒 (ns)     │  xrLocateSpace             │
└───────────────────┴────────────────┴────────────────────────────┘
```

**单位转换**：
```cpp
// 纳秒 → 微秒
int64_t timestampUs = timestampNs / 1000;

// 微秒 → 纳秒
int64_t timestampNs = timestampUs * 1000;
```

#### 原始时间戳保存方案

采用双层时间戳策略：微秒时间戳用于视频帧 PTS，纳秒时间戳通过 TimedText Track 保存：

```
┌─────────────────────────────────────────────────────────────────┐
│  MP4 文件结构                                                    │
├─────────────────────────────────────────────────────────────────┤
│  Track 1: Video (H.265)                                         │
│    - 编码后的视频帧                                              │
│    - presentationTimeUs (微秒精度)                               │
│                                                                 │
│  Track 2: TimedText (时间戳元数据)                               │
│    - 每帧的原始纳秒时间戳（纯文本数字字符串）                     │
└─────────────────────────────────────────────────────────────────┘
```

#### 实现代码

**1. 创建 TimedText Track**

在 `initEncoder()` 中创建：

```cpp
AMediaFormat* textFormat = AMediaFormat_new();
AMediaFormat_setString(textFormat, AMEDIAFORMAT_KEY_MIME, "application/x-subrip");
AMediaFormat_setString(textFormat, AMEDIAFORMAT_KEY_LANGUAGE, "und");
AMediaFormat_setInt32(textFormat, AMEDIAFORMAT_KEY_IS_FORCED_SUBTITLE, 0);
AMediaFormat_setInt32(textFormat, AMEDIAFORMAT_KEY_IS_AUTOSELECT, 0);
mTextTrackIndex = AMediaMuxer_addTrack(mMuxer, textFormat);
```

**2. 视频帧 PTS 设置**

通过 `eglPresentationTimeANDROID` 扩展在 swapBuffers 前设置纳秒时间戳，MediaCodec 内部将其转换为微秒作为 PTS：

```cpp
// 在 handleRGBFrame 中：
rgbEncoderSurface->setPresentationTime(frameTimestampNs);  // ns，编码器转为 µs 用作 PTS
rgbEncoderSurface->swapBuffers();
```

`EncoderSurface::setPresentationTime()` 实现通过 `eglGetProcAddress` 动态加载扩展函数：

```cpp
void EncoderSurface::setPresentationTime(int64_t timeNs) {
    auto pfn = (PFNEGLPRESENTATIONTIMEANDROID)eglGetProcAddress("eglPresentationTimeANDROID");
    pfn(mDisplay, mSurface, timeNs);
}
```

**3. 纳秒时间戳写入 TimedText Track**

通过 `submitNsTimestamp()` 提交纳秒时间戳，编码器输出线程在处理每个非 codec-config 帧时将时间戳写入 TimedText Track：

```cpp
// 调用方提交 ns 时间戳（线程安全队列）
rgbEncoder->submitNsTimestamp(frameTimestampNs);

// CameraEncoder::processOutputBuffer() 中：
// 从队列取出 ns 时间戳，写入 TimedText track
std::string text = std::to_string(nsTimestamp);
AMediaCodecBufferInfo textInfo;
textInfo.presentationTimeUs = info.presentationTimeUs;  // 与视频帧 PTS 对齐
textInfo.size = text.size();
AMediaMuxer_writeSampleData(mMuxer, mTextTrackIndex, (const uint8_t*)text.c_str(), &textInfo);
```

#### 时间戳对齐策略

```
相机帧到达 ──► 记录原始 ns 时间戳 ──► 渲染到 Surface ──► MediaCodec 编码
     │                                    │
     │  timestampNs (ns)                  │  eglPresentationTimeANDROID (ns)
     │  → submitNsTimestamp()             │  → 编码器内部 ns/1000 = µs (PTS)
     │  → TimedText Track                 │  → Video Track PTS
     │                                    │
     └────────────────────────────────────┘
                    同一帧，不同存储方式
```

**关键点**：
1. `eglPresentationTimeANDROID` 设置纳秒时间戳，编码器内部转为微秒作为视频帧 PTS
2. `submitNsTimestamp()` 将纳秒时间戳排队，输出线程逐帧写入 TimedText Track
3. TimedText 的 `presentationTimeUs` 与视频帧 PTS 对齐，保证后期关联
4. TimedText 内容为纯数字字符串（纳秒时间戳值），不是 JSON 格式

#### 读取时间戳数据

使用 `MediaExtractor` 找到 MIME 为 `application/x-subrip` 的 TimedText track，读取每个样本即可获得对应帧的纳秒时间戳字符串。

### MP4 文件分析命令

#### 查看文件结构

```bash
# 查看所有 track 信息（编码格式、分辨率、帧数等）
ffprobe -v quiet -show_format -show_streams rgb.mp4

# 输出示例：
#   Stream 0: data (mett)    — TimedText 时间戳 track, 286 帧
#   Stream 1: video (hevc)   — H.265 视频, 4656x1748, ~30fps, ~8Mbps
```

#### 查看 Video 帧时间戳

```bash
# 视频帧 PTS（秒）
ffprobe -v quiet -select_streams 1 -show_entries packet=pts_time -of csv=p=0 rgb.mp4 | head -5
# 输出: 0.000000
#       0.064511
#       0.096767
#       ...

# 视频帧 PTS（原始 tick 值，time_base=1/90000）
ffprobe -v quiet -select_streams 1 -show_entries packet=pts -of csv=p=0 rgb.mp4 | head -5
# 输出: 0
#       5806
#       8709
#       ...
```

#### 提取 TimedText 纳秒时间戳

```bash
# 提取原始 ns 时间戳（每个样本为 13 字节 ASCII 数字）
ffmpeg -i rgb.mp4 -map 0:0 -f data - 2>/dev/null | strings | head -5
# 输出: 2300450365549
#       2300514879299
#       2300547136226
#       2300579393101
#       2300611649976
```

#### 用 Python 完整解析时间戳

```python
#!/usr/bin/env python3
"""从 rgb.mp4 提取 TimedText 纳秒时间戳并与 Video PTS 对比"""
import subprocess, sys

def extract_ns_timestamps(mp4_path):
    """提取 TimedText track 中的纳秒时间戳"""
    result = subprocess.run(
        ['ffmpeg', '-i', mp4_path, '-map', '0:0', '-f', 'data', '-'],
        capture_output=True)
    data = result.stdout
    timestamps = []
    i = 0
    while i + 13 <= len(data):
        ts = int(data[i:i+13])
        timestamps.append(ts)
        i += 13
    return timestamps

def extract_video_pts(mp4_path):
    """提取视频帧 PTS（秒）"""
    result = subprocess.run(
        ['ffprobe', '-v', 'quiet', '-select_streams', '1',
         '-show_entries', 'packet=pts_time', '-of', 'csv=p=0', mp4_path],
        capture_output=True, text=True)
    return [float(line) for line in result.stdout.strip().split('\n') if line]

ns_ts = extract_ns_timestamps('rgb.mp4')
video_pts = extract_video_pts('rgb.mp4')

print(f'帧数: {len(ns_ts)} text, {len(video_pts)} video')
first_ns = ns_ts[0]
for i in range(min(5, len(ns_ts))):
    ns_delta_us = (ns_ts[i] - first_ns) / 1000.0
    video_us = video_pts[i] * 1e6
    print(f'[{i}] ns={ns_ts[i]}  delta={ns_delta_us:.3f}us  video={video_us:.3f}us  diff={ns_delta_us-video_us:.3f}us')
```

#### 验证时间戳质量

```bash
# 检查帧数是否一致
echo "Video frames: $(ffprobe -v quiet -select_streams 1 -count_packets \
  -show_entries stream=nb_read_packets -of csv=p=0 rgb.mp4)"
echo "Text frames:  $(ffprobe -v quiet -select_streams 0 -count_packets \
  -show_entries stream=nb_read_packets -of csv=p=0 rgb.mp4)"

# 计算平均帧率
ffprobe -v quiet -show_entries stream=avg_frame_rate -of csv=p=0 \
  -select_streams 1 rgb.mp4
```

#### 注意事项

- MP4 容器 time_base = 1/90000（分辨率 11.11 us），Video PTS 与 ns 时间戳之间存在 ~1.3 us/帧的量化漂移
- NS 原始精度通过 TimedText Track 完整保留，不受量化影响
- 后期处理时应以 TimedText 中的 ns 时间戳为准，Video PTS 仅用于播放同步

## 关键技术点

### 1. EGL 上下文共享

```
┌──────────────────────────────────────────────────────────────────┐
│                        主 GL 线程                                 │
│  ┌─────────────┐                                                 │
│  │ MainContext │◄─────────────────────────────────┐              │
│  └─────────────┘                                  │              │
│                                                   │ 共享资源     │
└───────────────────────────────────────────────────│──────────────┘
                                                    │
┌───────────────────────────────────────────────────│──────────────┐
│                     相机回调线程                   │              │
│  ┌─────────────┐      ┌─────────────┐            │              │
│  │SharedContext│◄─────│ RGBEncoder  │            │              │
│  │  (共享)     │      │  Surface    │            │              │
│  └─────────────┘      └─────────────┘            │              │
│        │              ┌─────────────┐            │              │
│        └──────────────│ CVEncoder   │────────────┘              │
│                       │  Surface    │                           │
│                       └─────────────┘                           │
└──────────────────────────────────────────────────────────────────┘

共享资源：纹理、Buffer、着色器程序
不共享资源：VAO、FBO、上下文状态
```

### 2. AHardwareBuffer → GL Texture

```cpp
// 步骤1: 获取 native client buffer
EGLClientBuffer clientBuffer = eglGetNativeClientBufferANDROID(hwBuffer);

// 步骤2: 创建 EGLImage（指定线性色彩空间）
EGLint attrs[] = {
    EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_LINEAR,
    EGL_NONE
};
EGLImageKHR eglImage = eglCreateImageKHR(display, EGL_NO_CONTEXT,
                                          EGL_NATIVE_BUFFER_ANDROID,
                                          clientBuffer, attrs);

// 步骤3: 绑定到纹理
glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, eglImage);

// 步骤4: 使用完后销毁 EGLImage（纹理持有引用）
eglDestroyImageKHR(display, eglImage);
```

### 3. VAO 跨上下文问题

**问题**：VAO（Vertex Array Object）不能跨 OpenGL 上下文共享。

**解决方案**：在渲染时直接设置顶点属性，而不是使用预创建的 VAO：

```cpp
// 错误：VAO 不能跨上下文共享
glBindVertexArray(grayscaleEncoderVAO);
glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

// 正确：直接设置顶点属性
glBindBuffer(GL_ARRAY_BUFFER, grayscaleEncoderVBO);  // VBO 可以共享
glEnableVertexAttribArray(posLoc);
glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, stride, 0);
glEnableVertexAttribArray(texLoc);
glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, stride, offset);
glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
```

### 4. 共享上下文初始化时序

**问题**：相机帧可能在共享上下文初始化完成前就开始到达。

**解决方案**：在编码器初始化时检查共享上下文状态：

```cpp
void initGrayscaleEncoder(SXR::CameraGroup group, int width, int height) {
    std::lock_guard<std::mutex> lock(sharedContextMutex);

    // 检查共享上下文是否就绪
    if (!sharedContextInitialized) {
        LOGI("Shared context not ready yet, skipping encoder init for now");
        return;  // 下一帧会重试
    }

    // ... 继续初始化 ...
}
```

### 5. 纹理坐标翻转

相机纹理通常需要翻转 Y 坐标：

```cpp
// 全屏四边形顶点数据
float vertices[] = {
    // position     texcoord (Y 翻转)
    -1.0f, -1.0f,   0.0f, 1.0f,  // 左下 → 纹理左上
     1.0f, -1.0f,   1.0f, 1.0f,  // 右下 → 纹理右上
    -1.0f,  1.0f,   0.0f, 0.0f,  // 左上 → 纹理左下
     1.0f,  1.0f,   1.0f, 0.0f,  // 右上 → 纹理右下
};
```

### 6. 灰度转换着色器

对于 CV 灰度摄像头，AHardwareBuffer 格式可能是厂商特定的 YUV 变体。使用亮度公式提取灰度：

```glsl
#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision highp float;
in vec2 vTexCoord;
uniform samplerExternalOES uGrayscaleTexture;
out vec4 fragColor;

void main() {
    // 采样外部 YUV 纹理（GPU 自动进行 YUV→RGB 转换）
    vec4 color = texture(uGrayscaleTexture, vTexCoord);
    // 使用 BT.601 亮度公式提取灰度
    float y = 0.299 * color.r + 0.587 * color.g + 0.114 * color.b;
    // 输出灰度图像
    fragColor = vec4(vec3(y), 1.0);
}
```

### 7. RGB SBS 编码渲染管线

RGB 编码采用 FBO 拼接方式，将左右眼渲染到同一个 FBO 再输出到编码器 Surface：

```
┌─────────────────────────────────────────────────────────────┐
│  1. 创建临时 GL_TEXTURE_EXTERNAL_OES 纹理（左右眼各一）      │
│     从 AHardwareBuffer 创建 EGLImage 并绑定                 │
├─────────────────────────────────────────────────────────────┤
│  2. 渲染左右眼到 SBS Stitch FBO (GL_TEXTURE_2D, 2W×H)      │
│     - encoderShaderProgram (samplerExternalOES)              │
│     - 左眼: viewport(0, 0, W, H)                            │
│     - 右眼: viewport(W, 0, W, H)                            │
├─────────────────────────────────────────────────────────────┤
│  3. 切换到 EncoderSurface 上下文                             │
│     - makeCurrent() 后需重新设置顶点属性（per-context 状态） │
│     - 渲染 SBS FBO 纹理到 encoder surface                   │
│     - sbsCopyShaderProgram (sampler2D, 非 samplerExternalOES)│
├─────────────────────────────────────────────────────────────┤
│  4. setPresentationTime(ns) → swapBuffers()                  │
└─────────────────────────────────────────────────────────────┘
```

**注意采样器类型匹配**：`encoderShaderProgram` 使用 `samplerExternalOES` 用于采样相机 AHardwareBuffer（`GL_TEXTURE_EXTERNAL_OES`），而 `sbsCopyShaderProgram` 使用 `sampler2D` 用于采样 SBS FBO 纹理（`GL_TEXTURE_2D`）。采样器类型不匹配会导致输出全黑。

## 线程模型

```
┌─────────────────────────────────────────────────────────────────┐
│  线程 ID    │  职责                    │  GL 上下文             │
├─────────────────────────────────────────────────────────────────┤
│  主线程     │  OpenXR 渲染、UI         │  MainContext          │
│  相机线程1  │  RGB 相机帧回调          │  rgbCtx               │
│  相机线程2  │  CV Tracking 帧回调      │  trackingCtx          │
│  相机线程3  │  CV CTRL 帧回调          │  ctrlCtx              │
│  编码线程   │  MediaCodec 编码输出     │  (无 GL 操作)          │
└─────────────────────────────────────────────────────────────────┘

同步机制：
- trackingFrameMutex: 保护跟踪帧数据
- 每个相机组独立 EGL 上下文，无需互斥锁保护 GL 操作
```

## 编码方案对比

| 特性 | Buffer 模式 | Surface 模式 |
|------|-------------|--------------|
| CPU 消耗 | 高（需拷贝帧数据） | 低（零拷贝） |
| 内存带宽 | 高 | 低 |
| 实现复杂度 | 简单 | 复杂 |
| 线程安全 | 容易保证 | 需要同步 |
| 适用场景 | 低帧率、简单场景 | 高帧率、性能敏感 |

## 视频编码格式选择

### H.264 与 H.265 对比

| 特性 | H.264 (AVC) | H.265 (HEVC) |
|------|-------------|--------------|
| MIME 类型 | `video/avc` | `video/hevc` |
| 压缩效率 | 基准 | 比 H.264 高约 50% |
| 编码延迟 | 较低 | 略高 |
| 兼容性 | 广泛支持 | 较新设备支持 |
| CPU/GPU 消耗 | 较低 | 较高 |
| 适用场景 | 兼容性优先 | 存储空间/带宽优先 |

### 切换编码格式

在 `CameraEncoder.cpp` 的 `initEncoder()` 函数中修改 MIME 类型：

**H.264 (AVC) - 默认配置：**
```cpp
void CameraEncoder::initEncoder() {
    mCodec = AMediaCodec_createEncoderByType("video/avc");

    AMediaFormat *format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    // ...
}
```

**H.265 (HEVC) - 高压缩效率：**
```cpp
void CameraEncoder::initEncoder() {
    mCodec = AMediaCodec_createEncoderByType("video/hevc");

    AMediaFormat *format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/hevc");
    // ...
}
```

### 注意事项

1. **设备兼容性**：部分旧设备可能不支持 H.265 硬件编码，建议在初始化时检查编码器是否可用：
   ```cpp
   // 检查 H.265 编码器是否可用
   AMediaCodec* codec = AMediaCodec_createEncoderByType("video/hevc");
   if (!codec) {
       // 回退到 H.264
       codec = AMediaCodec_createEncoderByType("video/avc");
   }
   ```

2. **播放兼容性**：H.265 编码的视频文件需要播放器支持 HEVC 解码

3. **性能权衡**：H.265 压缩率更高但编码计算量更大，在高帧率场景下可能增加功耗

## 注意事项

1. **EGL 上下文必须在线程使用前初始化**：相机帧可能在共享上下文初始化前就开始到达，需要检查 `sharedContextInitialized`

2. **VAO 不能跨上下文共享**：在跨上下文渲染时，使用 VBO 直接设置顶点属性

3. **纹理类型**：相机 AHardwareBuffer 必须使用 `GL_TEXTURE_EXTERNAL_OES`，配合 `samplerExternalOES`

4. **色彩空间**：创建 EGLImage 时指定 `EGL_GL_COLORSPACE_LINEAR` 确保正确的 YUV→RGB 转换

5. **每编码器独立纹理**：多个编码器不能共享同一个纹理，因为 EGLImage 绑定会覆盖之前的

6. **上下文切换**：跨上下文操作时保存和恢复原上下文状态

7. **EGLImage 生命周期**：使用完后调用 `eglDestroyImageKHR`，纹理会保持对图像数据的引用直到渲染完成

8. **AHardwareBuffer 所有权**：相机服务拥有 buffer，不要调用 `AHardwareBuffer_release()`，buffer 只在回调期间有效


---

# 截图与视频保存功能

## 概述

应用支持通过手柄按键或 ADB Intent 远程控制截图和视频录制，并附带中文语音提示。

## 按键映射

| 按键 | 功能 | 语音提示 |
|------|------|----------|
| VOLUME_UP | 保存所有相机截图 | "图片已保存" / "图片保存失败" |
| DPAD_CENTER / Right B（第1次） | 开始视频录制 | "开始录制" |
| DPAD_CENTER / Right B（第2次） | 停止视频录制 | "录制已保存" |

## 截图保存

### 触发方式

1. **手柄按键**：按下 VOLUME_UP
2. **ADB 远程命令**：
   ```bash
   adb shell am broadcast -a com.ssnwt.helloxr.SAVE_IMAGE
   ```

### 保存格式

- 格式：PNG
- 按相机组（group）保存，左右眼水平拼接为一张图
- 文件命名：`{cameraGroup}_{YYYYMMDD_HHmmss}.png`

### 保存路径

```
/sdcard/Android/data/com.ssnwt.helloxr/files/images/
├── rgb_20260428_101208.png          # RGB 左右眼拼接 (4656x1748)
├── tracking_20260428_101208.png     # Tracking 左右眼拼接 (1280x480)
├── ctrl_20260428_101208.png         # Ctrl 左右眼拼接 (1280x480)
```

### 拼接规则

| 相机组 | 左眼来源 | 右眼来源 | 拼接后尺寸 |
|--------|----------|----------|-----------|
| RGB | hwBuffer[0] | hwBuffer[1] | 4656x1748 |
| Tracking | hwBuffer[0] U=0..0.5 | hwBuffer[0] U=0.5..1.0 | 1280x480 |
| Ctrl | hwBuffer[0] U=0..0.5 | hwBuffer[0] U=0.5..1.0 | 1280x480 |

说明：
- RGB 相机有 2 个独立的 hwBuffer（左眼/右眼各一个）
- CV 灰度相机（Tracking/Ctrl）只有 1 个 hwBuffer，左右眼数据在同一 buffer 中水平拼接（U=0..0.5 为左眼，U=0.5..1.0 为右眼）

### 实现原理

截图通过 OpenGL `glReadPixels` 从渲染纹理中读取像素数据，使用 `stb_image_write` 编码为 PNG。保存操作在独立的工作线程中异步执行，不阻塞渲染循环。

```
VOLUME_UP / Intent
      │
      ▼
 snapshotRequested = true (原子标志)
      │
      ▼ (渲染循环中检测)
 glReadPixels → RGBA 数据
      │
      ▼
 ImageSaver 异步写入 PNG
      │
      ▼
 JNI → speak("图片已保存")
```

## 视频录制

### 触发方式

1. **手柄按键**：按 DPAD_CENTER 或 Right B 切换开始/停止
2. **ADB 远程命令**：
   ```bash
   # 开始录制
   adb shell am broadcast -a com.ssnwt.helloxr.START_RECORDING
   # 停止录制
   adb shell am broadcast -a com.ssnwt.helloxr.STOP_RECORDING
   ```

### 保存路径

```
/sdcard/Android/data/com.ssnwt.helloxr/files/dataset/<YYYYMMDD_HHMMSS>/
├── rgb.mp4                      # RGB 相机 SBS 视频（左眼左半 + 右眼右半，2W×H）
├── tracking.mp4                 # Tracking 灰度视频
├── ctrl.mp4                     # Ctrl 灰度视频
├── accel.csv                    # 加速度计数据（timestamp_ns, x, y, z）
├── gyro.csv                     # 陀螺仪数据（timestamp_ns, x, y, z）
├── head_pose.csv                # 6DOF 头部姿态（timestamp_ns, pos_x/y/z, quat_x/y/z/w）
├── audio.m4a                    # 音频录音（含 TimedText boottime 时间戳）
├── hand_tracking.csv            # 手部追踪数据（手势模式）
├── controller_poses.csv         # 手柄姿态数据（手柄模式）
├── camera_params_rgb.json       # RGB 相机内参/外参
├── camera_params_tracking.json  # Tracking 相机内参/外参
└── camera_params_ctrl.json      # Ctrl 相机内参/外参
```

### 编码参数

| 参数 | RGB | CV 灰度 |
|------|-----|---------|
| 编码器 | H.265 (HEVC) | H.265 (HEVC) |
| 模式 | Surface（零拷贝） | Surface（零拷贝） |
| 分辨率 | 2W×H（SBS） | W×H |
| 比特率 | 8 Mbps | 4 Mbps |
| 帧率 | 30 fps | 60 fps |

## 语音提示

### 实现方式

设备可能无内置 TTS 引擎，优先尝试 TTS，失败时使用预生成的中文音频文件（通过 gTTS 离线生成）打包到 APK 中通过 SoundPool 播放。

| 事件 | 音频文件 | 中文内容 |
|------|----------|----------|
| 截图成功 | `res/raw/image_saved.mp3` | "图片已保存" |
| 截图失败 | `res/raw/image_failed.mp3` | "图片保存失败" |
| 开始录制 | `res/raw/recording_start.mp3` | "开始录制" |
| 停止录制 | `res/raw/recording_stop.mp3` | "录制已保存" |

`VrNativeActivity.speak()` 通过子串匹配选择音频文件（`text.contains("开始录制")`），TTS 可用时直接朗读传入文本。

## Intent 远程控制

通过 ADB broadcast 可以远程控制截图和录制，适合自动化测试：

```bash
# 截图
adb shell am broadcast -a com.ssnwt.helloxr.SAVE_IMAGE

# 开始录制
adb shell am broadcast -a com.ssnwt.helloxr.START_RECORDING

# 停止录制
adb shell am broadcast -a com.ssnwt.helloxr.STOP_RECORDING
```

### 跨设备远程控制（通过 WiFi ADB）

```bash
# 连接设备
adb connect <设备IP>

# 截图
adb -s <设备IP> shell am broadcast -a com.ssnwt.helloxr.SAVE_IMAGE

# 拉取截图到本地
adb -s <设备IP> pull /sdcard/Android/data/com.ssnwt.helloxr/files/images/ ./images/
```

## 相关文件

| 文件 | 功能 |
|------|------|
| `app/src/main/cpp/stb_image_write.h` | PNG 编码库（单头文件） |
| `app/src/main/cpp/ImageSaver.h/cpp` | 异步 PNG 保存 |
| `app/src/main/cpp/CameraEncoder.h/cpp` | 视频硬件编码 |
| `app/src/main/cpp/EncoderSurface.h/cpp` | EGL Surface + MediaCodec 输入 Surface |
| `app/src/main/cpp/DatasetRecorder.h/cpp` | 数据集录制协调器 |
| `app/src/main/cpp/ImuPoseCollector.h/cpp` | IMU + 6DOF 头部姿态采集 |
| `app/src/main/cpp/AudioEncoder.h/cpp` | Native 音频编码（AAudio + AAC） |
| `app/src/main/cpp/ControllerPoseSaver.h/cpp` | 手柄姿态数据保存 |
| `app/src/main/cpp/RawDateSave.h/cpp` | 手势关节数据保存 |
| `app/src/main/cpp/RootSpaceQCOM.h` | QCOM Root Space 扩展定义 |
| `app/src/main/cpp/main.cpp` | 按键处理、截图逻辑、JNI 桥接 |
| `app/src/main/java/.../VrNativeActivity.java` | TTS/SoundPool、Intent 接收 |
| `app/src/main/res/raw/*.mp3` | 中文语音提示音频 |
