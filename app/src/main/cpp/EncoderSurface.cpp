#include "EncoderSurface.h"
#include <cstring>

namespace SXR {

EncoderSurface::EncoderSurface() {
}

EncoderSurface::~EncoderSurface() {
    release();
}

bool EncoderSurface::init(ANativeWindow* window, EGLDisplay sharedDisplay, EGLContext sharedContext) {
    if (!window) {
        LOGE("init: window is null");
        return false;
    }

    // Validate shared display and context
    if (sharedDisplay == EGL_NO_DISPLAY) {
        LOGE("init: sharedDisplay is EGL_NO_DISPLAY");
        return false;
    }
    if (sharedContext == EGL_NO_CONTEXT) {
        LOGE("init: sharedContext is EGL_NO_CONTEXT");
        return false;
    }

    mWindow = window;
    ANativeWindow_acquire(mWindow);

    mWidth = ANativeWindow_getWidth(mWindow);
    mHeight = ANativeWindow_getHeight(mWindow);
    LOGI("EncoderSurface init: %dx%d, display=%p, context=%p",
         mWidth, mHeight, sharedDisplay, sharedContext);

    // Use the shared EGL display
    mDisplay = sharedDisplay;

    // Choose EGL config
    EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };

    EGLint numConfigs = 0;
    if (!eglChooseConfig(mDisplay, configAttribs, &mConfig, 1, &numConfigs) || numConfigs < 1) {
        LOGE("eglChooseConfig failed: 0x%x", eglGetError());
        return false;
    }

    // Create EGL surface from ANativeWindow
    // Use linear color space for correct color reproduction
    EGLint surfaceAttribs[] = {
        EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_LINEAR,
        EGL_NONE
    };
    mSurface = eglCreateWindowSurface(mDisplay, mConfig, mWindow, surfaceAttribs);
    if (mSurface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed: 0x%x", eglGetError());
        return false;
    }

    // Create EGL context (sharing resources with main context)
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    mContext = eglCreateContext(mDisplay, mConfig, sharedContext, contextAttribs);
    if (mContext == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed: 0x%x", eglGetError());
        eglDestroySurface(mDisplay, mSurface);
        mSurface = EGL_NO_SURFACE;
        return false;
    }

    LOGI("EncoderSurface initialized successfully");
    return true;
}

void EncoderSurface::release() {
    if (mSurface != EGL_NO_SURFACE) {
        eglDestroySurface(mDisplay, mSurface);
        mSurface = EGL_NO_SURFACE;
    }

    if (mContext != EGL_NO_CONTEXT) {
        eglDestroyContext(mDisplay, mContext);
        mContext = EGL_NO_CONTEXT;
    }

    if (mWindow) {
        ANativeWindow_release(mWindow);
        mWindow = nullptr;
    }

    mDisplay = EGL_NO_DISPLAY;
    mWidth = 0;
    mHeight = 0;
}

bool EncoderSurface::makeCurrent() {
    if (mSurface == EGL_NO_SURFACE || mContext == EGL_NO_CONTEXT) {
        LOGE("makeCurrent: not initialized");
        return false;
    }

    if (!eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) {
        LOGE("eglMakeCurrent failed: 0x%x", eglGetError());
        return false;
    }

    // Set viewport
    glViewport(0, 0, mWidth, mHeight);
    return true;
}

bool EncoderSurface::swapBuffers() {
    if (mSurface == EGL_NO_SURFACE) {
        LOGE("swapBuffers: not initialized");
        return false;
    }

    if (!eglSwapBuffers(mDisplay, mSurface)) {
        LOGE("eglSwapBuffers failed: 0x%x", eglGetError());
        return false;
    }

    return true;
}

void EncoderSurface::setPresentationTime(int64_t timeNs) {
    if (mSurface == EGL_NO_SURFACE) {
        return;
    }

    auto pfn = (PFNEGLPRESENTATIONTIMEANDROID)eglGetProcAddress("eglPresentationTimeANDROID");
    if (!pfn) {
        LOGE("eglPresentationTimeANDROID not available");
        return;
    }

    EGLBoolean result = pfn(mDisplay, mSurface, timeNs);
    if (result != EGL_TRUE) {
        LOGE("eglPresentationTimeANDROID failed: 0x%x", eglGetError());
    }
}

} // namespace SXR
