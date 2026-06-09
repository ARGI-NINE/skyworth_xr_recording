/****************************************************************
 * Copyright (c) 2020-2022 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/
#pragma once
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <vector>
#include <errno.h>
#include "LogUtils.h"


// Returns true if not GL_NO_ERROR
inline bool CheckEglError(
        EGLint* const errorOut = nullptr,
        bool const logError = false,
        const char* logTag = "",
        const char* logTitle = "")
{
#ifdef NDEBUG
    return false;
#else
    EGLint const error = eglGetError();
    if (error == EGL_SUCCESS)
    {
        if (errorOut)
        {
            *errorOut = EGL_SUCCESS;
        }
        return false;
    }

    const char* errStr = nullptr;
    switch (error)
    {
        case EGL_NOT_INITIALIZED:
            errStr = "EGL_NOT_INITIALIZED";
            break;
        case EGL_BAD_ACCESS:
            errStr = "EGL_BAD_ACCESS";
            break;
        case EGL_BAD_ALLOC:
            errStr = "EGL_BAD_ALLOC";
            break;
        case EGL_BAD_ATTRIBUTE:
            errStr = "EGL_BAD_ATTRIBUTE";
            break;
        case EGL_BAD_CONTEXT:
            errStr = "EGL_BAD_CONTEXT";
            break;
        case EGL_BAD_CONFIG:
            errStr = "EGL_BAD_CONFIG";
            break;
        case EGL_BAD_CURRENT_SURFACE:
            errStr = "EGL_BAD_CURRENT_SURFACE";
            break;
        case EGL_BAD_DISPLAY:
            errStr = "EGL_BAD_DISPLAY";
            break;
        case EGL_BAD_SURFACE:
            errStr = "EGL_BAD_SURFACE";
            break;
        case EGL_BAD_MATCH:
            errStr = "EGL_BAD_MATCH";
            break;
        case EGL_BAD_PARAMETER:
            errStr = "EGL_BAD_PARAMETER";
            break;
        case EGL_BAD_NATIVE_PIXMAP:
            errStr = "EGL_BAD_NATIVE_PIXMAP";
            break;
        case EGL_BAD_NATIVE_WINDOW:
            errStr = "EGL_BAD_NATIVE_WINDOW";
            break;
        case EGL_CONTEXT_LOST:
            errStr = "EGL_CONTEXT_LOST";
            break;
        default:
            errStr = "Unknown EGL error";
    }

    if (errorOut)
    {
        *errorOut = error;
    }

    if (logError)
    {
        logTag = (logTag && logTag[0]) ? logTag : __func__;
        if (logTitle && logTitle[0])
        {
            LOGE(logTag, "%s", logTitle);
        }
        LOGE(logTag, "    %s", errStr);
    }

    return true;
#endif
}

// Returns true if not GL_NO_ERROR
inline bool CheckGlError(
        std::vector<EGLint>* errorsOut = nullptr,
        bool const logError = false,
        const char* logTag = "",
        const char* logTitle = "")
{
#ifdef NDEBUG
    return false;
#else
    std::vector<EGLint> errors;

    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR)
    {
        errors.push_back(err);
    }

    if (logError && !errors.empty())
    {
        logTag = (logTag && logTag[0]) ? logTag : __func__;

        if (logTitle && logTitle[0])
        {
            LOGE(logTag, "%s", logTitle);
        }
        for (auto glErr : errors)
        {
            const char* errStr = nullptr;
            switch (glErr)
            {
                case GL_INVALID_ENUM:                   errStr = "GL_INVALID_ENUM";                     break;
                case GL_INVALID_VALUE:                  errStr = "GL_INVALID_VALUE";                    break;
                case GL_INVALID_OPERATION:              errStr = "GL_INVALID_OPERATION";                break;
                case GL_OUT_OF_MEMORY:                  errStr = "GL_OUT_OF_MEMORY";                    break;
                case GL_INVALID_FRAMEBUFFER_OPERATION:  errStr = "GL_INVALID_FRAMEBUFFER_OPERATION";    break;
                default:
                    errStr = "Unknown GL error";
            }
            LOGE(logTag, "    %s", errStr);
        }
    }

    return !errors.empty();
#endif
}