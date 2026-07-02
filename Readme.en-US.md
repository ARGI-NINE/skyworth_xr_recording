# SXR EGO SDK User Guide

## Quick Reference: System Properties and Commands

### System Properties

| Property | Value | Description | Effective method |
|------|------|------|----------|
| `persist.xr.usecontroller` | `true` / `false` | Controller mode / Gesture mode | Restart the app |
| `persist.xr.project_hand` | `1` / `0` | Enable / Disable gesture skeleton projection (only affects encoded video recording, does not affect headset preview) | Restart the app |

```bash
# Check current settings
adb shell getprop persist.xr.usecontroller
adb shell getprop persist.xr.project_hand

# Enable Hand Gesture Mode
adb shell setprop persist.xr.usecontroller false

# Enable Hand Skeleton Projection (Works only under Hand Gesture Mode)
adb shell setprop persist.xr.project_hand 1

# Disable Hand Skeleton Projection
adb shell setprop persist.xr.project_hand 0
```

### ADB remote control commands

```bash
# Capture screenshots (all cameras)
adb shell am broadcast -a com.ssnwt.helloxr.SAVE_IMAGE

# Dataset Recording
adb shell am broadcast -a com.ssnwt.helloxr.START_RECORDING   # Start recording
adb shell am broadcast -a com.ssnwt.helloxr.STOP_RECORDING    # Stop recording
```

### Controller buttons

| Button | Function |
|------|------|
| VOLUME_UP | Screenshot (all cameras) |
| Right B / DPAD_CENTER | Toggle recording start/stop |

### Data path

```bash
# Dataset directory
/sdcard/Android/data/com.ssnwt.helloxr/files/dataset/<YYYYMMDD_HHMMSS>/

# Screenshot directory
/sdcard/Android/data/com.ssnwt.helloxr/files/images/

# Pull data to local storage
adb pull /sdcard/Android/data/com.ssnwt.helloxr/files/dataset/ ./dataset/
```

### Application management

```bash
# Restart application
adb shell am force-stop com.ssnwt.helloxr && sleep 1 && adb shell am start -n com.ssnwt.helloxr/com.ssnwt.helloxr.VrNativeActivity

# View logs
adb logcat | grep "HelloXr"
```

---

## Overview

The SXR Camera API is a set of native APIs for accessing the cameras of XR devices, which are used by dynamically loading the `libsxr_camera_client.so` library.Three camera groups are supported:
- **TRACKING**: Grayscale tracking camera (left and right eye)
- **CTRL**: Grayscale control camera (left and right eye)
- **RGB**: RGB color camera (left and right eyes)

## Core Data Structure

### CameraGroup enumeration

```cpp
enum class CameraGroup : uint8_t {
    TRACKING = 0,   // Grayscale tracking camera (GRAY_LEFT + GRAY_RIGHT)
    CTRL,           // Grayscale control camera (GRAY_LEFT_UP + GRAY_RIGHT_UP)
    RGB,            // RGB color camera (RGB_LEFT + RGB_RIGHT)
};
```

### FrameInfo structure

Metadata information for each frame:

```cpp
struct FrameInfo {
    // Frame metadata
    uint32_t frameId;           // Frame index
    uint64_t timestamp;         // Exposure start timestamp (boottime)
    uint32_t exposure;          // Exposure time
    uint32_t gain;              // Gain value

    // Frame size (after cropping)
    uint32_t width;             // Image width
    uint32_t height;            // Image height
    uint32_t stride;            // Row stride
    uint32_t format;            // Pixel format

    // Crop region (position inside HardwareBuffer)
    uint32_t cropX;
    uint32_t cropY;

    // Intrinsic parameters (static, attached to each frame for easy access)
    float focalX, focalY;       // Focal length
    float centerX, centerY;     // Principal point
    float radialDistortion[8];  // Kannala-Brandt fisheye distortion coefficients

    // Extrinsic parameters (static, pose of camera relative to device)
    float position[3];          // Camera position
    float rotation[4];          // Camera rotation (quaternion: x, y, z, w)
};
```

### FrameData structure

Data received by the frame callback function:

```cpp
struct FrameData {
    CameraGroup group;          // Camera group type

    FrameInfo frames[2];        // [0] = Left eye, [1] = Right eye

    uint8_t hwBufferCount;      // HardwareBuffer count: 1 = Grayscale, 2 = RGB
    AHardwareBuffer* hwBuffer[2];
    // Grayscale camera: hwBuffer[0] contains combined left & right eye data
    // RGB camera: hwBuffer[0] = Left eye, hwBuffer[1] = Right eye
};
```

### FrameCallback callback type

```cpp
typedef void (*FrameCallback)(void* userData, const FrameData* data);
```

## API functions

### SxrCameraApi structure

Dynamically loaded function pointer table:

```cpp
typedef struct SxrCameraApi {
    void* libHandle;                    // Dynamic library handle

    // Function pointers
    SxrCameraCreateFunc create;         // Create context
    SxrCameraDestroyFunc destroy;       // Destroy context
    SxrCameraOpenGroupFunc open_group;  // Open camera group
    SxrCameraCloseGroupFunc close_group;// Close camera group
    SxrCameraIsGroupOpenFunc is_group_open;     // Check if group is opened
    SxrCameraGetGroupInfoFunc get_group_info;   // Get group information
} SxrCameraApi;
```

### Initialize/de-initialize

```cpp
// Initialize API (load dynamic library)
int sxr_camera_api_init(SxrCameraApi* api, const char* libPath);
// Use default path "libsxr_camera_client.so" when libPath is NULL

// Deinitialize API (unload dynamic library)
void sxr_camera_api_deinit(SxrCameraApi* api);

// Check if API is valid
bool sxr_camera_api_is_valid(const SxrCameraApi* api);
```

### Context management

```cpp
// Create camera context
SxrCameraContext* sxr_camera_create(SxrCameraApi* api, JavaVM* vm, jobject activity);

// Destroy camera context
void sxr_camera_destroy(SxrCameraApi* api, SxrCameraContext* ctx);
```

### Camera group operation

```cpp
// Open camera group (start receiving frame callbacks)
int sxr_camera_open_group(SxrCameraApi* api,
                          SxrCameraContext* ctx,
                          SXR::CameraGroup group,
                          SXR::FrameCallback callback,
                          void* userData);

// Close camera group (stop frame callbacks)
int sxr_camera_close_group(SxrCameraApi* api, SxrCameraContext* ctx, SXR::CameraGroup group);

// Check if the camera group is opened
bool sxr_camera_is_group_open(SxrCameraApi* api, SxrCameraContext* ctx, SXR::CameraGroup group);

// Get camera group information
int sxr_camera_get_group_info(SxrCameraApi* api,
                              SxrCameraContext* ctx,
                              SXR::CameraGroup group,
                              uint32_t* maxWidth,
                              uint32_t* maxHeight,
                              uint32_t* format);
```

## Usage examples

### Complete sample code

```cpp
#include "sxr_camera.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "CameraDemo", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "CameraDemo", __VA_ARGS__)

// User data structure
struct MyCameraData {
    int frameCount;
    // ... other user data ...
};

// Frame callback function
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

    // Access frame information
    const SXR::FrameInfo* left = &data->frames[0];
    const SXR::FrameInfo* right = &data->frames[1];

    LOGI("Frame %d: group=%s, left=%dx%d, right=%dx%d, timestamp=%lu",
         myData->frameCount, groupName,
         left->width, left->height,
         right->width, right->height,
         left->timestamp);

    // Access AHardwareBuffer (only valid during callback)
    if (data->hwBuffer[0]) {
        // Grayscale camera: hwBuffer[0] contains combined left & right eye data
        // RGB camera: hwBuffer[0] = Left eye, hwBuffer[1] = Right eye

        // Get buffer description
        AHardwareBuffer_Desc desc;
        AHardwareBuffer_describe(data->hwBuffer[0], &desc);
        LOGI("  Buffer: %ux%u, format=%u, stride=%u",
             desc.width, desc.height, desc.format, desc.stride);

        // Use buffer (upload to texture, encode, etc.)
        // Note: Buffer is only valid during callback. Copy or add reference if long-term use is required.
    }

    // Access camera extrinsic parameters
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
        // 1. Initialize API
        if (sxr_camera_api_init(&mApi, NULL) != 0) {
            LOGE("Failed to load sxr_camera library");
            return false;
        }

        // 2. Create context
        mContext = sxr_camera_create(&mApi, vm, activity);
        if (!mContext) {
            LOGE("Failed to create camera context");
            sxr_camera_api_deinit(&mApi);
            return false;
        }

        // 3. Open camera groups
        mUserData.frameCount = 0;

        // Open grayscale tracking camera
        if (sxr_camera_open_group(&mApi, mContext, SXR::CameraGroup::TRACKING,
                                   onCameraFrame, &mUserData) != 0) {
            LOGE("Failed to open TRACKING group");
        }

        // Open grayscale control camera
        if (sxr_camera_open_group(&mApi, mContext, SXR::CameraGroup::CTRL,
                                   onCameraFrame, &mUserData) != 0) {
            LOGE("Failed to open CTRL group");
        }

        // Open RGB camera
        if (sxr_camera_open_group(&mApi, mContext, SXR::CameraGroup::RGB,
                                   onCameraFrame, &mUserData) != 0) {
            LOGE("Failed to open RGB group");
        }

        return true;
    }

    void cleanup() {
        if (mContext) {
            // Close all camera groups
            sxr_camera_close_group(&mApi, mContext, SXR::CameraGroup::TRACKING);
            sxr_camera_close_group(&mApi, mContext, SXR::CameraGroup::CTRL);
            sxr_camera_close_group(&mApi, mContext, SXR::CameraGroup::RGB);

            // Destroy context
            sxr_camera_destroy(&mApi, mContext);
            mContext = nullptr;
        }

        // Deinitialize API
        sxr_camera_api_deinit(&mApi);
    }
};
```

### Thread safety instructions

```
┌─────────────────────────────────────────────────────────────────┐
│  Thread      │  Responsibilities       │  Called APIs          │
├─────────────────────────────────────────────────────────────────┤
│  Main Thread │ Initialization, Teardown │ api_init, create      │
│              │                          │ destroy, api_deinit   │
├─────────────────────────────────────────────────────────────────┤
│  Camera Thread 1 │ TRACKING group frame callback │ (Process frames in callback) │
├─────────────────────────────────────────────────────────────────┤
│  Camera Thread 2 │ CTRL group frame callback     │ (Process frames in callback) │
├─────────────────────────────────────────────────────────────────┤
│  Camera Thread 3 │ RGB group frame callback      │ (Process frames in callback) │
└─────────────────────────────────────────────────────────────────┘

Notes:
Callback of each camera group runs on an independent thread
Callback functions must be thread-safe
AHardwareBuffer is only valid during callback execution
```

### AHardwareBuffer usage considerations

1. **Lifecycle**:` AHardwareBuffer` is only valid during the frame callback; the buffer may be reused after the callback returns
2. **Do not release**: The buffer is managed by the camera service; do not call `AHardwareBuffer_release()`
3. **Asynchronous processing**: For asynchronous processing, you need to:
   - Use `AHardwareBuffer_acquire() to` increase the reference count
   - Call `AHardwareBuffer_release()` after processing
4. **OpenGL binding**: Use `eglGetNativeClientBufferANDROID()` and `glEGLImageTargetTexture2DOES()` to bind to the texture

---

# Principle of camera pose transformation

## XR coordinate system convention

```
        Y (up)
        |
        |
        |_______ X (right)
       /
      /
     Z (backward)
```

- **Pitch** (around the X-axis): look up/down
- **Yaw** (around the Y-axis): turn left/right
- **Roll** (around the Z-axis): lateral roll

## Quaternion order description

Quaternion storage order for different libraries:

| Library/Structure | Order | Example |
|---------|------|------|
| XrQuaternionf | (x, y, z, w) | `q.x, q.y, q.z, q.w` |
| SxrPose.rotation | (x, y, z, w) | `rot[0], rot[1], rot[2], rot[3]` |
| GLM glm::quat | (w, x, y, z) | `q.w, q.x, q.y, q.z` |

**Transformation example**:
```cpp
// XrPosef → GLM
glm::quat q_glm(xrPose.orientation.w,   // w first
                xrPose.orientation.x,   // x later
                xrPose.orientation.y,
                xrPose.orientation.z);

// SxrPose → GLM
glm::quat q_glm(sxrPose.rotation[3],    // w = rot[3]
                sxrPose.rotation[0],    // x = rot[0]
                sxrPose.rotation[1],
                sxrPose.rotation[2]);
```

## Transformation matrix definition

Use a 4x4 homogeneous transformation matrix to represent the pose: T_AB represents the transformation from coordinate system B to coordinate system A

```
T_AB = | R_AB  t_AB |    
       |  0     1   |    
Where:
R_AB = 3x3 rotation matrix
t_AB = 3x1 translation vector
```


---

## Transformation composition formula

Given two coordinate systems A and B, the pose of B in A (q_ab, p_ab) and the pose of C in B (q_bc, p_bc) are known. Calculate the pose of C in A (q_ac, p_ac):

```cpp
// Rotation combination
glm::quat q_ac = q_ab * q_bc;

// Translation combination
glm::vec3 p_ac = glm::rotate(q_ab, p_bc) + p_ab;
```

---

### Transformation formula

**Goal**: Calculate the pose **T_WS** of the Sensor in the World coordinate system

**Transformation chain**:` World (W) ──T_WV──► View (V) ──T_VS──► Sensor (S)`

**Formula**:` T_WS = T_WV × T_VS`

### Code implementation

```cpp
// 1. Get pose of View in World (T_WV)
xrLocateSpace(xrViewSpace, xrRootSpace, timestamp, &location);
glm::quat q_wv(location.pose.orientation.w,
               location.pose.orientation.x,
               location.pose.orientation.y,
               location.pose.orientation.z);
glm::vec3 p_wv(location.pose.position.x,
               location.pose.position.y,
               location.pose.position.z);

// 2. Get extrinsic parameters - pose of Sensor in View (T_VS)
glm::quat q_vs(extrinsic.orientation.w,
               extrinsic.orientation.x,
               extrinsic.orientation.y,
               extrinsic.orientation.z);
glm::vec3 p_vs(extrinsic.position.x,
               extrinsic.position.y,
               extrinsic.position.z);

// 3. Combine transformations: calculate pose of Sensor in World (T_WS)
glm::quat q_ws = q_wv * q_vs;
glm::vec3 p_ws = glm::rotate(q_wv, p_vs) + p_wv;
```

### Coordinate system relationship diagram

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

### Extrinsic parameter data source

The extrinsic parameters of the grayscale camera come from `SXR::FrameInfo`:

```cpp
struct FrameInfo {
    // Extrinsic parameters - Pose of Camera in device coordinate system (T_VC)
    float position[3];          // Camera position
    float rotation[4];          // Camera rotation (quaternion: x, y, z, w)
    // ...
};
```

### Transformation formula

**Goal**: Calculate the pose **T_WC** of the Camera in the World coordinate system

**Transformation chain**:` World (W) ──T_WV──► View (V) ──T_VC──► Camera (C)`

**Formula**:` T_WC = T_WV × T_VC`

### Code implementation

```cpp
// 1. Get pose of View in World (T_WV)
xrLocateSpace(xrViewSpace, xrRootSpace, timestamp, &location);
glm::quat q_wv(location.pose.orientation.w,
               location.pose.orientation.x,
               location.pose.orientation.y,
               location.pose.orientation.z);
glm::vec3 p_wv(location.pose.position.x,
               location.pose.position.y,
               location.pose.position.z);

// 2. Get extrinsic parameters - pose of Camera in View (T_VC)
// Note: SxrPose.rotation order is (x, y, z, w), while GLM uses (w, x, y, z)
glm::quat q_vc(frameInfo.rotation[3],  // w
               frameInfo.rotation[0],  // x
               frameInfo.rotation[1],  // y
               frameInfo.rotation[2]); // z
glm::vec3 p_vc(frameInfo.position[0],
               frameInfo.position[1],
               frameInfo.position[2]);

// 3. Combine transformations: calculate pose of Camera in World (T_WC)
glm::quat q_wc = q_wv * q_vc;
glm::vec3 p_wc = glm::rotate(q_wv, p_vc) + p_wv;
```

### Grayscale camera configuration

The system supports 4 grayscale cameras (SXR::CAME_MAX = 4):

```
┌─────────────────┬──────────────┐
│  Enum Value     │  Position    │
├─────────────────┼──────────────┤
│  GRAY_LEFT (0)  │  Left grayscale camera │
│  GRAY_RIGHT (1) │  Right grayscale camera │
│  GRAY_LEFT_UP(2)│  Top-left grayscale camera │
│  GRAY_RIGHT_UP(3)│ Top-right grayscale camera │
└─────────────────┴──────────────┘
```

The calibration information for each camera includes:
- `position[3]` - The position of the camera in the device coordinate system
- `rotation[4]` - Camera rotation in the device coordinate system (quaternion)
- `width`, `height` - Image resolution

### Timestamp conversion

The grayscale camera uses the `boottime` timestamp, which needs to be converted to OpenXR's `XrTime`:

```cpp
XrTime boottimeToXrTime(uint64_t boottime_ns) {
    // 1. Calculate offset between CLOCK_BOOTTIME and CLOCK_MONOTONIC
    // 2. Convert boottime to monotonic time
    // 3. Convert to XrTime via xrConvertTimespecTimeToTimeKHR
}
```

Reason:
- Camera frame timestamp:` CLOCK_BOOTTIME` (including sleep time)
- OpenXR positioning: requires `XrTime` (based on `CLOCK_MONOTONIC`)

---

## Intrinsic parameter description

Camera intrinsic parameters are used for image distortion correction and 3D projection calculation:

Intrinsic parameters in FrameInfo:
```cpp
float focalX, focalY;       // Focal length
float centerX, centerY;     // Principal point
float radialDistortion[8];  // Kannala-Brandt fisheye distortion coefficients
```

---

## Gesture projection to RGB image

### Projection link

2D pixel coordinates for projecting the gesture joint (Root Space) onto the RGB camera image:

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
    │ rotate_uv_90cw (Rotate 90° clockwise around image center)
    ▼
Final pixel (u', v')
```

### Coordinate transformation

**The World → Camera** transformation uses a quaternion chain rotated by the extrinsic parameters:

```
R_WC = conj(extQuat) × conj(headQuat)
cam   = quatRotate(R_WC, joint - wcPos)
```

Where `headQuat` is the view→world rotation (OpenXR view space: X=right, Y=up, Z=backward), and` extQuat` is the camera→view extrinsic rotation.

**The camera's world position** uses device/IMU pose + extrinsic translation:

```
wcPos = devicePos + quatRotate(deviceQuat, extPos)
```

**After KB projection**, apply a 90° clockwise 2D rotation to match the Python reference implementation:

```cpp
u_rot = centerX + (v - centerY);
v_rot = centerY - (u - centerX);
```


### UV pixel offset

After the projection is completed, a per-eye UV offset can be applied for fine-tuning calibration (default is 0):

```cpp
handOverlay.setUVOffset(0, 0.0f, 0.0f);  // Left eye
handOverlay.setUVOffset(1, 0.0f, 0.0f);  // Right eye
```

### Enable/Disable

Gesture projection is controlled by the system property `persist.xr.project_hand`. It** is disabled by default** and only takes effect during encoding and recording (does not affect the headset preview screen):

```bash
# Check current status
adb shell getprop persist.xr.project_hand

# Enable projection (skeleton overlay will be drawn in encoded videos)
adb shell setprop persist.xr.project_hand 1

# Disable projection
adb shell setprop persist.xr.project_hand 0
```

Only takes effect in** gesture mode** (`persist.xr.usecontroller=false`); does not render in controller mode.After setting, the application needs to be restarted for it to take effect.

---

# Gesture Data Saving Function

## Overview

This application supports real-time capture and saving of gesture joint data. It obtains the position and orientation information of 26 joint points of both hands through the OpenXR Hand Tracking extension and saves it in CSV format to the device storage.

## Dependent OpenXR extensions

```
┌───────────────────────────────────────────────────────────────┐
│  Extension Name                              │  Function        │
├───────────────────────────────────────────────────────────────┤
│  XR_EXT_HAND_TRACKING_EXTENSION_NAME         │  Basic hand tracking │
│  XR_MSFT_HAND_TRACKING_MESH_EXTENSION_NAME   │  Hand mesh │
│  XR_QCOM_HAND_TRACKING_GESTURE_EXTENSION_NAME│  QCOM hand gesture recognition │
│  XR_EXT_hand_interaction                     │  Hand interaction │
│  XR_EXT_palm_pose                            │  Palm pose │
│  XR_MSFT_hand_interaction                    │  MSFT hand interaction │
└───────────────────────────────────────────────────────────────┘
```

API Layer: `XR_APILAYER_QCOM_handtracking`

## Core Components

### HandTrackerLogic structure

Core logic responsible for gesture tracking:

```cpp
struct HandTrackerLogic {
    // Hand tracker handles
    XrHandTrackerEXT LeftHandTrackerHandle;   // Left hand tracker
    XrHandTrackerEXT RightHandTrackerHandle;  // Right hand tracker

    // Joint position data (26 joints per hand)
    XrHandJointLocationEXT LeftHandJointLocations[XR_HAND_JOINT_COUNT_EXT];
    XrHandJointLocationEXT RightHandJointLocations[XR_HAND_JOINT_COUNT_EXT];

    // Status flags
    bool LeftHandIsActive;
    bool RightHandIsActive;

    // Data saver
    RawDateSave* rawDateSave;
};
```

### Input class

Handles OpenXR Action input and gesture updates:

```cpp
class Input {
    void Init(XrInstance instance, XrSession session, XrSpace space);
    void UpdateInput(const XrFrameState& frameState);
};
```

### RawDateSave class

Responsible for asynchronously saving gesture data to a file, supporting CSV output based on recorded sessions:

```cpp
class RawDateSave {
    void Init(const std::string& savePath);
    void Shutdown();
    void Resume();  // Start / Resume saving
    void Pause();   // Pause saving
    void SaveFrame(const FrameData& frameData);  // Save one frame of data (write only when session is active)

    // Recording session management
    bool StartNewSession(const std::string& csvPath);  // Start new session and specify CSV path
    void StopSession();                                 // Stop session, flush and close CSV file
    bool IsSessionActive() const;                       // Check if session is active
};
```

## Gesture joint definition

Each hand contains 26 joint points (XR_HAND_JOINT_COUNT_EXT = 26):

```
┌────────────────────────────────────────────────────────────────┐
│  Index │  Joint Name           │  Description                   │
├───────┼────────────────────────┼───────────────────────────────┤
│   0   │  PALM                  │  Palm center                   │
│   1   │  WRIST                 │  Wrist                         │
│   2   │  THUMB_METACARPAL      │  Thumb metacarpal              │
│   3   │  THUMB_PROXIMAL        │  Thumb proximal phalanx        │
│   4   │  THUMB_DISTAL          │  Thumb distal phalanx          │
│   5   │  THUMB_TIP             │  Thumb tip                     │
│   6   │  INDEX_METACARPAL      │  Index finger metacarpal       │
│   7   │  INDEX_PROXIMAL        │  Index finger proximal phalanx │
│   8   │  INDEX_INTERMEDIATE    │  Index finger middle phalanx   │
│   9   │  INDEX_DISTAL          │  Index finger distal phalanx   │
│  10   │  INDEX_TIP             │  Index finger tip              │
│  11   │  MIDDLE_METACARPAL     │  Middle finger metacarpal     │
│  12   │  MIDDLE_PROXIMAL       │  Middle finger proximal phalanx │
│  13   │  MIDDLE_INTERMEDIATE   │  Middle finger middle phalanx  │
│  14   │  MIDDLE_DISTAL         │  Middle finger distal phalanx  │
│  15   │  MIDDLE_TIP            │  Middle finger tip             │
│  16   │  RING_METACARPAL       │  Ring finger metacarpal        │
│  17   │  RING_PROXIMAL         │  Ring finger proximal phalanx  │
│  18   │  RING_INTERMEDIATE     │  Ring finger middle phalanx    │
│  19   │  RING_DISTAL           │  Ring finger distal phalanx    │
│  20   │  RING_TIP              │  Ring finger tip               │
│  21   │  LITTLE_METACARPAL     │  Little finger metacarpal      │
│  22   │  LITTLE_PROXIMAL       │  Little finger proximal phalanx │
│  23   │  LITTLE_INTERMEDIATE   │  Little finger middle phalanx  │
│  24   │  LITTLE_DISTAL         │  Little finger distal phalanx  │
│  25   │  LITTLE_TIP            │  Little finger tip             │
└───────┴────────────────────────┴───────────────────────────────┘
```

## Joint data structure

Each joint contains the following information:

```cpp
struct HandJointPosition {
    float position[3];       // Position (x, y, z) - Root Space coordinate system
    float orientation[4];    // Pose quaternion (x, y, z, w)
    float radius;            // Joint radius (for collision detection)
};

struct HandFrameData {
    bool isActive;           // Whether the hand is detected
    uint32_t jointCount;     // Joint quantity (26)
    HandJointPosition joints[26]; // Data of all joints
};

struct FrameData {
    uint32_t frameNumber;    // Frame index
    XrTime timestamp;        // boottime timestamp (nanosecond)
    bool hasLeftHand;        // Whether left hand data exists
    bool hasRightHand;       // Whether right hand data exists
    HandFrameData leftHand;  // Left hand data
    HandFrameData rightHand; // Right hand data
};
```

## Data saving format

### Storage path

Gesture data is saved in the recorded dataset directory:

```
/storage/emulated/0/Android/data/com.ssnwt.helloxr/files/dataset/<YYYYMMDD_HHMMSS>/
├── hand_tracking.csv       # Hand joint data
└── ...
```

### CSV file format

One row per frame, with all joint data for the left and right hands arranged in columns:

```csv
frame_number,timestamp,left_active,right_active,left_joint0_id,left_joint0_name,left_joint0_radius,left_joint0_pos_x,left_joint0_pos_y,left_joint0_pos_z,left_joint0_orientation_x,...,right_joint25_orientation_w
0,123456789012345678,1,1,0,PALM,0.01,0.1,0.2,0.3,0.0,...,0.35
...
```

**Field description**:
- `frame_number`: Frame sequence number (uint32)
- `timestamp`: boottime timestamp (nanoseconds, int64)
- `left_active`/`right_active`: Whether the hand is active (1=active, 0=inactive)
- 26 joints per hand, 10 fields per joint:` {left|right}_joint{i}_id`,`_name`,`_radius`,`_pos_x`,`_pos_y`,`_pos_z`,`_orientation_x`,`_orientation_y`,`_orientation_z`,`_orientation_w`
- When the hand is not active, the joint fields are filled with:` -1,"",0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0`

## Coordinate System Description

Gesture joint data uses **Root Space (world coordinate system)**:

```
        Y (up)
        |
        |
        |_______ X (right)
       /
      /
     Z (backward)
```

- Position unit: meters
- Quaternion order: (x, y, z, w)

### Global pose correction (coordinate space consistency)

Automatically attempts to create QCOM Root Space (`xrCreateRootSpaceQCOM`) when the application starts, and if creation is successful, it is enabled globally.

```
┌─────────────────────────────────────────────────────────────────┐
│  Space             │  Type                          │  Usage       │
├─────────────────────────────────────────────────────────────────┤
│  xrRootSpace       │  QCOM extension Root Space        │  Global SLAM positioning │
│  xrLocalSpace      │  XR_REFERENCE_SPACE_TYPE_LOCAL │  Scene rendering │
│  xrViewSpace       │  XR_REFERENCE_SPACE_TYPE_VIEW  │  View locking │
└─────────────────────────────────────────────────────────────────┘
```

Space link of the rendering pipeline:

```
xrLocateViews(space=xrRootSpace or xrLocalSpace, depends on useRootSpace)
    → view matrix
    → model/view/projection transformation during rendering
    → Submit projection layer (space=xrRootSpace or xrLocalSpace)
```

#### Automatic Root Space detection

In `engine_init_openxr`, the application attempts to create a Root Space through the QCOM extension:

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

#### Dynamic selection of baseSpace

`HandTrackerLogic` and the rendering pipeline dynamically select the reference space based on the `useRootSpace` flag:

```cpp
// HandTrackerLogic::UpdateLeftHand / UpdateRightHand
HandJointsLocateInfo.baseSpace = engine->useRootSpace
    ? engine->state.xrRootSpace    // Root Space (Global SLAM coordinate system)
    : engine->state.xrLocalSpace;  // Local Space (Local coordinate system)

// xrLocateViews
viewLocateInfo.space = engine->useRootSpace
    ? engine->state.xrRootSpace
    : engine->state.xrLocalSpace;
```

**Key point**: The gesture joint positioning and the rendering pipeline always use the same reference space to ensure the correct display position.When Root Space is available, all data (gestures, IMU pose, etc.) is in the global coordinate system, suitable for dataset collection.

## Rendering visualization

The headset preview does not render the skeleton.When encoding and recording, if `persist.xr.project_hand=1`, a 2D skeleton overlay (blue left hand, red-orange right hand, including skeleton connections and joint points) will be superimposed on the SBS video screen.

## Asynchronous saving mechanism

To avoid blocking the rendering thread, data saving uses a separate worker thread:

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│  Render Thread  │     │  Data Queue     │     │  Saving Thread  │
│                 │     │                 │     │                 │
│  Update()       │────►│  FrameQueue     │────►│  SaveWorker     │
│  SaveFrame()    │     │  (Thread-safe)  │     │  Write to CSV   │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

## Usage example

### Initialization

```cpp
// Inside engine_init_openxr
engine->inputPtr = std::make_unique<Input>();
engine->inputPtr->Init(instance, session,
    engine->useRootSpace ? engine->state.xrRootSpace : engine->state.xrLocalSpace);
engine->mHandTrackerLogic.Init();  // Create and initialize RawDateSave internally
```

### Input mode selection

The application decides whether to use controller mode or gesture mode through the system property `persist.xr.usecontroller`:

```cpp
engine.useControllerMode = readUseControllerProperty();  // Read persist.xr.usecontroller
if (!engine.useControllerMode) {
    engine.mHandTrackerLogic.Init();       // Hand tracking mode
} else {
    engine.mControllerPoseSaver.Init(storagePath);  // Controller pose mode
}
```

### Update per frame

```cpp
// Inside main loop
engine.inputPtr->UpdateInput(frameState);
if (!engine.useControllerMode) {
    engine.mHandTrackerLogic.Update(frameState);  // Hand tracking mode
} else if (engine.mDatasetRecorder.isRecording()) {
    // Manually save controller pose under controller mode
    ControllerPoseRecord rec;
    // ... fill rec ...
    engine.mControllerPoseSaver.SaveFrame(rec);
}
```

### Recording session management

```cpp
// Start recording (triggered by button or Intent)
engine.mDatasetRecorder.start();
engine.mHandTrackerLogic.rawDateSave->StartNewSession(
    engine.mDatasetRecorder.getHandTrackingCsvPath());  // hand_tracking.csv

// Stop recording
engine.mHandTrackerLogic.rawDateSave->StopSession();
engine.mDatasetRecorder.stop();
```

## Notes

1. **Permission requirements**: `android.permission.WRITE_EXTERNAL_STORAGE permission is` required
2. **Storage space**: Long-term operation will generate a large amount of data; pay attention to managing storage space
3. **Performance impact**: Asynchronous saving mechanism ensures that XR rendering performance is not affected
4. **Data validity**: Check the `isActive` flag to determine whether the gesture is correctly recognized


---

# Dataset Recording System

## Overview

The dataset recording system synchronously collects all sensor and video data into a timestamped directory for offline data processing and SLAM algorithm development.

## Dataset directory structure

```
/storage/emulated/0/Android/data/com.ssnwt.helloxr/files/
├── dataset/
│   ├── 20260518_143025/                          # Recording Session 1
│   │   ├── rgb.mp4                               # RGB camera SBS video (2W×H, H.265)
│   │   │   ├── Track 1: TimedText (per-frame boottime ns timestamp)
│   │   │   └── Track 2: Video (H.265, left eye on left half + right eye on right half)
│   │   ├── tracking.mp4                          # Tracking grayscale video (W×H, H.265)
│   │   │   ├── Track 1: TimedText
│   │   │   └── Track 2: Video
│   │   ├── ctrl.mp4                              # Ctrl grayscale video (W×H, H.265)
│   │   │   ├── Track 1: TimedText
│   │   │   └── Track 2: Video
│   │   ├── audio.m4a                             # AAC audio (44.1kHz, mono, 96kbps)
│   │   │   ├── Track 1: Audio (AAC-LC)
│   │   │   └── Track 2: TimedText (audio frame boottime ns timestamp)
│   │   ├── accel.csv                             # Accelerometer (timestamp_ns, x, y, z)  [m/s²]
│   │   ├── gyro.csv                              # Gyroscope (timestamp_ns, x, y, z)    [rad/s]
│   │   ├── head_pose.csv                         # Head 6DOF (timestamp_ns, pos_x/y/z, quat_x/y/z/w)
│   │   ├── hand_tracking.csv                     # Hand joint data (Hand Gesture Mode, mutually exclusive with the next line)
│   │   ├── controller_poses.csv                  # Controller pose data (Controller Mode)
│   │   ├── camera_params_rgb.json                # RGB camera intrinsic/extrinsic parameters
│   │   ├── camera_params_tracking.json           # Tracking camera intrinsic/extrinsic parameters
│   │   ├── camera_params_ctrl.json               # Ctrl camera intrinsic/extrinsic parameters
│   │   └── time_offset.json                      # BOOTTIME → REALTIME(UTC) time offset samples
│   │
│   ├── 20260518_150312/                          # Recording Session 2
│   │   └── ...                                   # Same structure as above
│   └── 20260518_161027/                          # Recording Session 3
│       └── ...
│
└── images/                                       # Screenshot directory (triggered by VOLUME_UP)
    ├── rgb_20260428_101208.png                   # Combined RGB left & right eye (4656×1748)
    ├── tracking_20260428_101208.png              # Combined Tracking left & right eye (1280×480)
    └── ctrl_20260428_101208.png                  # Combined Ctrl left & right eye (1280×480)
```

### Data Stream and Timestamp System

```
                    boottime (CLOCK_BOOTTIME, nanosecond)
                           │
          ┌────────────────┼────────────────────────┐
          │                │                        │
     IMU Sensor        Camera Frame          Render Frame (XrTime)
     accel/gyro.csv    *.mp4 TimedText         head_pose/hand
     timestamp_ns      (raw ns value)         Snapshot cache
          │                │                        │
          │                └──────┬─────────────────┘
          │                       │ RGB callback reads snapshot
          │                       ▼
          │               head_pose.csv   (Same timestamp as RGB, 30fps)
          │               hand_tracking.csv (Same timestamp as RGB, 30fps)
          │
          └── Collected independently, not restricted by RGB frame rate
```

> **Timestamp alignment**: The timestamps of head_pose.csv and hand_tracking.csv are exactly the same as the TimedText Track timestamps of rgb.mp4 (both are the boottime in nanoseconds provided by the camera driver).The rendering thread updates the latest sensor snapshot.
> The RGB camera callback thread reads the snapshot and writes to the CSV when encoding each frame, ensuring that the timestamps of the three data streams are strictly aligned.
>
> **Timestamp conversion**: The timestamps in all files are `CLOCK_BOOTTIME` (the number of nanoseconds since the device was powered on).During recording, `DatasetRecorder`
> simultaneously samples `CLOCK_BOOTTIME` and `CLOCK_REALTIME` (NTP-synchronized UTC time) at a frequency of 1 Hz, calculates the offset, and writes it to
> `time_offset.json`.Post-processing converts to an absolute UTC timestamp using the formula `utc_ns = boottime_ns + offset_ns`, facilitating time synchronization between multiple devices.The offset value of the nearest point between adjacent offset sampling points is taken.

### time_offset.json format

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

- **Sampling frequency**: 1 Hz (once per second, with the first point sampled immediately at the start of recording)
- **Conversion method**: Find the offset sampling point closest to `boottime_ns` and use its `offset_ns` to calculate `utc_ns = boottime_ns + offset_ns`
- **Stability**: `offset_ns` fluctuates < 100 ns during normal recording (only inherent jitter between two `clock_gettime` calls)

### File size reference (approximately 10 seconds of recording)

| File | Size estimate | Description |
|------|----------|------|
| rgb.mp4 | ~10 MB | 8Mbps, 30fps, SBS |
| tracking.mp4 | ~5 MB | 4 Mbps, 60 fps |
| ctrl.mp4 | ~5 MB | 4Mbps, 60fps |
| audio.m4a | ~120 KB | 96 kbps, mono |
| accel.csv | ~300 KB | ~2kHz sampling |
| gyro.csv | ~300 KB | ~2kHz sampling |
| head_pose.csv | ~50 KB | 30fps |
| hand_tracking.csv | ~100 KB | 30fps, 52 joints × 10 fields |
| camera_params_*.json | ~1 KB | First frame only, static parameters |
| time_offset.json | ~0.5 KB | 1Hz offset sampling |

## Core components

### DatasetRecorder

Central coordinator that manages the creation of the recording dataset directory and the start and stop of data collection:

```cpp
class DatasetRecorder {
    void init(const std::string& basePath);
    bool start();     // Create dataset/<YYYYMMDD_HHMMSS>/ directory, start IMU, audio and head pose collection
    void stop();      // Stop all collectors and flush data

    std::string getDatasetDir() const;           // Dataset directory path
    std::string getHandTrackingCsvPath() const;  // hand_tracking.csv path
    std::string getControllerPoseCsvPath() const;// controller_poses.csv path
    std::string getAudioPath() const;            // audio.m4a path

    // Asynchronously save head pose from rendering thread (non-blocking)
    void saveHeadPose(int64_t boottimeNs, const XrPosef& pose);
};
```

`saveHeadPose() is` called in each frame rendering loop to asynchronously write the device/IMU pose returned `by xrLocateSpace(viewSpace)` to `head_pose.csv` with the boottime timestamp.The camera extrinsic parameters are in the device coordinate system, and the device pose is used to ensure that `the T_WC = T_WD × T_DC` transformation chain is correct.The timestamp is converted via `xrTimeToBoottime` (if the extension is available) to ensure it is in the same clock domain as the camera frame timestamp.

### ImuPoseCollector

Collects IMU sensor data (accelerometer and gyro) and outputs it to `accel.csv` and `gyro.csv` respectively:

```cpp
class ImuPoseCollector {
    bool start(const std::string& accelCsvPath, const std::string& gyroCsvPath);
    void stop();
};
```

**Thread model**:
- `sensorThreadFunc`: Listens to accelerometer and gyro events via ALooper (fastest sampling rate), and queues events
- `writerThreadFunc`: Takes events from the queue and writes them to `accel.csv` or `gyro.csv` according to type

**accel.csv format**:
```csv
timestamp_ns,x,y,z
```

**gyro.csv format**:
```csv
timestamp_ns,x,y,z
```

- Accelerometer unit: m/s²
- Gyro unit: rad/s
- Timestamp: sensor event timestamp (boottime in nanoseconds)

### AudioEncoder

Native layer audio encoder, which uses AAudio to collect PCM data and encodes it to AAC via AMediaCodec, while saving the boottime nanosecond timestamp to the TimedText Track:

```cpp
class AudioEncoder {
    AudioEncoder(int sampleRate = 44100, int bitRate = 96000, int channelCount = 1);
    bool start(const std::string& outputPath);
    void stop();
};
```

**Audio parameters**:

```
┌─────────────────────┬─────────────────┐
│  Parameter          │  Value          │
├─────────────────────┼─────────────────┤
│  Sample Rate        │  44100 Hz       │
│  Bit Rate           │  96000 bps      │
│  Channel Count      │  1 (Mono)       │
│  Encoder            │  AAC-LC         │
│  MIME               │  audio/mp4a-latm│
│  Input              │  AAudio PCM_I16 │
└─────────────────────┴─────────────────┘
```

**Timestamp scheme**: The same TimedText Track scheme as the video encoder. The absolute boottime timestamp of each audio frame (`mBoottimeBaseNs + ptsUs * 1000`) is written to TimedText as a pure numeric string.

### ControllerPoseSaver

Saves the position and rotation data of the left and right controllers in controller mode:

```cpp
class ControllerPoseSaver {
    void Init(const std::string& savePath);
    bool StartSession(const std::string& csvPath);
    void StopSession();
    bool SaveFrame(const ControllerPoseRecord& record);
};
```

**CSV format**:
```csv
frame_number,timestamp_ns,left_active,left_px,left_py,left_pz,left_qx,left_qy,left_qz,left_qw,right_active,right_px,right_py,right_pz,right_qx,right_qy,right_qz,right_qw
```

### RootSpaceQCOM

Type definitions and function pointers for the QCOM extension Root Space:

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

Root Space provides global SLAM positioning, and the pose data is not reset with Recenter in the global coordinate system.

### Camera Params Save

After each recording starts, the internal/external parameters are automatically saved to JSON when a valid frame is first received in the camera frame callback:

```
dataset/<YYYYMMDD_HHMMSS>/camera_params_rgb.json
dataset/<YYYYMMDD_HHMMSS>/camera_params_tracking.json
dataset/<YYYYMMDD_HHMMSS>/camera_params_ctrl.json
```

JSON format:
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

## Recording process

```
Button/Intent → nativeStartRecording()
    │
    ├─ DatasetRecorder.start()
    │   ├─ Create directory: dataset/<YYYYMMDD_HHMMSS>/
    │   ├─ AudioEncoder.start() → audio.m4a
    │   ├─ ImuPoseCollector.start() → accel.csv + gyro.csv
    │   └─ Head pose writer thread → head_pose.csv
    │
    ├─ encodingEnabled = true, encodersStopped = false
    ├─ cameraParamsSavedRgb/Tracking/Ctrl = false
    ├─ encoderBaseDir = getDatasetDir()
    │
    ├─ if (controllerMode)
    │   └─ ControllerPoseSaver.StartSession() → controller_poses.csv
    │   else
    │   └─ RawDataSave.StartNewSession() → hand_tracking.csv
    │
    └─ ttsSpeak("Recording started")

Per Frame in Render Loop:
    │
    ├─ DatasetRecorder.saveHeadPose(boottimeNs, devicePose) → head_pose.csv
    │   ├─ devicePose = xrLocateSpace(viewSpace, rootSpace, predictedDisplayTime)
    │   └─ boottimeNs = xrTimeToBoottime(predictedDisplayTime)
    ├─ Camera Callback → Encoder → rgb.mp4 / tracking.mp4 / ctrl.mp4
    ├─ Camera Callback → saveCameraParams() → camera_params_*.json (on first valid frame)
    └─ ControllerPoseSaver.SaveFrame() / RawDataSave.SaveFrame()

Button/Intent → nativeStopRecording()
    │
    ├─ encodingEnabled = false
    ├─ ControllerPoseSaver/RawDataSave.StopSession()
    ├─ DatasetRecorder.stop()
    │   ├─ AudioEncoder.stop()
    │   ├─ ImuPoseCollector.stop()
    │   └─ Stop head pose writer thread
    ├─ stopEncoder() (async thread), encoderBaseDir.clear()
    └─ ttsSpeak("Recording saved")
```

---

# Device Hardware Test Function

## Overview

This application integrates the hardware testing functions of XR devices, including LED indicator control, audio recording, battery status monitoring, and button event monitoring.

## Dependency library

```
┌───────────────────────────────────────────────────────────────┐
│  Library File                              │  Function                  │
├───────────────────────────────────────────────────────────────┤
│  svr_plugin_android_api.aar          │  SVR device management API     │
│  gson-2.8.0.jar                      │  JSON serialization/deserialization   │
└───────────────────────────────────────────────────────────────┘
```

## Core components

### AndroidInterface initialization

Use the SVR Android SDK to initialize the device interface:

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

## LED Indicator Control

### Function Description

Control the RGB LED indicators on the device via `DeviceUtils`:

| Method | LED color | Effect |
|------|---------|------|
| `flashRedLight()` | Red | Always on |
| `flashGreenLight()` | Green | Always on |
| `flashBlueLight()` | Blue | Always on |
| `blinkRedLed()` | Red | Flashing |
| `blinkGreenLed()` | Green | Flashing |
| `blinkBlueLed()` | Blue | Flashing |

### Implementation Code

```java
/**
 * LED Type Definition
 * 1 = Red LED
 * 2 = Green LED
 * 3 = Blue LED
 */
private void flashLed(int type) {
    AndroidInterface.getInstance().getDeviceUtils().flashLed(type);
}

private void blinkLed(int type) {
    // Blink interval: 100ms on, 100ms off
    AndroidInterface.getInstance().getDeviceUtils().blinkLed(type, 100, 100);
}
```

## Audio recording function

### Function description

Use `MediaRecorder` to record microphone audio, with the output format being an AAC-encoded M4A file.

### Implementation code

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

    // Output file path
    String filepath = getExternalCacheDir().getAbsolutePath() + File.separator
        + System.currentTimeMillis() + ".m4a";
    mRecorder.setOutputFile(filepath);

    // Audio parameters
    mRecorder.setAudioSamplingRate(44100);      // Sample rate: 44.1kHz
    mRecorder.setAudioEncodingBitRate(96000);   // Bit rate: 96kbps

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

### Recording parameters

```
┌─────────────────────┬─────────────────┐
│  Parameter               │  Value             │
├─────────────────────┼─────────────────┤
│  Audio Source             │  MIC            │
│  Output Format           │  MPEG_4         │
│  Encoder             │  AAC            │
│  Sample Rate             │  44100 Hz       │
│  Bit Rate             │  96000 bps      │
│  File Extension         │  .m4a           │
└─────────────────────┴─────────────────┘
```

## Battery status monitoring

### Function description

Use `BroadcastReceiver` to monitor changes in the system battery status and obtain detailed battery information.

### BatteryInfo data structure

```java
private class BatteryInfo {
    private int status;          // Battery status (charging/discharging/full etc.)
    private int health;          // Battery health (normal/overheated/cold etc.)
    private boolean present;     // Battery presence
    private int level;           // Current battery level (percentage)
    private int scale;           // Full scale value
    private int plugged;         // Charging type (USB/AC)
    private int voltage;         // Voltage (mV)
    private int temperature;     // Temperature (0.1°C)
    private String technology;   // Battery technology (e.g. Li-ion)
    private int capacity;        // Capacity (mAh)
    private int current;         // Current (mA)
}
```

### Battery status enumeration

```
┌───────────────────────────────────────────────────────────────┐
│  Status Type                           │  Value               │
├───────────────────────────────────────────────────────────────┤
│  BATTERY_STATUS_UNKNOWN                │  1 (Unknown)         │
│  BATTERY_STATUS_CHARGING               │  2 (Charging)        │
│  BATTERY_STATUS_DISCHARGING            │  3 (Discharging)     │
│  BATTERY_STATUS_FULL                   │  4 (Fully Charged)   │
│  BATTERY_STATUS_NOT_CHARGING           │  5 (Not Charging)    │
├───────────────────────────────────────────────────────────────┤
│  BATTERY_HEALTH_UNKNOWN                │  1 (Unknown)         │
│  BATTERY_HEALTH_GOOD                   │  2 (Good)            │
│  BATTERY_HEALTH_OVERHEAT               │  3 (Overheated)      │
│  BATTERY_HEALTH_DEAD                   │  4 (Damaged)         │
│  BATTERY_HEALTH_OVER_VOLTAGE           │  5 (Over Voltage)    │
│  BATTERY_HEALTH_UNSPECIFIED_FAILURE    │  6 (Retrieval Failed)│
│  BATTERY_HEALTH_COLD                   │  7 (Too Cold)        │
├───────────────────────────────────────────────────────────────┤
│  PLUGGED_NONE                          │  0 (Unplugged)       │
│  PLUGGED_AC                            │  1 (AC Power)        │
│  PLUGGED_USB                           │  2 (USB)             │
└───────────────────────────────────────────────────────────────┘
```

### Broadcast receiver implementation

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

            // Retrieve additional properties via BatteryManager
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

### Lifecycle management

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

## System event monitoring

### SystemEventUtils.Listener interface

Implement `the SystemEventUtils.Listener` interface to receive system event callbacks:

```java
public class VrNativeActivity extends NativeActivity implements SystemEventUtils.Listener {

    @Override
    public void onStartHome() {
        // Home key pressed
    }

    @Override
    public void onStartQuickMenu(String s) {
        // Quick menu opened
    }

    @Override
    public void onRecenter() {
        // Recenter event
    }

    @Override
    public void onOtherCommand(int i, int i1, String s) {
        // Other commands
    }

    @Override
    public void onKeyEvent(int keycode, KeyEvent event) {
        // Key event
        Log.d(TAG, "onKeyEvent code:" + keycode + ", action:" + event.getAction());
    }
}
```

## String resources

The localized strings for the battery information display are defined in `app/src/main/res/values/arrays.xml`:

```xml
<!-- Battery Information Titles -->
<string-array name="case_battery_info">
    <item>Battery Status:</item>
    <item>Health Status:</item>
    <item>Valid Information:</item>
    <item>Battery Level:</item>
    <item>Max Capacity:</item>
    <item>Power Connection:</item>
    <item>Voltage:</item>
    <item>Temperature:</item>
    <item>Capacity:</item>
    <item>Current:</item>
    <item>Power Status:</item>
    <item>Battery Technology:</item>
</string-array>

<!-- Battery Status -->
<string-array name="case_battery_status">
    <item>Unknown</item>
    <item>Charging</item>
    <item>Discharging</item>
    <item>Fully Charged</item>
    <item>Not Charging</item>
</string-array>

<!-- Battery Health Status -->
<string-array name="case_battery_health">
    <item>Unknown</item>
    <item>Good</item>
    <item>Overheated</item>
    <item>Damaged</item>
    <item>Over Voltage</item>
    <item>Failed to Retrieve Info</item>
    <item>Too Cold</item>
</string-array>

<!-- Battery Level -->
<string-array name="case_battery_level">
    <item>Normal</item>
    <item>Low Battery</item>
    <item>Critically Low</item>
</string-array>

<!-- Charging Type -->
<string-array name="case_battery_plugged">
    <item>Unplugged</item>
    <item>Connected to AC Power</item>
    <item>Connected to USB</item>
</string-array>
```

## Permission requirements

The following permissions are required to use these features:

```xml
<!-- Microphone recording permission -->
<uses-permission android:name="android.permission.RECORD_AUDIO" />

<!-- Battery status access (No permission declaration required, system broadcast) -->
```

## Precautions

1. **API initialization sequence**: You must wait for the `AndroidInterface` initialization to complete before calling the device control API
2. **Lifecycle management**:` BroadcastReceiver` must be unregistered when the Activity is paused to avoid memory leaks
3. **Audio recording**: `RECORD_AUDIO` runtime permission needs to be requested before actual use
4. **Battery temperature unit**: The temperature value returned by the system is in 0.1°C units and needs to be divided by 10 to convert to degrees Celsius
5. **LED control**: Different devices may support different LED colors, which need to be tested according to the actual hardware


---

# Camera Hardware Encoding Scheme

## Overview

This application implements hardware-accelerated encoding of camera video streams, supporting RGB cameras and CV grayscale cameras.The RGB camera splices the left and right eye images via FBO into **Side-by-Side (SBS)** format and encodes them into a single MP4 file.The encoding uses the **Surface mode**, rendering to `EncoderSurface` via OpenGL to achieve zero-copy hardware encoding, significantly reducing CPU consumption.

## Encoding architecture

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
   │    → GL_TEXTURE_EXTERNAL_OES │  │ 2. YUV→Grayscale Shader Conversion│
   │ 2. Render to SBS Stitch FBO   │  │ 3. Render to EncoderSurface        │
   │    (Left viewport=0, Right=W) │  └─────────────────────────────────┘
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
   │   rgb.mp4    │  (2W×H SBS, left half for left eye, right half for right eye)
   └──────────────┘
```

## Core components

### EncoderSurface

Encapsulates the input Surface of EGL Surface and MediaCodec:

```cpp
class EncoderSurface {
    ANativeWindow* mWindow;      // MediaCodec input Surface
    EGLDisplay mDisplay;         // EGL display
    EGLSurface mSurface;         // EGL Surface
    EGLContext mContext;         // EGL context (shared with main context)
    EGLConfig mConfig;           // EGL configuration

    bool init(ANativeWindow* window, EGLDisplay sharedDisplay, EGLContext sharedContext);
    bool makeCurrent();
    bool swapBuffers();
    void setPresentationTime(int64_t timeNs);  // Set frame timestamp (call before swapBuffers)
    void release();
};
```

### CameraEncoder

Encapsulates the MediaCodec encoder:

```cpp
class CameraEncoder {
    // RGB Camera: Surface mode (Encoder is unaware of SBS; caller specifies resolution and filename)
    CameraEncoder(int width, int height, int frameRate, int bitRate,
                  const std::string& outputName, const std::string& baseDir = "");

    // Grayscale Camera: Buffer mode (Deprecated)
    CameraEncoder(const std::string& groupName, int width, int height,
                  int frameRate = 60, const std::string& baseDir = "");

    // Grayscale Camera: Surface mode (Recommended)
    CameraEncoder(const std::string& groupName, int width, int height,
                  int frameRate, EncoderMode mode, const std::string& baseDir = "");

    ANativeWindow* getInputSurface();  // Get encoder input Surface
    void submitNsTimestamp(int64_t timestampNs);  // Submit nanosecond timestamp (write to TimedText)
    void signalEndOfInputStream();  // Send end-of-stream signal for Surface mode
    bool feedFrame(const uint8_t* data, size_t size, int64_t timestampNs);  // Feed frame data for Buffer mode
    bool start();
    void stop();
};
```

### Independent EGL context (one for each camera group)

In order to perform GL rendering in the camera callback thread, each camera group (RGB, Tracking, Ctrl) has a separate EGL context to avoid lock contention:

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
    // Independent context for each camera group (no lock contention)
    CameraGLContext rgbCtx;
    CameraGLContext trackingCtx;
    CameraGLContext ctrlCtx;
};
```

**Improvement compared to shared context**: Each camera callback thread uses an independent context, eliminating the need for mutual exclusion lock protection and eliminating context switching wait times between camera callback threads.

### Timestamp processing scheme

#### Timestamp unit difference

```
┌─────────────────────────────────────────────────────────────────┐
│  Source            │  Unit         │  Description               │
├───────────────────┼────────────────┼────────────────────────────┤
│  Camera frame timestamp │  Nanosecond (ns) │ FrameInfo.timestamp │
│  MediaCodec       │  Microsecond (µs) │ presentationTimeUs      │
│  OpenXR XrTime    │  Nanosecond (ns) │ xrLocateSpace            │
└───────────────────┴────────────────┴────────────────────────────┘
```

**Unit conversion**:
```cpp
// Nanosecond → Microsecond
int64_t timestampUs = timestampNs / 1000;

// Microsecond → Nanosecond
int64_t timestampNs = timestampUs * 1000;
```

#### Original timestamp storage scheme

Adopt a two-tier timestamp strategy: microsecond timestamps are used for video frame PTS, and nanosecond timestamps are saved via TimedText Track:

```
┌─────────────────────────────────────────────────────────────────┐
│  MP4 File Structure                                              │
├─────────────────────────────────────────────────────────────────┤
│  Track 1: Video (H.265)                                         │
│    - Encoded video frames                                        │
│    - presentationTimeUs (microsecond precision)                   │
│                                                                 │
│  Track 2: TimedText (Timestamp metadata)                         │
│    - Original nanosecond timestamp of each frame (plain numeric string) │
└─────────────────────────────────────────────────────────────────┘
```

#### Implementation code

**1. Create TimedText Track**

Create in `initEncoder()`:

```cpp
AMediaFormat* textFormat = AMediaFormat_new();
AMediaFormat_setString(textFormat, AMEDIAFORMAT_KEY_MIME, "application/x-subrip");
AMediaFormat_setString(textFormat, AMEDIAFORMAT_KEY_LANGUAGE, "und");
AMediaFormat_setInt32(textFormat, AMEDIAFORMAT_KEY_IS_FORCED_SUBTITLE, 0);
AMediaFormat_setInt32(textFormat, AMEDIAFORMAT_KEY_IS_AUTOSELECT, 0);
mTextTrackIndex = AMediaMuxer_addTrack(mMuxer, textFormat);
```

**2. Video frame PTS settings**

Set the nanosecond timestamp before swapBuffers via the `eglPresentationTimeANDROID` extension, and MediaCodec will internally convert it to microseconds as PTS:

```cpp
// Inside handleRGBFrame:
rgbEncoderSurface->setPresentationTime(frameTimestampNs);  // ns, encoder converts to µs for PTS
rgbEncoderSurface->swapBuffers();
```

`EncoderSurface::setPresentationTime()` implements dynamic loading of extension functions via `eglGetProcAddress`:

```cpp
void EncoderSurface::setPresentationTime(int64_t timeNs) {
    auto pfn = (PFNEGLPRESENTATIONTIMEANDROID)eglGetProcAddress("eglPresentationTimeANDROID");
    pfn(mDisplay, mSurface, timeNs);
}
```

**3. Write nanosecond timestamps to TimedText Track**

Submit the nanosecond timestamp via `submitNsTimestamp()`, and the encoder output thread writes the timestamp to the TimedText Track when processing each non-codec-config frame:

```cpp
// Caller submits nanosecond timestamp (thread-safe queue)
rgbEncoder->submitNsTimestamp(frameTimestampNs);

// Inside CameraEncoder::processOutputBuffer():
// Fetch nanosecond timestamp from queue and write to TimedText track
std::string text = std::to_string(nsTimestamp);
AMediaCodecBufferInfo textInfo;
textInfo.presentationTimeUs = info.presentationTimeUs;  // Align with video frame PTS
textInfo.size = text.size();
AMediaMuxer_writeSampleData(mMuxer, mTextTrackIndex, (const uint8_t*)text.c_str(), &textInfo);
```

#### Timestamp alignment strategy

```
Camera frame arrives ──► Record original ns timestamp ──► Render to Surface ──► MediaCodec Encoding
     │                                    │
     │  timestampNs (ns)                  │  eglPresentationTimeANDROID (ns)
     │  → submitNsTimestamp()             │  → Encoder converts ns/1000 = µs (PTS)
     │  → TimedText Track                 │  → Video Track PTS
     │                                    │
     └────────────────────────────────────┘
                    Same frame, different storage methods
```

**Key points**:
1. `eglPresentationTimeANDROID` sets the nanosecond timestamp, which is internally converted to microseconds by the encoder as the video frame PTS
2. `submitNsTimestamp()` queues the nanosecond timestamps, and the output thread writes them to the TimedText Track frame by frame
3. TimedText's `presentationTimeUs is` aligned with the video frame PTS to ensure later association
4. The TimedText content is a pure numeric string (nanosecond timestamp value), not in JSON format

#### Read timestamp data

Use `MediaExtractor` to find the TimedText track with MIME type `application/x-subrip`, and read each sample to get the nanosecond timestamp string for the corresponding frame.

### MP4 file analysis command

#### View file structure

```bash
# Check all track information (encoding format, resolution, frame count, etc.)
ffprobe -v quiet -show_format -show_streams rgb.mp4

# Sample output:
#   Stream 0: data (mett)    — TimedText timestamp track, 286 frames
#   Stream 1: video (hevc)   — H.265 video, 4656x1748, ~30fps, ~8Mbps
```

#### View Video frame timestamps

```bash
# Video frame PTS (in seconds)
ffprobe -v quiet -select_streams 1 -show_entries packet=pts_time -of csv=p=0 rgb.mp4 | head -5
# Output: 0.000000
#       0.064511
#       0.096767
#       ...

# Raw video frame PTS ticks (time_base=1/90000)
ffprobe -v quiet -select_streams 1 -show_entries packet=pts -of csv=p=0 rgb.mp4 | head -5
# Output: 0
#       5806
#       8709
#       ...
```

#### Extract TimedText nanosecond timestamps

```bash
# Extract original nanosecond timestamps (each sample is 13-byte ASCII number)
ffmpeg -i rgb.mp4 -map 0:0 -f data - 2>/dev/null | strings | head -5
# Output: 2300450365549
#       2300514879299
#       2300547136226
#       2300579393101
#       2300611649976
```

#### Fully parse timestamps with Python

```python
#!/usr/bin/env python3
"""Extract nanosecond timestamps from TimedText and compare with Video PTS from rgb.mp4"""
import subprocess, sys

def extract_ns_timestamps(mp4_path):
    """Extract nanosecond timestamps from TimedText track"""
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
    """Extract video frame PTS (in seconds)"""
    result = subprocess.run(
        ['ffprobe', '-v', 'quiet', '-select_streams', '1',
         '-show_entries', 'packet=pts_time', '-of', 'csv=p=0', mp4_path],
        capture_output=True, text=True)
    return [float(line) for line in result.stdout.strip().split('\n') if line]

ns_ts = extract_ns_timestamps('rgb.mp4')
video_pts = extract_video_pts('rgb.mp4')

print(f'Frame count: {len(ns_ts)} text, {len(video_pts)} video')
first_ns = ns_ts[0]
for i in range(min(5, len(ns_ts))):
    ns_delta_us = (ns_ts[i] - first_ns) / 1000.0
    video_us = video_pts[i] * 1e6
    print(f'[{i}] ns={ns_ts[i]}  delta={ns_delta_us:.3f}us  video={video_us:.3f}us  diff={ns_delta_us-video_us:.3f}us')
```

#### Verify timestamp quality

```bash
# Check if frame counts match
echo "Video frames: $(ffprobe -v quiet -select_streams 1 -count_packets \
  -show_entries stream=nb_read_packets -of csv=p=0 rgb.mp4)"
echo "Text frames:  $(ffprobe -v quiet -select_streams 0 -count_packets \
  -show_entries stream=nb_read_packets -of csv=p=0 rgb.mp4)"

# Calculate average frame rate
ffprobe -v quiet -show_entries stream=avg_frame_rate -of csv=p=0 \
  -select_streams 1 rgb.mp4
```

#### Notes

- MP4 container time_base = 1/90000 (resolution 11.11 us), there is a quantization drift of ~1.3 us/frame between Video PTS and ns timestamps
- The original NS precision is fully preserved through the TimedText Track and is not affected by quantization
- The ns timestamp in TimedText should be used for post-processing, and Video PTS is only used for playback synchronization

## Key technical points

### 1. EGL context sharing

```
┌──────────────────────────────────────────────────────────────────┐
│                        Main GL Thread                             │
│  ┌─────────────┐                                                 │
│  │ MainContext │◄─────────────────────────────────┐              │
│  └─────────────┘                                  │              │
│                                                   │ Shared Resources │
└───────────────────────────────────────────────────│──────────────┘
                                                    │
┌───────────────────────────────────────────────────│──────────────┐
│                     Camera Callback Threads        │              │
│  ┌─────────────┐      ┌─────────────┐            │              │
│  │SharedContext│◄─────│ RGBEncoder  │            │              │
│  │ (Shared)    │      │  Surface    │            │              │
│  └─────────────┘      └─────────────┘            │              │
│        │              ┌─────────────┐            │              │
│        └──────────────│ CVEncoder   │────────────┘              │
│                       │  Surface    │                           │
│                       └─────────────┘                           │
└──────────────────────────────────────────────────────────────────┘

Shared resources: Textures, Buffers, Shader Programs
Non-shared resources: VAO, FBO, Context states
```

### 2. AHardwareBuffer → GL Texture

```cpp
// Step 1: Acquire native client buffer
EGLClientBuffer clientBuffer = eglGetNativeClientBufferANDROID(hwBuffer);

// Step 2: Create EGLImage (specify linear color space)
EGLint attrs[] = {
    EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_LINEAR,
    EGL_NONE
};
EGLImageKHR eglImage = eglCreateImageKHR(display, EGL_NO_CONTEXT,
                                          EGL_NATIVE_BUFFER_ANDROID,
                                          clientBuffer, attrs);

// Step 3: Bind to texture
glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, eglImage);

// Step 4: Destroy EGLImage after use (texture holds reference)
eglDestroyImageKHR(display, eglImage);
```

### 3. VAO cross-context issue

**Problem**: VAO (Vertex Array Object) cannot be shared across OpenGL contexts.

**Solution**: Set vertex attributes directly during rendering instead of using a pre-created VAO:

```cpp
// Wrong: VAO cannot be shared across contexts
glBindVertexArray(grayscaleEncoderVAO);
glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

// Correct: Set vertex attributes directly
glBindBuffer(GL_ARRAY_BUFFER, grayscaleEncoderVBO);  // VBO can be shared
glEnableVertexAttribArray(posLoc);
glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, stride, 0);
glEnableVertexAttribArray(texLoc);
glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, stride, offset);
glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
```

### 4. Shared context initialization timing

**Problem**: Camera frames may start arriving before the shared context initialization is complete.

**Solution**: Check the shared context status when the encoder is initialized:

```cpp
void initGrayscaleEncoder(SXR::CameraGroup group, int width, int height) {
    std::lock_guard<std::mutex> lock(sharedContextMutex);

    // Check if shared context is ready
    if (!sharedContextInitialized) {
        LOGI("Shared context not ready yet, skipping encoder init for now");
        return;  // Retry on next frame
    }

    // ... Continue initialization ...
}
```

### 5. Texture coordinate flipping

Camera textures usually require flipping the Y coordinate:

```cpp
// Full-screen quad vertex data
float vertices[] = {
    // position     texcoord (Y flipped)
    -1.0f, -1.0f,   0.0f, 1.0f,  // Bottom-left → Texture top-left
     1.0f, -1.0f,   1.0f, 1.0f,  // Bottom-right → Texture top-right
    -1.0f,  1.0f,   0.0f, 0.0f,  // Top-left → Texture bottom-left
     1.0f,  1.0f,   1.0f, 0.0f,  // Top-right → Texture bottom-right
};
```

### 6. Grayscale conversion shader

For CV grayscale cameras, the AHardwareBuffer format may be a vendor-specific YUV variant.Use the luminance formula to extract grayscale:

```glsl
#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision highp float;
in vec2 vTexCoord;
uniform samplerExternalOES uGrayscaleTexture;
out vec4 fragColor;

void main() {
    // Sample external YUV texture (GPU performs YUV→RGB conversion automatically)
    vec4 color = texture(uGrayscaleTexture, vTexCoord);
    // Extract grayscale with BT.601 luminance formula
    float y = 0.299 * color.r + 0.587 * color.g + 0.114 * color.b;
    // Output grayscale image
    fragColor = vec4(vec3(y), 1.0);
}
```

### 7. RGB SBS encoding rendering pipeline

RGB encoding uses the FBO splicing method, rendering the left and right eyes to the same FBO and then outputting to the encoder Surface:

```
┌─────────────────────────────────────────────────────────────┐
│  1. Create temporary GL_TEXTURE_EXTERNAL_OES textures (one for each eye)
│     Create EGLImage from AHardwareBuffer and bind texture
├─────────────────────────────────────────────────────────────┤
│  2. Render left/right eye frames to SBS Stitch FBO (GL_TEXTURE_2D, 2W×H)
│     - encoderShaderProgram (samplerExternalOES)
│     - Left eye: viewport(0, 0, W, H)
│     - Right eye: viewport(W, 0, W, H)
├─────────────────────────────────────────────────────────────┤
│  3. Switch to EncoderSurface context
│     - Reconfigure vertex attributes after makeCurrent (per-context states)
│     - Render SBS FBO texture to encoder surface
│     - sbsCopyShaderProgram (sampler2D, not samplerExternalOES)
├─────────────────────────────────────────────────────────────┤
│  4. setPresentationTime(ns) → swapBuffers()
└─────────────────────────────────────────────────────────────┘
```

**Note the sampler type matching**:` encoderShaderProgram` uses `samplerExternalOES` to sample the camera AHardwareBuffer (`GL_TEXTURE_EXTERNAL_OES`), while `sbsCopyShaderProgram` uses `sampler2D` to sample the SBS FBO texture (`GL_TEXTURE_2D`).Mismatched sampler types will result in a completely black output.

## Thread model

```
┌─────────────────────────────────────────────────────────────────┐
│  Thread ID   │  Responsibility         │  GL Context           │
├─────────────────────────────────────────────────────────────────┤
│  Main Thread │  OpenXR rendering, UI  │  MainContext          │
│  Camera Thread 1 │  RGB camera frame callback │  rgbCtx      │
│  Camera Thread 2 │  CV Tracking frame callback │  trackingCtx │
│  Camera Thread 3 │  CV CTRL frame callback     │  ctrlCtx     │
│  Encoding Thread │  MediaCodec encoding & output │  (No GL operations) │
└─────────────────────────────────────────────────────────────────┘

Synchronization mechanism:
- trackingFrameMutex: Protect tracking frame data
- Independent EGL context for each camera group, no mutex required for GL operations
```

## Encoding scheme comparison

| Characteristics | Buffer mode | Surface mode |
|------|-------------|--------------|
| CPU Consumption | High (frame data needs to be copied) | Low (zero copy) |
| Memory bandwidth | High | Low |
| Implementation complexity | Simple | Complex |
| Thread safety | Easy to ensure | Synchronization required |
| Applicable scenarios | Low frame rate, simple scenarios | High frame rate, performance sensitive |

## Video encoding format selection

### Comparison of H.264 and H.265

| Characteristics | H.264 (AVC) | H.265 (HEVC) |
|------|-------------|--------------|
| MIME type | `video/avc` | `video/hevc` |
| Compression efficiency | Baseline | About 50% higher than H.264 |
| Encoding delay | Lower | Slightly higher |
| Compatibility | Broad support | Newer device support |
| CPU/GPU consumption | Lower | High |
| Applicable scenarios | Compatibility first | Storage space/bandwidth priority |

### Switch encoding format

Modify the MIME type in the initEncoder `()` function of `CameraEncoder.cpp`:

**H.264 (AVC) - Default configuration:**
```cpp
void CameraEncoder::initEncoder() {
    mCodec = AMediaCodec_createEncoderByType("video/avc");

    AMediaFormat *format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    // ...
}
```

**H.265 (HEVC) - High compression efficiency:**
```cpp
void CameraEncoder::initEncoder() {
    mCodec = AMediaCodec_createEncoderByType("video/hevc");

    AMediaFormat *format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/hevc");
    // ...
}
```

### Notes

1. **Device compatibility**: Some older devices may not support H.265 hardware encoding. It is recommended to check if the encoder is available during initialization:
   ```cpp
// Check H.265 encoder support
AMediaCodec* codec = AMediaCodec_createEncoderByType("video/hevc");
if (!codec) {
    // Fallback to H.264
    codec = AMediaCodec_createEncoderByType("video/avc");
}
   ```

2. **Playback compatibility**: H.265-encoded video files require the player to support HEVC decoding

3. **Performance trade-off**: H.265 has a higher compression ratio but requires more encoding computation, which may increase power consumption in high-frame-rate scenarios

## Precautions

1. **The EGL context must be initialized before the thread is used**: Camera frames may start arriving before the shared context is initialized, so you need to check `sharedContextInitialized`

2. **VAO cannot be shared across contexts**: When rendering across contexts, use VBO to directly set vertex attributes

3. **Texture type**: Camera AHardwareBuffer must use `GL_TEXTURE_EXTERNAL_OES` with `samplerExternalOES`

4. **Color space**: Specify `EGL_GL_COLORSPACE_LINEAR` when creating EGLImage to ensure correct YUV→RGB conversion

5. **Separate texture for each encoder**: Multiple encoders cannot share the same texture, as EGLImage binding will overwrite the previous one

6. **Context switching**: Save and restore the original context state when operating across contexts

7. **EGLImage lifecycle**: Call `eglDestroyImageKHR` after use; the texture will retain a reference to the image data until rendering is complete

8. **AHardwareBuffer ownership**: The camera service owns the buffer. Do not call `AHardwareBuffer_release()`. The buffer is only valid during the callback.


---

# Screenshot and video saving function

## Overview

The app supports remote control of screenshot and video recording via controller buttons or ADB Intent, with Chinese voice prompts.

## Button mapping

| Button | Function | Voice prompt |
|------|------|----------|
| VOLUME_UP | Save all camera screenshots | "Image saved" / "Image saving failed" |
| DPAD_CENTER / Right B (1st time) | Start video recording | "Start recording" |
| DPAD_CENTER / Right B (2nd time) | Stop video recording | "Recording saved" |

## Save screenshot

### Trigger method

1. **Controller button**: Press VOLUME_UP
2. **ADB remote command**:
   ```bash
   adb shell am broadcast -a com.ssnwt.helloxr.SAVE_IMAGE
   ```

### Save format

- Format: PNG
- Save by camera group, with the left and right eyes horizontally spliced into one image
- File naming:` {cameraGroup}_{YYYYMMDD_HHmmss}.png`

### Save path

```
/sdcard/Android/data/com.ssnwt.helloxr/files/images/
├── rgb_20260428_101208.png          # Stitched RGB left & right eye (4656x1748)
├── tracking_20260428_101208.png     # Stitched Tracking left & right eye (1280x480)
├── ctrl_20260428_101208.png         # Stitched Ctrl left & right eye (1280x480)
```

### Stitching rules

| Camera group | Left eye source | Right eye source | Stitched size |
|--------|----------|----------|-----------|
| RGB | hwBuffer [0] | hwBuffer [1] | 4656x1748 |
| Tracking | hwBuffer [0] U=0..0.5 | hwBuffer [0] U=0.5..1.0 | 1280x480 |
| Ctrl | hwBuffer [0] U=0..0.5 | hwBuffer [0] U=0.5..1.0 | 1280x480 |

Description:
- The RGB camera has 2 independent hwBuffers (one each for the left eye/right eye)
- The CV grayscale camera (Tracking/Ctrl) has only 1 hwBuffer, and the left and right eye data are horizontally spliced in the same buffer (U=0..0.5 for the left eye, U=0.5..1.0 for the right eye)

### Implementation Principle

The screenshot reads pixel data from the render texture via OpenGL `glReadPixels` and encodes it as PNG using `stb_image_write`.The save operation is performed asynchronously in a separate worker thread, without blocking the rendering loop.

```
VOLUME_UP / Intent Trigger
      │
      ▼
 snapshotRequested = true (atomic flag)
      │
      ▼ (Detected in rendering loop)
 glReadPixels → RGBA pixel data
      │
      ▼
 ImageSaver writes PNG asynchronously
      │
      ▼
 JNI → speak("Image saved")
```

## Video recording

### Trigger method

1. **Controller button**: Press DPAD_CENTER or Right B to toggle start/stop
2. **ADB remote command**:
   ```bash
# Start recording
adb shell am broadcast -a com.ssnwt.helloxr.START_RECORDING
# Stop recording
adb shell am broadcast -a com.ssnwt.helloxr.STOP_RECORDING
   ```

### Save path

```
/sdcard/Android/data/com.ssnwt.helloxr/files/dataset/<YYYYMMDD_HHMMSS>/
├── rgb.mp4                      # RGB SBS video (left half for left eye, right half for right eye, 2W×H)
├── tracking.mp4                 # Tracking grayscale video
├── ctrl.mp4                     # Ctrl grayscale video
├── accel.csv                    # Accelerometer data (timestamp_ns, x, y, z)
├── gyro.csv                     # Gyroscope data (timestamp_ns, x, y, z)
├── head_pose.csv                # 6DOF head pose (timestamp_ns, pos_x/y/z, quat_x/y/z/w)
├── audio.m4a                    # Audio recording (with TimedText boottime timestamp)
├── hand_tracking.csv            # Hand tracking data (hand tracking mode)
├── controller_poses.csv         # Controller pose data (controller mode)
├── camera_params_rgb.json       # RGB camera intrinsic & extrinsic parameters
├── camera_params_tracking.json  # Tracking camera intrinsic & extrinsic parameters
└── camera_params_ctrl.json      # Ctrl camera intrinsic & extrinsic parameters
```

### Encoding parameters

| Parameters | RGB | CV grayscale |
|------|-----|---------|
| Encoder | H.265 (HEVC) | H.265 (HEVC) |
| Mode | Surface (zero copy) | Surface (zero copy) |
| Resolution | 2W×H (SBS) | W×H |
| Bit rate | 8 Mbps | 4 Mbps |
| Frame rate | 30 fps | 60 fps |

## Voice prompts

### Implementation method

The device may not have a built-in TTS engine. Try TTS first. If it fails, use a pre-generated Chinese audio file (generated offline via gTTS), package it into the APK, and play it via SoundPool.

| Event | Audio file | Chinese content |
|------|----------|----------|
| Screenshot successful | `res/raw/image_saved.mp3` | "Image saved" |
| Screenshot failed | `res/raw/image_failed.mp3` | "Failed to save image" |
| Start recording | `res/raw/recording_start.mp3` | "Start recording" |
| Stop recording | `res/raw/recording_stop.mp3` | "Recording saved" |

`VrNativeActivity.speak()` selects the audio file through substring matching (`text.contains("Start recording")`), and directly reads the input text when TTS is available.

## Intent Remote Control

Screenshots and recording can be remotely controlled via ADB broadcast, which is suitable for automated testing:

```bash
# Capture screenshot
adb shell am broadcast -a com.ssnwt.helloxr.SAVE_IMAGE

# Start recording
adb shell am broadcast -a com.ssnwt.helloxr.START_RECORDING

# Stop recording
adb shell am broadcast -a com.ssnwt.helloxr.STOP_RECORDING
```

### Cross-device remote control (via WiFi ADB)

```bash
# Connect to device
adb connect <Device IP>

# Capture screenshot
adb -s <Device IP> shell am broadcast -a com.ssnwt.helloxr.SAVE_IMAGE

# Pull screenshots to local directory
adb -s <Device IP> pull /sdcard/Android/data/com.ssnwt.helloxr/files/images/ ./images/
```

## Related documents

| File | Function |
|------|------|
| `app/src/main/cpp/stb_image_write.h` | PNG encoding library (single header file) |
| `app/src/main/cpp/ImageSaver.h/cpp` | Asynchronous PNG saving |
| `app/src/main/cpp/CameraEncoder.h/cpp` | Video hardware encoding |
| `app/src/main/cpp/EncoderSurface.h/cpp` | EGL Surface + MediaCodec input Surface |
| `app/src/main/cpp/DatasetRecorder.h/cpp` | Dataset recording coordinator |
| `app/src/main/cpp/ImuPoseCollector.h/cpp` | IMU + 6DOF head pose capture |
| `app/src/main/cpp/AudioEncoder.h/cpp` | Native audio encoding (AAudio + AAC) |
| `app/src/main/cpp/ControllerPoseSaver.h/cpp` | Controller pose data saving |
| `app/src/main/cpp/RawDateSave.h/cpp` | Gesture joint data saving |
| `app/src/main/cpp/RootSpaceQCOM.h` | QCOM Root Space extension definition |
| `app/src/main/cpp/main.cpp` | Key handling, screenshot logic, JNI bridging |
| `app/src/main/java/.../VrNativeActivity.java` | TTS/SoundPool, Intent reception |
| `app/src/main/res/raw/*.mp3` | Chinese voice prompt audio |
