#pragma once

#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#include <string>

#include "NativeLogger.h"

// EGL_ANDROID_presentation_time extension
typedef EGLBoolean (EGLAPIENTRYP PFNEGLPRESENTATIONTIMEANDROID)(EGLDisplay dpy, EGLSurface surface, int64_t time);

#define LOG_TAG "EncoderSurface"

#define LOGI(...)  NATIVE_LOGI(LOG_TAG, __VA_ARGS__)
#define LOGW(...)  NATIVE_LOGW(LOG_TAG, __VA_ARGS__)
#define LOGE(...)  NATIVE_LOGE(LOG_TAG, __VA_ARGS__)

namespace SXR {

/**
 * Helper class to manage EGL surface for encoder input.
 * This enables rendering camera frames directly to encoder surface (zero-copy).
 */
class EncoderSurface {
public:
    EncoderSurface();
    ~EncoderSurface();

    // Initialize with encoder's input surface (ANativeWindow)
    bool init(ANativeWindow* window, EGLDisplay sharedDisplay, EGLContext sharedContext);

    // Release resources
    void release();

    // Make this surface current for rendering
    bool makeCurrent();

    // Swap buffers to submit frame to encoder
    bool swapBuffers();

    // Set presentation time for the frame (in nanoseconds, must be called before swapBuffers)
    void setPresentationTime(int64_t timeNs);

    // Get surface dimensions
    int getWidth() const { return mWidth; }
    int getHeight() const { return mHeight; }

    // Check if initialized
    bool isInitialized() const { return mSurface != EGL_NO_SURFACE; }

private:
    ANativeWindow* mWindow{nullptr};
    EGLDisplay mDisplay{EGL_NO_DISPLAY};
    EGLContext mContext{EGL_NO_CONTEXT};
    EGLSurface mSurface{EGL_NO_SURFACE};
    EGLConfig mConfig{nullptr};

    int mWidth{0};
    int mHeight{0};
};

} // namespace SXR
