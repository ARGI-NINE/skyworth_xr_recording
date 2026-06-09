#ifndef OPENXR_QCOM_H_
#define OPENXR_QCOM_H_ 1

/******************************************************************************
 * File: openxr_qcom.h
 * Copyright (c) 2023 Qualcomm Technologies, Inc. and/or its subsidiaries. All rights reserved.
 *
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 *
 ******************************************************************************/

#include <openxr/openxr.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XR_QCOM_image_tracking 1
XR_DEFINE_HANDLE(XrImageTargetQCOM)
XR_DEFINE_HANDLE(XrImageTrackerQCOM)
#define XR_QCOM_image_tracking_SPEC_VERSION 2
#define XR_QCOM_IMAGE_TRACKING_EXTENSION_NAME "XR_QCOM_image_tracking"

typedef enum XrImageTargetTrackingModeQCOM {
    XR_IMAGE_TARGET_TRACKING_MODE_DYNAMIC_QCOM = 0,
    XR_IMAGE_TARGET_TRACKING_MODE_STATIC_QCOM = 1,
    XR_IMAGE_TARGET_TRACKING_MODE_ADAPTIVE_QCOM = 2,
    XR_IMAGE_TARGET_TRACKING_MODE_DISABLED_QCOM = 3,
    XR_IMAGE_TARGET_TRACKING_MODE_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrImageTargetTrackingModeQCOM;
// XrSystemImageTrackingPropertiesQCOM extends XrSystemProperties
typedef struct XrSystemImageTrackingPropertiesQCOM {
    XrStructureType                  type;
    void* XR_MAY_ALIAS               next;
    XrBool32                         supportsImageTracking;
    XrImageTargetTrackingModeQCOM    defaultTrackingMode;
} XrSystemImageTrackingPropertiesQCOM;

typedef struct XR_MAY_ALIAS XrImageTrackerDataSetBaseHeaderQCOM {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
} XrImageTrackerDataSetBaseHeaderQCOM;

typedef struct XrImageTrackerDataSetCollectionQCOM {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    uint8_t*                    buffer;
    uint32_t                    bufferSize;
} XrImageTrackerDataSetCollectionQCOM;

// XrImageTrackerHeightSetCollectionQCOM extends XrImageTrackerDataSetCollectionQCOM
typedef struct XrImageTrackerHeightSetCollectionQCOM {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    const char**                names;
    float*                      heights;
    uint32_t                    count;
} XrImageTrackerHeightSetCollectionQCOM;

typedef struct XrImageTrackerDataSetImageQCOM {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    const char*                 name;
    float                       height;
    XrVector2f                  pixelSize;
    uint8_t*                    buffer;
    uint32_t                    bufferSize;
} XrImageTrackerDataSetImageQCOM;

typedef struct XrImageTrackerCreateInfoQCOM {
    XrStructureType                                      type;
    const void* XR_MAY_ALIAS                             next;
    uint32_t                                             dataSetCount;
    const XrImageTrackerDataSetBaseHeaderQCOM* const*    dataSets;
    uint32_t                                             imageCount;
} XrImageTrackerCreateInfoQCOM;

// XrImageTargetsTrackingModeInfoQCOM extends XrImageTrackerCreateInfoQCOM
typedef struct XrImageTargetsTrackingModeInfoQCOM {
    XrStructureType                         type;
    const void* XR_MAY_ALIAS                next;
    uint32_t                                targetCount;
    const char**                            targetNames;
    const XrImageTargetTrackingModeQCOM*    targetModes;
} XrImageTargetsTrackingModeInfoQCOM;

typedef struct XrImageTargetsLocateInfoQCOM {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrSpace                     baseSpace;
    XrTime                      time;
} XrImageTargetsLocateInfoQCOM;

typedef struct XrImageTargetLocationQCOM {
    XrSpaceLocationFlags    locationFlags;
    XrPosef                 pose;
    XrImageTargetQCOM       imageTarget;
} XrImageTargetLocationQCOM;

typedef struct XrImageTargetVelocityQCOM {
    XrSpaceVelocityFlags    velocityFlags;
    XrVector3f              linearVelocity;
    XrVector3f              angularVelocity;
    XrImageTargetQCOM       imageTarget;
} XrImageTargetVelocityQCOM;

typedef struct XrImageTargetLocationsQCOM {
    XrStructureType               type;
    void* XR_MAY_ALIAS            next;
    XrBool32                      isActive;
    uint32_t                      imageCount;
    XrImageTargetLocationQCOM*    imageTargetLocations;
} XrImageTargetLocationsQCOM;

// XrImageTargetVelocitiesQCOM extends XrImageTargetLocationsQCOM
typedef struct XrImageTargetVelocitiesQCOM {
    XrStructureType               type;
    void* XR_MAY_ALIAS            next;
    uint32_t                      imageCount;
    XrImageTargetVelocityQCOM*    imageTargetVelocities;
} XrImageTargetVelocitiesQCOM;

typedef XrResult (XRAPI_PTR *PFN_xrEnumerateImageTargetTrackingModesQCOM)(XrSession session, uint32_t trackingModeCapacityInput, uint32_t* trackingModeCountOutput, XrImageTargetTrackingModeQCOM* trackingModes);
typedef XrResult (XRAPI_PTR *PFN_xrCreateImageTrackerQCOM)(XrSession session, const XrImageTrackerCreateInfoQCOM* createInfo, XrImageTrackerQCOM* imageTracker);
typedef XrResult (XRAPI_PTR *PFN_xrDestroyImageTrackerQCOM)(XrImageTrackerQCOM imageTracker);
typedef XrResult (XRAPI_PTR *PFN_xrLocateImageTargetsQCOM)(XrImageTrackerQCOM imageTracker, const XrImageTargetsLocateInfoQCOM* locateInfo, XrImageTargetLocationsQCOM* locations);
typedef XrResult (XRAPI_PTR *PFN_xrImageTargetToNameAndIdQCOM)(XrImageTargetQCOM imageTarget, uint32_t bufferCapacityInput, uint32_t* bufferCountOutput, char* buffer, uint32_t* id);
typedef XrResult (XRAPI_PTR *PFN_xrSetImageTargetsTrackingModeQCOM)(XrImageTrackerQCOM imageTracker, const XrImageTargetsTrackingModeInfoQCOM* trackingModeInfo);
typedef XrResult (XRAPI_PTR *PFN_xrStopImageTargetTrackingQCOM)(XrImageTargetQCOM imageTarget);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateImageTargetTrackingModesQCOM(
    XrSession                                   session,
    uint32_t                                    trackingModeCapacityInput,
    uint32_t*                                   trackingModeCountOutput,
    XrImageTargetTrackingModeQCOM*              trackingModes);

XRAPI_ATTR XrResult XRAPI_CALL xrCreateImageTrackerQCOM(
    XrSession                                   session,
    const XrImageTrackerCreateInfoQCOM*         createInfo,
    XrImageTrackerQCOM*                         imageTracker);

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyImageTrackerQCOM(
    XrImageTrackerQCOM                          imageTracker);

XRAPI_ATTR XrResult XRAPI_CALL xrLocateImageTargetsQCOM(
    XrImageTrackerQCOM                          imageTracker,
    const XrImageTargetsLocateInfoQCOM*         locateInfo,
    XrImageTargetLocationsQCOM*                 locations);

XRAPI_ATTR XrResult XRAPI_CALL xrImageTargetToNameAndIdQCOM(
    XrImageTargetQCOM                           imageTarget,
    uint32_t                                    bufferCapacityInput,
    uint32_t*                                   bufferCountOutput,
    char*                                       buffer,
    uint32_t*                                   id);

XRAPI_ATTR XrResult XRAPI_CALL xrSetImageTargetsTrackingModeQCOM(
    XrImageTrackerQCOM                          imageTracker,
    const XrImageTargetsTrackingModeInfoQCOM*   trackingModeInfo);

XRAPI_ATTR XrResult XRAPI_CALL xrStopImageTargetTrackingQCOM(
    XrImageTargetQCOM                           imageTarget);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */

#define XR_QCOM_object_tracking 1
XR_DEFINE_HANDLE(XrObjectTrackerDataSetQCOM)
XR_DEFINE_HANDLE(XrObjectTargetQCOM)
XR_DEFINE_HANDLE(XrObjectTrackerQCOM)
#define XR_QCOM_object_tracking_SPEC_VERSION 2
#define XR_QCOM_OBJECT_TRACKING_EXTENSION_NAME "XR_QCOM_object_tracking"

typedef enum XrObjectTargetTrackingModeQCOM {
    XR_OBJECT_TARGET_TRACKING_MODE_DYNAMIC_QCOM = 0,
    XR_OBJECT_TARGET_TRACKING_MODE_STATIC_QCOM = 1,
    XR_OBJECT_TARGET_TRACKING_MODE_ADAPTIVE_QCOM = 2,
    XR_OBJECT_TARGET_TRACKING_MODE_DISABLED_QCOM = 3,
    XR_OBJECT_TARGET_TRACKING_MODE_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrObjectTargetTrackingModeQCOM;

typedef enum XrObjectTrackerStateQCOM {
    XR_OBJECT_TRACKER_STATE_NONE_QCOM = 0,
    XR_OBJECT_TRACKER_STATE_INITIALIZING_QCOM = 1,
    XR_OBJECT_TRACKER_STATE_TRACKING_QCOM = 2,
    XR_OBJECT_TRACKER_STATE_ERROR_QCOM = 3,
    XR_OBJECT_TRACKER_STATE_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrObjectTrackerStateQCOM;

typedef enum XrObjectTrackerDataSetTypeQCOM {
    XR_OBJECT_TRACKER_DATA_SET_TYPE_COLLECTION_QCOM = 0,
    XR_OBJECT_TRACKER_DATA_SET_TYPE_OBJECT_QCOM = 1,
    XR_OBJECT_TRACKER_DATA_SET_TYPE_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrObjectTrackerDataSetTypeQCOM;

typedef enum XrObjectTrackerDataSetStateQCOM {
    XR_OBJECT_TRACKER_DATA_SET_STATE_NONE_QCOM = 0,
    XR_OBJECT_TRACKER_DATA_SET_STATE_LOADING_QCOM = 1,
    XR_OBJECT_TRACKER_DATA_SET_STATE_READY_QCOM = 2,
    XR_OBJECT_TRACKER_DATA_SET_STATE_ERROR_QCOM = 3,
    XR_OBJECT_TRACKER_DATA_SET_STATE_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrObjectTrackerDataSetStateQCOM;
typedef XrFlags64 XrObjectTargetBoxBoundFlagsQCOM;

// Flag bits for XrObjectTargetBoxBoundFlagsQCOM
static const XrObjectTargetBoxBoundFlagsQCOM XR_OBJECT_TARGET_BOX_BOUND_EXTENTS_VALID_BIT_QCOM = 0x00000001;

// XrSystemObjectTrackingPropertiesQCOM extends XrSystemProperties
typedef struct XrSystemObjectTrackingPropertiesQCOM {
    XrStructureType                   type;
    void* XR_MAY_ALIAS                next;
    XrBool32                          supportsObjectTracking;
    XrObjectTargetTrackingModeQCOM    defaultTrackingMode;
} XrSystemObjectTrackingPropertiesQCOM;

typedef struct XrObjectTrackerDataSetCreateInfoQCOM {
    XrStructureType                   type;
    const void* XR_MAY_ALIAS          next;
    XrObjectTrackerDataSetTypeQCOM    dataSetType;
    const char*                       name;
    uint8_t*                          buffer;
    uint32_t                          bufferSize;
} XrObjectTrackerDataSetCreateInfoQCOM;

typedef struct XrObjectTrackerCreateInfoQCOM {
    XrStructureType                      type;
    const void* XR_MAY_ALIAS             next;
    uint32_t                             dataSetCount;
    const XrObjectTrackerDataSetQCOM*    dataSets;
    uint32_t                             objectCount;
} XrObjectTrackerCreateInfoQCOM;

// XrObjectTargetsTrackingModeInfoQCOM extends XrObjectTrackerCreateInfoQCOM
typedef struct XrObjectTargetsTrackingModeInfoQCOM {
    XrStructureType                          type;
    const void* XR_MAY_ALIAS                 next;
    uint32_t                                 targetCount;
    const char**                             targetNames;
    const XrObjectTargetTrackingModeQCOM*    targetModes;
} XrObjectTargetsTrackingModeInfoQCOM;

typedef struct XrObjectTargetsLocateInfoQCOM {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrSpace                     baseSpace;
    XrTime                      time;
} XrObjectTargetsLocateInfoQCOM;

typedef struct XrObjectTargetLocationQCOM {
    XrSpaceLocationFlags    locationFlags;
    XrPosef                 pose;
    XrObjectTargetQCOM      objectTarget;
} XrObjectTargetLocationQCOM;

typedef struct XrObjectTargetVelocityQCOM {
    XrSpaceVelocityFlags    velocityFlags;
    XrVector3f              linearVelocity;
    XrVector3f              angularVelocity;
    XrObjectTargetQCOM      objectTarget;
} XrObjectTargetVelocityQCOM;

typedef struct XrObjectTargetBoxBoundQCOM {
    XrObjectTargetBoxBoundFlagsQCOM    boxBoundFlags;
    XrVector3f                         extents;
    XrObjectTargetQCOM                 objectTarget;
} XrObjectTargetBoxBoundQCOM;

typedef struct XrObjectTargetLocationsQCOM {
    XrStructureType                type;
    void* XR_MAY_ALIAS             next;
    XrBool32                       isActive;
    uint32_t                       objectCount;
    XrObjectTargetLocationQCOM*    objectTargetLocations;
} XrObjectTargetLocationsQCOM;

// XrObjectTargetVelocitiesQCOM extends XrObjectTargetLocationsQCOM
typedef struct XrObjectTargetVelocitiesQCOM {
    XrStructureType                type;
    void* XR_MAY_ALIAS             next;
    uint32_t                       objectCount;
    XrObjectTargetVelocityQCOM*    objectTargetVelocities;
} XrObjectTargetVelocitiesQCOM;

// XrObjectTargetBoxBoundsQCOM extends XrObjectTargetLocationsQCOM
typedef struct XrObjectTargetBoxBoundsQCOM {
    XrStructureType                type;
    void* XR_MAY_ALIAS             next;
    uint32_t                       objectCount;
    XrObjectTargetBoxBoundQCOM*    objectTargetBoxBounds;
} XrObjectTargetBoxBoundsQCOM;

typedef struct XrObjectTrackerDataSetInfoQCOM {
    uint32_t     targetCount;
    uint32_t*    targetNameLengths;
    char**       targetNames;
} XrObjectTrackerDataSetInfoQCOM;

typedef struct XrObjectTrackerDataSetsInfoQCOM {
    uint32_t                           dataSetCount;
    XrObjectTrackerDataSetInfoQCOM*    dataSetInfos;
} XrObjectTrackerDataSetsInfoQCOM;

typedef struct XrObjectTrackerInfoQCOM {
    XrObjectTrackerDataSetsInfoQCOM    dataSetsInfo;
} XrObjectTrackerInfoQCOM;

typedef XrResult (XRAPI_PTR *PFN_xrEnumerateObjectTargetTrackingModesQCOM)(XrSession session, uint32_t trackingModeCapacityInput, uint32_t* trackingModeCountOutput, XrObjectTargetTrackingModeQCOM* trackingModes);
typedef XrResult (XRAPI_PTR *PFN_xrCreateObjectTrackerDataSetQCOM)(XrSession session, const XrObjectTrackerDataSetCreateInfoQCOM* createInfo, XrObjectTrackerDataSetQCOM* dataSet);
typedef XrResult (XRAPI_PTR *PFN_xrDestroyObjectTrackerDataSetQCOM)(XrObjectTrackerDataSetQCOM dataSet);
typedef XrResult (XRAPI_PTR *PFN_xrGetObjectTrackerDataSetStateQCOM)(XrObjectTrackerDataSetQCOM dataSet, XrObjectTrackerDataSetStateQCOM* state);
typedef XrResult (XRAPI_PTR *PFN_xrCreateObjectTrackerQCOM)(XrSession session, const XrObjectTrackerCreateInfoQCOM* createInfo, XrObjectTrackerQCOM* objectTracker);
typedef XrResult (XRAPI_PTR *PFN_xrDestroyObjectTrackerQCOM)(XrObjectTrackerQCOM objectTracker);
typedef XrResult (XRAPI_PTR *PFN_xrGetObjectTrackerStateQCOM)(XrObjectTrackerQCOM objectTracker, XrObjectTrackerStateQCOM* state);
typedef XrResult (XRAPI_PTR *PFN_xrLocateObjectTargetsQCOM)(XrObjectTrackerQCOM objectTracker, const XrObjectTargetsLocateInfoQCOM* locateInfo, XrObjectTargetLocationsQCOM* locations);
typedef XrResult (XRAPI_PTR *PFN_xrObjectTargetToNameAndIdQCOM)(XrObjectTargetQCOM objectTarget, uint32_t bufferCapacityInput, uint32_t* bufferCountOutput, char* buffer, uint32_t* id);
typedef XrResult (XRAPI_PTR *PFN_xrQueryObjectTrackerInfoQCOM)(XrObjectTrackerQCOM objectTracker, XrObjectTrackerInfoQCOM* trackerInfo);
typedef XrResult (XRAPI_PTR *PFN_xrSetObjectTargetsTrackingModeQCOM)(XrObjectTrackerQCOM objectTracker, const XrObjectTargetsTrackingModeInfoQCOM* trackingModeInfo);
typedef XrResult (XRAPI_PTR *PFN_xrStopObjectTargetTrackingQCOM)(XrObjectTargetQCOM objectTarget);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateObjectTargetTrackingModesQCOM(
    XrSession                                   session,
    uint32_t                                    trackingModeCapacityInput,
    uint32_t*                                   trackingModeCountOutput,
    XrObjectTargetTrackingModeQCOM*             trackingModes);

XRAPI_ATTR XrResult XRAPI_CALL xrCreateObjectTrackerDataSetQCOM(
    XrSession                                   session,
    const XrObjectTrackerDataSetCreateInfoQCOM* createInfo,
    XrObjectTrackerDataSetQCOM*                 dataSet);

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyObjectTrackerDataSetQCOM(
    XrObjectTrackerDataSetQCOM                  dataSet);

XRAPI_ATTR XrResult XRAPI_CALL xrGetObjectTrackerDataSetStateQCOM(
    XrObjectTrackerDataSetQCOM                  dataSet,
    XrObjectTrackerDataSetStateQCOM*            state);

XRAPI_ATTR XrResult XRAPI_CALL xrCreateObjectTrackerQCOM(
    XrSession                                   session,
    const XrObjectTrackerCreateInfoQCOM*        createInfo,
    XrObjectTrackerQCOM*                        objectTracker);

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyObjectTrackerQCOM(
    XrObjectTrackerQCOM                         objectTracker);

XRAPI_ATTR XrResult XRAPI_CALL xrGetObjectTrackerStateQCOM(
    XrObjectTrackerQCOM                         objectTracker,
    XrObjectTrackerStateQCOM*                   state);

XRAPI_ATTR XrResult XRAPI_CALL xrLocateObjectTargetsQCOM(
    XrObjectTrackerQCOM                         objectTracker,
    const XrObjectTargetsLocateInfoQCOM*        locateInfo,
    XrObjectTargetLocationsQCOM*                locations);

XRAPI_ATTR XrResult XRAPI_CALL xrObjectTargetToNameAndIdQCOM(
    XrObjectTargetQCOM                          objectTarget,
    uint32_t                                    bufferCapacityInput,
    uint32_t*                                   bufferCountOutput,
    char*                                       buffer,
    uint32_t*                                   id);

XRAPI_ATTR XrResult XRAPI_CALL xrQueryObjectTrackerInfoQCOM(
    XrObjectTrackerQCOM                         objectTracker,
    XrObjectTrackerInfoQCOM*                    trackerInfo);

XRAPI_ATTR XrResult XRAPI_CALL xrSetObjectTargetsTrackingModeQCOM(
    XrObjectTrackerQCOM                         objectTracker,
    const XrObjectTargetsTrackingModeInfoQCOM*  trackingModeInfo);

XRAPI_ATTR XrResult XRAPI_CALL xrStopObjectTargetTrackingQCOM(
    XrObjectTargetQCOM                          objectTarget);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */

#define XR_QCOM_plane_detection 1
XR_DEFINE_HANDLE(XrPlaneDetectionQCOM)
#define XR_QCOM_plane_detection_SPEC_VERSION 1
#define XR_QCOM_PLANE_DETECTION_EXTENSION_NAME "XR_QCOM_plane_detection"

typedef enum XrPlaneFilterQCOM {
    XR_PLANE_FILTER_HORIZONTAL_UPWARD_QCOM = 1,
    XR_PLANE_FILTER_HORIZONTAL_DOWNWARD_QCOM = 2,
    XR_PLANE_FILTER_HORIZONTAL_QCOM = 3,
    XR_PLANE_FILTER_VERTICAL_QCOM = 4,
    XR_PLANE_FILTER_ARBITRARY_QCOM = 5,
    XR_PLANE_FILTER_ANY_QCOM = 6,
    XR_PLANE_FILTER_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrPlaneFilterQCOM;

typedef enum XrPlaneDetectionStateQCOM {
    XR_PLANE_DETECTION_STATE_NONE_QCOM = 0,
    XR_PLANE_DETECTION_STATE_INITIALIZING_QCOM = 1,
    XR_PLANE_DETECTION_STATE_TRACKING_QCOM = 2,
    XR_PLANE_DETECTION_STATE_ERROR_QCOM = 3,
    XR_PLANE_DETECTION_STATE_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrPlaneDetectionStateQCOM;

typedef enum XrPlaneTypeQCOM {
    XR_PLANE_TYPE_HORIZONTAL_UPWARD_QCOM = 0,
    XR_PLANE_TYPE_HORIZONTAL_DOWNWARD_QCOM = 1,
    XR_PLANE_TYPE_VERTICAL_QCOM = 2,
    XR_PLANE_TYPE_ARBITRARY_QCOM = 3,
    XR_PLANE_TYPE_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrPlaneTypeQCOM;
// XrSystemPlaneDetectionPropertiesQCOM extends XrSystemProperties
typedef struct XrSystemPlaneDetectionPropertiesQCOM {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    XrBool32              supportsPlaneDetection;
} XrSystemPlaneDetectionPropertiesQCOM;

typedef struct XrPlaneDetectionCreateInfoQCOM {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrPlaneFilterQCOM           planeFilter;
    XrBool32                    enableConvexHull;
} XrPlaneDetectionCreateInfoQCOM;

typedef struct XrPlanesLocateInfoQCOM {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrSpace                     baseSpace;
    XrTime                      time;
} XrPlanesLocateInfoQCOM;

typedef struct XrExtent2DfQCOM {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    float                 min;
    float                 max;
} XrExtent2DfQCOM;

typedef struct XrPlaneExtentQCOM {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    XrExtent2DfQCOM       extentX;
    XrExtent2DfQCOM       extentY;
} XrPlaneExtentQCOM;

typedef struct XrPlaneLocationQCOM {
    XrSpaceLocationFlags    locationFlags;
    XrPosef                 pose;
    uint32_t                id;
    XrPlaneTypeQCOM         planeType;
    float                   confidence;
    XrPlaneExtentQCOM       size;
    uint64_t                convexHullBufferId;
} XrPlaneLocationQCOM;

typedef struct XrPlaneLocationsQCOM {
    XrStructureType         type;
    void* XR_MAY_ALIAS      next;
    uint32_t                planeCapacityInput;
    uint32_t*               planeCountOutput;
    XrPlaneLocationQCOM*    planeLocations;
} XrPlaneLocationsQCOM;

typedef struct XrPlaneConvexHullBufferInfoQCOM {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    uint64_t              convexHullBufferId;
} XrPlaneConvexHullBufferInfoQCOM;

typedef struct XrPlaneConvexHullVertexBufferQCOM {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    uint32_t              vertexCapacityInput;
    uint32_t*             vertexCountOutput;
    XrVector3f*           vertices;
} XrPlaneConvexHullVertexBufferQCOM;

typedef XrResult (XRAPI_PTR *PFN_xrCreatePlaneDetectionQCOM)(XrSession session, const XrPlaneDetectionCreateInfoQCOM* createInfo, XrPlaneDetectionQCOM* planeDetection);
typedef XrResult (XRAPI_PTR *PFN_xrDestroyPlaneDetectionQCOM)(XrPlaneDetectionQCOM planeDetection);
typedef XrResult (XRAPI_PTR *PFN_xrGetPlaneDetectionStateQCOM)(XrPlaneDetectionQCOM planeDetection, XrPlaneDetectionStateQCOM* state);
typedef XrResult (XRAPI_PTR *PFN_xrLocatePlanesQCOM)(XrPlaneDetectionQCOM planeDetection, const XrPlanesLocateInfoQCOM* locateInfo, XrPlaneLocationsQCOM* locations);
typedef XrResult (XRAPI_PTR *PFN_xrGetPlaneConvexHullVertexBufferQCOM)(XrPlaneDetectionQCOM planeDetection, const XrPlaneConvexHullBufferInfoQCOM* convexHullInfo, XrPlaneConvexHullVertexBufferQCOM* buffers);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrCreatePlaneDetectionQCOM(
    XrSession                                   session,
    const XrPlaneDetectionCreateInfoQCOM*       createInfo,
    XrPlaneDetectionQCOM*                       planeDetection);

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyPlaneDetectionQCOM(
    XrPlaneDetectionQCOM                        planeDetection);

XRAPI_ATTR XrResult XRAPI_CALL xrGetPlaneDetectionStateQCOM(
    XrPlaneDetectionQCOM                        planeDetection,
    XrPlaneDetectionStateQCOM*                  state);

XRAPI_ATTR XrResult XRAPI_CALL xrLocatePlanesQCOM(
    XrPlaneDetectionQCOM                        planeDetection,
    const XrPlanesLocateInfoQCOM*               locateInfo,
    XrPlaneLocationsQCOM*                       locations);

XRAPI_ATTR XrResult XRAPI_CALL xrGetPlaneConvexHullVertexBufferQCOM(
    XrPlaneDetectionQCOM                        planeDetection,
    const XrPlaneConvexHullBufferInfoQCOM*      convexHullInfo,
    XrPlaneConvexHullVertexBufferQCOM*          buffers);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */

#define XR_QCOM_tracking_optimization_settings 1
#define XR_QCOM_tracking_optimization_settings_SPEC_VERSION 1
#define XR_QCOM_TRACKING_OPTIMIZATION_SETTINGS_EXTENSION_NAME "XR_QCOM_tracking_optimization_settings"

typedef enum XrTrackingOptimizationSettingsDomainQCOM {
    XR_TRACKING_OPTIMIZATION_SETTINGS_DOMAIN_ALL_QCOM = 1,
    XR_TRACKING_OPTIMIZATION_SETTINGS_DOMAIN_IMAGE_TRACKING_QCOM = 1000303000,
    XR_TRACKING_OPTIMIZATION_SETTINGS_DOMAIN_OBJECT_TRACKING_QCOM = 1000304000,
    XR_TRACKING_OPTIMIZATION_SETTINGS_DOMAIN_PLANE_DETECTION_QCOM = 1000305000,
    XR_TRACKING_OPTIMIZATION_SETTINGS_DOMAIN_MARKER_TRACKING_QCOMX = 1000312000,
    XR_TRACKING_OPTIMIZATION_SETTINGS_DOMAIN_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrTrackingOptimizationSettingsDomainQCOM;

typedef enum XrTrackingOptimizationSettingsHintQCOM {
    XR_TRACKING_OPTIMIZATION_SETTINGS_HINT_NONE_QCOM = 0,
    XR_TRACKING_OPTIMIZATION_SETTINGS_HINT_LONG_RANGE_PRIORIZATION_QCOM = 1,
    XR_TRACKING_OPTIMIZATION_SETTINGS_HINT_CLOSE_RANGE_PRIORIZATION_QCOM = 2,
    XR_TRACKING_OPTIMIZATION_SETTINGS_HINT_LOW_POWER_PRIORIZATION_QCOM = 3,
    XR_TRACKING_OPTIMIZATION_SETTINGS_HINT_HIGH_POWER_PRIORIZATION_QCOM = 4,
    XR_TRACKING_OPTIMIZATION_SETTINGS_HINT_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrTrackingOptimizationSettingsHintQCOM;
typedef XrResult (XRAPI_PTR *PFN_xrSetTrackingOptimizationSettingsHintQCOM)(XrSession session, XrTrackingOptimizationSettingsDomainQCOM domain, XrTrackingOptimizationSettingsHintQCOM hint);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrSetTrackingOptimizationSettingsHintQCOM(
    XrSession                                   session,
    XrTrackingOptimizationSettingsDomainQCOM    domain,
    XrTrackingOptimizationSettingsHintQCOM      hint);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */

#define XR_QCOM_ray_casting 1
XR_DEFINE_HANDLE(XrRayCastQCOM)
#define XR_QCOM_ray_casting_SPEC_VERSION  3
#define XR_QCOM_RAY_CASTING_EXTENSION_NAME "XR_QCOM_ray_casting"

typedef enum XrRayCastTargetTypeQCOM {
    XR_RAY_CAST_TARGET_TYPE_MESH_QCOM = 1000098000,
    XR_RAY_CAST_TARGET_TYPE_PLANE_QCOM = 1000305000,
    XR_RAY_CAST_TARGET_TYPE_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrRayCastTargetTypeQCOM;

typedef enum XrRayTypeQCOM {
    XR_RAY_TYPE_STOPPING_QCOM = 0,
    XR_RAY_TYPE_PASSTHROUGH_QCOM = 1,
    XR_RAY_TYPE_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrRayTypeQCOM;

typedef enum XrRayCastStateQCOM {
    XR_RAY_CAST_STATE_NONE_QCOM = 0,
    XR_RAY_CAST_STATE_CASTING_QCOM = 1,
    XR_RAY_CAST_STATE_FINISHED_QCOM = 2,
    XR_RAY_CAST_STATE_ERROR_QCOM = 3,
    XR_RAY_CAST_STATE_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrRayCastStateQCOM;
typedef struct XrRayCastCreateInfoQCOM {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrRayTypeQCOM               rayType;
    uint32_t                    targetTypeCount;
    XrRayCastTargetTypeQCOM*    targetTypes;
} XrRayCastCreateInfoQCOM;

typedef struct XrRayCollisionsGetInfoQCOM {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrSpace                     baseSpace;
} XrRayCollisionsGetInfoQCOM;

typedef struct XrRayCollisionQCOM {
    XrStructureType            type;
    void* XR_MAY_ALIAS         next;
    uint64_t                   targetId;
    XrRayCastTargetTypeQCOM    targetType;
    XrSceneObjectTypeMSFT      objectType;
    XrVector3f                 position;
    XrVector3f                 surfaceNormal;
    XrTime                     time;
} XrRayCollisionQCOM;

typedef struct XrRayCollisionsQCOM {
    XrStructureType        type;
    void* XR_MAY_ALIAS     next;
    uint32_t               collisionCapacityInput;
    uint32_t*              collisionCountOutput;
    XrRayCollisionQCOM*    collisions;
} XrRayCollisionsQCOM;

typedef XrResult (XRAPI_PTR *PFN_xrEnumerateRayCastTargetTypesQCOM)(XrSession session, uint32_t targetTypeCapacityInput, uint32_t* targetTypeCountOutput, XrRayCastTargetTypeQCOM* targetTypes);
typedef XrResult (XRAPI_PTR *PFN_xrCreateRayCastQCOM)(XrSession session, const XrRayCastCreateInfoQCOM* createInfo, XrRayCastQCOM* rayCast);
typedef XrResult (XRAPI_PTR *PFN_xrDestroyRayCastQCOM)(XrRayCastQCOM rayCast);
typedef XrResult (XRAPI_PTR *PFN_xrGetRayCastStateQCOM)(XrRayCastQCOM rayCast, XrRayCastStateQCOM* state);
typedef XrResult (XRAPI_PTR *PFN_xrCastRayQCOM)(XrRayCastQCOM rayCast, XrSpace space, XrVector3f origin, XrVector3f direction, float maxDistance);
typedef XrResult (XRAPI_PTR *PFN_xrGetRayCollisionsQCOM)(XrRayCastQCOM rayCast, const XrRayCollisionsGetInfoQCOM* getInfo, XrRayCollisionsQCOM* collisions);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateRayCastTargetTypesQCOM(
    XrSession                                   session,
    uint32_t                                    targetTypeCapacityInput,
    uint32_t*                                   targetTypeCountOutput,
    XrRayCastTargetTypeQCOM*                    targetTypes);

XRAPI_ATTR XrResult XRAPI_CALL xrCreateRayCastQCOM(
    XrSession                                   session,
    const XrRayCastCreateInfoQCOM*              createInfo,
    XrRayCastQCOM*                              rayCast);

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyRayCastQCOM(
    XrRayCastQCOM                               rayCast);

XRAPI_ATTR XrResult XRAPI_CALL xrGetRayCastStateQCOM(
    XrRayCastQCOM                               rayCast,
    XrRayCastStateQCOM*                         state);

XRAPI_ATTR XrResult XRAPI_CALL xrCastRayQCOM(
    XrRayCastQCOM                               rayCast,
    XrSpace                                     space,
    XrVector3f                                  origin,
    XrVector3f                                  direction,
    float                                       maxDistance);

XRAPI_ATTR XrResult XRAPI_CALL xrGetRayCollisionsQCOM(
    XrRayCastQCOM                               rayCast,
    const XrRayCollisionsGetInfoQCOM*           getInfo,
    XrRayCollisionsQCOM*                        collisions);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */

#define XR_QCOM_component_versioning 1
#define XR_MAX_COMPONENT_NAME_SIZE_QCOM   256
#define XR_MAX_IDENTIFIER_NAME_SIZE_QCOM  64
#define XR_QCOM_component_versioning_SPEC_VERSION 1
#define XR_QCOM_COMPONENT_VERSIONING_EXTENSION_NAME "XR_QCOM_component_versioning"
#define XR_MAX_DATE_TIME_SIZE_QCOM        16
typedef struct XrComponentVersionQCOM {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    char                  componentName[XR_MAX_COMPONENT_NAME_SIZE_QCOM];
    char                  versionIdentifier[XR_MAX_IDENTIFIER_NAME_SIZE_QCOM];
    char                  buildIdentifier[XR_MAX_IDENTIFIER_NAME_SIZE_QCOM];
    char                  buildDateTime[XR_MAX_DATE_TIME_SIZE_QCOM];
    char                  sourceIdentifier[XR_MAX_IDENTIFIER_NAME_SIZE_QCOM];
} XrComponentVersionQCOM;

typedef XrResult (XRAPI_PTR *PFN_xrGetComponentVersionsQCOM)(XrInstance instance, uint32_t componentVersionCapacityInput, uint32_t* componentVersionCountOutput, XrComponentVersionQCOM* componentVersions);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrGetComponentVersionsQCOM(
    XrInstance                                  instance,
    uint32_t                                    componentVersionCapacityInput,
    uint32_t*                                   componentVersionCountOutput,
    XrComponentVersionQCOM*                     componentVersions);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */

#define XR_QCOM_hand_tracking_gesture 1
#define XR_QCOM_hand_tracking_gesture_SPEC_VERSION 1
#define XR_QCOM_HAND_TRACKING_GESTURE_EXTENSION_NAME "XR_QCOM_hand_tracking_gesture"

typedef enum XrHandGestureTypeQCOM {
    XR_HAND_GESTURE_TYPE_UNKNOWN_QCOM = -1,
    XR_HAND_GESTURE_TYPE_OPEN_HAND_QCOM = 0,
    XR_HAND_GESTURE_TYPE_GRAB_QCOM = 2,
    XR_HAND_GESTURE_TYPE_PINCH_QCOM = 7,
    XR_HAND_GESTURE_TYPE_MAX_ENUM_QCOM = 0x7FFFFFFF
} XrHandGestureTypeQCOM;
typedef struct XrHandGestureQCOM {
    XrHandGestureTypeQCOM    gesture;
    float                    gestureRatio;
    float                    flipRatio;
} XrHandGestureQCOM;

typedef XrResult (XRAPI_PTR *PFN_xrGetHandGestureQCOM)(XrHandTrackerEXT handTracker, XrTime time, XrHandGestureQCOM* handGesture);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrGetHandGestureQCOM(
    XrHandTrackerEXT                            handTracker,
    XrTime                                      time,
    XrHandGestureQCOM*                          handGesture);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */

#define XR_QCOMX_camera_frame_access 1
XR_DEFINE_HANDLE(XrCameraFrameQCOMX)
XR_DEFINE_HANDLE(XrCameraQCOMX)
#define XR_QCOMX_camera_frame_access_SPEC_VERSION 2
#define XR_QCOMX_CAMERA_FRAME_ACCESS_EXTENSION_NAME "XR_QCOMX_camera_frame_access"
#define XR_MAX_CAMERA_SET_LENGTH_QCOMX    64
#define XR_MAX_CAMERA_RADIAL_DISTORSION_PARAMS_LENGTH_QCOMX 6
#define XR_MAX_CAMERA_TANGENTIAL_DISTORSION_PARAMS_LENGTH_QCOMX 2
#define XR_MAX_CAMERA_VIGNETTING_PARAMS_LENGTH_QCOMX 2
#define XR_MAX_CAMERA_RESOLUTION_NAME_LENGTH_QCOMX 64
#define XR_CAMERA_FRAME_PLANES_SIZE_QCOMX 4

typedef enum XrCameraTypeQCOMX {
    XR_CAMERA_TYPE_UNKNOWN_QCOMX = 0,
    XR_CAMERA_TYPE_RGB_QCOMX = 1,
    XR_CAMERA_TYPE_TRACKING_QCOMX = 2,
    XR_CAMERA_TYPE_DEPTH_QCOMX = 3,
    XR_CAMERA_TYPE_FACE_QCOMX = 4,
    XR_CAMERA_TYPE_EYE_QCOMX = 5,
    XR_CAMERA_TYPE_MAX_ENUM_QCOMX = 0x7FFFFFFF
} XrCameraTypeQCOMX;

typedef enum XrCameraDistortionModelQCOMX {
    XR_CAMERA_DISTORTION_MODEL_LINEAR_QCOMX = 0,
    XR_CAMERA_DISTORTION_MODEL_RADIAL_2_QCOMX = 1,
    XR_CAMERA_DISTORTION_MODEL_RADIAL_3_QCOMX = 2,
    XR_CAMERA_DISTORTION_MODEL_RADIAL_6_QCOMX = 3,
    XR_CAMERA_DISTORTION_MODEL_RADIAL_FISHEYE_1_QCOMX = 4,
    XR_CAMERA_DISTORTION_MODEL_RADIAL_FISHEYE_4_QCOMX = 5,
    XR_CAMERA_DISTORTION_MODEL_MAX_ENUM_QCOMX = 0x7FFFFFFF
} XrCameraDistortionModelQCOMX;

typedef enum XrCameraFrameFormatQCOMX {
    XR_CAMERA_FRAME_FORMAT_UNKNOWN_QCOMX = 0,
    XR_CAMERA_FRAME_FORMAT_YUV420_NV12_QCOMX = 1,
    XR_CAMERA_FRAME_FORMAT_YUV420_NV21_QCOMX = 2,
    XR_CAMERA_FRAME_FORMAT_MJPEG_QCOMX = 3,
    XR_CAMERA_FRAME_FORMAT_YUYV_QCOMX = 4,
    XR_CAMERA_FRAME_FORMAT_SIZE_QCOMX = 5,
    XR_CAMERA_FRAME_FORMAT_MAX_ENUM_QCOMX = 0x7FFFFFFF
} XrCameraFrameFormatQCOMX;

typedef enum XrCameraFramePlaneTypeQCOMX {
    XR_CAMERA_FRAME_PLANE_TYPE_Y_QCOMX = 0,
    XR_CAMERA_FRAME_PLANE_TYPE_U_QCOMX = 1,
    XR_CAMERA_FRAME_PLANE_TYPE_V_QCOMX = 2,
    XR_CAMERA_FRAME_PLANE_TYPE_UV_QCOMX = 3,
    XR_CAMERA_FRAME_PLANE_TYPE_YUV_QCOMX = 4,
    XR_CAMERA_FRAME_PLANE_TYPE_MAX_ENUM_QCOMX = 0x7FFFFFFF
} XrCameraFramePlaneTypeQCOMX;
typedef XrFlags64 XrCameraSensorFacingFlagsQCOMX;

// Flag bits for XrCameraSensorFacingFlagsQCOMX
static const XrCameraSensorFacingFlagsQCOMX XR_CAMERA_SENSOR_FACING_UP_BIT_QCOMX = 0x00000001;
static const XrCameraSensorFacingFlagsQCOMX XR_CAMERA_SENSOR_FACING_DOWN_BIT_QCOMX = 0x00000002;
static const XrCameraSensorFacingFlagsQCOMX XR_CAMERA_SENSOR_FACING_LEFT_BIT_QCOMX = 0x00000004;
static const XrCameraSensorFacingFlagsQCOMX XR_CAMERA_SENSOR_FACING_RIGHT_BIT_QCOMX = 0x00000008;
static const XrCameraSensorFacingFlagsQCOMX XR_CAMERA_SENSOR_FACING_FRONT_BIT_QCOMX = 0x00000010;
static const XrCameraSensorFacingFlagsQCOMX XR_CAMERA_SENSOR_FACING_BACK_BIT_QCOMX = 0x00000020;

typedef struct XrCameraInfoQCOMX {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    char                        cameraSet[XR_MAX_CAMERA_SET_LENGTH_QCOMX];
    XrCameraTypeQCOMX           cameraType;
    uint32_t                    sensorCount;
} XrCameraInfoQCOMX;

typedef struct XrCameraFrameConfigurationQCOMX {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrCameraFrameFormatQCOMX    format;
    char                        resolutionName[XR_MAX_CAMERA_RESOLUTION_NAME_LENGTH_QCOMX];
    XrExtent2Di                 dimensions;
    uint32_t                    minFPS;
    uint32_t                    maxFPS;
    uint32_t                    frameBufferCount;
    uint32_t                    frameHardwareBufferCount;
} XrCameraFrameConfigurationQCOMX;

typedef struct XrCameraActivationInfoQCOMX {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    const char*                 cameraSet;
} XrCameraActivationInfoQCOMX;

typedef struct XrCameraActivationFrameConfigurationInfoQCOMX {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrCameraFrameFormatQCOMX    format;
    const char*                 resolutionName;
    uint32_t                    fps;
} XrCameraActivationFrameConfigurationInfoQCOMX;

typedef struct XrCameraFrameDataQCOMX {
    XrStructureType             type;
    void* XR_MAY_ALIAS          next;
    XrCameraFrameQCOMX          handle;
    XrCameraFrameFormatQCOMX    format;
    int32_t                     frameNumber;
    XrTime                      timestamp;
} XrCameraFrameDataQCOMX;

typedef struct XrCameraFramePlaneQCOMX {
    XrStructureType                type;
    void* XR_MAY_ALIAS             next;
    XrCameraFramePlaneTypeQCOMX    planeType;
    uint32_t                       offset;
    uint32_t                       stride;
} XrCameraFramePlaneQCOMX;

typedef struct XrCameraFrameBufferQCOMX {
    XrStructureType            type;
    void* XR_MAY_ALIAS         next;
    uint32_t                   bufferSize;
    uint8_t*                   buffer;
    XrOffset2Di                offset;
    uint32_t                   planeCount;
    XrCameraFramePlaneQCOMX    planes[XR_CAMERA_FRAME_PLANES_SIZE_QCOMX];
} XrCameraFrameBufferQCOMX;

typedef struct XrCameraFrameBuffersQCOMX {
    XrStructureType              type;
    void* XR_MAY_ALIAS           next;
    uint32_t                     frameBufferCount;
    XrCameraFrameBufferQCOMX*    frameBuffers;
} XrCameraFrameBuffersQCOMX;

#ifdef XR_USE_PLATFORM_ANDROID
typedef struct XrCameraFrameHardwareBufferQCOMX {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    AHardwareBuffer*      buffer;
} XrCameraFrameHardwareBufferQCOMX;
#endif // XR_USE_PLATFORM_ANDROID

typedef struct XrCameraSensorIntrinsicsQCOMX {
    XrStructureType                 type;
    void* XR_MAY_ALIAS              next;
    XrVector2f                      principalPoint;
    XrVector2f                      focalLength;
    float                           radialDistortion[XR_MAX_CAMERA_RADIAL_DISTORSION_PARAMS_LENGTH_QCOMX];
    float                           tangentialDistortion[XR_MAX_CAMERA_TANGENTIAL_DISTORSION_PARAMS_LENGTH_QCOMX];
    XrCameraDistortionModelQCOMX    distortionModel;
} XrCameraSensorIntrinsicsQCOMX;

typedef struct XrCameraSensorPropertiesQCOMX {
    XrStructureType                   type;
    void* XR_MAY_ALIAS                next;
    XrCameraSensorIntrinsicsQCOMX     intrinsics;
    XrPosef                           extrinsic;
    XrOffset2Di                       imageOffset;
    XrExtent2Di                       imageDimensions;
    XrCameraSensorFacingFlagsQCOMX    facing;
    XrDuration                        rollingShutterLineTime;
} XrCameraSensorPropertiesQCOMX;

typedef struct XrCameraSensorInfosQCOMX {
    XrStructureType                   type;
    void* XR_MAY_ALIAS                next;
    XrSpace                           baseSpace;
    uint32_t                          sensorPropertyCount;
    XrCameraSensorPropertiesQCOMX*    sensorProperties;
} XrCameraSensorInfosQCOMX;

typedef XrResult (XRAPI_PTR *PFN_xrEnumerateCamerasQCOMX)(XrSession session, uint32_t cameraInfoCapacityInput, uint32_t* cameraInfoCountOutput, XrCameraInfoQCOMX* cameraInfos);
typedef XrResult (XRAPI_PTR *PFN_xrGetSupportedFrameConfigurationsQCOMX)(XrSession session, const char* cameraSet, uint32_t frameConfigurationCapacityInput, uint32_t* frameConfigurationCountOutput, XrCameraFrameConfigurationQCOMX* frameConfigurations);
typedef XrResult (XRAPI_PTR *PFN_xrCreateCameraHandleQCOMX)(XrSession session, const XrCameraActivationInfoQCOMX* activationInfo, XrCameraQCOMX* cameraHandle);
typedef XrResult (XRAPI_PTR *PFN_xrReleaseCameraHandleQCOMX)(XrCameraQCOMX cameraHandle);
typedef XrResult (XRAPI_PTR *PFN_xrAccessFrameQCOMX)(XrCameraQCOMX cameraHandle, XrCameraFrameDataQCOMX* frameData, XrCameraFrameBuffersQCOMX* frameBuffers);
typedef XrResult (XRAPI_PTR *PFN_xrReleaseFrameQCOMX)(XrCameraFrameQCOMX frame);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateCamerasQCOMX(
    XrSession                                   session,
    uint32_t                                    cameraInfoCapacityInput,
    uint32_t*                                   cameraInfoCountOutput,
    XrCameraInfoQCOMX*                          cameraInfos);

XRAPI_ATTR XrResult XRAPI_CALL xrGetSupportedFrameConfigurationsQCOMX(
    XrSession                                   session,
    const char*                                 cameraSet,
    uint32_t                                    frameConfigurationCapacityInput,
    uint32_t*                                   frameConfigurationCountOutput,
    XrCameraFrameConfigurationQCOMX*            frameConfigurations);

XRAPI_ATTR XrResult XRAPI_CALL xrCreateCameraHandleQCOMX(
    XrSession                                   session,
    const XrCameraActivationInfoQCOMX*          activationInfo,
    XrCameraQCOMX*                              cameraHandle);

XRAPI_ATTR XrResult XRAPI_CALL xrReleaseCameraHandleQCOMX(
    XrCameraQCOMX                               cameraHandle);

XRAPI_ATTR XrResult XRAPI_CALL xrAccessFrameQCOMX(
    XrCameraQCOMX                               cameraHandle,
    XrCameraFrameDataQCOMX*                     frameData,
    XrCameraFrameBuffersQCOMX*                  frameBuffers);

XRAPI_ATTR XrResult XRAPI_CALL xrReleaseFrameQCOMX(
    XrCameraFrameQCOMX                          frame);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */

#define XR_QCOMX_marker_tracking 1
XR_DEFINE_HANDLE(XrMarkerQCOMX)
XR_DEFINE_HANDLE(XrMarkerTrackerQCOMX)
XR_DEFINE_HANDLE(XrMarkerUpdateQCOMX)
#define XR_QCOMX_marker_tracking_SPEC_VERSION 1
#define XR_QCOMX_MARKER_TRACKING_EXTENSION_NAME "XR_QCOMX_marker_tracking"

typedef enum XrMarkerTypeQCOMX {
    XR_MARKER_TYPE_QR_CODE_QCOMX = 0,
    XR_MARKER_TYPE_MAX_ENUM_QCOMX = 0x7FFFFFFF
} XrMarkerTypeQCOMX;

typedef enum XrMarkerTrackingModeQCOMX {
    XR_MARKER_TRACKING_MODE_DYNAMIC_QCOMX = 0,
    XR_MARKER_TRACKING_MODE_STATIC_QCOMX = 1,
    XR_MARKER_TRACKING_MODE_ADAPTIVE_QCOMX = 2,
    XR_MARKER_TRACKING_MODE_DISABLED_QCOMX = 3,
    XR_MARKER_TRACKING_MODE_MAX_ENUM_QCOMX = 0x7FFFFFFF
} XrMarkerTrackingModeQCOMX;

typedef enum XrUserDefinedMarkerSizeSupportQCOMX {
    XR_USER_DEFINED_MARKER_SIZE_SUPPORTED_QCOMX = 0,
    XR_USER_DEFINED_MARKER_SIZE_REQUIRED_QCOMX = 1,
    XR_USER_DEFINED_MARKER_SIZE_NOT_SUPPORTED_QCOMX = 2,
    XR_USER_DEFINED_MARKER_SIZE_SUPPORT_MAX_ENUM_QCOMX = 0x7FFFFFFF
} XrUserDefinedMarkerSizeSupportQCOMX;

typedef enum XrQrCodeSymbolTypeQCOMX {
    XR_QR_CODE_SYMBOL_TYPE_QR_CODE_QCOMX = 0,
    XR_QR_CODE_SYMBOL_TYPE_MICRO_QR_CODE_QCOMX = 1,
    XR_QR_CODE_SYMBOL_TYPE_MAX_ENUM_QCOMX = 0x7FFFFFFF
} XrQrCodeSymbolTypeQCOMX;
// XrSystemMarkerTrackingPropertiesQCOMX extends XrSystemProperties
typedef struct XrSystemMarkerTrackingPropertiesQCOMX {
    XrStructureType                        type;
    void* XR_MAY_ALIAS                     next;
    XrBool32                               supportsMarkerTracking;
    XrMarkerTrackingModeQCOMX              defaultTrackingMode;
    XrUserDefinedMarkerSizeSupportQCOMX    userDefinedMarkerSizeSupport;
} XrSystemMarkerTrackingPropertiesQCOMX;

typedef struct XrMarkerTrackerCreateInfoQCOMX {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
} XrMarkerTrackerCreateInfoQCOMX;

typedef struct XrMarkerDetectionStartInfoQCOMX {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    uint32_t                    markerTypeCount;
    const XrMarkerTypeQCOMX*    markerTypes;
} XrMarkerDetectionStartInfoQCOMX;

typedef struct XrQrCodeVersionRangeQCOMX {
    XrQrCodeSymbolTypeQCOMX    symbolType;
    uint8_t                    minQrVersion;
    uint8_t                    maxQrVersion;
} XrQrCodeVersionRangeQCOMX;

typedef struct XrQrCodeVersionFilterQCOMX {
    XrStructureType               type;
    const void* XR_MAY_ALIAS      next;
    uint32_t                      rangeCount;
    XrQrCodeVersionRangeQCOMX*    ranges;
} XrQrCodeVersionFilterQCOMX;

typedef struct XrQrCodeVersionQCOMX {
    XrQrCodeSymbolTypeQCOMX    symbolType;
    uint8_t                    qrVersion;
} XrQrCodeVersionQCOMX;

typedef struct XrMarkerSpaceCreateInfoQCOMX {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrPosef                     poseInMarkerSpace;
} XrMarkerSpaceCreateInfoQCOMX;

typedef struct XrMarkerTrackingModeInfoQCOMX {
    XrStructureType                    type;
    const void* XR_MAY_ALIAS           next;
    const XrMarkerTrackingModeQCOMX    trackingMode;
} XrMarkerTrackingModeInfoQCOMX;

typedef struct XrUserDefinedMarkerSizeQCOMX {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrExtent2Df                 size;
} XrUserDefinedMarkerSizeQCOMX;

typedef struct XrMarkerUpdateInfoQCOMX {
    XrMarkerQCOMX    marker;
    XrTime           time;
    XrBool32         isDetected;
    XrBool32         isMarkerDataAvailable;
} XrMarkerUpdateInfoQCOMX;

typedef XrResult (XRAPI_PTR *PFN_xrEnumerateMarkerTypesQCOMX)(XrSession session, uint32_t markerTypeCapacityInput, uint32_t* markerTypeCountOutput, XrMarkerTypeQCOMX* markerTypes);
typedef XrResult (XRAPI_PTR *PFN_xrEnumerateMarkerTrackingModesQCOMX)(XrSession session, uint32_t trackingModeCapacityInput, uint32_t* trackingModeCountOutput, XrMarkerTrackingModeQCOMX* trackingModes);
typedef XrResult (XRAPI_PTR *PFN_xrCreateMarkerTrackerQCOMX)(XrSession session, const XrMarkerTrackerCreateInfoQCOMX* createInfo, XrMarkerTrackerQCOMX* markerTracker);
typedef XrResult (XRAPI_PTR *PFN_xrDestroyMarkerTrackerQCOMX)(XrMarkerTrackerQCOMX markerTracker);
typedef XrResult (XRAPI_PTR *PFN_xrStartMarkerDetectionQCOMX)(XrMarkerTrackerQCOMX markerTracker, const XrMarkerDetectionStartInfoQCOMX* startInfo);
typedef XrResult (XRAPI_PTR *PFN_xrStopMarkerDetectionQCOMX)(XrMarkerTrackerQCOMX markerTracker);
typedef XrResult (XRAPI_PTR *PFN_xrGetMarkerSizeQCOMX)(XrMarkerQCOMX marker, XrExtent2Df* size);
typedef XrResult (XRAPI_PTR *PFN_xrGetMarkerTypeQCOMX)(XrMarkerQCOMX marker, XrMarkerTypeQCOMX* type);
typedef XrResult (XRAPI_PTR *PFN_xrGetQrCodeVersionQCOMX)(XrMarkerQCOMX marker, XrQrCodeVersionQCOMX* version);
typedef XrResult (XRAPI_PTR *PFN_xrGetQrCodeStringDataQCOMX)(XrMarkerQCOMX marker, uint32_t bufferCapacityInput, uint32_t* bufferCountOutput, char* buffer);
typedef XrResult (XRAPI_PTR *PFN_xrGetQrCodeRawDataQCOMX)(XrMarkerQCOMX marker, uint32_t bufferCapacityInput, uint32_t* bufferCountOutput, uint8_t* buffer);
typedef XrResult (XRAPI_PTR *PFN_xrCreateMarkerSpaceQCOMX)(XrMarkerQCOMX marker, const XrMarkerSpaceCreateInfoQCOMX* createInfo, XrSpace* space);
typedef XrResult (XRAPI_PTR *PFN_xrSetMarkerTrackingModeQCOMX)(XrMarkerQCOMX marker, const XrMarkerTrackingModeInfoQCOMX* trackingModeInfo);
typedef XrResult (XRAPI_PTR *PFN_xrPollMarkerUpdateQCOMX)(XrMarkerTrackerQCOMX tracker, XrMarkerUpdateQCOMX* update);
typedef XrResult (XRAPI_PTR *PFN_xrGetMarkerUpdateInfoQCOMX)(XrMarkerUpdateQCOMX update, uint32_t* updateInfoCount, XrMarkerUpdateInfoQCOMX** updateInfos);
typedef XrResult (XRAPI_PTR *PFN_xrReleaseMarkerUpdateQCOMX)(XrMarkerUpdateQCOMX update);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateMarkerTypesQCOMX(
    XrSession                                   session,
    uint32_t                                    markerTypeCapacityInput,
    uint32_t*                                   markerTypeCountOutput,
    XrMarkerTypeQCOMX*                          markerTypes);

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateMarkerTrackingModesQCOMX(
    XrSession                                   session,
    uint32_t                                    trackingModeCapacityInput,
    uint32_t*                                   trackingModeCountOutput,
    XrMarkerTrackingModeQCOMX*                  trackingModes);

XRAPI_ATTR XrResult XRAPI_CALL xrCreateMarkerTrackerQCOMX(
    XrSession                                   session,
    const XrMarkerTrackerCreateInfoQCOMX*       createInfo,
    XrMarkerTrackerQCOMX*                       markerTracker);

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyMarkerTrackerQCOMX(
    XrMarkerTrackerQCOMX                        markerTracker);

XRAPI_ATTR XrResult XRAPI_CALL xrStartMarkerDetectionQCOMX(
    XrMarkerTrackerQCOMX                        markerTracker,
    const XrMarkerDetectionStartInfoQCOMX*      startInfo);

XRAPI_ATTR XrResult XRAPI_CALL xrStopMarkerDetectionQCOMX(
    XrMarkerTrackerQCOMX                        markerTracker);

XRAPI_ATTR XrResult XRAPI_CALL xrGetMarkerSizeQCOMX(
    XrMarkerQCOMX                               marker,
    XrExtent2Df*                                size);

XRAPI_ATTR XrResult XRAPI_CALL xrGetMarkerTypeQCOMX(
    XrMarkerQCOMX                               marker,
    XrMarkerTypeQCOMX*                          type);

XRAPI_ATTR XrResult XRAPI_CALL xrGetQrCodeVersionQCOMX(
    XrMarkerQCOMX                               marker,
    XrQrCodeVersionQCOMX*                       version);

XRAPI_ATTR XrResult XRAPI_CALL xrGetQrCodeStringDataQCOMX(
    XrMarkerQCOMX                               marker,
    uint32_t                                    bufferCapacityInput,
    uint32_t*                                   bufferCountOutput,
    char*                                       buffer);

XRAPI_ATTR XrResult XRAPI_CALL xrGetQrCodeRawDataQCOMX(
    XrMarkerQCOMX                               marker,
    uint32_t                                    bufferCapacityInput,
    uint32_t*                                   bufferCountOutput,
    uint8_t*                                    buffer);

XRAPI_ATTR XrResult XRAPI_CALL xrCreateMarkerSpaceQCOMX(
    XrMarkerQCOMX                               marker,
    const XrMarkerSpaceCreateInfoQCOMX*         createInfo,
    XrSpace*                                    space);

XRAPI_ATTR XrResult XRAPI_CALL xrSetMarkerTrackingModeQCOMX(
    XrMarkerQCOMX                               marker,
    const XrMarkerTrackingModeInfoQCOMX*        trackingModeInfo);

XRAPI_ATTR XrResult XRAPI_CALL xrPollMarkerUpdateQCOMX(
    XrMarkerTrackerQCOMX                        tracker,
    XrMarkerUpdateQCOMX*                        update);

XRAPI_ATTR XrResult XRAPI_CALL xrGetMarkerUpdateInfoQCOMX(
    XrMarkerUpdateQCOMX                         update,
    uint32_t*                                   updateInfoCount,
    XrMarkerUpdateInfoQCOMX**                   updateInfos);

XRAPI_ATTR XrResult XRAPI_CALL xrReleaseMarkerUpdateQCOMX(
    XrMarkerUpdateQCOMX                         update);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */


#ifdef __cplusplus
}
#endif

#endif

