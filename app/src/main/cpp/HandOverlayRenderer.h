#pragma once

#include <GLES3/gl3.h>
#include <cstdint>

// KB fisheye projection + GL 2D overlay renderer for hand joints.
// Projects 3D hand joints (RootSpace) to 2D pixel coordinates using
// Kannala-Brandt fisheye model, then renders bone lines and joint dots
// as a 2D overlay on any target FBO.

class HandOverlayRenderer {
public:
    struct EyeCameraParams {
        float focalX, focalY;
        float centerX, centerY;
        float distortion[8];
        float extPos[3];
        float extQuat[4];
        uint32_t width, height;
        float offsetX = 0.0f;   // UV pixel offset, applied after projection
        float offsetY = 0.0f;
        bool valid = false;
    };

    struct ProjectedHand {
        float joints[26][2]; // (u, v) in pixels, (-1, -1) = behind camera
        bool active = false;
    };

    struct FrameProjection {
        ProjectedHand leftHand[2];  // [0]=left eye, [1]=right eye
        ProjectedHand rightHand[2];
    };

    HandOverlayRenderer() = default;
    ~HandOverlayRenderer();

    void init();
    void updateCameraParams(int eyeIndex, float focalX, float focalY,
                            float centerX, float centerY,
                            const float distortion[8],
                            const float extPos[3],
                            const float extQuat[4],
                            uint32_t width, uint32_t height);
    void setUVOffset(int eyeIndex, float offsetX, float offsetY);
    void computeProjection(bool leftActive, const float leftJoints[26][3],
                           bool rightActive, const float rightJoints[26][3],
                           const float headPos[3], const float headQuat[4]);
    void render(int eyeIndex, int offsetX, int regionW, int regionH) const;
    void render(int eyeIndex, int vpX, int vpY, int vpW, int vpH,
                int resW, int resH) const;
    const FrameProjection& getProjection() const { return projection_; }
    const EyeCameraParams& getEyeParams(int eyeIndex) const { return eyeParams_[eyeIndex]; }
    GLuint getShaderProgram() const { return shaderProgram_; }

private:
    static bool projectKB(const float point3d[3],
                          float focalX, float focalY,
                          float centerX, float centerY,
                          const float distortion[8],
                          float& outU, float& outV);
    static void worldToCamera(const float jointWorld[3],
                              const float wcPos[3],
                              const float headQuat[4],
                              const float extQuat[4],
                              float outCam[3]);
    static void quatMultiply(const float a[4], const float b[4], float out[4]);
    static void quatConjugate(const float q[4], float out[4]);
    static void quatRotate(const float q[4], const float v[3], float out[3]);

    EyeCameraParams eyeParams_[2];
    FrameProjection projection_{};
    GLuint shaderProgram_ = 0;
    GLuint lineVBO_ = 0;
    GLuint circleVBO_ = 0;
    bool initialized_ = false;
};
