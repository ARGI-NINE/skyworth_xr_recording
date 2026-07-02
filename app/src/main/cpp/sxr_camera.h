//
// Created by joey on 2023/12/27.
//

#ifndef SXR_CAMERA_API_H
#define SXR_CAMERA_API_H
#include <jni.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <dlfcn.h>
#include "sxr_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque context handle
struct SxrCameraContext;

/**********************************************************
 * Function pointer types for dynamic loading
 **********************************************************/

typedef SxrCameraContext* (*SxrCameraCreateFunc)(JavaVM* vm, jobject activity);
typedef void (*SxrCameraDestroyFunc)(SxrCameraContext* ctx);

typedef int (*SxrCameraOpenGroupFunc)(SxrCameraContext* ctx,
                                       SXR::CameraGroup group,
                                       SXR::FrameCallback callback,
                                       void* userData);
typedef int (*SxrCameraCloseGroupFunc)(SxrCameraContext* ctx, SXR::CameraGroup group);
typedef bool (*SxrCameraIsGroupOpenFunc)(SxrCameraContext* ctx, SXR::CameraGroup group);

typedef int (*SxrCameraGetGroupInfoFunc)(SxrCameraContext* ctx,
                                          SXR::CameraGroup group,
                                          uint32_t* maxWidth,
                                          uint32_t* maxHeight,
                                          uint32_t* format);

typedef int (*SxrCameraGetImuCalibrationFunc)(SXR::SxrImuCalibration* out);

/**********************************************************
 * API function table - holds dynamically loaded function pointers
 **********************************************************/
typedef struct SxrCameraApi {
    // Library handle
    void* libHandle;

    // Function pointers
    SxrCameraCreateFunc create;
    SxrCameraDestroyFunc destroy;
    SxrCameraOpenGroupFunc open_group;
    SxrCameraCloseGroupFunc close_group;
    SxrCameraIsGroupOpenFunc is_group_open;
    SxrCameraGetGroupInfoFunc get_group_info;
    SxrCameraGetImuCalibrationFunc get_imu_calibration;
} SxrCameraApi;

/**********************************************************
 * Dynamic loading API (inline implementation)
 **********************************************************/

/**
 * Default library name
 */
#define SXR_CAMERA_DEFAULT_LIB "libsxr_camera_client.so"

/**
 * sxr_camera_api_init - Initialize the API table by loading the library
 *
 * @param api       Pointer to SxrCameraApi struct to initialize
 * @param libPath   Path to the shared library (e.g., "libsxr_camera_client.so")
 *                  If NULL, uses default library name
 * @return          0 on success, 0 on failure
 */
static inline int sxr_camera_api_init(SxrCameraApi* api, const char* libPath) {
    if (api == NULL) {
        return -1;
    }

    // Initialize the struct
    memset(api, 0, sizeof(SxrCameraApi));

    // Use default library path if not provided
    const char* path = libPath ? libPath : SXR_CAMERA_DEFAULT_LIB;

    // Load the library
    api->libHandle = dlopen(path, RTLD_NOW);
    if (api->libHandle == NULL) {
        return -1;
    }

    // Load function pointers
    api->create = (SxrCameraCreateFunc)dlsym(api->libHandle, "sxr_camera_create");
    if (api->create == NULL) {
        dlclose(api->libHandle);
        memset(api, 0, sizeof(SxrCameraApi));
        return -1;
    }

    api->destroy = (SxrCameraDestroyFunc)dlsym(api->libHandle, "sxr_camera_destroy");
    if (api->destroy == NULL) {
        dlclose(api->libHandle);
        memset(api, 0, sizeof(SxrCameraApi));
        return -1;
    }

    api->open_group = (SxrCameraOpenGroupFunc)dlsym(api->libHandle, "sxr_camera_open_group");
    if (api->open_group == NULL) {
        dlclose(api->libHandle);
        memset(api, 0, sizeof(SxrCameraApi));
        return -1;
    }

    api->close_group = (SxrCameraCloseGroupFunc)dlsym(api->libHandle, "sxr_camera_close_group");
    if (api->close_group == NULL) {
        dlclose(api->libHandle);
        memset(api, 0, sizeof(SxrCameraApi));
        return -1;
    }

    api->is_group_open = (SxrCameraIsGroupOpenFunc)dlsym(api->libHandle, "sxr_camera_is_group_open");
    if (api->is_group_open == NULL) {
        dlclose(api->libHandle);
        memset(api, 0, sizeof(SxrCameraApi));
        return -1;
    }

    api->get_group_info = (SxrCameraGetGroupInfoFunc)dlsym(api->libHandle, "sxr_camera_get_group_info");
    if (api->get_group_info == NULL) {
        dlclose(api->libHandle);
        memset(api, 0, sizeof(SxrCameraApi));
        return -1;
    }

    api->get_imu_calibration = (SxrCameraGetImuCalibrationFunc)dlsym(api->libHandle, "sxr_camera_get_imu_calibration");
    // Optional symbol: do NOT dlclose/fail init if absent (older .so without IMU support).
    // The inline wrapper sxr_camera_get_imu_calibration null-checks and returns -1.

    return 0;
}

/**
 * sxr_camera_api_deinit - Cleanup and unload the library
 *
 * @param api       Pointer to SxrCameraApi struct to cleanup
 */
static inline void sxr_camera_api_deinit(SxrCameraApi* api) {
    if (api == NULL) {
        return;
    }

    if (api->libHandle != NULL) {
        dlclose(api->libHandle);
        api->libHandle = NULL;
    }

    memset(api, 0, sizeof(SxrCameraApi));
}

/**
 * sxr_camera_api_is_valid - Check if the API table is valid and ready to use
 *
 * @param api       Pointer to SxrCameraApi struct
 * @return          true if valid, false otherwise
 */
static inline bool sxr_camera_api_is_valid(const SxrCameraApi* api) {
    return api != NULL && api->libHandle != NULL && api->create != NULL;
}

/**********************************************************
 * Inline wrapper functions for convenience
 **********************************************************/

static inline SxrCameraContext* sxr_camera_create(SxrCameraApi* api, JavaVM* vm, jobject activity) {
    return api->create(vm, activity);
}

static inline void sxr_camera_destroy(SxrCameraApi* api, SxrCameraContext* ctx) {
    api->destroy(ctx);
}

static inline int sxr_camera_open_group(SxrCameraApi* api,
                                         SxrCameraContext* ctx,
                                         SXR::CameraGroup group,
                                         SXR::FrameCallback callback,
                                         void* userData) {
    return api->open_group(ctx, group, callback, userData);
}

static inline int sxr_camera_close_group(SxrCameraApi* api, SxrCameraContext* ctx, SXR::CameraGroup group) {
    return api->close_group(ctx, group);
}

static inline bool sxr_camera_is_group_open(SxrCameraApi* api, SxrCameraContext* ctx, SXR::CameraGroup group) {
    return api->is_group_open(ctx, group);
}

static inline int sxr_camera_get_group_info(SxrCameraApi* api,
                                             SxrCameraContext* ctx,
                                             SXR::CameraGroup group,
                                             uint32_t* maxWidth,
                                             uint32_t* maxHeight,
                                             uint32_t* format) {
    return api->get_group_info(ctx, group, maxWidth, maxHeight, format);
}

static inline int sxr_camera_get_imu_calibration(SxrCameraApi* api, SXR::SxrImuCalibration* out) {
    if (!api || !api->get_imu_calibration) return -1;
    return api->get_imu_calibration(out);
}

#ifdef __cplusplus
}
#endif

/*
 * API usage example (dynamic loading):
 *
 * #include "sxr_camera.h"
 *
 * void onFrame(void* userData, const SXR::FrameData* data) {
 *     // Process frame data
 *     const SXR::FrameInfo* left = &data->frames[0];
 *     const SXR::FrameInfo* right = &data->frames[1];
 *
 *     // Access HardwareBuffer (valid only during callback)
 *     AHardwareBuffer* hwBuffer = data->hwBuffer[0];
 *     // ... upload to texture or process ...
 * }
 *
 * int main() {
 *     // Initialize API with dynamic loading
 *     SxrCameraApi api;
 *     if (sxr_camera_api_init(&api, NULL) != 0) {
 *         LOGE("Failed to load camera library");
 *         return -1;
 *     }
 *
 *     // Create context
 *     SxrCameraContext* ctx = sxr_camera_create(&api, vm, activity);
 *
 *     // Open camera groups
 *     sxr_camera_open_group(&api, ctx, SXR::CameraGroup::TRACKING, onFrame, myUserData);
 *     sxr_camera_open_group(&api, ctx, SXR::CameraGroup::CTRL, onFrame, myUserData);
 *     sxr_camera_open_group(&api, ctx, SXR::CameraGroup::RGB, onFrame, myUserData);
 *
 *     // ... running ...
 *
 *     // Cleanup
 *     sxr_camera_close_group(&api, ctx, SXR::CameraGroup::TRACKING);
 *     sxr_camera_close_group(&api, ctx, SXR::CameraGroup::CTRL);
 *     sxr_camera_close_group(&api, ctx, SXR::CameraGroup::RGB);
 *     sxr_camera_destroy(&api, ctx);
 *     sxr_camera_api_deinit(&api);
 * }
 */

#endif //SXR_CAMERA_API_H
