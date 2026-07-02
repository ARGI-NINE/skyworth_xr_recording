# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a Qualcomm XR (Extended Reality) OpenXR sample application for Android that demonstrates RGB camera access on XR devices. The app is built using C++ with native Android activity and OpenXR API with Qualcomm extensions.

**Key Technologies:**
- OpenXR API with Qualcomm QCOMX extensions for camera access
- OpenGL ES 3.x for rendering
- Android NDK with native_app_glue
- GLM for math operations
- Gradle/CMake build system

**Application ID:** `com.ssnwt.helloxr`
**Native Library:** `mixedreality`
**Target Architecture:** `arm64-v8a` only
**Minimum SDK:** 29 (Android 10)
**Target SDK:** 34 (Android 14)

## Build Commands

### Build the APK
```bash
./gradlew assembleDebug
```

### Build release APK
```bash
./gradlew assembleRelease
```

### Clean build
```bash
./gradlew clean
./gradlew assembleDebug
```

### Install to connected device
```bash
./gradlew installDebug
```

### Build and install in one command
```bash
./gradlew assembleDebug && adb install -r app/build/outputs/apk/debug/app-debug.apk
```

### View build logs
```bash
./gradlew assembleDebug --info
```

## Architecture Overview

### High-Level Structure

```
app/
├── src/main/
│   ├── cpp/              # Native C++ code (main application logic)
│   ├── java/             # Android Java activity wrapper
│   ├── assets/raw/       # Shader files and texture assets
│   └── AndroidManifest.xml
├── build.gradle          # App-level Gradle config
└── src/main/cpp/CMakeLists.txt  # Native build config

External/                 # Dependencies and third-party libraries
├── AppCommon/cpp/        # Common OpenXR application code
├── OpenXR-SDK/           # OpenXR headers and loader
├── Common/
│   ├── GL/cpp/          # OpenGL helpers and geometry utilities
│   ├── Log/             # Logging utilities
│   └── DataStructures/  # Common data structures
└── ThirdParty/
    ├── glm/             # OpenGL Mathematics library
    └── tinyobjloader/   # OBJ file loader

3rdlibs/                  # Native library dependencies
└── libopenxr_loader.aar # OpenXR loader library
```

### Core Components

**1. Application Entry Point**
- `VrNativeActivity.java` (app/src/main/java/com/ssnwt/helloxr/VrNativeActivity.java)
  - Extends `NativeActivity`
  - Copies assets from `assets/raw/` to external storage on first run
  - Sets immersive sticky mode for VR experience
  - Native library: `libmixedreality.so`

**2. Main Engine Structure** (`app/src/main/cpp/main.cpp`)
```cpp
struct engine : public AppCommon::base_engine {
    // Render dimensions
    uint32_t width, height;

    // Stereo swapchains for XR rendering
    std::unordered_map<uint32_t, StereoSwapchain> swapchainMap;

    // Scene objects
    QtiGL::Geometry cube;
    QtiGL::Shader *cubeShader;
    GLuint cubeTexture;

    // Quad layer for camera display
    AppCommon::Swapchain quadSwapchain, quadSwapchain2;
    XrExtent2Df quadLayerSize;
    XrPosef quadLayerPose;

    // Camera access extension
    CameraAccessExtension mCameraAccessExtension;
};
```

**3. Camera Access Extension** (`app/src/main/cpp/main.cpp:85-100`)
The `CameraAccessExtension` struct demonstrates Qualcomm's camera access API:
```cpp
struct CameraAccessExtension {
    // Function pointers to QCOMX extension functions
    PFN_xrEnumerateCamerasQCOMX pfnxrEnumerateCamerasQCOMX;
    PFN_xrGetSupportedFrameConfigurationsQCOMX pfnxrGetSupportedFrameConfigurationsQCOMX;
    PFN_xrCreateCameraHandleQCOMX pfnxrCreateCameraHandleQCOMX;
    PFN_xrReleaseCameraHandleQCOMX pfnxrReleaseCameraHandleQCOMX;
    PFN_xrAccessFrameQCOMX pfnxrAccessFrameQCOMX;
    PFN_xrReleaseFrameQCOMX pfnxrReleaseFrameQCOMX;

    // Camera state
    XrCameraQCOMX XrCameraHandle;
    XrCameraFrameConfigurationQCOMX mCameraFrameConfiguration;
    XrCameraInfoQCOMX selectCameraInfo;
    XrCameraFrameDataQCOMX CameraFrameData;
    std::vector<SpaceFrame> frames;
};
```

Key camera access workflow:
1. `GetCamerasInfo()` - Enumerate available cameras and select RGB camera
2. `CreateCameraHandle()` - Create handle for camera with specific configuration
3. `AccessCameraFrame()` - Access camera frame data via AHardwareBuffer
4. `ReleaseFrame()` - Release frame after processing

**4. SharedTexture** (`app/src/main/cpp/SharedTexture.h/cpp`)
Wrapper for Android `AHardwareBuffer` to enable cross-process texture sharing between camera and rendering pipeline.

**5. AppCommon** (`External/AppCommon/cpp/`)
Base OpenXR application framework providing:
- `base_engine` - Core engine structure with OpenXR session management
- `base_saved_state` - Persistent OpenXR state (instance, session, spaces)
- Swapchain management utilities
- OpenXR instance/session/space lifecycle functions

**6. GL Helpers** (`External/Common/GL/cpp/`)
OpenGL utilities:
- `Geometry` - Mesh geometry management
- `Shader` - Shader program loading and compilation
- Texture loading (KTX format support via KtxLoader)

### OpenXR Extensions Used

**Qualcomm Extensions (defined in `openxr_qcom.h`):**
- `XR_QCOM_image_tracking` - Image tracking functionality
- Camera access extensions (`XR_QCOMX_*`) - RGB camera frame access
- Plane detection, hand tracking, ray casting, etc.

**Third-party Extensions (defined in `openxr_qcom_type.h`):**
- `XR_KHR_SKYWORTH_*` - Various SKYWORTH vendor extensions

### Shader Assets

Located in `app/src/main/assets/raw/`:
- `model_v.glsl` - Vertex shader
- `model_f.glsl` - Fragment shader
- Textures: `*.ktx` files and `*.jpg` images

Assets are copied to external storage at runtime: `/storage/emulated/0/Android/data/com.qualcomm.qti.xr.mixedreality/files/`

## Development Notes

### Adding New OpenXR Extensions

1. Define extension types in `app/src/main/cpp/openxr_qcom.h` or `openxr_qcom_type.h`
2. Load function pointers using `xrGetInstanceProcAddr()` during initialization
3. Enable extension in `XrInstanceCreateInfo` extensions list

### Modifying Camera Configuration

Camera frame configuration is selected in `CameraAccessExtension::GetCamerasInfo()` (main.cpp:164-214). Currently prioritizes:
- `XR_CAMERA_FRAME_FORMAT_YUV420_NV21_QCOMX`
- `XR_CAMERA_FRAME_FORMAT_YUV420_NV12_QCOMX`

### Render Pipeline

The application uses a standard OpenXR rendering loop:
1. Wait for frame (`xrWaitFrame`)
2. Begin frame (`xrBeginFrame`)
3. Render scene to swapchain images
4. End frame with layers (`xrEndFrame`)

Supports:
- Multi-sampling anti-aliasing (4x MSAA)
- Depth composition layers
- Quad layers for 2D content (camera feed display)

### CMake Build Configuration

Key libraries linked in `app/src/main/cpp/CMakeLists.txt`:
- `lib_xr_loader` - OpenXR loader
- `qxr-app-common` - AppCommon framework
- `qxr-common-gl` - OpenGL utilities
- `qxr-thirdparty-glm` - GLM math library
- `EGL`, `GLESv3` - Graphics APIs
- `android`, `native_app_glue` - Android NDK

### Important Constants

- Storage path defined in main.cpp:61
- Application requires camera permission: `android.permission.CAMERA`
- Requires OpenXR runtime: `com.qualcomm.qti.openxrruntime` or `com.qualcomm.qti.spaces.services`
- Dataset media is fragmented MP4 via `FMP4Writer` (`app/src/main/cpp/FMP4Writer.{h,cpp}`), used by both `CameraEncoder` and `AudioEncoder`. HEVC encoder runs with `max-bframes=0` (I/P frames only), so decode order == presentation order.

## Device Operation & Debugging

### System Properties

| Property | Values | Description |
|----------|--------|-------------|
| `persist.xr.usecontroller` | `true` / `false` | Controller mode / Hand tracking mode (requires app restart) |
| `persist.xr.project_hand` | `1` / `0` | Enable/disable hand skeleton projection in encoded video only, not headset preview. Requires hand tracking mode (requires app restart) |

```bash
# Check current settings
adb shell getprop persist.xr.usecontroller
adb shell getprop persist.xr.project_hand

# Switch to hand tracking mode
adb shell setprop persist.xr.usecontroller false

# Enable hand skeleton projection (hand tracking mode only)
adb shell setprop persist.xr.project_hand 1
```

### ADB Remote Commands

```bash
# Screenshot (all cameras → /sdcard/.../files/images/)
adb shell am broadcast -a com.ssnwt.helloxr.SAVE_IMAGE

# Dataset recording (→ /sdcard/.../files/dataset/<YYYYMMDD_HHMMSS>/)
adb shell am broadcast -a com.ssnwt.helloxr.START_RECORDING
adb shell am broadcast -a com.ssnwt.helloxr.STOP_RECORDING

# Pull dataset to local
adb pull /sdcard/Android/data/com.ssnwt.helloxr/files/dataset/ ./dataset/

# Restart app
adb shell am force-stop com.ssnwt.helloxr && sleep 1 && adb shell am start -n com.ssnwt.helloxr/com.ssnwt.helloxr.VrNativeActivity
```

### Logcat & Debugging

```bash
# Main app logs
adb logcat | grep "HelloXr"

# AppCommon framework logs
adb logcat | grep "AppCommon"

# Hand overlay logs
adb logcat | grep "HandOverlay"

# Encoder / recording logs
adb logcat | grep "Encoder\|Recording\|DatasetRecorder"
```

### Controller Button Mapping

| Button | Function |
|--------|----------|
| VOLUME_UP | Screenshot (all cameras) |
| Right B / DPAD_CENTER | Toggle recording start/stop |

### Dataset Output Structure

```
dataset/<YYYYMMDD_HHMMSS>/
├── rgb.mp4              # RGB SBS video (2W×H, H.265, 30fps, 8Mbps) — fragmented MP4
├── tracking.mp4         # Tracking grayscale (W×H, H.265, 60fps, 4Mbps) — fragmented MP4
├── ctrl.mp4             # Ctrl grayscale (W×H, H.265, 60fps, 4Mbps) — fragmented MP4
├── audio.m4a            # AAC audio (44.1kHz, mono, 96kbps) — fragmented MP4
├── rgb_metainfo.csv        # Per RGB frame metadata
├── tracking_metainfo.csv   # Per tracking frame metadata
├── ctrl_metainfo.csv       # Per ctrl frame metadata
├── audio_metainfo.csv      # Per AAC packet metadata
├── accel.csv            # Accelerometer (timestamp_ns, x, y, z) [m/s²]
├── gyro.csv             # Gyroscope (timestamp_ns, x, y, z) [rad/s]
├── head_pose.csv        # 6DOF head pose (timestamp_ns, pos_x/y/z, quat_x/y/z/w)
├── hand_tracking.csv    # Hand joints 26×2 (hand mode) / controller_poses.csv (controller mode)
├── camera_params_rgb.json
├── camera_params_tracking.json
└── camera_params_ctrl.json
```

Media files (`*.mp4`, `*.m4a`) are **fragmented MP4** written by the custom `FMP4Writer` module (`app/src/main/cpp/FMP4Writer.{h,cpp}`): `moov` is written up front and one `moof`/`mdat` pair per encoded sample, so a truncated or ungracefully-stopped recording still plays back to the last complete fragment. Video PTS is zero-based (first frame = 0). HEVC uses I/P frames only (`max-bframes=0`), so decode order == presentation order (PTS strictly monotonic).

**`*_metainfo.csv` schemas** (header row + append-per-sample, crash-safe):

- Video (`rgb_metainfo.csv`, `tracking_metainfo.csv`, `ctrl_metainfo.csv`):
  `frame_index,frame_id,pts_us,exposure_start_utc_ns,exposure_duration_ns,gain,mid_exposure_utc_ns`
- Audio (`audio_metainfo.csv`):
  `packet_index,pts_us,capture_utc_ns`

`pts_us` is zero-based microseconds (matches the mp4 video PTS exactly; audio uses a sample-rate timescale internally). The `*_utc_ns` columns are absolute UTC (CLOCK_BOOTTIME + a one-shot BOOTTIME→REALTIME offset captured once at recording start). Per-frame timestamps/exposure/gain live in `*_metainfo.csv`; the media files no longer carry any TimedText/metadata track.

### Timestamps & UTC Conversion

All dataset streams share **one absolute UTC timeline**. A single `BOOTTIME→REALTIME` offset (`mBoottimeToRealtimeOffsetNs = CLOCK_REALTIME - CLOCK_BOOTTIME`) is captured once at recording start and added to every stream's raw `CLOCK_BOOTTIME` timestamp, so no separate offset file is needed:

- **Video** (`*_metainfo.csv` `exposure_start_utc_ns` / `mid_exposure_utc_ns`): raw source is the camera frame exposure timestamp (`start_of_exposure_ts`), which is **kernel `CLOCK_BOOTTIME`**.
- **Audio** (`audio_metainfo.csv` `capture_utc_ns`): reconstructed from a `CLOCK_BOOTTIME` base + codec PTS, `+ offset → UTC`.
- **IMU** (`accel.csv` / `gyro.csv` `timestamp_ns`): Android `ASensorEvent.timestamp` (`CLOCK_BOOTTIME`), `+ offset → UTC` — these are UTC, **not** raw boottime.
- **head_pose.csv / controller_poses.csv / hand_tracking.csv `timestamp_ns`**: `CLOCK_BOOTTIME`, `+ offset → UTC`.

Every `*_utc_ns` and `timestamp_ns` column is therefore absolute UTC and mutually consistent — IMU, video, audio, and poses are on the same timeline. (`pts_us` in `*_metainfo.csv` is separate: zero-based microseconds matching the mp4 video PTS; audio uses a sample-rate timescale internally.)

> Verified empirically: an `accel.csv` `timestamp_ns` decodes to the recording instant in UTC, and `accel` vs `audio capture_utc_ns` differ only by real sample timing (sub-100 ms) — same domain.

### MP4 Timestamp Extraction

```bash
# Per-frame timestamps/exposure/gain come from the *_metainfo.csv, not the mp4.
# Video: read mid_exposure_utc_ns (or pts_us) from *_metainfo.csv.
# Audio: read capture_utc_ns from audio_metainfo.csv.

# Inspect the fragmented mp4 itself (one moof/mdat per sample; survives truncation)
ffprobe -v quiet -show_format -show_streams rgb.mp4

# Sample/packet counts for sanity-checking against the CSV row count
ffprobe -v quiet -select_streams 0 -count_packets -show_entries stream=nb_read_packets -of csv=p=0 rgb.mp4
```

## Common Issues

**Camera Access Fails:**
- Verify camera permission is granted in Android settings
- Check that device supports RGB camera access extension
- Ensure `XR_SESSION_STATE_FOCUSED` before accessing camera

**Build Errors:**
- Ensure NDK path is set in `local.properties`
- Verify `arm64-v8a` ABI is available
- Check that OpenXR loader AAR is present in `3rdlibs/`

**Runtime Crashes:**
- Check logcat with `adb logcat | grep "HelloXr"` or `"AppCommon"`
- Verify OpenXR runtime is installed and up-to-date
- Ensure device supports required OpenXR extensions

**Hand Projection Not Working:**
- Verify `persist.xr.usecontroller` is `false` (hand tracking mode)
- Verify `persist.xr.project_hand` is `1`
- Restart app after changing properties
- Hand projection only appears in encoded video, not in headset preview

**Recording Issues:**
- Check `adb logcat | grep "Recording\|DatasetRecorder"` for errors
- Ensure sufficient storage space on device
- Wait for `stopEncoder` to complete before starting new recording (check `stopInProgress` flag)
