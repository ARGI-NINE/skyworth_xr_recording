/****************************************************************
 * Copyright (c) 2023 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/

#pragma once

#include "openxr/openxr.h"

typedef struct XrRootSpaceCreateInfoQCOM {
    // type, and next are not used/defined now. Leave them here for
    // potential future usage
    XrStructureType type;
    const void *XR_MAY_ALIAS next;
    XrPosef poseInSpace;
} XrRootSpaceCreateInfoQCOM;

typedef XrResult(XRAPI_PTR *PFN_xrCreateRootSpaceQCOM)(
        XrSession session,
        const XrRootSpaceCreateInfoQCOM *createInfo,
        XrSpace *space);
