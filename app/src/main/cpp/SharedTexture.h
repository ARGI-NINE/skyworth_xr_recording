/*
 * SharedTexture
 * @author 	: keith@robot9.me
 *
 */

#pragma once

#include <string>
#include <memory>

#include <jni.h>
#include <android/hardware_buffer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES/glext.h>


class SharedTexture {
 public:
  static bool available();

  static std::shared_ptr<SharedTexture> Make(int width, int height,bool isMultiview);

  static std::shared_ptr<SharedTexture> MakeAdopted(AHardwareBuffer *buffer);

  static std::shared_ptr<SharedTexture> MakeAdoptedJObject(JNIEnv *env, jobject buffer);

  virtual ~SharedTexture();

  bool bindTexture(unsigned texId,bool ismultiview);
  void unbindTexture(){
      bindTexId_ = 0;
  }

  AHardwareBuffer *getBuffer() const;

  jobject getBufferJObject(JNIEnv *env) const;

  int getWidth() const;

  int getHeight() const;

  unsigned getBindTexture() const;

  static int createEGLFence();

  static bool waitEGLFence(int fenceFd);

 private:
  SharedTexture(AHardwareBuffer *buffer, int width, int height);

 private:
  static bool AVAILABLE;

  AHardwareBuffer *buffer_ = nullptr;
  EGLImageKHR eglImage_ = EGL_NO_IMAGE_KHR;
  int width_ = 0;
  int height_ = 0;
  GLuint bindTexId_ = 0;
};
