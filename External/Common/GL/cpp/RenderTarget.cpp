/****************************************************************
 * Copyright (c) 2020-2022 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/
#include <GLES3/gl3.h>
#include <cassert>
#include "../../Log/LogUtils.h"
#include "Extensions.h"
#include "RenderTarget.h"

namespace QtiGL
{
    void GetFormatTypeFromSizedFormat(int32_t const sizedFormat, int32_t& outFormat, int32_t& outType)
    {
        switch(sizedFormat)
        {
        case GL_R8:
            outFormat = GL_RED;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_R8_SNORM:
            outFormat = GL_RED;
            outType = GL_BYTE;
            return;
        case GL_R16F:
            outFormat = GL_RED;
            outType = GL_HALF_FLOAT;
            return;
        case GL_R32F:
            outFormat = GL_RED;
            outType = GL_FLOAT;
            return;
        case GL_R8UI:
            outFormat = GL_RED_INTEGER;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_R8I:
            outFormat = GL_RED_INTEGER;
            outType = GL_BYTE;
            return;
        case GL_R16UI:
            outFormat = GL_RED_INTEGER;
            outType = GL_UNSIGNED_SHORT;
            return;
        case GL_R16I:
            outFormat = GL_RED_INTEGER;
            outType = GL_SHORT;
            return;
        case GL_R32UI:
            outFormat = GL_RED_INTEGER;
            outType = GL_UNSIGNED_INT;
            return;
        case GL_R32I:
            outFormat = GL_RED_INTEGER;
            outType = GL_INT;
            return;
        case GL_RG8:
            outFormat = GL_RG;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_RG8_SNORM:
            outFormat = GL_RG;
            outType = GL_BYTE;
            return;
        case GL_RG16F:
            outFormat = GL_RG;
            outType = GL_HALF_FLOAT;
            return;
        case GL_RG32F:
            outFormat = GL_RG;
            outType = GL_FLOAT;
            return;
        case GL_RG8UI:
            outFormat = GL_RG_INTEGER;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_RG8I:
            outFormat = GL_RG_INTEGER;
            outType = GL_BYTE;
            return;
        case GL_RG16UI:
            outFormat = GL_RG_INTEGER;
            outType = GL_UNSIGNED_SHORT;
            return;
        case GL_RG16I:
            outFormat = GL_RG_INTEGER;
            outType = GL_SHORT;
            return;
        case GL_RG32UI:
            outFormat = GL_RG_INTEGER;
            outType = GL_UNSIGNED_INT;
            return;
        case GL_RG32I:
            outFormat = GL_RG_INTEGER;
            outType = GL_INT;
            return;
        case GL_RGB8:
            outFormat = GL_RGB;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_SRGB8:
            outFormat = GL_RGB;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_RGB565:
            outFormat = GL_RGB;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_RGB8_SNORM:
            outFormat = GL_RGB;
            outType = GL_BYTE;
            return;
        case GL_R11F_G11F_B10F:
            outFormat = GL_RGB;
            outType = GL_HALF_FLOAT;
            return;
        case GL_RGB9_E5:
            outFormat = GL_RGB;
            outType = GL_HALF_FLOAT;
            return;
        case GL_RGB16F:
            outFormat = GL_RGB;
            outType = GL_HALF_FLOAT;
            return;
        case GL_RGB32F:
            outFormat = GL_RGB;
            outType = GL_FLOAT;
            return;
        case GL_RGB8UI:
            outFormat = GL_RGB_INTEGER;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_RGB8I:
            outFormat = GL_RGB_INTEGER;
            outType = GL_BYTE;
            return;
        case GL_RGB16UI:
            outFormat = GL_RGB_INTEGER;
            outType = GL_UNSIGNED_SHORT;
            return;
        case GL_RGB16I:
            outFormat = GL_RGB_INTEGER;
            outType = GL_SHORT;
            return;
        case GL_RGB32UI:
            outFormat = GL_RGB_INTEGER;
            outType = GL_UNSIGNED_INT;
            return;
        case GL_RGB32I:
            outFormat = GL_RGB_INTEGER;
            outType = GL_INT;
            return;
        case GL_RGBA8:
            outFormat = GL_RGBA;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_SRGB8_ALPHA8:
            outFormat = GL_RGBA;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_RGBA8_SNORM:
            outFormat = GL_RGBA;
            outType = GL_BYTE;
            return;
        case GL_RGB5_A1:
            outFormat = GL_RGBA;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_RGBA4:
            outFormat = GL_RGBA;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_RGB10_A2:
            outFormat = GL_RGBA;
            outType = GL_UNSIGNED_INT_2_10_10_10_REV;
            return;
        case GL_RGBA16F:
            outFormat = GL_RGBA;
            outType = GL_HALF_FLOAT;
            return;
        case GL_RGBA32F:
            outFormat = GL_RGBA;
            outType = GL_FLOAT;
            return;
        case GL_RGBA8UI:
            outFormat = GL_RGBA_INTEGER;
            outType = GL_UNSIGNED_BYTE;
            return;
        case GL_RGBA8I:
            outFormat = GL_RGBA_INTEGER;
            outType = GL_BYTE;
            return;
        case GL_RGB10_A2UI:
            outFormat = GL_RGBA_INTEGER;
            outType = GL_UNSIGNED_INT_2_10_10_10_REV;
            return;
        case GL_RGBA16UI:
            outFormat = GL_RGBA_INTEGER;
            outType = GL_UNSIGNED_SHORT;
            return;
        case GL_RGBA16I:
            outFormat = GL_RGBA_INTEGER;
            outType = GL_SHORT;
            return;
        case GL_RGBA32I:
            outFormat = GL_RGBA_INTEGER;
            outType = GL_INT;
            return;
        case GL_RGBA32UI:
            outFormat = GL_RGBA_INTEGER;
            outType = GL_UNSIGNED_INT;
            return;
        }
    }

    bool CheckFrameBuffer()
    {
        // Check results
        GLenum eResult = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (eResult != GL_FRAMEBUFFER_COMPLETE)
        {
            return false;
        }
        return true;
    }

    RenderTarget::RenderTarget()
        : mWidth(0)
        , mHeight(0)
        , mSamples(0)
        , mDepthAttachmentId(0)
        , mFramebufferId(0)
        , mColorAttachmentLeftEyeId(0)
        , mColorAttachmentRightEyeId(0)
    {
        mColorAttachmentIds.resize(0);
        mNumColorAttach = 0;
        mRefCount = 0;
    }

    void RenderTarget::InitializeMultiView(int32_t const width, int32_t const height, int32_t const colorSizedFormat, bool const isProtectedContent)
    {
        typedef void (*PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVR)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei);
        typedef void (*PFNGLTEXTUREVIEWPROC)(GLuint texture, GLenum target, GLuint origtexture, GLenum internalformat, GLuint minlevel, GLuint numlevels, GLuint minlayer, GLuint numlayers);
        static PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVR  glFramebufferTextureMultiviewOVR = nullptr;
        static PFNGLTEXTUREVIEWPROC glTextureView = nullptr;
        if (!glFramebufferTextureMultiviewOVR)
        {
            glFramebufferTextureMultiviewOVR = (PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVR)eglGetProcAddress("glFramebufferTextureMultiviewOVR");
            assert(glFramebufferTextureMultiviewOVR);
        }
        if (!glTextureView)
        {
            glTextureView = reinterpret_cast<PFNGLTEXTUREVIEWPROC>(eglGetProcAddress("glTextureViewOES"));
            assert(glTextureView);
        }

        if (!(glFramebufferTextureMultiviewOVR && glTextureView))
        {
            LOGE("RenderTarget::InitializeMultiView", "Required GL extensions aren't supported!");
            return;
        }

        mWidth = width;
        mHeight = height;
        mSamples = 1;
        mIsProtectedContent = isProtectedContent;
        mNumColorAttach = 1;

        int32_t format, type;
        GetFormatTypeFromSizedFormat( colorSizedFormat, format, type);

        //Create the color attachment
        mColorAttachmentIds.resize(1);
        glGenTextures(1, &(mColorAttachmentIds[0]));
        glBindTexture(GL_TEXTURE_2D_ARRAY, mColorAttachmentIds[0]);

        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        if (mIsProtectedContent)
        {
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_PROTECTED_EXT, GL_TRUE);
        }
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, colorSizedFormat, width, height, 2);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

        //Create the framebuffer
        glGenFramebuffers(1, &mFramebufferId);
        glBindFramebuffer(GL_FRAMEBUFFER, mFramebufferId);

        glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                         mColorAttachmentIds[0], 0, 0, 2);

        bool checkRes = CheckFrameBuffer();
        assert(checkRes);

        glBindFramebuffer( GL_FRAMEBUFFER, 0 );

        // Create texture views for each eye
        glGenTextures(1, &mColorAttachmentLeftEyeId);
        glTextureView(mColorAttachmentLeftEyeId, GL_TEXTURE_2D, mColorAttachmentIds[0], colorSizedFormat, 0, 1, 0, 1);
        glBindTexture(GL_TEXTURE_2D, mColorAttachmentLeftEyeId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenTextures(1, &mColorAttachmentRightEyeId);
        glTextureView(mColorAttachmentRightEyeId, GL_TEXTURE_2D, mColorAttachmentIds[0], colorSizedFormat, 0, 1, 1, 1);
        glBindTexture(GL_TEXTURE_2D, mColorAttachmentRightEyeId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void RenderTarget::InitializeSingleSample(int32_t const width, int32_t const height, int32_t const samples, int32_t const colorSizedFormat, int32_t const format, int32_t const type, bool const requiresDepth)
    {
        glGenTextures(1, &(mColorAttachmentIds[0]));
        glBindTexture(GL_TEXTURE_2D, mColorAttachmentIds[0]);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        if (mIsProtectedContent)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_PROTECTED_EXT, GL_TRUE);
            glTexStorage2D(GL_TEXTURE_2D, 1, colorSizedFormat, width, height);
        }
        else
        {
            glTexImage2D(GL_TEXTURE_2D, 0, colorSizedFormat, width, height, 0, format, type, nullptr);
        }
        glBindTexture(GL_TEXTURE_2D, 0);

        if (requiresDepth)
        {
            //Create the depth attachment (forced to 32bit currently)
            glGenTextures(1, &mDepthAttachmentId);
            glBindTexture(GL_TEXTURE_2D, mDepthAttachmentId);
            if(mIsProtectedContent)
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_PROTECTED_EXT, GL_TRUE);
                glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, width, height);

            }
            else
            {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        //Create the framebuffer
        glGenFramebuffers(1, &mFramebufferId);
        glBindFramebuffer(GL_FRAMEBUFFER, mFramebufferId);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mColorAttachmentIds[0], 0);
        if (requiresDepth)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mDepthAttachmentId, 0);
        }
    }

    void RenderTarget::InitializeMultiSample(int32_t const width, int32_t const height, int32_t const samples, int32_t const colorSizedFormat, int32_t const format, bool const requiresDepth)
    {
        GLenum const renderBuffer = GL_RENDERBUFFER;
        glGenRenderbuffers(1, &(mColorAttachmentIds[0]));
        glBindRenderbuffer(renderBuffer, mColorAttachmentIds[0]);
        glRenderbufferStorageMultisample(renderBuffer, samples, colorSizedFormat, width, height);

        if (requiresDepth)
        {
            //Create the depth attachment (forced to 32bit currently)
            glGenRenderbuffers(1, &mDepthAttachmentId);
            glBindRenderbuffer(renderBuffer, mDepthAttachmentId);
            glRenderbufferStorageMultisample(renderBuffer, samples, GL_DEPTH24_STENCIL8, width, height);
            glBindRenderbuffer(renderBuffer, 0);
        }

        //Create the framebuffer
        GLenum const frameBuffer = GL_FRAMEBUFFER;
        glGenFramebuffers(1, &mFramebufferId);
        glBindFramebuffer(frameBuffer, mFramebufferId);

        glFramebufferRenderbuffer(frameBuffer, GL_COLOR_ATTACHMENT0, renderBuffer, mColorAttachmentIds[0]);
        if (requiresDepth)
        {
            glFramebufferRenderbuffer(frameBuffer, GL_DEPTH_ATTACHMENT, renderBuffer, mDepthAttachmentId);
        }
    }

    void RenderTarget::InitializeSingleSampleMultiAttach(int32_t const width, int32_t const height, int32_t const samples, int32_t const colorSizedFormat, int32_t const format, int32_t const type, bool const requiresDepth)
    {
        //Create the framebuffer
        glGenFramebuffers(1, &mFramebufferId);
        glBindFramebuffer(GL_FRAMEBUFFER, mFramebufferId);
        GLenum drawBuffers[mNumColorAttach];

        for (int i = 0; i < mNumColorAttach; i++)
        {
            glGenTextures(1, &(mColorAttachmentIds[i]));
            glBindTexture(GL_TEXTURE_2D, mColorAttachmentIds[i]);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            if (mIsProtectedContent)
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_PROTECTED_EXT, GL_TRUE);
                glTexStorage2D(GL_TEXTURE_2D, 1, colorSizedFormat, width, height);
            }
            else
            {
                glTexImage2D(GL_TEXTURE_2D, 0, colorSizedFormat, width, height, 0, format, type, nullptr);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, mColorAttachmentIds[i], 0);
            drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
        }
        // Set Draw Buffers based on num color attachements
        glDrawBuffers(mNumColorAttach, drawBuffers);

        if (requiresDepth)
        {
            //Create the depth attachment (forced to 32bit currently)
            glGenTextures(1, &mDepthAttachmentId);
            glBindTexture(GL_TEXTURE_2D, mDepthAttachmentId);
            if(mIsProtectedContent)
            {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_PROTECTED_EXT, GL_TRUE);
                glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, width, height);

            }
            else
            {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mDepthAttachmentId, 0);
        }
    }

    void RenderTarget::InitializeMultiSampleMultiAttach(int32_t const width, int32_t const height, int32_t const samples, int32_t const colorSizedFormat, int32_t const format, bool const requiresDepth)
    {
        //Create the framebuffer
        GLenum const frameBuffer = GL_FRAMEBUFFER;
        glGenFramebuffers(1, &mFramebufferId);
        glBindFramebuffer(frameBuffer, mFramebufferId);
        GLenum DrawBuffers[mNumColorAttach];

        GLenum const renderBuffer = GL_RENDERBUFFER;
        for (int i = 0; i < mNumColorAttach; i++) {
            glGenRenderbuffers(1, &(mColorAttachmentIds[i]));
            glBindRenderbuffer(renderBuffer, mColorAttachmentIds[i]);

            glRenderbufferStorageMultisample(renderBuffer, samples, colorSizedFormat, width, height);
            glBindRenderbuffer(renderBuffer, 0);
            glFramebufferRenderbuffer(frameBuffer, GL_COLOR_ATTACHMENT0 + i, renderBuffer, mColorAttachmentIds[i]);
            DrawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
        }

        // Set Draw Buffers based on num color attachements
        glDrawBuffers(mNumColorAttach, DrawBuffers);

        if (requiresDepth)
        {
            //Create the depth attachment (forced to 32bit currently)
            glGenRenderbuffers(1, &mDepthAttachmentId);
            glBindRenderbuffer(renderBuffer, mDepthAttachmentId);
            glRenderbufferStorageMultisample(renderBuffer, samples, GL_DEPTH24_STENCIL8, width, height);
            glBindRenderbuffer(renderBuffer, 0);
            glFramebufferRenderbuffer(frameBuffer, GL_DEPTH_ATTACHMENT, renderBuffer, mDepthAttachmentId);
        }
    }

    void RenderTarget::Initialize(int32_t const width, int32_t const height, int32_t const samples, int32_t const colorSizedFormat, bool const requiresDepth, bool const isProtectedContent, uint8_t const  numColorAttach)
    {
        assert(numColorAttach >= 1);
        mWidth = width;
        mHeight = height;
        mSamples = samples;
        mIsProtectedContent = isProtectedContent;
        mNumColorAttach = numColorAttach;
        mColorAttachmentIds.clear();
        mColorAttachmentIds.resize(numColorAttach);

        int32_t format, type;
        GetFormatTypeFromSizedFormat( colorSizedFormat, format, type);

        //Create the color attachment
        if (numColorAttach > 1)
        {
            if (samples > 1)
            {
                InitializeMultiSampleMultiAttach(width, height, samples, colorSizedFormat, format, requiresDepth);
            }
            else
            {
                InitializeSingleSampleMultiAttach(width, height, samples, colorSizedFormat, format, type, requiresDepth);
            }
        }
        else
        {
            if (samples > 1)
            {
                InitializeMultiSample(width, height, samples, colorSizedFormat, format, requiresDepth);
            }
            else
            {
                InitializeSingleSample(width, height, samples, colorSizedFormat, format, type, requiresDepth);
            }
        }
        bool checkRes = CheckFrameBuffer();
        assert(checkRes);

        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    }

    void RenderTarget::InitializeImageTargetRenderbuffer(int32_t const width, int32_t const height, EGLImageKHR const image, bool const isProtectedContent, GLenum targetType, int32_t layer)
    {
        assert(image != EGL_NO_IMAGE_KHR);

        static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;
        if (!glEGLImageTargetTexture2DOES)
        {
            glEGLImageTargetTexture2DOES = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
            assert(glEGLImageTargetTexture2DOES);
        }

        mWidth = width;
        mHeight = height;
        mSamples = 1;
        mNumColorAttach = 1;

        mColorAttachmentIds.resize(1);
        glGenTextures(1, &(mColorAttachmentIds[0]));
        glBindTexture(targetType, mColorAttachmentIds[0]);

        glTexParameteri(targetType, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(targetType, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(targetType, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(targetType, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(targetType, GL_TEXTURE_PROTECTED_EXT, isProtectedContent ? GL_TRUE : GL_FALSE);
        glEGLImageTargetTexture2DOES(targetType, image);

        glBindTexture(targetType, 0);

        //Create the framebuffer
        glGenFramebuffers(1, &mFramebufferId);
        glBindFramebuffer(GL_FRAMEBUFFER, mFramebufferId);

        if (targetType == GL_TEXTURE_2D_ARRAY)
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, mColorAttachmentIds[0], 0, layer);
        else
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetType, mColorAttachmentIds[0], 0);

        bool checkRes = CheckFrameBuffer();
        assert(checkRes);

        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    }

    void RenderTarget::InitializeImageTargetRenderbuffer(int32_t const width, int32_t const height, GLuint const image, bool const isProtectedContent, GLenum targetType, int32_t layer)
    {
        assert(image);

        mWidth = width;
        mHeight = height;
        mSamples = 1;
        mNumColorAttach = 1;

        //Create the framebuffer
        glGenFramebuffers(1, &mFramebufferId);
        glBindFramebuffer(GL_FRAMEBUFFER, mFramebufferId);

        mColorAttachmentIds.resize(1);
        mColorAttachmentIds[0] = image;
        if (targetType == GL_TEXTURE_2D_ARRAY)
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, image, 0, layer);
        else
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, targetType, image, 0);

        bool checkRes = CheckFrameBuffer();
        assert(checkRes);

        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    }

    void RenderTarget::Destroy()
    {
        glDeleteFramebuffers( 1, &mFramebufferId);

        if (mSamples > 1)
        {
            for (auto itr = mColorAttachmentIds.begin(); itr != mColorAttachmentIds.end(); ++itr)
            {
                uint32_t colorAttachmentId = *itr;
                if (colorAttachmentId != 0)
                {
                    glDeleteRenderbuffers(1, &colorAttachmentId);
                }
            }
            if (mDepthAttachmentId != 0)
            {
                glDeleteRenderbuffers(1, &mDepthAttachmentId);
            }
        }
        else
        {
            for (auto itr = mColorAttachmentIds.begin(); itr != mColorAttachmentIds.end(); ++itr)
            {
                uint32_t colorAttachmentId = *itr;
                if (colorAttachmentId != 0)
                {
                    glDeleteTextures(1, &colorAttachmentId);
                }
            }
            if (mDepthAttachmentId != 0)
            {
                glDeleteTextures(1, &mDepthAttachmentId);
            }
            if (mColorAttachmentLeftEyeId != 0)
            {
                glDeleteTextures(1, &mColorAttachmentLeftEyeId);
            }
            if (mColorAttachmentRightEyeId != 0)
            {
                glDeleteTextures(1, &mColorAttachmentRightEyeId);
            }
        }

        mFramebufferId = 0;
        mColorAttachmentIds.clear();
        mColorAttachmentIds.resize(0);
        mDepthAttachmentId = 0;
        mColorAttachmentLeftEyeId = 0;
        mColorAttachmentRightEyeId = 0;
    }

    void RenderTarget::Bind()
    {
        if (mRefCount != 0)
        {
            LOGW("RenderTarget::Bind", "RenderTarget (Handle = %d) is being bound without being unbound. Bind count = %d", mFramebufferId, mRefCount);
        }
        mRefCount = mRefCount + 1;

        glBindFramebuffer(GL_FRAMEBUFFER, mFramebufferId);
    }

    void RenderTarget::Unbind()
    {
        if (mRefCount == 0)
        {
            LOGW("RenderTarget::Unbind", "RenderTarget (Handle = %d) is being unbound without being bound.", mFramebufferId);
        }
        else
        {
            mRefCount = mRefCount - 1;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    uint32_t RenderTarget::GetColorAttachment() const
    {
        assert(mNumColorAttach == 1);
        return mColorAttachmentIds[0];
    }

    uint32_t RenderTarget::GetColorAttachmentByIdx(uint8_t idx) const
    {
        assert(idx < mNumColorAttach);
        return mColorAttachmentIds[idx];
    }

    void RenderTarget::GetMultiViewColorAttachments(uint32_t& leftEyeOut, uint32_t& rightEyeOut)
    {
        leftEyeOut = mColorAttachmentLeftEyeId;
        rightEyeOut = mColorAttachmentRightEyeId;
    }

    uint32_t RenderTarget::GetDepthAttachment() const
    {
        return mDepthAttachmentId;
    }

    uint32_t RenderTarget::GetFrameBufferId() const
    {
        return mFramebufferId;
    }

    int32_t RenderTarget::GetWidth() const
    {
        return mWidth;
    }

    int32_t RenderTarget::GetHeight() const
    {
        return mHeight;
    }

    int32_t RenderTarget::GetSamples() const
    {
        return mSamples;
    }
}
