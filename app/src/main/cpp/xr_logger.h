// 
// Created by joey.zhang on 2019/12/17.
// Copyright (c) 2019 zhgyee@gmail.com All rights reserved.

#ifndef XREALITY_XR_LOGGER_H
#define XREALITY_XR_LOGGER_H

#include <stdint.h>
#include <stdlib.h>

extern const int XR_LOG_DEBUG;
extern const int XR_LOG_ERROR;
extern const int XR_LOG_WARN;
extern const int XR_LOG_FATAL;
extern const int XR_LOG_INFO;

#define XR_LOGGER(logger) xr_logger logger((int64_t)runtime, __FUNCTION__)
#define XR_LOGRENDER(logger) xr_logger logger((int64_t)renderer, __FUNCTION__)
#define XR_LOGRENDERLOOPER(logger) xr_logger logger((int64_t)looper, __FUNCTION__)
#define XR_LOGTEXTUREQUE(logger) xr_logger logger((int64_t)textureQueue, __FUNCTION__)
#define XR_LOGFEATURE(logger) xr_logger logger((int64_t)feature, __FUNCTION__)
#define XR_LOGGER_NO_INSTANCE(logger) xr_logger logger(__FUNCTION__)

#define LOG_TAG "HelloXr"
struct xr_logger {
	int64_t handle;
	const char* api_name;
	xr_logger(const char* name);
	xr_logger(int64_t h, const char* name);
	~xr_logger();
	void log(const int priority, const char* fmt, ...)
	__attribute__((__format__(printf, 3, 4)))
	__attribute__((__nonnull__(3)));
};

// Log with an explicit tag
void LogWithTag( const int prio, const char * tag, const char * fmt, ... )
	__attribute__((__format__(printf, 3, 4)))
	__attribute__((__nonnull__(3)));

// Strips the directory and extension from fileTag to give a concise log tag
void LogWithFileTag( const int prio, const char * fileTag, const char * fmt, ... )
__attribute__((__format__(printf, 3, 4)))
__attribute__((__nonnull__(3)));

#ifdef BUILD_DEBUG
#ifdef LOG_TAG
	#define XR_DEBUG(...) ( (void)LogWithTag( XR_LOG_DEBUG, LOG_TAG, __VA_ARGS__) )
	#define XR_WARN(...) ( (void)LogWithTag( XR_LOG_WARN, LOG_TAG, __VA_ARGS__) )
	#define XR_ERROR(...) ( (void)LogWithTag( XR_LOG_ERROR, LOG_TAG, __VA_ARGS__) )
	#define XR_FAIL(...) { (void)LogWithTag( XR_LOG_ERROR, LOG_TAG, __VA_ARGS__); abort(); }
	#define XR_LOG(...) ( (void)LogWithTag( XR_LOG_INFO, LOG_TAG, __VA_ARGS__) )
	#else
	#define XR_DEBUG( ... ) LogWithFileTag( XR_LOG_DEBUG, __FILE__, __VA_ARGS__ )
	#define XR_WARN( ... ) LogWithFileTag( XR_LOG_WARN, __FILE__, __VA_ARGS__ )
	#define XR_ERROR( ... ) LogWithFileTag( XR_LOG_ERROR, __FILE__, __VA_ARGS__ )
	#define XR_FAIL( ... ) {LogWithFileTag( XR_LOG_ERROR, __FILE__, __VA_ARGS__ );abort();}
	#define XR_LOG( ... ) LogWithFileTag( XR_LOG_INFO, __FILE__, __VA_ARGS__ )
	#endif
#else
#ifdef LOG_TAG
    #define XR_DEBUG(...)
	#define XR_ERROR(...) ( (void)LogWithTag( XR_LOG_ERROR, LOG_TAG, __VA_ARGS__) )
	#define XR_WARN(...) ( (void)LogWithTag( XR_LOG_WARN, LOG_TAG, __VA_ARGS__) )
	#define XR_FAIL(...) { (void)LogWithTag( XR_LOG_ERROR, LOG_TAG, __VA_ARGS__); abort(); }
	#define XR_LOG(...) ( (void)LogWithTag( XR_LOG_INFO, LOG_TAG, __VA_ARGS__) )
#else
    #define XR_DEBUG( ... )
    #define XR_ERROR(...) ( (void)LogWithTag( XR_LOG_ERROR, __FILE__, __VA_ARGS__) )
    #define XR_WARN( ... ) LogWithFileTag( XR_LOG_WARN, __FILE__, __VA_ARGS__ )
    #define XR_FAIL( ... ) {LogWithFileTag( XR_LOG_ERROR, __FILE__, __VA_ARGS__ );abort();}
	#define XR_LOG( ... ) LogWithFileTag( XR_LOG_INFO, __FILE__, __VA_ARGS__ )
    #endif
#endif

#endif //XREALITY_XR_LOGGER_H
