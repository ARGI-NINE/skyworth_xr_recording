#include "HandOverlayRenderer.h"

#include <android/log.h>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Bone connections (OpenXR XR_HAND_JOINT_* indices)
// Same as Python script BONE_CONNECTIONS.
// ---------------------------------------------------------------------------
static constexpr int BONE_CONNECTIONS[][2] = {
    {1,  0},                                                          // WRIST -> PALM
    {1,  2}, {2,  3}, {3,  4}, {4,  5},                              // Thumb
    {1,  6}, {6,  7}, {7,  8}, {8,  9}, {9, 10},                     // Index
    {1, 11}, {11, 12}, {12, 13}, {13, 14}, {14, 15},                 // Middle
    {1, 16}, {16, 17}, {17, 18}, {18, 19}, {19, 20},                 // Ring
    {1, 21}, {21, 22}, {22, 23}, {23, 24}, {24, 25},                 // Pinky
};
static constexpr int NUM_BONES = 25;

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

HandOverlayRenderer::~HandOverlayRenderer() {
    if (initialized_) {
        glDeleteProgram(shaderProgram_);
        glDeleteBuffers(1, &lineVBO_);
        glDeleteBuffers(1, &circleVBO_);
        initialized_ = false;
    }
}

// ---------------------------------------------------------------------------
// Quaternion math  (convention: x, y, z, w  -- matches OpenXR XrQuaternionf)
// ---------------------------------------------------------------------------

/*static*/ void HandOverlayRenderer::quatMultiply(const float a[4],
                                                   const float b[4],
                                                   float out[4]) {
    float ax = a[0], ay = a[1], az = a[2], aw = a[3];
    float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    out[0] = aw * bx + ax * bw + ay * bz - az * by;
    out[1] = aw * by - ax * bz + ay * bw + az * bx;
    out[2] = aw * bz + ax * by - ay * bx + az * bw;
    out[3] = aw * bw - ax * bx - ay * by - az * bz;
}

/*static*/ void HandOverlayRenderer::quatConjugate(const float q[4],
                                                    float out[4]) {
    out[0] = -q[0];
    out[1] = -q[1];
    out[2] = -q[2];
    out[3] =  q[3];
}

/*static*/ void HandOverlayRenderer::quatRotate(const float q[4],
                                                 const float v[3],
                                                 float out[3]) {
    // result = q * (v,0) * conj(q), take xyz
    float qv[4] = {v[0], v[1], v[2], 0.0f};
    float conj[4];
    quatConjugate(q, conj);
    float tmp[4];
    quatMultiply(q, qv, tmp);
    float result[4];
    quatMultiply(tmp, conj, result);
    out[0] = result[0];
    out[1] = result[1];
    out[2] = result[2];
}

// ---------------------------------------------------------------------------
// Kannala-Brandt fisheye projection
// ---------------------------------------------------------------------------

/*static*/ bool HandOverlayRenderer::projectKB(const float point3d[3],
                                                float focalX, float focalY,
                                                float centerX, float centerY,
                                                const float distortion[8],
                                                float& outU, float& outV) {
    float x = point3d[0], y = point3d[1], z = point3d[2];
    if (z <= 1e-9f) {
        outU = -1.0f;
        outV = -1.0f;
        return false;
    }

    float xn = x / z;
    float yn = y / z;
    float r2 = xn * xn + yn * yn;
    float r  = sqrtf(r2);
    float theta = atanf(r);

    // theta_d = theta * (1 + k1*theta^2 + k2*theta^4 + k3*theta^6 + k4*theta^8)
    float theta2 = theta * theta;
    float theta_d = theta;
    if (fabsf(distortion[0]) > 1e-12f)
        theta_d += distortion[0] * theta * theta2;
    if (fabsf(distortion[1]) > 1e-12f)
        theta_d += distortion[1] * theta * theta2 * theta2;
    if (fabsf(distortion[2]) > 1e-12f)
        theta_d += distortion[2] * theta * theta2 * theta2 * theta2;
    if (fabsf(distortion[3]) > 1e-12f)
        theta_d += distortion[3] * theta * theta2 * theta2 * theta2 * theta2;

    float x_dist, y_dist;
    if (r < 1e-9f) {
        x_dist = 0.0f;
        y_dist = 0.0f;
    } else {
        float scale = theta_d / r;
        x_dist = scale * xn;
        y_dist = scale * yn;
    }

    outU = focalX * x_dist + centerX;
    outV = focalY * y_dist + centerY;
    return true;
}

// ---------------------------------------------------------------------------
// World -> Camera transform
//
// cam = conj(extQuat) * conj(headQuat) * (joint_world - wc_pos)
//
// headQuat: view→world rotation (OpenXR: X=right, Y=up, Z=backward)
// extQuat:  camera→view extrinsics rotation
//
// Matches the Python visualize_hand_on_rgb.py reference implementation.
// ---------------------------------------------------------------------------

/*static*/ void HandOverlayRenderer::worldToCamera(const float jointWorld[3],
                                                    const float wcPos[3],
                                                    const float headQuat[4],
                                                    const float extQuat[4],
                                                    float outCam[3]) {
    float offset[3] = {
        jointWorld[0] - wcPos[0],
        jointWorld[1] - wcPos[1],
        jointWorld[2] - wcPos[2],
    };
    // R_WC = conj(extQuat) * conj(headQuat)
    float headConj[4], extConj[4], R_WC[4];
    quatConjugate(headQuat, headConj);
    quatConjugate(extQuat, extConj);
    quatMultiply(extConj, headConj, R_WC);
    quatRotate(R_WC, offset, outCam);
}

// ---------------------------------------------------------------------------
// updateCameraParams
// ---------------------------------------------------------------------------

void HandOverlayRenderer::updateCameraParams(int eyeIndex,
                                              float focalX, float focalY,
                                              float centerX, float centerY,
                                              const float distortion[8],
                                              const float extPos[3],
                                              const float extQuat[4],
                                              uint32_t width, uint32_t height) {
    if (eyeIndex < 0 || eyeIndex > 1) return;
    EyeCameraParams& p = eyeParams_[eyeIndex];
    p.focalX   = focalX;
    p.focalY   = focalY;
    p.centerX  = centerX;
    p.centerY  = centerY;
    memcpy(p.distortion, distortion, sizeof(p.distortion));
    memcpy(p.extPos, extPos, sizeof(p.extPos));
    memcpy(p.extQuat, extQuat, sizeof(p.extQuat));
    p.width  = width;
    p.height = height;
    p.valid  = true;
}

// ---------------------------------------------------------------------------
// computeProjection
// ---------------------------------------------------------------------------

void HandOverlayRenderer::setUVOffset(int eyeIndex, float offsetX, float offsetY) {
    if (eyeIndex < 0 || eyeIndex > 1) return;
    eyeParams_[eyeIndex].offsetX = offsetX;
    eyeParams_[eyeIndex].offsetY = offsetY;
}

void HandOverlayRenderer::computeProjection(bool leftActive,
                                             const float leftJoints[26][3],
                                             bool rightActive,
                                             const float rightJoints[26][3],
                                             const float headPos[3],
                                             const float headQuat[4]) {
    for (int eye = 0; eye < 2; ++eye) {
        const EyeCameraParams& cam = eyeParams_[eye];
        if (!cam.valid) {
            __android_log_print(ANDROID_LOG_INFO, "HelloXr",
                "computeProjection: eye[%d] params NOT valid", eye);
            continue;
        }

        // Compute world camera position. headQuat is view→world (matches the
        // Python reference convention).
        //   wcPos = headPos + headQuat * extPos
        float extPosRotated[3];
        quatRotate(headQuat, cam.extPos, extPosRotated);
        float wcPos[3] = {
            headPos[0] + extPosRotated[0],
            headPos[1] + extPosRotated[1],
            headPos[2] + extPosRotated[2],
        };

        const float ox = cam.offsetX;
        const float oy = cam.offsetY;

        // Left hand
        if (leftActive) {
            projection_.leftHand[eye].active = true;
            for (int j = 0; j < 26; ++j) {
                float camPt[3];
                worldToCamera(leftJoints[j], wcPos, headQuat, cam.extQuat, camPt);
                float& u = projection_.leftHand[eye].joints[j][0];
                float& v = projection_.leftHand[eye].joints[j][1];
                if (!projectKB(camPt, cam.focalX, cam.focalY,
                               cam.centerX, cam.centerY,
                               cam.distortion, u, v)) {
                    u = -1.0f;
                    v = -1.0f;
                } else {
                    // 90° CW rotation around image center (matches Python rotate_uv_90cw)
                    float cx = cam.centerX, cy = cam.centerY;
                    float u_rot = cx + (v - cy);
                    float v_rot = cy - (u - cx);
                    u = u_rot + ox;
                    v = v_rot + oy;
                }
                // Log one joint per ~1s at WRIST (j=1) for left eye only
                static int s_leftLogCnt = 0;
                if (eye == 0 && j == 1 && (s_leftLogCnt++ % 60) == 0) {
                    __android_log_print(ANDROID_LOG_INFO, "HelloXr",
                        "L wrist eye0: joint=(%.3f,%.3f,%.3f) wc=(%.3f,%.3f,%.3f) cam=(%.3f,%.3f,%.3f) uv=(%.1f,%.1f)",
                        leftJoints[j][0], leftJoints[j][1], leftJoints[j][2],
                        wcPos[0], wcPos[1], wcPos[2],
                        camPt[0], camPt[1], camPt[2], u, v);
                }
            }
        } else {
            projection_.leftHand[eye].active = false;
        }

        // Right hand
        if (rightActive) {
            projection_.rightHand[eye].active = true;
            for (int j = 0; j < 26; ++j) {
                float camPt[3];
                worldToCamera(rightJoints[j], wcPos, headQuat, cam.extQuat, camPt);
                float& u = projection_.rightHand[eye].joints[j][0];
                float& v = projection_.rightHand[eye].joints[j][1];
                if (!projectKB(camPt, cam.focalX, cam.focalY,
                               cam.centerX, cam.centerY,
                               cam.distortion, u, v)) {
                    u = -1.0f;
                    v = -1.0f;
                } else {
                    // 90° CW rotation around image center (matches Python rotate_uv_90cw)
                    float cx = cam.centerX, cy = cam.centerY;
                    float u_rot = cx + (v - cy);
                    float v_rot = cy - (u - cx);
                    u = u_rot + ox;
                    v = v_rot + oy;
                }
            }
        } else {
            projection_.rightHand[eye].active = false;
        }
    }
}

// ---------------------------------------------------------------------------
// GL shader sources
// ---------------------------------------------------------------------------

static const char* kVertSrc = R"(
#version 300 es
precision highp float;

uniform vec2 uResolution;
uniform float uPointSize;

// aPosition.xy = pixel coords (line verts) or joint pixel coords (points)
layout(location = 0) in vec2 aPosition;

void main() {
    vec2 ndc = (aPosition / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    gl_PointSize = uPointSize;
}
)";

static const char* kFragSrc = R"(
#version 300 es
precision highp float;

uniform vec4 uColor;
uniform bool uIsPoints;

layout(location = 0) out vec4 fragColor;

void main() {
    if (uIsPoints) {
        vec2 coord = gl_PointCoord - vec2(0.5);
        if (length(coord) > 0.5) discard;
    }
    fragColor = uColor;
}
)";

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void HandOverlayRenderer::init() {
    if (initialized_) return;

    // Compile vertex shader
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &kVertSrc, nullptr);
    glCompileShader(vs);
    {
        GLint ok = 0;
        glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(vs, sizeof(log), nullptr, log);
            __android_log_print(ANDROID_LOG_ERROR, "HelloXr",
                "HandOverlay VS compile failed: %s", log);
            glDeleteShader(vs);
            return;
        }
    }

    // Compile fragment shader
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &kFragSrc, nullptr);
    glCompileShader(fs);
    {
        GLint ok = 0;
        glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
            __android_log_print(ANDROID_LOG_ERROR, "HelloXr",
                "HandOverlay FS compile failed: %s", log);
            glDeleteShader(vs);
            glDeleteShader(fs);
            return;
        }
    }

    // Link program
    shaderProgram_ = glCreateProgram();
    glAttachShader(shaderProgram_, vs);
    glAttachShader(shaderProgram_, fs);
    glLinkProgram(shaderProgram_);
    {
        GLint ok = 0;
        glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetProgramInfoLog(shaderProgram_, sizeof(log), nullptr, log);
            __android_log_print(ANDROID_LOG_ERROR, "HelloXr",
                "HandOverlay program link failed: %s", log);
            glDeleteProgram(shaderProgram_);
            glDeleteShader(vs);
            glDeleteShader(fs);
            shaderProgram_ = 0;
            return;
        }
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    __android_log_print(ANDROID_LOG_INFO, "HelloXr",
        "HandOverlay init OK: prog=%u lineVBO=%u circleVBO=%u",
        shaderProgram_, lineVBO_, circleVBO_);

    // Create VBOs (no VAO — VAOs are not shared between EGL contexts)
    // Bone quads: 25 bones * 6 verts * 2 coords (2 triangles per bone)
    glGenBuffers(1, &lineVBO_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
    glBufferData(GL_ARRAY_BUFFER, NUM_BONES * 6 * 2 * sizeof(float),
                 nullptr, GL_DYNAMIC_DRAW);

    // Joint quads: 26 joints * 6 verts (2 triangles) * 2 coords
    glGenBuffers(1, &circleVBO_);
    glBindBuffer(GL_ARRAY_BUFFER, circleVBO_);
    glBufferData(GL_ARRAY_BUFFER, 26 * 6 * 2 * sizeof(float),
                 nullptr, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    initialized_ = true;
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------

void HandOverlayRenderer::render(int eyeIndex, int offsetX,
                                  int regionW, int regionH) const {
    render(eyeIndex, offsetX, 0, regionW, regionH, regionW, regionH);
}

// Full version: separate viewport and resolution
void HandOverlayRenderer::render(int eyeIndex, int vpX, int vpY,
                                  int vpW, int vpH,
                                  int resW, int resH) const {
    if (!initialized_) return;
    if (eyeIndex < 0 || eyeIndex > 1) return;

    const EyeCameraParams& cam = eyeParams_[eyeIndex];
    if (!cam.valid) return;

    float fResW = static_cast<float>(resW);
    float fResH = static_cast<float>(resH);

    // Save GL state
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevBlend     = glIsEnabled(GL_BLEND);
    GLboolean prevScissor   = glIsEnabled(GL_SCISSOR_TEST);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glViewport(vpX, vpY, vpW, vpH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(shaderProgram_);

    GLint uResolution = glGetUniformLocation(shaderProgram_, "uResolution");
    GLint uPointSize  = glGetUniformLocation(shaderProgram_, "uPointSize");
    GLint uColor      = glGetUniformLocation(shaderProgram_, "uColor");
    GLint uIsPoints   = glGetUniformLocation(shaderProgram_, "uIsPoints");

    glUniform2f(uResolution, fResW, fResH);
    glUniform1f(uPointSize, 8.0f);

    struct HandInfo {
        const ProjectedHand& hand;
        float r, g, b;
    };
    HandInfo hands[2] = {
        { projection_.leftHand[eyeIndex],  0.0f, 0.5f, 1.0f },
        { projection_.rightHand[eyeIndex], 1.0f, 0.5f, 0.0f },
    };

    for (int h = 0; h < 2; ++h) {
        if (!hands[h].hand.active) continue;

        const float alpha = 0.85f;

        // --- Bone lines as thin quads ---
        float boneVerts[NUM_BONES * 6 * 2];
        int boneCount = 0;
        const float halfThick = 2.0f;
        for (int b = 0; b < NUM_BONES; ++b) {
            int ja = BONE_CONNECTIONS[b][0];
            int jb = BONE_CONNECTIONS[b][1];
            const float* pa = hands[h].hand.joints[ja];
            const float* pb = hands[h].hand.joints[jb];
            if (pa[0] < 0.0f || pb[0] < 0.0f) continue;

            float dx = pb[0] - pa[0];
            float dy = pb[1] - pa[1];
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 0.001f) continue;
            float px = (-dy / len) * halfThick;
            float py = (dx / len) * halfThick;

            int idx = boneCount * 12;
            boneVerts[idx + 0]  = pa[0] + px; boneVerts[idx + 1]  = pa[1] + py;
            boneVerts[idx + 2]  = pa[0] - px; boneVerts[idx + 3]  = pa[1] - py;
            boneVerts[idx + 4]  = pb[0] + px; boneVerts[idx + 5]  = pb[1] + py;
            boneVerts[idx + 6]  = pa[0] - px; boneVerts[idx + 7]  = pa[1] - py;
            boneVerts[idx + 8]  = pb[0] - px; boneVerts[idx + 9]  = pb[1] - py;
            boneVerts[idx + 10] = pb[0] + px; boneVerts[idx + 11] = pb[1] + py;
            boneCount++;
        }

        if (boneCount > 0) {
            glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            boneCount * 6 * 2 * sizeof(float), boneVerts);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(0);

            glUniform4f(uColor, hands[h].r, hands[h].g, hands[h].b, alpha);
            glUniform1i(uIsPoints, 0);
            glDrawArrays(GL_TRIANGLES, 0, boneCount * 6);
        }

        // --- Joint dots as small quads ---
        float outlineVerts[26 * 6 * 2];
        float jointVerts[26 * 6 * 2];
        int jointCount = 0;
        const float os = 7.0f;
        const float js = 5.0f;
        for (int j = 0; j < 26; ++j) {
            const float* pj = hands[h].hand.joints[j];
            if (pj[0] < 0.0f) continue;
            float cx = pj[0], cy = pj[1];
            int oi = jointCount * 12;
            outlineVerts[oi + 0]  = cx - os; outlineVerts[oi + 1]  = cy - os;
            outlineVerts[oi + 2]  = cx + os; outlineVerts[oi + 3]  = cy - os;
            outlineVerts[oi + 4]  = cx + os; outlineVerts[oi + 5]  = cy + os;
            outlineVerts[oi + 6]  = cx - os; outlineVerts[oi + 7]  = cy - os;
            outlineVerts[oi + 8]  = cx + os; outlineVerts[oi + 9]  = cy + os;
            outlineVerts[oi + 10] = cx - os; outlineVerts[oi + 11] = cy + os;
            int ji = jointCount * 12;
            jointVerts[ji + 0]  = cx - js; jointVerts[ji + 1]  = cy - js;
            jointVerts[ji + 2]  = cx + js; jointVerts[ji + 3]  = cy - js;
            jointVerts[ji + 4]  = cx + js; jointVerts[ji + 5]  = cy + js;
            jointVerts[ji + 6]  = cx - js; jointVerts[ji + 7]  = cy - js;
            jointVerts[ji + 8]  = cx + js; jointVerts[ji + 9]  = cy + js;
            jointVerts[ji + 10] = cx - js; jointVerts[ji + 11] = cy + js;
            jointCount++;
        }

        if (jointCount > 0) {
            glBindBuffer(GL_ARRAY_BUFFER, circleVBO_);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(0);
            glUniform1i(uIsPoints, 0);

            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            jointCount * 6 * 2 * sizeof(float), outlineVerts);
            glUniform4f(uColor, 1.0f, 1.0f, 1.0f, alpha * 0.7f);
            glDrawArrays(GL_TRIANGLES, 0, jointCount * 6);

            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            jointCount * 6 * 2 * sizeof(float), jointVerts);
            glUniform4f(uColor, hands[h].r, hands[h].g, hands[h].b, alpha);
            glDrawArrays(GL_TRIANGLES, 0, jointCount * 6);
        }
    }

    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    // Restore GL state
    if (prevDepthTest) glEnable(GL_DEPTH_TEST);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (!prevBlend) glDisable(GL_BLEND);
    glViewport(prevViewport[0], prevViewport[1],
               prevViewport[2], prevViewport[3]);
}
