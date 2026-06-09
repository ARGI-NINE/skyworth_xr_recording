/****************************************************************
 * Copyright (c) 2020-2022 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#ifdef ANDROID
#include <android/log.h>
#include <sys/system_properties.h>
#elif LINUX
#include <log/log.h>
#endif

enum {
    LOGMASK_ERROR   = 0x1,
    LOGMASK_WARNING = 0x2,
    LOGMASK_INFO    = 0x4,
    LOGMASK_DEBUG   = 0x8,
    LOGMASK_VERBOSE = 0x10,
};

extern int32_t logLevel;

#ifdef LINUX_DEBUG
        // Since logcat contains lost of info out of the compositor
        // Thus, use the debug mode to print related logs on the screen with the same format on LA
#define LOG(TAG, ...)  __android_log_print__(ANDROID_LOG_ERROR,    TAG, __VA_ARGS__)
#define LOGE(TAG, ...) __android_log_print__(ANDROID_LOG_ERROR,    TAG, __VA_ARGS__)
#define LOGW(TAG, ...) __android_log_print__(ANDROID_LOG_WARN,     TAG, __VA_ARGS__)
#define LOGI(TAG, ...) __android_log_print__(ANDROID_LOG_INFO,     TAG, __VA_ARGS__)
#define LOGD(TAG, ...) __android_log_print__(ANDROID_LOG_DEBUG,    TAG, __VA_ARGS__)

#else

#ifdef ANDROID

#define LOGE(TAG, ...) ({if (logLevel & LOGMASK_ERROR)   LogImpl(ANDROID_LOG_ERROR,    TAG, __VA_ARGS__);})
#define LOGW(TAG, ...) ({if (logLevel & LOGMASK_WARNING) LogImpl(ANDROID_LOG_WARN,     TAG, __VA_ARGS__);})
#define LOGI(TAG, ...) ({if (logLevel & LOGMASK_INFO)    LogImpl(ANDROID_LOG_INFO,     TAG, __VA_ARGS__);})
#define LOGV(TAG, ...) ({if (logLevel & LOGMASK_VERBOSE) LogImpl(ANDROID_LOG_VERBOSE,  TAG, __VA_ARGS__);})
#ifdef NDEBUG
#define LOGD(TAG, ...) ({if (logLevel & LOGMASK_DEBUG)   LogImpl(ANDROID_LOG_DEBUG,    TAG, __VA_ARGS__);})
#else
#define LOGD(TAG, ...) LogImpl(ANDROID_LOG_DEBUG,    TAG, __VA_ARGS__)
#endif

#elif LINUX
#define LOGE(TAG, ...) LogImpl(ANDROID_LOG_ERROR,    TAG, __VA_ARGS__)
#define LOGW(TAG, ...) LogImpl(ANDROID_LOG_WARN,     TAG, __VA_ARGS__)
#define LOGI(TAG, ...) LogImpl(ANDROID_LOG_INFO,     TAG, __VA_ARGS__)
#ifdef NDEBUG
#define LOGD(TAG, ...)
#else
#define LOGD(TAG, ...) LogImpl(ANDROID_LOG_DEBUG,    TAG, __VA_ARGS__)
#endif
#endif

#endif

//For printing OpenXR handles
#ifdef __LP64__
#define PRxrH "p"
#else
#include <cinttypes>
#define PRxrH PRIu64
#endif

// Print logs on the screen
inline int __android_log_print__(int prio, const char *tag, const char *fmt, ...)
{
    va_list ap;
    char buf[1024];
    va_start(ap, fmt);
    vsnprintf(buf, 1024, fmt, ap);
    va_end(ap);
    fprintf(stdout, "%s\n", buf);

    return 0;
}

void UpdateLogLevel();

void LogImpl(android_LogPriority const priority, const char* tag, const char* fmt, ...);

#ifdef __cplusplus
}  // extern "C"
#endif



