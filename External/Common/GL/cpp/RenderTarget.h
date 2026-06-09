/****************************************************************
 * Copyright (c) 2020-2022 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/
#pragma once

#include <cstdint>
#include <vector>
#include "Extensions.h"

namespace QtiGL
{
    class RenderTarget
    {
    public:
        RenderTarget();

        void Initialize(int32_t const width, int32_t const height, int32_t const samples, int32_t const colorSizedFormat, bool const requiresDepth, bool const isProtectedContent = false, uint8_t const  numColorAttach = 1);
        void InitializeMultiView(int32_t const width, int32_t const height, int32_t const colorSizedFormat, bool const isProtectedContent = false);
        void InitializeImageTargetRenderbuffer(int32_t const width, int32_t const height, EGLImageKHR const image, bool const isProtectedContent, GLenum targetType = GL_TEXTURE_2D, int32_t layer = 0);
        // The GLuint texture that is passed to this function will have its ownership transferred
        // to this RenderTarget. The Destroy() function will free the texture for you.
        void InitializeImageTargetRenderbuffer(int32_t const width, int32_t const height, GLuint const image, bool const isProtectedContent, GLenum targetType = GL_TEXTURE_2D, int32_t layer = 0);
        void Destroy();
        void Bind();
        void Unbind();

        uint32_t GetColorAttachment() const;
        uint32_t GetColorAttachmentByIdx(uint8_t idx) const;
        void GetMultiViewColorAttachments(uint32_t& leftEyeOut, uint32_t& rightEyeOut);
        uint32_t GetDepthAttachment() const;
        uint32_t GetFrameBufferId() const;

        int32_t GetWidth() const;
        int32_t GetHeight() const;
        int32_t GetSamples() const;
        uint8_t GetNumColorAttachs() const;

    private:
        void InitializeSingleSample(int32_t const width, int32_t const height, int32_t const samples, int32_t const colorSizedFormat, int32_t const format, int32_t const type, bool const requiresDepth);
        void InitializeMultiSample(int32_t const width, int32_t const height, int32_t const samples, int32_t const colorSizedFormat, int32_t const format, bool const requiresDepth);
        void InitializeSingleSampleMultiAttach(int32_t const width, int32_t const height, int32_t const samples, int32_t const colorSizedFormat, int32_t const format, int32_t const type, bool const requiresDepth);
        void InitializeMultiSampleMultiAttach(int32_t const width, int32_t const height, int32_t const samples, int32_t const colorSizedFormat, int32_t const format, bool const requiresDepth);

        int32_t     mWidth;
        int32_t     mHeight;
        int32_t     mSamples;

        uint32_t    mRefCount;

        std::vector<uint32_t> mColorAttachmentIds;
        uint32_t mColorAttachmentLeftEyeId;
        uint32_t mColorAttachmentRightEyeId;
        uint32_t mDepthAttachmentId;
        uint32_t mFramebufferId;
        bool     mIsProtectedContent;
        uint8_t  mNumColorAttach;
    };
}
