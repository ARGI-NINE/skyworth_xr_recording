//
// Created by Administrator on 2022/5/7.
//

#pragma once
#include "openxr_extension_helpers.h"

#define XR_KHR_SKYWORTH_Quli 1
#define XR_KHR_SKYWORTH_Quli_SPEC_VERSION 1
#define XR_KHR_SKYWORTH_QULI_EXTENSION_NAME "XR_KHR_SKYWORTH_Quli"

XR_RESULT_ENUM(XR_ERROR_GETGROUNDHEIGHT,-1200003002);
typedef struct XrGroundHeightStatus{
    /*-1 高度异常，重新获取
     * 0 正常*/
    int status{0};
    float groundHeight{0.0};
}XrGroundHeightStatus;
typedef XrResult (XRAPI_PTR *PFN_xrGetOfflineMapRelocStateSKYWORTH)(
        XrSession                              session,
        int*    State);
typedef XrResult (XRAPI_PTR *PFN_xrStartSeeThroughSKYWORTH)(XrSession session);
typedef XrResult (XRAPI_PTR *PFN_xrStopSeeThroughSKYWORTH)(XrSession session);
typedef XrResult (XRAPI_PTR *PFN_xrResaveMapSKYWORTH)(XrSession session,const char* forTest);
typedef XrResult (XRAPI_PTR *PFN_xrSaveMapSKYWORTH)(XrSession session);
typedef XrResult (XRAPI_PTR *PFN_xrRecenterPoseSKYWORTH)(XrSession session);
typedef XrResult (XRAPI_PTR *PFN_xrSet6DofSKYWORTH)(XrSession session,bool is6dof,bool yawOnly);
typedef XrResult (XRAPI_PTR *PFN_xrGet6DofSKYWORTH)(XrSession session,bool* is6dof);
typedef XrResult (XRAPI_PTR *PFN_xrGetGroundHeightSKYWORTH)(XrSession session,XrGroundHeightStatus* heightStatus);
typedef XrResult (XRAPI_PTR *PFN_xrSetBoundaryEnableSKYWORTH)(XrInstance instance,bool enable);
typedef XrResult (XRAPI_PTR *PFN_xrGetFocusSKYWORTH)(XrSession session,bool* focus);
#ifndef XR_NO_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrGetOfflineMapRelocStateSKYWORTH(XrSession session,int* State);
XRAPI_ATTR XrResult XRAPI_CALL xrStartSeeThroughSKYWORTH(XrSession session);
XRAPI_ATTR XrResult XRAPI_CALL xrStopSeeThroughSKYWORTH(XrSession session);
XRAPI_ATTR XrResult XRAPI_CALL xrResaveMapSKYWORTH(XrSession session,const char* forTest);
XRAPI_ATTR XrResult XRAPI_CALL xrSaveMapSKYWORTH(XrSession session);
XRAPI_ATTR XrResult XRAPI_CALL xrRecenterPoseSKYWORTH(XrSession session);
XRAPI_ATTR XrResult XRAPI_CALL xrSet6DofSKYWORTH(XrSession session,bool is6dof,bool yawOnly);
XRAPI_ATTR XrResult XRAPI_CALL xrGet6DofSKYWORTH(XrSession session,bool* is6dof);
XRAPI_ATTR XrResult XRAPI_CALL xrGetGroundHeightSKYWORTH(XrSession session,XrGroundHeightStatus* heightStatus);
XRAPI_ATTR XrResult XRAPI_CALL xrSetBoundaryEnableSKYWORTH(XrInstance instance,bool enable);
XRAPI_ATTR XrResult XRAPI_CALL xrGetFocusSKYWORTH(XrSession session,bool* focus);
#endif

#define XR_KHR_SKYWORTH_reset_sensor  1
#define XR_KHR_SKYWORTH_reset_sensor_SPEC_VERSION 1
#define XR_KHR_SKYWORTH_RESET_SENSOR_EXTENSION_NAME "XR_KHR_SKYWORTH_reset_sensor"
typedef XrResult (XRAPI_PTR *PFN_xrDisableRuntimeResetSensorSKYWORTH)(XrSession session);
typedef XrResult (XRAPI_PTR *PFN_xrGetRuntimeResetSensorCountSKYWORTH)(XrSession session,uint32_t* count);
#ifndef XR_NO_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrDisableRuntimeResetSensorSKYWORTH(XrSession session);
XRAPI_ATTR XrResult XRAPI_CALL xrGetRuntimeResetSensorCountSKYWORTH(XrSession session,uint32_t* count);
#endif

#define XR_KHR_SKYWORTH_foveation 1
#define XR_KHR_SKYWORTH_foveation_SPEC_VERSION 1
#define XR_KHR_SKYWORTH_FOVEATION_EXTENSION_NAME "XR_KHR_SKYWORTH_foveation"

typedef enum XrFoveationLevel{
    XR_FOVEATION_LEVEL_NONE = -1,
    XR_FOVEATION_LEVEL_LOW = 0,
    XR_FOVEATION_LEVEL_MID = 1,
    XR_FOVEATION_LEVEL_HIGH = 2,
    XR_FOVEATION_LEVEL_TOP_HIGH = 3
}XrFoveationLevel;

XR_STRUCT_ENUM(XR_TYPE_EVENT_FOVEATION_LEVEL_CHANGED,1200006068);
typedef struct XrFoveationParameters {
    XrStructureType         type;
    void* XR_MAY_ALIAS      next;
    XrFoveationLevel        level;
    uint32_t                layer;
    XrVector2f              focal;
} XrFoveationParameters;

typedef XrResult (XRAPI_ATTR *PFN_xrTextureFoveationParametersSKYWORTH)(XrSwapchain swapchain,const XrFoveationParameters* xrFoveationParameters);

#ifndef XR_NO_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrTextureFoveationParametersSKYWORTH(XrSwapchain swapchain,const XrFoveationParameters* xrFoveationParameters);
#endif

#define XR_KHR_SKYWORTH_color_space 1
#define XR_KHR_SKYWORTH_color_space_SPEC_VERSION 1
#define XR_KHR_SKYWORTH_COLOR_SPACE_EXTENSION_NAME "XR_KHR_SKYWORTH_color_space"

XR_RESULT_ENUM(XR_ERROR_SESSION_NOT_CREATED,-1200003000);
typedef enum XrColorSpaceSKYWORTH {
    XR_COLOR_SPACE_Linear = 0,
    XR_COLOR_SPACE_SRGB = 1,
    XR_NumColorSpaces
} XrColorSpaceSKYWORTH;

typedef XrResult (XRAPI_ATTR *PFN_xrSetColorSpaceSKYWORTH)(XrSession session,const XrColorSpaceSKYWORTH colorspace);
#ifndef XR_NO_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrSetColorSpaceSKYWORTH(XrSession session,const XrColorSpaceSKYWORTH colorspace);
#endif

XR_STRUCT_ENUM(XR_TYPE_FOV_FRUSTUM_SXR,1200006068);
typedef struct XrFrustum{
    XrStructureType         type;
    float               left;           //!< Left Plane of Frustum
    float               right;          //!< Right Plane of Frustum
    float               top;            //!< Top Plane of Frustum
    float               bottom;         //!< Bottom Plane of Frustum

    float               near;           //!< Near Plane of Frustum
    float               _far;            //!< Far Plane of Frustum (Arbitrary)
} XrFrustum;

#define XR_KHR_SKYWORTH_nativewindow 1
#define XR_KHR_SKYWORTH_nativewindow_SPEC_VERSION 1
#define XR_KHR_SKYWORTH_NATIVEWINDOW_EXTENSION_NAME "XR_KHR_SKYWORTH_nativewindow"
XR_RESULT_ENUM(XR_ERROR_NO_NATIVEWINDOW,-1200003001);

