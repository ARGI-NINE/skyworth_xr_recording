/****************************************************************
 * Copyright (c) 2020-2021 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/

#pragma once

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES

#include <unistd.h>
#include <vector>
#include <jni.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <sys/system_properties.h>

#include "glm/glm.hpp"

#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"
#include <android_native_app_glue.h>
#include "xr_linear.h"

#define LOG_TAG "AppCommon"
//#define THERMAL_INTERVAL 1
// bool ThermalLoop;
extern XrPosef idPose;

namespace AppCommon {

struct Swapchain {
    XrSwapchain xrSwapchain;
    std::vector<XrSwapchainImageOpenGLESKHR> xrImages;
    //for depth usage only
    XrSwapchain xrSwapchainDepth;
    std::vector<XrSwapchainImageOpenGLESKHR> xrImagesDepth;
    std::vector<GLuint> glFramebuffers;
};

struct DepthInfo
{
    // supporting depth layers is *optional* for runtimes
    bool supported;
    XrCompositionLayerDepthInfoKHR* infos;
};

/**
 * saved state data.
 */
struct base_saved_state {
    ANativeWindow *NativeWindow = nullptr;
    bool Resumed = false;

    XrInstance xrInstance = XR_NULL_HANDLE;
    XrSystemId xrSysId = XR_NULL_SYSTEM_ID;
    XrSession xrSession = XR_NULL_HANDLE;
    XrSpace xrLocalSpace = XR_NULL_HANDLE;
    XrSpace xrViewSpace = XR_NULL_HANDLE;
    XrSpace xrRootSpace = XR_NULL_HANDLE;
    uint32_t viewCount = 0;
    XrViewConfigurationView viewConfigs[2];
    std::vector<XrView> m_views;
};

/**
 * base state structure shared for apps
 */
struct base_engine {
    struct android_app *app;

    int ready = 0;

    bool has_exit_sess_once;
    XrEventDataBuffer m_eventDataBuffer;
    XrSessionState sessionState;
    XrInstanceProperties runtimeInfo;

    EGLDisplay display;
    EGLContext context;
    EGLConfig config;
    EGLSurface surface;

    struct base_saved_state state;

    GLuint depthBuffer; // common depth buffer... should be alright as we assume
                        // both XR views have the same dimensions
    bool dual_display_enabled =
        false; // if support displaying on both host and viewer
    bool projection_layer_only = true;
    bool useRootSpace = false;
    DepthInfo depthInfo;
};

/**
 * Thermal Test func
 */
// void *thermal_func(void *arg);

/**
 * Process the next main command.
 */
void app_handle_cmd(struct android_app *app, int32_t cmd);

int32_t engine_handle_input(struct android_app *app, AInputEvent *event);

/**
 * Process events according to state machine.
 */
void app_handle_event(struct base_engine *engine);

/**
 * Call openxr API to begin xrSession
 */
void app_begin_session(struct base_engine *engine);

/**
 * Call openxr API to end xrSession
 */
void app_end_session(struct base_engine *engine);

/**
 * Wait until window initialized
 */
void app_wait_window(struct base_engine *engine);

/**
 * Get content from given files.
 */
void *get_file_buffer(const char *pFileName, int *pBufferSize);

/**
 * Initialize an EGL context for the current display.
 */
int app_init_display(struct base_engine *engine);

/**
 * Initialize an protected EGL context for the current display.
 */
int app_init_display_protected(struct base_engine *engine, bool isprotected);

/**
 * Tear down the EGL context currently associated with the display.
 */
int app_term_display(struct base_engine *engine);

XrResult app_destroy_space(XrSpace space);

XrResult app_destroy_session(XrSession session);

XrResult app_destroy_instance(struct base_engine *engine);

void app_query_layers_and_extensions(struct base_engine *engine,JavaVM* vm = nullptr, void* context = nullptr);

void app_create_instance(XrInstanceCreateInfo *info,
                         struct base_engine *engine);

void app_get_instance_properties(struct base_engine *engine);

void app_get_system_prop(struct base_engine *engine);

void app_enum_view_configuration(struct base_engine *engine);

void app_create_session(XrSessionCreateInfo *info, struct base_engine *engine);

void app_create_space(XrReferenceSpaceCreateInfo *info,
                      struct base_engine *engine);

void app_enum_sc_format(struct base_engine *engine);

void app_create_swapchain(XrSwapchainCreateInfo *info,
                          XrSession *session,
                          Swapchain *sc);
//depth swapchain
void app_create_swapchain_depth(XrSwapchainCreateInfo *info,
                          XrSession *session,
                          Swapchain *sc);

void app_init_depth_buffer(struct base_engine *engine);

void app_destroy_swapchain(Swapchain *sc);

void app_destroy_swapchain_depth(Swapchain *sc);

void array2matrix(XrMatrix4x4f array, glm::mat4 &proj_mat);

/**
 * Process all pending messages.
 */
void app_poll_events(struct base_engine *engine);

void HandleSessionStateChangedEvent(
        struct base_engine *engine,
        const XrEventDataSessionStateChanged &stateChangedEvent);

void app_req_exit_session(struct base_engine *engine);

/**
 * Return event if one is available, otherwise return null.
 */
XrEventDataBaseHeader *try_read_next_event(struct base_engine *engine);

const char *to_string(XrSessionState state);
}; // namespace AppCommon
