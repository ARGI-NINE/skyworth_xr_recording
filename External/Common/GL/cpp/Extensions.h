/****************************************************************
 * Copyright (c) 2020-2022 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/
#pragma once
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#ifdef LINUX
#ifndef EGL_CAST
#define EGL_CAST(type, value) ((type) (value))

#ifndef GL_APIENTRYP
#define GL_APIENTRYP GL_APIENTRY*
#endif

typedef void *EGLImage;
#define EGL_NO_IMAGE                      EGL_CAST(EGLImage,0)
typedef EGLImage (EGLAPIENTRYP PFNEGLCREATEIMAGEPROC) (EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLDESTROYIMAGEPROC) (EGLDisplay dpy, EGLImage image);
EGLAPI EGLImage EGLAPIENTRY eglCreateImage (EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list);
EGLAPI EGLBoolean EGLAPIENTRY eglDestroyImage (EGLDisplay dpy, EGLImage image);
#endif
#endif

#ifndef EGL_KHR_image
#define EGL_KHR_image 1
typedef void *EGLImageKHR;
#define EGL_NATIVE_PIXMAP_KHR             0x30B0
#define EGL_NO_IMAGE_KHR                  EGL_CAST(EGLImageKHR,0)
typedef EGLImageKHR (EGLAPIENTRYP PFNEGLCREATEIMAGEKHRPROC) (EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLDESTROYIMAGEKHRPROC) (EGLDisplay dpy, EGLImageKHR image);
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLImageKHR EGLAPIENTRY eglCreateImageKHR (EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list);
EGLAPI EGLBoolean EGLAPIENTRY eglDestroyImageKHR (EGLDisplay dpy, EGLImageKHR image);
#endif
#endif

#ifndef EGL_EXT_external_objects
#define EGL_EXT_external_objects 1
#define GL_PROTECTED_MEMORY_OBJECT_EXT    0x959B
#define GL_DEDICATED_MEMORY_OBJECT_EXT    0x9581
typedef void (GL_APIENTRYP PFNGLCREATEMEMORYOBJECTSEXTPROC) (GLsizei n, GLuint* memoryObjects);
typedef GLboolean (GL_APIENTRYP PFNGLISMEMORYOBJECTEXTPROC) (GLuint memoryObject);
typedef void (GL_APIENTRYP PFNGLTEXSTORAGEMEM2DEXTPROC) (GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset);
typedef void (GL_APIENTRYP PFNGLTEXSTORAGEMEM3DEXTPROC) (GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLuint memory, GLuint64 offset);
typedef void (GL_APIENTRYP PFNGLDELETEMEMORYOBJECTSEXTPROC) (GLsizei n, const GLuint *memoryObjects);
typedef void (GL_APIENTRYP PFNGLGETMEMORYOBJECTPARAMETERIVEXTPROC) (GLuint memoryObject, GLenum pname, GLint* params);
typedef void (GL_APIENTRYP PFNGLMEMORYOBJECTPARAMETERIVEXTPROC) (GLuint memoryObject, GLenum pname, const GLint* params);
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI void GL_APIENTRYP glCreateMemoryObjectsEXT(GLsizei n, GLuint *memoryObjects);
EGLAPI GLboolean GL_APIENTRYP glIsMemoryObjectEXT(GLuint memoryObject);
EGLAPI void GL_APIENTRYP glTexStorageMem2DEXT(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset);
EGLAPI void GL_APIENTRYP glTexStorageMem3DEXT(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLuint memory, GLuint64 offset);
EGLAPI void GL_APIENTRYP glDeleteMemoryObjectsEXT(GLsizei n, const GLuint *memoryObjects);
EGLAPI void GL_APIENTRYP glGetMemoryObjectParameterivEXT(GLuint memoryObject, GLenum pname, GLint* params);
EGLAPI void GL_APIENTRYP glMemoryObjectParameterivEXT(GLuint memoryObject, GLenum pname, const GLint* params);
#endif
#endif

#ifndef EGL_EXT_external_objects_fd
#define EGL_EXT_external_objects_fd 1
#define GL_HANDLE_TYPE_OPAQUE_FD_EXT 0x9586
typedef void (GL_APIENTRYP PFNGLIMPORTMEMORYFDEXTPROC) (GLuint memory, GLuint64 size, GLenum handleType, GLint fd);
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI void EGLAPIENTRY glImportMemoryFdEXT(GLuint memory, GLuint64 size, GLenum handleType, GLint fd);
#endif
#endif

#ifndef EGL_KHR_image_base
#define EGL_KHR_image_base 1
#define EGL_IMAGE_PRESERVED_KHR           0x30D2
#endif

#ifndef EGL_ANDROID_get_native_client_buffer
#define EGL_ANDROID_get_native_client_buffer 1
struct AHardwareBuffer;
typedef EGLClientBuffer (EGLAPIENTRYP PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC) (const struct AHardwareBuffer *buffer);
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLClientBuffer EGLAPIENTRY eglGetNativeClientBufferANDROID (const struct AHardwareBuffer *buffer);
#endif
#endif

#ifndef EGL_ANDROID_image_native_buffer
#define EGL_ANDROID_image_native_buffer 1
#define EGL_NATIVE_BUFFER_ANDROID         0x3140
#endif

#if !defined( EGL_OPENGL_ES3_BIT_KHR )
#define EGL_OPENGL_ES3_BIT_KHR		            0x0040
#endif

#ifndef EGL_EXT_protected_surface
#define EGL_EXT_protected_surface               1
#define EGL_PROTECTED_CONTENT_EXT               0x32C0
#endif

#if !defined(EGL_CONTEXT_PRIORITY_LEVEL_IMG)
#define EGL_CONTEXT_PRIORITY_LEVEL_IMG          0x3100
#define EGL_CONTEXT_PRIORITY_HIGH_IMG           0x3101
#define EGL_CONTEXT_PRIORITY_MEDIUM_IMG         0x3102
#define EGL_CONTEXT_PRIORITY_LOW_IMG            0x3103
#endif

#if !defined(EGL_FRONT_BUFFER_AUTO_REFRESH_ANDROID)
#define EGL_FRONT_BUFFER_AUTO_REFRESH_ANDROID   0x314C
#endif

#ifdef LINUX
#ifndef GL_OES_EGL_image_external
#define GL_OES_EGL_image_external 1
#endif
#endif

// These are no longer defined in GL3Ext.h
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES           0x8D65
#endif

#ifndef GL_SAMPLER_EXTERNAL_OES
#define GL_SAMPLER_EXTERNAL_OES           0x8D66
#endif

#if !defined(GL_BINNING_CONTROL_HINT_QCOM)
#define GL_BINNING_CONTROL_HINT_QCOM            0x8FB0
#define GL_BINNING_QCOM                         0x8FB1
#define GL_VISIBILITY_OPTIMIZED_BINNING_QCOM    0x8FB2
#define GL_RENDER_DIRECT_TO_FRAMEBUFFER_QCOM    0x8FB3
#endif

#if !defined( GL_TEXTURE_PROTECTED_EXT )
#define GL_TEXTURE_PROTECTED_EXT    0x8BFA
#endif

#ifndef GL_OES_EGL_image
#define GL_OES_EGL_image 1
typedef void *GLeglImageOES;
typedef void (GL_APIENTRYP PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) (GLenum target, GLeglImageOES image);
typedef void (GL_APIENTRYP PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC) (GLenum target, GLeglImageOES image);
#ifdef GL_GLEXT_PROTOTYPES
GL_APICALL void GL_APIENTRY glEGLImageTargetTexture2DOES (GLenum target, GLeglImageOES image);
GL_APICALL void GL_APIENTRY glEGLImageTargetRenderbufferStorageOES (GLenum target, GLeglImageOES image);
#endif
#endif /* GL_OES_EGL_image */

#ifndef GL_EXT_EGL_image_storage
#define GL_EXT_EGL_image_storage 1
typedef void (GL_APIENTRYP PFNGLEGLIMAGETARGETTEXSTORAGEEXTPROC) (GLenum target, GLeglImageOES image, const GLint* attrib_list);
typedef void (GL_APIENTRYP PFNGLEGLIMAGETARGETTEXTURESTORAGEEXTPROC) (GLuint texture, GLeglImageOES image, const GLint* attrib_list);
#ifdef GL_GLEXT_PROTOTYPES
GL_APICALL void GL_APIENTRY glEGLImageTargetTexStorageEXT (GLenum target, GLeglImageOES image, const GLint* attrib_list);
GL_APICALL void GL_APIENTRY glEGLImageTargetTextureStorageEXT (GLuint texture, GLeglImageOES image, const GLint* attrib_list);
#endif
#endif /* GL_EXT_EGL_image_storage */

#if !defined(TEXTURE_BORDER_COLOR_EXT)
#define TEXTURE_BORDER_COLOR_EXT                0x1004
#define CLAMP_TO_BORDER_EXT                     0x812D
#endif

/* GL_QCOM_tiled_rendering */
#ifndef GL_QCOM_tiled_rendering
#define GL_QCOM_tiled_rendering 1
#define GL_COLOR_BUFFER_BIT0_QCOM                               0x00000001
#define GL_COLOR_BUFFER_BIT1_QCOM                               0x00000002
#define GL_COLOR_BUFFER_BIT2_QCOM                               0x00000004
#define GL_COLOR_BUFFER_BIT3_QCOM                               0x00000008
#define GL_COLOR_BUFFER_BIT4_QCOM                               0x00000010
#define GL_COLOR_BUFFER_BIT5_QCOM                               0x00000020
#define GL_COLOR_BUFFER_BIT6_QCOM                               0x00000040
#define GL_COLOR_BUFFER_BIT7_QCOM                               0x00000080
#define GL_DEPTH_BUFFER_BIT0_QCOM                               0x00000100
#define GL_DEPTH_BUFFER_BIT1_QCOM                               0x00000200
#define GL_DEPTH_BUFFER_BIT2_QCOM                               0x00000400
#define GL_DEPTH_BUFFER_BIT3_QCOM                               0x00000800
#define GL_DEPTH_BUFFER_BIT4_QCOM                               0x00001000
#define GL_DEPTH_BUFFER_BIT5_QCOM                               0x00002000
#define GL_DEPTH_BUFFER_BIT6_QCOM                               0x00004000
#define GL_DEPTH_BUFFER_BIT7_QCOM                               0x00008000
#define GL_STENCIL_BUFFER_BIT0_QCOM                             0x00010000
#define GL_STENCIL_BUFFER_BIT1_QCOM                             0x00020000
#define GL_STENCIL_BUFFER_BIT2_QCOM                             0x00040000
#define GL_STENCIL_BUFFER_BIT3_QCOM                             0x00080000
#define GL_STENCIL_BUFFER_BIT4_QCOM                             0x00100000
#define GL_STENCIL_BUFFER_BIT5_QCOM                             0x00200000
#define GL_STENCIL_BUFFER_BIT6_QCOM                             0x00400000
#define GL_STENCIL_BUFFER_BIT7_QCOM                             0x00800000
#define GL_MULTISAMPLE_BUFFER_BIT0_QCOM                         0x01000000
#define GL_MULTISAMPLE_BUFFER_BIT1_QCOM                         0x02000000
#define GL_MULTISAMPLE_BUFFER_BIT2_QCOM                         0x04000000
#define GL_MULTISAMPLE_BUFFER_BIT3_QCOM                         0x08000000
#define GL_MULTISAMPLE_BUFFER_BIT4_QCOM                         0x10000000
#define GL_MULTISAMPLE_BUFFER_BIT5_QCOM                         0x20000000
#define GL_MULTISAMPLE_BUFFER_BIT6_QCOM                         0x40000000
#define GL_MULTISAMPLE_BUFFER_BIT7_QCOM                         0x80000000
#ifdef GL_GLEXT_PROTOTYPES
GL_APICALL void GL_APIENTRY glStartTilingQCOM (GLuint x, GLuint y, GLuint width, GLuint height, GLbitfield preserveMask);
GL_APICALL void GL_APIENTRY glEndTilingQCOM (GLbitfield preserveMask);
#endif
typedef void (GL_APIENTRYP PFNGLSTARTTILINGQCOMPROC) (GLuint x, GLuint y, GLuint width, GLuint height, GLbitfield preserveMask);
typedef void (GL_APIENTRYP PFNGLENDTILINGQCOMPROC) (GLbitfield preserveMask);
#endif

/* GL_EXT_texture_filter_anisotropic */
#ifndef GL_EXT_texture_filter_anisotropic
#define GL_TEXTURE_MAX_ANISOTROPY_EXT                           0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT                       0x84FF
#endif

// Timers
#define GL_TIMESTAMP_EXT 0x8E28
#define GL_TIME_ELAPSED_EXT 0x88BF
#define GL_GPU_DISJOINT_EXT 0x8FBB

/*GL_KHR_debug*/
#ifndef GL_KHR_debug

#define GL_DEBUG_OUTPUT_KHR 0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR 0x8242

#define GL_DEBUG_SOURCE_API_KHR 0x8246
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM_KHR 0x8247
#define GL_DEBUG_SOURCE_SHADER_COMPILER_KHR 0x8248
#define GL_DEBUG_SOURCE_THIRD_PARTY_KHR 0x8249
#define GL_DEBUG_SOURCE_APPLICATION_KHR 0x824A
#define GL_DEBUG_SOURCE_OTHER_KHR 0x824B

#define GL_DEBUG_TYPE_ERROR_KHR 0x824C
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_KHR 0x824D
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR 0x824E
#define GL_DEBUG_TYPE_PORTABILITY_KHR 0x824F
#define GL_DEBUG_TYPE_PERFORMANCE_KHR 0x8250
#define GL_DEBUG_TYPE_OTHER_KHR 0x8251
#define GL_DEBUG_TYPE_MARKER_KHR 0x8268

#define GL_DEBUG_TYPE_PUSH_GROUP_KHR 0x8269
#define GL_DEBUG_TYPE_POP_GROUP_KHR 0x826A

#define GL_DEBUG_SEVERITY_HIGH_KHR 0x9146
#define GL_DEBUG_SEVERITY_MEDIUM_KHR 0x9147
#define GL_DEBUG_SEVERITY_LOW_KHR 0x9148
#define GL_DEBUG_SEVERITY_NOTIFICATION_KHR 0x826B

typedef void (GL_APIENTRYP GLDEBUGKHRPROC)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam);
typedef void (GL_APIENTRYP PGNGLDEBUGMESSAGECALLBACKKHRPROC)(GLDEBUGKHRPROC callback, const void* userParam);
typedef void (GL_APIENTRYP PFNGLDEBUGMESSAGEINSERTKHRPROC)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* buf);
#endif

#ifndef GL_ARB_texture_view
#define GL_ARB_texture_view 1
#ifdef GLCOREARB_PROTOTYPES
GLAPI void APIENTRY glTextureViewOES (GLuint texture, GLenum target, GLuint origtexture, GLenum internalformat, GLuint minlevel, GLuint numlevels, GLuint minlayer, GLuint numlayers);
#endif /* GLCOREARB_PROTOTYPES */
typedef void (GL_APIENTRYP PFNGLTEXTUREVIEWPROC)(GLuint texture, GLenum target, GLuint origtexture, GLenum internalformat, GLuint minlevel, GLuint numlevels, GLuint minlayer, GLuint numlayers);
#endif

#ifndef GL_EXT_buffer_storage
#define GL_EXT_buffer_storage 1
#define GL_MAP_READ_BIT                   0x0001
#define GL_MAP_WRITE_BIT                  0x0002
#define GL_MAP_PERSISTENT_BIT_EXT         0x0040
#define GL_MAP_COHERENT_BIT_EXT           0x0080
#define GL_DYNAMIC_STORAGE_BIT_EXT        0x0100
#define GL_CLIENT_STORAGE_BIT_EXT         0x0200
#define GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT_EXT 0x00004000
#define GL_BUFFER_IMMUTABLE_STORAGE_EXT   0x821F
#define GL_BUFFER_STORAGE_FLAGS_EXT       0x8220
typedef void (GL_APIENTRYP PFNGLBUFFERSTORAGEEXTPROC) (GLenum target, GLsizeiptr size, const void *data, GLbitfield flags);
#ifdef GL_GLEXT_PROTOTYPES
GL_APICALL void GL_APIENTRY glBufferStorageEXT (GLenum target, GLsizeiptr size, const void *data, GLbitfield flags);
#endif
#endif /* GL_EXT_buffer_storage */

#define GL_SHADER_STORAGE_BUFFER          0x90D2