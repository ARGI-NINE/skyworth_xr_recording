/****************************************************************
 * Copyright (c) 2020-2022 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include "LogUtils.h"

#define LOG_LEVEL_DEFAULT (LOGMASK_ERROR | LOGMASK_WARNING | LOGMASK_INFO)

int32_t logLevel = LOG_LEVEL_DEFAULT;

static int64_t GetTimeNano(clockid_t clk_id)
{
	struct timespec t;
	clock_gettime(clk_id, &t);
	int64_t result = t.tv_sec * 1000000000LL + t.tv_nsec;
	return result;
}

void LogImpl(android_LogPriority const priority, const char* tag, const char* fmt, ...)
{
    int64_t const callTime = GetTimeNano(CLOCK_MONOTONIC);
    va_list args;
    va_start(args, fmt);

    int32_t tryCount = -1;
    int32_t errnoSaved = 0;
    int32_t res;
    int64_t actualLogTime = -1;
    while (true)
    {
        res = __android_log_vprint(priority, tag, fmt, args);
        if (res > 0)
        {
            actualLogTime = GetTimeNano(CLOCK_MONOTONIC);
            break;
        }

        errnoSaved = errno;
        ++tryCount;
    }

    if (tryCount > 0)
    {
        double const lateTimeMs = (actualLogTime - callTime) * 0.000001; // ns -> ms
        int32_t tryCount2 = 0;
        int32_t len = strlen("[LOG] ") + strlen(tag) + 1;
        char* newTag = (char *)malloc(len);
        if (newTag == NULL)
        {
            __android_log_print(ANDROID_LOG_ERROR, "LogImpl", "malloc failed: %d", len);
            return;
        }
        strcpy(newTag, "[LOG] ");
        strcat(newTag, tag);
        do
        {
            res = __android_log_print(ANDROID_LOG_ERROR, newTag, "Had to retry %i+%i times to print the previous message, late by %fms (%s)!", tryCount, ++tryCount2, lateTimeMs, strerror(errnoSaved));
        } while(res < 0);
        free(newTag);
    }

    va_end(args);
}

#ifdef ANDROID
void UpdateLogLevel()
{
    char propValue[PROP_VALUE_MAX];
    if (__system_property_get("debug.openxr.logLevel", propValue) && strlen(propValue))
        logLevel = atoi(propValue);
    LOGI("UpdateLogLevel", "Set log level to %d", logLevel);
}
#endif
