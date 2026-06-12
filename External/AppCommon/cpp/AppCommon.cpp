/****************************************************************
 * Copyright (c) 2020-2021 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/

#include "AppCommon.h"
#include "../../Common/Log/LogUtils.h"
#include <EGL/eglext.h>
#include <assert.h>

XrPosef idPose = {.orientation = {.x = 0, .y = 0, .z = 0, .w = 1.0},
                  .position = {.x = 0, .y = 0, .z = 0}};

namespace AppCommon {
/*
void *thermal_func(void *arg)
{
    int ret = 0;
    while(ThermalLoop) {
        sleep(THERMAL_INTERVAL);
        struct base_engine* engine = (struct base_engine*)arg;
        XrPerfSettingsDomainEXT cpu_domain = XR_PERF_SETTINGS_DOMAIN_CPU_EXT;
        XrPerfSettingsDomainEXT gpu_domain = XR_PERF_SETTINGS_DOMAIN_GPU_EXT;
        XrPerfSettingsNotificationLevelEXT cpu_notificationLevel =
XR_PERF_SETTINGS_NOTIF_LEVEL_NORMAL_EXT; XrPerfSettingsNotificationLevelEXT
gpu_notificationLevel = XR_PERF_SETTINGS_NOTIF_LEVEL_NORMAL_EXT; float
cpu_tempHeadroom = 0, gpu_tempHeadroom = 0; float cpu_tempSlope = 0,
gpu_tempSlope = 0; ret =
xrThermalGetTemperatureTrendEXT(engine->state.xrSession, cpu_domain,
&cpu_notificationLevel, &cpu_tempHeadroom, &cpu_tempSlope); if(ret !=
XR_SUCCESS) { LOGE(LOG_TAG, "%s %d get thermal data error", __func__, __LINE__);
        } else {
            LOGI(LOG_TAG, "cpu thermal data:  tempHeadroom=%f, tempSlope=%f,
notificationLevel=%d", cpu_tempHeadroom, cpu_tempSlope, cpu_notificationLevel);
        }

        ret = xrThermalGetTemperatureTrendEXT(engine->state.xrSession,
gpu_domain, &gpu_notificationLevel, &gpu_tempHeadroom, &gpu_tempSlope); if(ret
!= XR_SUCCESS) { LOGE(LOG_TAG, "%s %d get thermal data error", __func__,
__LINE__); } else { LOGI(LOG_TAG, "gpu thermal data:  tempHeadroom=%f,
tempSlope=%f, notificationLevel=%d", gpu_tempHeadroom, gpu_tempSlope,
gpu_notificationLevel);
        }
    }
    return NULL;
}
 */

const char *to_string(XrSessionState state)
{
    switch (state) {
    case XR_SESSION_STATE_UNKNOWN:
        return "XR_SESSION_STATE_UNKNOWN";
    case XR_SESSION_STATE_IDLE:
        return "XR_SESSION_STATE_IDLE";
    case XR_SESSION_STATE_READY:
        return "XR_SESSION_STATE_READY";
    case XR_SESSION_STATE_SYNCHRONIZED:
        return "XR_SESSION_STATE_SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE:
        return "XR_SESSION_STATE_VISIBLE";
    case XR_SESSION_STATE_FOCUSED:
        return "XR_SESSION_STATE_FOCUSED";
    case XR_SESSION_STATE_STOPPING:
        return "XR_SESSION_STATE_STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING:
        return "XR_SESSION_STATE_LOSS_PENDING";
    case XR_SESSION_STATE_EXITING:
        return "XR_SESSION_STATE_EXITING";
    case XR_SESSION_STATE_MAX_ENUM:
        return "XR_SESSION_STATE_MAX_ENUM";
    default:
        return "";
    }
}

void app_begin_session(struct base_engine *engine)
{
    XrSessionBeginInfo sessionBeginInfo = {
            .type = XR_TYPE_SESSION_BEGIN_INFO,
            .next = nullptr,
            .primaryViewConfigurationType =
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
    XrResult result =
            xrBeginSession(engine->state.xrSession, &sessionBeginInfo);
    if (XR_SUCCESS != result) {
        LOGE(LOG_TAG, "xrBeginSession failed: %d", result);
        assert(0);
    } else {
        LOGI(LOG_TAG, "XR session began");
    }
}

void app_end_session(struct base_engine *engine)
{
    XrResult result = xrEndSession(engine->state.xrSession);
    if (XR_SUCCESS != result) {
        LOGI(LOG_TAG, "xrEndSession failed: %d", result);
        assert(0);
    } else {
        LOGI(LOG_TAG, "XR session ended");
    }
}

/**
 * Process the next main command.
 */
void app_handle_cmd(struct android_app *app, int32_t cmd)
{
    struct base_engine *engine = (struct base_engine *)app->userData;

    switch (cmd) {
    // There is no APP_CMD_CREATE. The ANativeActivity creates the
    // application thread from onCreate(). The application thread
    // then calls android_main().
    case APP_CMD_START: {
        LOGI(LOG_TAG, "onStart()");
        LOGI(LOG_TAG, "    APP_CMD_START");
        break;
    }
    case APP_CMD_RESUME: {
        LOGI(LOG_TAG, "onResume()");
        LOGI(LOG_TAG, "    APP_CMD_RESUME");
        engine->state.Resumed = true;
        engine->has_exit_sess_once = false;
        break;
    }
    case APP_CMD_PAUSE: {
        LOGI(LOG_TAG, "onPause()");
        LOGI(LOG_TAG, "    APP_CMD_PAUSE");
        engine->state.Resumed = false;
        break;
    }
    case APP_CMD_STOP: {
        LOGI(LOG_TAG, "onStop()");
        LOGI(LOG_TAG, "    APP_CMD_STOP");
        break;
    }
    case APP_CMD_DESTROY: {
        LOGI(LOG_TAG, "onDestroy()");
        LOGI(LOG_TAG, "    APP_CMD_DESTROY");
        engine->state.NativeWindow = NULL;
        break;
    }
    case APP_CMD_INIT_WINDOW: {
        LOGI(LOG_TAG, "surfaceCreated()");
        LOGI(LOG_TAG, "    APP_CMD_INIT_WINDOW");
        engine->state.NativeWindow = app->window;
        break;
    }
    case APP_CMD_TERM_WINDOW: {
        LOGI(LOG_TAG, "surfaceDestroyed()");
        LOGI(LOG_TAG, "    APP_CMD_TERM_WINDOW");
        engine->state.NativeWindow = NULL;
        break;
    }
    case APP_CMD_CONFIG_CHANGED: {
        LOGI(LOG_TAG, "configChanged()");
        LOGI(LOG_TAG, "    APP_CMD_CONFIG_CHANGED");
        break;
    }
    case APP_CMD_LOST_FOCUS: {
        LOGI(LOG_TAG, "focusChanged()");
        LOGI(LOG_TAG, "    APP_CMD_LOST_FOCUS");
        break;
    }
    case APP_CMD_GAINED_FOCUS: {
        LOGI(LOG_TAG, "focusChanged()");
        LOGI(LOG_TAG, "    APP_CMD_GAINED_FOCUS");
        break;
    }
    }
}

/**
 * Process the next input event.
 */
int32_t engine_handle_input(struct android_app *app, AInputEvent *event) {
  auto *engine = (struct engine *)app->userData;
  if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
    LOGI(LOG_TAG, "state of engine, x: %f, y: %f", AMotionEvent_getX(event, 0),
         AMotionEvent_getY(event, 0));
    return 1;
  }
  return 0;
}

XrEventDataBaseHeader *try_read_next_event(base_engine *engine)
{
    // It is sufficient to clear the just the XrEventDataBuffer header to
    // XR_TYPE_EVENT_DATA_BUFFER
    XrEventDataBaseHeader *baseHeader =
            reinterpret_cast<XrEventDataBaseHeader *>(
                    &engine->m_eventDataBuffer);
    *baseHeader = {XR_TYPE_EVENT_DATA_BUFFER};
    const XrResult xr =
            xrPollEvent(engine->state.xrInstance, &engine->m_eventDataBuffer);
    if (xr == XR_SUCCESS) {
        if (baseHeader->type == XR_TYPE_EVENT_DATA_EVENTS_LOST) {
            const XrEventDataEventsLost *const eventsLost =
                    reinterpret_cast<const XrEventDataEventsLost *>(baseHeader);
            LOGW(LOG_TAG, "%p events lost", eventsLost);
        }

        return baseHeader;
    } else
        return nullptr;
}

void app_req_exit_session(struct base_engine *engine)
{
    XrResult result = xrRequestExitSession(engine->state.xrSession);
    if (XR_SUCCESS != result) {
        LOGE(LOG_TAG, "xrRequestExitSession failed: %d", result);
        assert(0);
    } else
        LOGI(LOG_TAG, "xrRequestExitSession success");
}

void app_poll_events(struct base_engine *engine)
{
//    LOGI(LOG_TAG,"app_poll_events");
    while (const XrEventDataBaseHeader *event = try_read_next_event(engine)) {
        LOGI(LOG_TAG, "poll event type:%d", event->type);
        switch (event->type) {
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
            LOGI(LOG_TAG, "xrPollEvent: received XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING event");

            const XrEventDataReferenceSpaceChangePending *state_change_event = (XrEventDataReferenceSpaceChangePending *) event;

            LOGI(LOG_TAG, "change time:%lld, prev pose:(%f %f %f %f, %f %f %f), valid:%d",
                  state_change_event->changeTime,
                  state_change_event->poseInPreviousSpace.orientation.x,
                  state_change_event->poseInPreviousSpace.orientation.y,
                  state_change_event->poseInPreviousSpace.orientation.z,
                  state_change_event->poseInPreviousSpace.orientation.w,
                  state_change_event->poseInPreviousSpace.position.x,
                  state_change_event->poseInPreviousSpace.position.y,
                  state_change_event->poseInPreviousSpace.position.z,
                  state_change_event->poseValid);
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
            const auto &instanceLossPending =
                    *reinterpret_cast<const XrEventDataInstanceLossPending *>(
                            event);
            LOGI(LOG_TAG, "XrEventDataInstanceLossPending by %ld",
                 instanceLossPending.lossTime);
            return;
        }
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            auto sessionStateChangedEvent =
                    *reinterpret_cast<const XrEventDataSessionStateChanged *>(
                            event);
            HandleSessionStateChangedEvent(engine, sessionStateChangedEvent);
            break;
        }
        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            break;
        default: {
            LOGI(LOG_TAG, "Ignoring event type %d", event->type);
            break;
        }
    }
}
}

void HandleSessionStateChangedEvent(
    struct base_engine *engine,
    const XrEventDataSessionStateChanged &stateChangedEvent)
{
const XrSessionState oldState = engine->sessionState;
engine->sessionState = stateChangedEvent.state;

LOGI(LOG_TAG,
     "XrEventDataSessionStateChanged: state %s->%s session=%p time=%ld",
     to_string(oldState), to_string(engine->sessionState),
     stateChangedEvent.session, stateChangedEvent.time);

if ((stateChangedEvent.session != XR_NULL_HANDLE) &&
    (stateChangedEvent.session != engine->state.xrSession)) {
    LOGE(LOG_TAG, "XrEventDataSessionStateChanged for unknown session");
    return;
}

switch (engine->sessionState) {
case XR_SESSION_STATE_READY: {
    app_begin_session(engine);
    engine->ready = 1;
    break;
}
case XR_SESSION_STATE_STOPPING: {
    engine->ready = 0;
    app_end_session(engine);
    break;
}
case XR_SESSION_STATE_EXITING: {
    // Do not attempt to restart because user closed this session.
    break;
}
case XR_SESSION_STATE_LOSS_PENDING: {
    // Poll for a new instance.
    break;
}
default:
    break;
}
}

void app_wait_window(struct base_engine *engine)
{
int events;
struct android_poll_source *source;
while (!engine->state.NativeWindow) {
    ALooper_pollOnce(-1, nullptr, &events, (void **)&source);
    if (source != nullptr) {
        source->process(engine->app, source);
    }
}
}

void *get_file_buffer(const char *pFileName, int *pBufferSize)
{
LOGI(LOG_TAG, "Opening File: %s", pFileName);
FILE *fp = fopen(pFileName, "rb");
if (!fp) {
    return nullptr;
}

fseek(fp, 0, SEEK_END);
*pBufferSize = ftell(fp);
fseek(fp, 0, SEEK_SET);

char *pRetBuff = new char[*pBufferSize + 1];
fread(pRetBuff, 1, *pBufferSize, fp);
pRetBuff[*pBufferSize] = 0;

fclose(fp);

return pRetBuff;
}

void GetSysProperty(char const *name, char *value, size_t const len,
                char const *defaultValue) {
if (0 == __system_property_get(name, value)) {
strncpy(value, defaultValue, len);
} else {
if (0 == strlen(value)) {
  strncpy(value, defaultValue, len);
}
}
}

int app_init_display(struct base_engine *engine)
{
return app_init_display_protected(engine, false);
}

int app_init_display_protected(struct base_engine *engine, bool isprotected)
{
EGLDisplay display;
EGLConfig config;
EGLContext context;
EGLint num_config;

EGLint attrib_list[] = {
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_DEPTH_SIZE, 16,
    EGL_NONE
};

display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
assert(display != EGL_NO_DISPLAY);

EGLBoolean res = eglInitialize(display, nullptr, nullptr);
assert(res == EGL_TRUE);

res = eglChooseConfig(display, attrib_list, &config, 1, &num_config);
assert(res == EGL_TRUE);

EGLint const contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3,
                                 EGL_PROTECTED_CONTENT_EXT, isprotected,
                                 EGL_NONE};
context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
assert(context != EGL_NO_CONTEXT);

res = eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
assert(res == EGL_TRUE);

engine->config = config;
engine->context = context;
engine->display = display;

// Get display property (single display or dual display) from system prop.
char propValue[PROP_VALUE_MAX];
GetSysProperty("debug.openxr.enableDualDisplay", propValue,
               sizeof(propValue),
               engine->dual_display_enabled ? "true" : "false");
engine->dual_display_enabled = (propValue[0] == 't');
LOGI(LOG_TAG, "Display %s",
     engine->dual_display_enabled ? "on both host and viewer"
                                  : "only on viewer");

return 0;
}

int app_term_display(struct base_engine *engine)
{
EGLBoolean res;
if (engine->display != EGL_NO_DISPLAY) {
    res = eglMakeCurrent(engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         EGL_NO_CONTEXT);
    assert(res == EGL_TRUE);
    if (engine->context != EGL_NO_CONTEXT) {
        eglDestroyContext(engine->display, engine->context);
    }
    res = eglTerminate(engine->display);
    assert(res == EGL_TRUE);
}
engine->display = EGL_NO_DISPLAY;
engine->context = EGL_NO_CONTEXT;
engine->config = EGL_NO_CONFIG_KHR;
engine->surface = EGL_NO_SURFACE;
return 0;
}

XrResult app_destroy_space(XrSpace space)
{
XrResult result = xrDestroySpace(space);
if (XR_SUCCESS == result) {
    LOGI(LOG_TAG, "XR space destroyed");
    space = XR_NULL_HANDLE;
}

return result;
}

XrResult app_destroy_session(XrSession session)
{
XrResult result = xrDestroySession(session);
if (XR_SUCCESS == result) {
    LOGI(LOG_TAG, "XR session destroyed");
    session = XR_NULL_HANDLE;
}

return result;
}

XrResult app_destroy_instance(struct base_engine *engine)
{
XrResult result = xrDestroyInstance(engine->state.xrInstance);
if (XR_SUCCESS == result) {
    LOGI(LOG_TAG, "XR instance destroyed");
    engine->state.xrInstance = XR_NULL_HANDLE;
    engine->state.xrSysId = XR_NULL_SYSTEM_ID;
}

return result;
}

void app_query_layers_and_extensions(struct base_engine *engine,JavaVM* vm, void* context) {
//initial loader
PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
if (XR_SUCCEEDED(
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                          (PFN_xrVoidFunction*)(&initializeLoader)))) {
XrLoaderInitInfoAndroidKHR loaderInitInfoAndroid;
memset(&loaderInitInfoAndroid, 0, sizeof(loaderInitInfoAndroid));
loaderInitInfoAndroid.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
loaderInitInfoAndroid.next = NULL;
if (vm && context) {
  loaderInitInfoAndroid.applicationVM = vm;
  loaderInitInfoAndroid.applicationContext = context;
} else {
  loaderInitInfoAndroid.applicationVM = engine->app->activity->vm;
  loaderInitInfoAndroid.applicationContext = engine->app->activity->clazz;
}

initializeLoader((const XrLoaderInitInfoBaseHeaderKHR *)&loaderInitInfoAndroid);
}

// Query extension properties for a given layer.
const auto queryExtensions = [](const char *layerName, struct base_engine *engine) {
XrResult result;
uint32_t extensionCount = 0;
result = xrEnumerateInstanceExtensionProperties(layerName, 0,
                                                &extensionCount, nullptr);

if (XR_SUCCESS != result) {
  LOGE(LOG_TAG, "xrEnumerateInstanceExtensionProperties failed: %d",
       result);
  assert(0);
}

XrExtensionProperties extensionProperties[extensionCount];
for (uint16_t i = 0; i < extensionCount; i++) {
  extensionProperties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
  extensionProperties[i].next = nullptr;
}

result = xrEnumerateInstanceExtensionProperties(
    layerName, extensionCount, &extensionCount, extensionProperties);
if (XR_SUCCESS != result) {
  LOGE(LOG_TAG, "xrEnumerateInstanceExtensionProperties failed: %d",
       result);
  assert(0);
}

for (uint16_t i = 0; i < extensionCount; i++) {
  LOGI(LOG_TAG, "Supported XR extension: %s",
       extensionProperties[i].extensionName);
  if (strcmp(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME, extensionProperties[i].extensionName) == 0) {
      engine->depthInfo.supported = true;
  }
}
};

// Query non-layer extensions (layerName==nullptr).
queryExtensions(nullptr, engine);

// Query layers and any of their extensions.
{
XrResult result;
uint32_t layerCount = 0;
result = xrEnumerateApiLayerProperties(0, &layerCount, nullptr);

if (XR_SUCCESS != result) {
  LOGE(LOG_TAG, "xrEnumerateApiLayerProperties failed: %d", result);
  assert(0);
}

XrApiLayerProperties layerProperties[layerCount];
for (uint16_t i = 0; i < layerCount; i++) {
  layerProperties[i].type = XR_TYPE_API_LAYER_PROPERTIES;
  layerProperties[i].next = nullptr;
}

result =
    xrEnumerateApiLayerProperties(layerCount, &layerCount, layerProperties);
if (XR_SUCCESS != result) {
  LOGE(LOG_TAG, "xrEnumerateApiLayerProperties failed: %d", result);
  assert(0);
}

for (uint16_t i = 0; i < layerCount; i++) {
  LOGI(LOG_TAG, "Supported XR layer: %s", layerProperties[i].layerName);
  queryExtensions(layerProperties[i].layerName, engine);
}
}
}

void app_create_instance(XrInstanceCreateInfo *info, struct base_engine *engine)
{
XrResult result = xrCreateInstance(info, &engine->state.xrInstance);
if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrCreateInstance failed: %d", result);
    assert(0);
} else {
    LOGI(LOG_TAG, "Created new XR instance");
}
}

void app_get_instance_properties(struct base_engine *engine)
{
XrResult result = xrGetInstanceProperties(engine->state.xrInstance, &engine->runtimeInfo);
if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrGetInstanceProperties failed: %d", result);
    assert(0);
} else {
    LOGI(LOG_TAG, "Got instance properties");
}
}

void app_get_system_prop(struct base_engine *engine)
{
XrSystemGetInfo systemGetInfo = {
        .type = XR_TYPE_SYSTEM_GET_INFO,
        .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
        .next = nullptr};
XrResult result = xrGetSystem(engine->state.xrInstance, &systemGetInfo,
                              &engine->state.xrSysId);
if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrGetSystem failed: %d", result);
    assert(0);
}

XrSystemProperties systemProperties = {
        .type = XR_TYPE_SYSTEM_PROPERTIES,
        .next = nullptr,
        .graphicsProperties = {0},
        .trackingProperties = {0},
};
result = xrGetSystemProperties(engine->state.xrInstance,
                               engine->state.xrSysId, &systemProperties);
if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrGetSystemProperties failed: %d", result);
    assert(0);
} else {
    LOGI(LOG_TAG,
         "System properties for system %lu: \"%s\", vendor ID %d\n",
         systemProperties.systemId, systemProperties.systemName,
         systemProperties.vendorId);
    LOGI(LOG_TAG, "\tMax layers          : %d\n",
         systemProperties.graphicsProperties.maxLayerCount);
    LOGI(LOG_TAG, "\tMax swapchain height: %d\n",
         systemProperties.graphicsProperties.maxSwapchainImageHeight);
    LOGI(LOG_TAG, "\tMax swapchain width : %d\n",
         systemProperties.graphicsProperties.maxSwapchainImageWidth);
    LOGI(LOG_TAG, "\tOrientation Tracking: %d\n",
         systemProperties.trackingProperties.orientationTracking);
    LOGI(LOG_TAG, "\tPosition Tracking   : %d\n",
         systemProperties.trackingProperties.positionTracking);
}
}

void app_enum_view_configuration(struct base_engine *engine)
{
uint32_t viewConfigurationCount;
XrResult result = xrEnumerateViewConfigurations(
        engine->state.xrInstance, engine->state.xrSysId, 0,
        &viewConfigurationCount, nullptr);
if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrEnumerateViewConfigurations failed: %d", result);
    assert(0);
}

LOGI(LOG_TAG, "Runtime supports %d view configurations\n",
     viewConfigurationCount);

XrViewConfigurationType viewConfigurations[viewConfigurationCount];
for (uint32_t i = 0; i < viewConfigurationCount; ++i)
    viewConfigurations[i] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
result = xrEnumerateViewConfigurations(
        engine->state.xrInstance, engine->state.xrSysId,
        viewConfigurationCount, &viewConfigurationCount,
        viewConfigurations);
if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrEnumerateViewConfigurations failed: %d", result);
    assert(0);
}

// Ensure runtime supports XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO
XrViewConfigurationType stereoViewConfigType =
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
bool stereoIsSupported = false;
for (auto const &conf : viewConfigurations) {
    if (conf == stereoViewConfigType) {
        stereoIsSupported = true;
        break;
    }
}
if (stereoIsSupported)
    LOGI(LOG_TAG,
         "Runtime supports XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO");
else {
    LOGE(LOG_TAG, "Runtime doesn't support "
                  "XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO");
    assert(0);
}

result = xrEnumerateViewConfigurationViews(
        engine->state.xrInstance, engine->state.xrSysId,
        stereoViewConfigType, 0, &engine->state.viewCount, nullptr);
if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrEnumerateViewConfigurationViews failed: %d", result);
    assert(0);
}

// Assert 2 views for stereo
assert(2 == engine->state.viewCount);

engine->state.m_views.resize(engine->state.viewCount, {XR_TYPE_VIEW});

for (uint32_t i = 0; i < engine->state.viewCount; i++) {
    engine->state.viewConfigs[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    engine->state.viewConfigs[i].next = nullptr;
}

result = xrEnumerateViewConfigurationViews(
        engine->state.xrInstance, engine->state.xrSysId,
        stereoViewConfigType, engine->state.viewCount,
        &engine->state.viewCount, engine->state.viewConfigs);
if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrEnumerateViewConfigurationViews failed: %d", result);
    assert(0);
}

LOGI(LOG_TAG, "View count: %d\n", engine->state.viewCount);
for (uint32_t i = 0; i < engine->state.viewCount; ++i) {
    LOGI(LOG_TAG, "View %d:\n", i);
    LOGI(LOG_TAG, "\tResolution       : Recommended %dx%d, Max: %dx%d\n",
         engine->state.viewConfigs[i].recommendedImageRectWidth,
         engine->state.viewConfigs[i].recommendedImageRectHeight,
         engine->state.viewConfigs[i].maxImageRectWidth,
         engine->state.viewConfigs[i].maxImageRectHeight);
    LOGI(LOG_TAG, "\tSwapchain Samples: Recommended: %d, Max: %d\n",
         engine->state.viewConfigs[i].recommendedSwapchainSampleCount,
         engine->state.viewConfigs[i].maxSwapchainSampleCount);
}
}

void app_create_session(XrSessionCreateInfo *info, struct base_engine *engine)
{
PFN_xrGetOpenGLESGraphicsRequirementsKHR
        pfnGetOpenGLESGraphicsRequirementsKHR = nullptr;

XrResult result = xrGetInstanceProcAddr(
        engine->state.xrInstance, "xrGetOpenGLESGraphicsRequirementsKHR",
        reinterpret_cast<PFN_xrVoidFunction *>(
                &pfnGetOpenGLESGraphicsRequirementsKHR));
if (XR_SUCCESS != result) {
    LOGW(LOG_TAG,
         "xrGetInstanceProcAddr  PFN_xrGetOpenGLESGraphicsRequirementsKHR "
         "failed: %d",
         result);
}
XrGraphicsRequirementsOpenGLESKHR graphicsRequirements{
        XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};

result = pfnGetOpenGLESGraphicsRequirementsKHR(engine->state.xrInstance,
                                               engine->state.xrSysId,
                                               &graphicsRequirements);

if (XR_SUCCESS != result) {
    LOGW(LOG_TAG, "xrGetOpenGLESGraphicsRequirementsKHR failed: %d",
         result);
}

// Create XR session
result = xrCreateSession(engine->state.xrInstance, info,
                         &engine->state.xrSession);
if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrCreateSession failed");
    assert(0);
} else {
    LOGI(LOG_TAG, "Created new XR session");
}
}

void app_create_space(XrReferenceSpaceCreateInfo *info,
                  struct base_engine *engine)
{
XrResult result = XR_SUCCESS;
switch (info->referenceSpaceType) {
case XR_REFERENCE_SPACE_TYPE_LOCAL: {
    result = xrCreateReferenceSpace(engine->state.xrSession, info,
                                    &engine->state.xrLocalSpace);
    break;
}
case XR_REFERENCE_SPACE_TYPE_VIEW: {
    result = xrCreateReferenceSpace(engine->state.xrSession, info,
                                    &engine->state.xrViewSpace);
    break;
}
case XR_REFERENCE_SPACE_TYPE_STAGE:
    break;
case XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT:
    result = xrCreateReferenceSpace(engine->state.xrSession, info, &engine->state.xrRootSpace);
    break;
case XR_REFERENCE_SPACE_TYPE_MAX_ENUM:
    break;
case XR_REFERENCE_SPACE_TYPE_COMBINED_EYE_VARJO:
    break;
}

if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "Created XR space failed: %d", result);
    assert(0);
} else {
    LOGI(LOG_TAG, "New XR space created");
}
}

void app_enum_sc_format(struct base_engine *engine)
{
uint32_t swapchainFormatCount;
XrResult result = xrEnumerateSwapchainFormats(
        engine->state.xrSession, 0, &swapchainFormatCount, nullptr);
if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrEnumerateSwapchainFormats failed");
    assert(0);
}

LOGI(LOG_TAG, "Runtime supports %d swapchain formats\n",
     swapchainFormatCount);
int64_t swapchainFormats[swapchainFormatCount];
result = xrEnumerateSwapchainFormats(
        engine->state.xrSession, swapchainFormatCount,
        &swapchainFormatCount, swapchainFormats);

if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrEnumerateSwapchainFormats failed");
    assert(0);
}

// Ensure RGBA8 is support
bool rgba8Supported = false;
for (int32_t i = 0; i < swapchainFormatCount; ++i) {
    if (GL_RGBA8 == swapchainFormats[i]) {
        rgba8Supported = true;
        break;
    }
}
if (!rgba8Supported) {
    LOGE(LOG_TAG, "RGBA8 not supported");
    assert(0);
}
LOGI(LOG_TAG, "RGBA8 format supported by the runtime");
}

void app_create_swapchain(XrSwapchainCreateInfo *info,
                      XrSession *session,
                      Swapchain *sc)
{
u_int32_t sc_length;
XrResult result = xrCreateSwapchain(*session, info, &sc->xrSwapchain);
if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrCreateSwapchain failed: %d", result);
    assert(0);
}

result =
        xrEnumerateSwapchainImages(sc->xrSwapchain, 0, &sc_length, nullptr);
if (XR_SUCCESS != result) {
    LOGW(LOG_TAG, "xrEnumerateSwapchainImages failed: %d", result);
    assert(0);
}

sc->xrImages.resize(sc_length);
sc->glFramebuffers.resize(sc_length);
for (uint32_t j = 0; j < sc_length; j++) {
    sc->xrImages[j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    sc->xrImages[j].next = nullptr;
}
result = xrEnumerateSwapchainImages(
        sc->xrSwapchain, sc_length, &sc_length,
        (XrSwapchainImageBaseHeader *)&sc->xrImages[0]);

if (XR_SUCCESS != result) {
    LOGW(LOG_TAG, "xrEnumerateSwapchainImages failed: %d", result);
    assert(0);
}

glGenFramebuffers(sc_length, &sc->glFramebuffers[0]);
}

void app_create_swapchain_depth(XrSwapchainCreateInfo *info,
                      XrSession *session,
                      Swapchain *sc)
{
u_int32_t sc_length;
XrResult result = xrCreateSwapchain(*session, info, &sc->xrSwapchainDepth);
if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrCreateSwapchain depth failed: %d", result);
    assert(0);
}

result =
        xrEnumerateSwapchainImages(sc->xrSwapchainDepth, 0, &sc_length, nullptr);
if (XR_SUCCESS != result) {
    LOGW(LOG_TAG, "xrEnumerateSwapchainImages depth failed: %d", result);
    assert(0);
}

sc->xrImagesDepth.resize(sc_length);
for (uint32_t j = 0; j < sc_length; j++) {
    sc->xrImagesDepth[j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    sc->xrImagesDepth[j].next = nullptr;
}
result = xrEnumerateSwapchainImages(
        sc->xrSwapchainDepth, sc_length, &sc_length,
        (XrSwapchainImageBaseHeader *)&sc->xrImagesDepth[0]);

if (XR_SUCCESS != result) {
    LOGW(LOG_TAG, "xrEnumerateSwapchainImages depth failed: %d", result);
    assert(0);
}
}


void app_init_depth_buffer(struct base_engine *engine)
{
// Init depth buffer
glGenTextures(1, &engine->depthBuffer);
glBindTexture(GL_TEXTURE_2D, engine->depthBuffer);
glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16,
             engine->state.viewConfigs[0].recommendedImageRectWidth,
             engine->state.viewConfigs[0].recommendedImageRectHeight, 0,
             GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 0);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
glBindTexture(GL_TEXTURE_2D, 0);
if (engine->depthBuffer == 0) {
    LOGE(LOG_TAG, "Failed to init depth buffer");
    assert(0);
}
}

void app_destroy_swapchain(Swapchain *sc)
{
for (auto &framebuf : sc->glFramebuffers)
    glDeleteFramebuffers(1, &framebuf);

XrResult result = xrDestroySwapchain(sc->xrSwapchain);
sc->xrSwapchain = XR_NULL_HANDLE;

if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrDestroySwapchain failed");
    assert(0);
}
}


void app_destroy_swapchain_depth(Swapchain *sc)
{
XrResult result = xrDestroySwapchain(sc->xrSwapchainDepth);
sc->xrSwapchainDepth = XR_NULL_HANDLE;

if (XR_SUCCESS != result) {
    LOGE(LOG_TAG, "xrDestroySwapchain depth failed");
    assert(0);
}
}


void array2matrix(XrMatrix4x4f array, glm::mat4 &proj_mat)
{
proj_mat[0][0] = array.m[0];
proj_mat[0][1] = array.m[1];
proj_mat[0][2] = array.m[2];
proj_mat[0][3] = array.m[3];
proj_mat[1][0] = array.m[4];
proj_mat[1][1] = array.m[5];
proj_mat[1][2] = array.m[6];
proj_mat[1][3] = array.m[7];
proj_mat[2][0] = array.m[8];
proj_mat[2][1] = array.m[9];
proj_mat[2][2] = array.m[10];
proj_mat[2][3] = array.m[11];
proj_mat[3][0] = array.m[12];
proj_mat[3][1] = array.m[13];
proj_mat[3][2] = array.m[14];
proj_mat[3][3] = array.m[15];
}
} // namespace AppCommon
