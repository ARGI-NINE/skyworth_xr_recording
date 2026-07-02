//
// Created by joey on 2024/4/23.
//

#ifndef SXR_COMMON_H
#define SXR_COMMON_H

#include <android/hardware_buffer.h>
namespace SXR {
    struct SxrMapPoint {
        uint32_t id{0};
        float x{0.0};
        float y{0.0};
        float z{0.0};
    };

    // NOTE: SxrPose, SxrAnchor, and ImuFrame have been removed.
    // These were part of the old synchronous polling API and are no longer needed
    // in the new callback-based API. Pose data is now provided via FrameInfo.

    struct CameraInfo {
        float width, height;
        float centerX, centerY;
        float focalX, focalY;
        /**
         * radialDisortion:
         *  radialDisortion[0]~radialDisortion[4]:
         *  4-parameter Kannala-Brandt fisheye model in OpenCV – See
            https://docs.opencv.org/3.4/db/d58/group__calib3d__fisheye.html.
            radialDisortion[5]~: reserved
         */
        float radialDisortion[8];
        float position[3];
        float rotation[4];
    };

    enum eCameraID {
        GRAY_LEFT = 0,      // tracking camera left eye
        GRAY_RIGHT,         // tracking camera right eye
        GRAY_LEFT_UP,       // ctrl-tracking camera left eye
        GRAY_RIGHT_UP,      // ctrl-tracking camera right eye
        CAME_MAX            // now equals 4, maximum camera count
    };

    enum class CameraGroup : uint8_t {
        TRACKING = 0,   // GRAY_LEFT + GRAY_RIGHT
        CTRL,           // GRAY_LEFT_UP + GRAY_RIGHT_UP
        RGB,            // RGB_LEFT + RGB_RIGHT (reserved for future)
        DEPTH,          // DEPTH sensor (single)
    };

    struct FrameInfo {
        // Frame metadata
        uint32_t frameId;
        uint64_t timestamp;          // start_of_exposure_ts (CLOCK_BOOTTIME, ns)
        uint32_t exposure;
        uint32_t gain;

        // Frame dimensions (after crop)
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        uint32_t format;

        // Crop region (position in HardwareBuffer)
        uint32_t cropX;
        uint32_t cropY;

        // Intrinsic parameters (static, included per frame for convenience)
        float focalX, focalY;
        float centerX, centerY;
        float radialDistortion[8];   // Kannala-Brandt fisheye + tangential

        // Extrinsic parameters (static, included per frame for convenience)
        float position[3];           // Camera position relative to device
        float rotation[4];           // Camera rotation relative to device (quaternion)
    };

    struct FrameData {
        CameraGroup group;

        FrameInfo frames[2];         // [0]=left eye, [1]=right eye, always valid

        uint8_t hwBufferCount;       // 1 = tracking/ctrl, 2 = rgb
        AHardwareBuffer* hwBuffer[2];
        // tracking/ctrl: hwBuffer[0] contains left+right stitched data
        // rgb: hwBuffer[0]=left, hwBuffer[1]=right
    };

    // Per-camera <TimeAlignment delta> entry.
    struct CameraTimeAlign { char name[32]; float deltaSec; };

    // IMU calibration sidecar payload (crosses the dlopen ABI — keep POD, no std::string).
    struct SxrImuCalibration {
        bool   valid;              // true once parsed successfully
        int    imuId;              // <Stateinit imuId>
        bool   isPrimary;          // <Stateinit isPrimaryImu>
        float  accelBias[3];       // aBias, m/s²
        float  gyroBias[3];        // wBias, rad/s
        float  accelScale[3];      // ka, dimensionless
        float  gyroScale[3];       // kg, dimensionless
        float  accelNonorth[3];    // na, dimensionless
        float  gyroNonorth[3];     // ng, dimensionless
        CameraTimeAlign cameraTimeAlign[8];  // per-camera <TimeAlignment delta>, seconds
        int    cameraTimeAlignCount;
        float  stateinitDelta;     // <Stateinit delta>, s (IMU↔tracking/pose time offset)
        float  accelDelta;         // <Stateinit accelDelta>, s
        float  accelNoiseStd[3];   // σ_a, m/s² (from <IMUNoise> stationary, or fallback)
        float  gyroNoiseStd[3];    // σ_g, rad/s (from <IMUNoise> stationary, or fallback)
        float  accelBiasStd[3];    // σ_ba, m/s² (hardcoded; not in XML)
        float  gyroBiasStd[3];     // σ_bg, rad/s (hardcoded; not in XML)
        char   deviceUid[64];      // <DeviceConfiguration deviceUID>
    };

    typedef void (*FrameCallback)(void* userData, const FrameData* data);
}
#endif //SXR_COMMON_H
