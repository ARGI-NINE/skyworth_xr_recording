/****************************************************************
 * Copyright (c) 2020-2021 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/

// Define XR_USE_TIMESPEC before including OpenXR headers to enable timespec time conversion
#define XR_USE_TIMESPEC 1

#include <chrono>
#include <fstream>
#include <iomanip>
#include <unistd.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>
#include <mutex>
#include <condition_variable>

#include <android/log.h>
#include <android/looper.h>
#include <android_native_app_glue.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <jni.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <map>

#include "AppCommon.h"
#include "Geometry.h"
#include "KtxLoader.h"
#include "Shader.h"
#include "CameraEncoder.h"
#include "EncoderSurface.h"
#include "HandOverlayRenderer.h"
#include "ImageSaver.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "xr_logger.h"
#include "openxr_qcom.h"
#include "pch.h"
#include "SharedTexture.h"
#include <EGL/eglext.h>

// EGL extension functions (defined in SharedTexture.cpp)
namespace glext {
    extern PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC eglGetNativeClientBufferANDROID;
    extern PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
    extern PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
    extern PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
}
#include "gfxHelper.h"
#include "sxr_camera.h"
#include "sxr_common.h"
#include "RootSpaceQCOM.h"
#include "RawDateSave.h"
#include "input.h"
#include "DatasetRecorder.h"
#include "DatasetExporter.h"
#include "ControllerPoseSaver.h"
#include "NativeLogger.h"
#include <sys/system_properties.h>

#define LOGI(...)                                                              \
    NATIVE_LOGI(LOG_TAG, __VA_ARGS__)
#define LOGW(...)                                                              \
    NATIVE_LOGW(LOG_TAG, __VA_ARGS__)
#define LOGE(...)                                                              \
    NATIVE_LOGE(LOG_TAG, __VA_ARGS__)

#define EGL_SAMPLE_COUNT 4
#define CUBE_COUNT 3 

static int engine_init_xr_swapchains(struct engine *engine);

static bool readUseControllerProperty() {
    char value[PROP_VALUE_MAX] = {0};
    __system_property_get("persist.xr.usecontroller", value);
    return strcmp(value, "1") == 0 || strcmp(value, "true") == 0;
}

static bool readProjectHandProperty() {
    char value[PROP_VALUE_MAX] = {0};
    __system_property_get("persist.xr.project_hand", value);
    return strcmp(value, "1") == 0 || strcmp(value, "true") == 0;
}

glm::vec3 CUBE_COLORS[CUBE_COUNT] = {{0.16f, 0.32f, 0.85f},
                                     {1.0f, 0.8f, 0.5f},
                                     {0.80f, 0.57f, 0.84f}};
const char* storagePath = "/storage/emulated/0/Android/data/com.ssnwt.helloxr/files";

// TTS JNI bridge
static JavaVM* g_javaVm = nullptr;
static jobject g_activity = nullptr;

// Time conversion function pointers (set by CameraAccessExtension::initTimeConversion)
static XrTime (*g_boottimeToXrTimeFn)(uint64_t) = nullptr;
static int64_t (*g_xrtimeToboottimeFn)(XrTime) = nullptr;

void ttsSpeak(const char* text) {
    if (!g_javaVm || !g_activity) {
        LOGW("TTS not available: JVM/activity not set");
        return;
    }

    JNIEnv* env = nullptr;
    bool attached = false;
    int ret = g_javaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (ret != JNI_OK) {
        ret = g_javaVm->AttachCurrentThread(&env, nullptr);
        if (ret != JNI_OK) {
            LOGE("Failed to attach thread for TTS");
            return;
        }
        attached = true;
    }

    jclass cls = env->GetObjectClass(g_activity);
    if (cls) {
        jmethodID mid = env->GetMethodID(cls, "speak", "(Ljava/lang/String;)V");
        if (mid) {
            jstring jtext = env->NewStringUTF(text);
            env->CallVoidMethod(g_activity, mid, jtext);
            env->DeleteLocalRef(jtext);
        }
        env->DeleteLocalRef(cls);
    }

    if (attached) {
        g_javaVm->DetachCurrentThread();
    }
}

struct Swapchain : public AppCommon::Swapchain {
    std::vector<GLuint> fbos;
    std::vector<GLuint> dbos;
};

struct StereoSwapchain {
    std::vector<Swapchain> eyeSwapchain;
};
struct VertexLayoutPos3Uv2 {
    float position[3];
    float texCoord[2];
};
std::map<uint32_t, uint32_t> m_colorToDepthMap;
QtiGL::Geometry mNotificationMesh;
QtiGL::Shader *mNotificationShader;
GLuint quadTexture;
uint32_t quadTextureWidth, quadTextureHeight;

// ========== Text label rendering ==========
#include <cmath>
struct TextLabel {
    GLuint texture{0};
    QtiGL::Geometry geometry;
    uint32_t width{0};
    uint32_t height{0};
    std::string lastText;  // for dynamic update
};

// Simple 8x8 bitmap font (ASCII 32-127)
static const uint8_t FONT8X8[96][8] = {
    // Space (32)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ! (33)
    {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    // " (34)
    {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00},
    // # (35)
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
    // $ (36)
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},
    // % (37)
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00},
    // & (38)
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},
    // ' (39)
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    // ( (40)
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    // ) (41)
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    // * (42)
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    // + (43)
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    // , (44)
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    // - (45)
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    // . (46)
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    // / (47)
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
    // 0-9 (48-57)
    {0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0x7C,0x00},
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00},
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00},
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00},
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00},
    {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00},
    {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00},
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00},
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00},
    // : (58)
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
    // ; (59)
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},
    // < (60)
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00},
    // = (61)
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    // > (62)
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00},
    // ? (63)
    {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00},
    // @ (64)
    {0x7C,0xC6,0xDE,0xDE,0xDC,0xC0,0x78,0x00},
    // A-Z (65-90)
    {0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0x00},
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00},
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00},
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00},
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00},
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00},
    {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00},
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00},
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00},
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00},
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00},
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00},
    {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00},
    {0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06},
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00},
    {0x3C,0x66,0x30,0x18,0x0C,0x66,0x3C,0x00},
    {0x7E,0x5A,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    {0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00},
    {0xC6,0xC6,0xC6,0xD6,0xD6,0xFE,0x6C,0x00},
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00},
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00},
    {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00},
    // [ (91)
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    // \ (92)
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},
    // ] (93)
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    // ^ (94)
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
    // _ (95)
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00},
    // ` (96)
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
    // a-z (97-122)
    {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00},
    {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00},
    {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00},
    {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00},
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00},
    {0x1C,0x36,0x30,0x78,0x30,0x30,0x78,0x00},
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8},
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00},
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    {0x06,0x00,0x0E,0x06,0x06,0x66,0x66,0x3C},
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0x00},
    {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00},
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00},
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0},
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E},
    {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00},
    {0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00},
    {0x30,0x30,0x7C,0x30,0x30,0x36,0x1C,0x00},
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00},
    {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00},
    {0x00,0x00,0xC6,0xD6,0xD6,0xFE,0x6C,0x00},
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00},
    {0x00,0x00,0xC6,0xC6,0xCE,0x76,0x06,0x7C},
    {0x00,0x00,0xFC,0x98,0x30,0x64,0xFC,0x00},
    // { (123)
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    // | (124)
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    // } (125)
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
    // ~ (126)
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00},
    // DEL (127)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

// Generate a texture from text string using the bitmap font
static GLuint generateTextTexture(const std::string& text, uint32_t& outWidth, uint32_t& outHeight,
                                   int scale = 2, uint8_t fgR = 255, uint8_t fgG = 255, uint8_t fgB = 255) {
    int charW = 8 * scale;
    int charH = 8 * scale;
    outWidth = (uint32_t)(text.length() * charW);
    outHeight = (uint32_t)charH;
    if (outWidth == 0) outWidth = 1;

    std::vector<uint8_t> pixels(outWidth * outHeight * 4, 0);
    for (size_t ci = 0; ci < text.length(); ci++) {
        int ch = (unsigned char)text[ci];
        if (ch < 32 || ch > 127) ch = 32;
        const uint8_t* glyph = FONT8X8[ch - 32];
        int baseX = (int)ci * charW;
        for (int gy = 0; gy < 8; gy++) {
            uint8_t row = glyph[gy];
            for (int gx = 0; gx < 8; gx++) {
                if (row & (0x80 >> gx)) {
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            int px = baseX + gx * scale + sx;
                            int py = gy * scale + sy;
                            if (px < (int)outWidth && py < (int)outHeight) {
                                int idx = (py * outWidth + px) * 4;
                                pixels[idx + 0] = fgR;
                                pixels[idx + 1] = fgG;
                                pixels[idx + 2] = fgB;
                                pixels[idx + 3] = 255;
                            }
                        }
                    }
                }
            }
        }
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_SRGB8_ALPHA8, outWidth, outHeight);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, outWidth, outHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

// ========== Camera info panel (single texture, 6 lines, below cameras) ==========
struct engine;  // forward declaration

struct CameraInfoPanel {
    GLuint texture{0};
    uint32_t texWidth{0};
    uint32_t texHeight{0};
    QtiGL::Geometry* geometry{nullptr};
    std::string cachedText;
    float lastFps[6] = {-1.f};
    int frameCounter{0};

    // Generate multi-line text texture for all cameras
    void update(struct engine* engine);

    void render(QtiGL::Shader* shader) {
        if (!texture || !geometry) return;
        glm::mat4 mat = glm::translate(glm::vec3(0.0f, -1.9f, -5.0f));
        shader->SetUniformMat4("modelMatrix", mat);
        shader->SetUniformSampler("srcTex", texture, GL_TEXTURE_2D, 0);
        geometry->Submit();
    }

    void cleanup() {
        if (texture) { glDeleteTextures(1, &texture); texture = 0; }
        if (geometry) { delete geometry; geometry = nullptr; }
    }
};
CameraInfoPanel gInfoPanel;


struct SpaceFrame{
    std::shared_ptr<SharedTexture> shareTexture{nullptr};
};

struct HandTrackerLogic{
    const AppCommon::base_engine* engine;
    XrHandTrackerEXT LeftHandTrackerHandle{XR_NULL_HANDLE};
    XrHandTrackerEXT RightHandTrackerHandle{XR_NULL_HANDLE};

    XrHandJointLocationEXT  LeftHandJointLocations[XR_HAND_JOINT_COUNT_EXT];
    XrHandJointLocationsEXT LeftHandLocations;
    XrHandJointLocationEXT  RightHandJointLocations[XR_HAND_JOINT_COUNT_EXT];
    XrHandJointLocationsEXT RightHandLocations;
    bool LeftHandIsActive = false;
    bool RightHandIsActive = false;

    PFN_xrCreateHandTrackerEXT pfnCreateHandTrackerEXT = nullptr;
    PFN_xrDestroyHandTrackerEXT pfnDestroyHandTrackerEXT = nullptr;
    PFN_xrLocateHandJointsEXT   pfnLocateHandJointsEXT = nullptr;
    PFN_xrCreateHandMeshSpaceMSFT pfnCreateHandMeshSpaceMSFT = nullptr;
    PFN_xrUpdateHandMeshMSFT      pfnUpdateHandMeshMSFT = nullptr;
    RawDateSave* rawDateSave;
    u_int64_t FrameCounter = 0;
    bool isResumed = false;
    void Init(){
        XrResult res;
        res = xrGetInstanceProcAddr(engine->state.xrInstance,"xrCreateHandTrackerEXT",
                                    reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateHandTrackerEXT));
        if(res != XR_SUCCESS)
        {
            LOGE("get xrCreateHandTrackerEXT function failed!");
        }
        res = xrGetInstanceProcAddr(engine->state.xrInstance,"xrDestroyHandTrackerEXT",
                                    reinterpret_cast<PFN_xrVoidFunction*>(&pfnDestroyHandTrackerEXT));
        if(res != XR_SUCCESS)
        {
            LOGE("get xrDestroyHandTrackerEXT function failed!");
        }
        res = xrGetInstanceProcAddr(engine->state.xrInstance,"xrLocateHandJointsEXT",
                                    reinterpret_cast<PFN_xrVoidFunction*>(&pfnLocateHandJointsEXT));
        if(res != XR_SUCCESS)
        {
            LOGE("get xrLocateHandJointsEXT function failed!");
        }
        res = xrGetInstanceProcAddr(engine->state.xrInstance,"xrCreateHandMeshSpaceMSFT",
                                    reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateHandMeshSpaceMSFT));
        if(res != XR_SUCCESS)
        {
            LOGE("get xrCreateHandMeshSpaceMSFT function failed!");
        }
        res = xrGetInstanceProcAddr(engine->state.xrInstance,"xrUpdateHandMeshMSFT",
                                    reinterpret_cast<PFN_xrVoidFunction*>(&pfnUpdateHandMeshMSFT));
        if(res != XR_SUCCESS)
        {
            LOGE("get xrUpdateHandMeshMSFT function failed!");
        }

        XrHandTrackerCreateInfoEXT HTCreateInfo{
                XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,
                nullptr,
                XrHandEXT::XR_HAND_LEFT_EXT,
                XR_HAND_JOINT_SET_DEFAULT_EXT
        };
        res = pfnCreateHandTrackerEXT(engine->state.xrSession, &HTCreateInfo, &LeftHandTrackerHandle);
        if(res != XR_SUCCESS)
        {
            LOGE("create left hand tracker failed!");
        }

        HTCreateInfo.hand = XrHandEXT::XR_HAND_RIGHT_EXT;
        res = pfnCreateHandTrackerEXT(engine->state.xrSession, &HTCreateInfo, &RightHandTrackerHandle);
        if(res != XR_SUCCESS)
        {
            LOGE("create right hand tracker failed!");
        }

        LeftHandLocations = {
                .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
                .next = nullptr,
                .isActive = false,
                .jointCount = XR_HAND_JOINT_COUNT_EXT,
                .jointLocations = LeftHandJointLocations
        };

        RightHandLocations = {
                .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
                .next = nullptr,
                .isActive = false,
                .jointCount = XR_HAND_JOINT_COUNT_EXT,
                .jointLocations = RightHandJointLocations
        };

        rawDateSave = new RawDateSave();
        rawDateSave->Init(storagePath);
        if(engine->state.Resumed) {
            rawDateSave->Resume();
            isResumed = true;
        }
    }

    void Update(XrTime atTime){
        UpdateLeftHand(atTime);
        UpdateRightHand(atTime);
        FrameCounter++;
    }
    private:
    void UpdateLeftHand(XrTime atTime){
        if(LeftHandTrackerHandle == nullptr)
            return;
        XrHandJointsLocateInfoEXT HandJointsLocateInfo{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};

        HandJointsLocateInfo.time = atTime;
        HandJointsLocateInfo.baseSpace = engine->useRootSpace ? engine->state.xrRootSpace : engine->state.xrLocalSpace;
        XrResult result = pfnLocateHandJointsEXT(LeftHandTrackerHandle, &HandJointsLocateInfo,
                                             &LeftHandLocations);

        if (result != XR_SUCCESS)
        {
            LOGW("left LocateHandJointsEXT failed %d time：%ld", result,atTime);
            LeftHandIsActive = false;
        }
        else
        {
            LeftHandIsActive = LeftHandLocations.isActive;
        }

    }
    void UpdateRightHand(XrTime atTime){
        if(RightHandTrackerHandle == nullptr)
            return;
        XrHandJointsLocateInfoEXT HandJointsLocateInfo{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};

        HandJointsLocateInfo.time = atTime;
        HandJointsLocateInfo.baseSpace = engine->useRootSpace ? engine->state.xrRootSpace : engine->state.xrLocalSpace;
        XrResult result = pfnLocateHandJointsEXT(RightHandTrackerHandle, &HandJointsLocateInfo,
                                             &RightHandLocations);
        if (result != XR_SUCCESS)
        {
            LOGW("right LocateHandJointsEXT failed %d time：%ld", result,atTime);
            RightHandIsActive = false;
        }
        else
        {
            RightHandIsActive = RightHandLocations.isActive;
        }
    }
};

// Shared sensor snapshot for RGB-aligned timestamp saving.
// Render loop writes, RGB camera callback reads.
struct AlignedSensorSnapshot {
    std::mutex mutex;

    struct {
        float pos[3];
        float quat[4]; // x,y,z,w  — device/IMU pose in world (Root) space
        bool valid = false;
    } headPose;

    struct {
        bool active = false;
        float joints[26][3];  // positions
        float quats[26][4];   // orientations x,y,z,w
        float radii[26];
    } leftHand, rightHand;

    uint32_t rgbFrameCount = 0;

    // Copy data fields (excluding mutex) from another snapshot
    void copyFrom(const AlignedSensorSnapshot& other) {
        headPose = other.headPose;
        leftHand = other.leftHand;
        rightHand = other.rightHand;
        rgbFrameCount = other.rgbFrameCount;
    }
};

// Lightweight snapshot for overlay projection at encoder time.
// Captured in camera callback at RGB frame time, consumed in render loop encoder section.
struct OverlaySnapshot {
    float headPos[3] = {};
    float headQuat[4] = {};
    bool headValid = false;
    bool leftActive = false;
    bool rightActive = false;
    float leftJoints[26][3] = {};
    float rightJoints[26][3] = {};
    std::mutex mutex;

    void copyFrom(const AlignedSensorSnapshot& snap) {
        headValid = snap.headPose.valid;
        // Device/IMU pose — camera extrinsics are in device frame
        memcpy(headPos, snap.headPose.pos, sizeof(headPos));
        memcpy(headQuat, snap.headPose.quat, sizeof(headQuat));
        leftActive = snap.leftHand.active;
        rightActive = snap.rightHand.active;
        memcpy(leftJoints, snap.leftHand.joints, sizeof(leftJoints));
        memcpy(rightJoints, snap.rightHand.joints, sizeof(rightJoints));
    }
};

// Ring buffer of timestamped {head pose, hand joints} samples.
//
// Render thread pushes one sample per frame at current CLOCK_BOOTTIME.
// The RGB camera callback samples this ring at the frame's
// start_of_exposure timestamp (also CLOCK_BOOTTIME) to recover time-aligned
// data for hand overlay projection and CSV output.
// projection. Head pose is slerp-interpolated; hand joints use nearest neighbor
// (matches the offline Python visualization pipeline).
struct PoseHandSampleRing {
    struct Sample {
        int64_t bootTimeNs = 0;
        float headPos[3] = {0, 0, 0};        // device/IMU position (from xrLocateSpace on viewSpace)
        float headQuat[4] = {0, 0, 0, 1};    // x,y,z,w
        bool poseValid = false;
        bool leftActive = false;
        float leftJoints[26][3] = {};
        float leftRadii[26] = {};
        float leftQuats[26][4] = {};
        bool rightActive = false;
        float rightJoints[26][3] = {};
        float rightRadii[26] = {};
        float rightQuats[26][4] = {};
    };

    static constexpr int CAPACITY = 64;

    mutable std::mutex mutex;
    Sample buffer[CAPACITY];
    int writeIdx = 0;
    int count = 0;

    void push(const Sample& s) {
        std::lock_guard<std::mutex> lock(mutex);
        buffer[writeIdx] = s;
        writeIdx = (writeIdx + 1) % CAPACITY;
        if (count < CAPACITY) count++;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        writeIdx = 0;
        count = 0;
    }

    struct SampleInfo {
        int curCount = 0;
        int64_t oldestNs = 0;
        int64_t newestNs = 0;
        double alpha = 0.0;       // 0..1 within bracket; 0 if clamped to oldest, 1 if to newest
        bool clampedLow = false;
        bool clampedHigh = false;
    };

    // Sample at boottime t. Returns false if the ring is empty.
    // Position: linear; orientation: slerp; hand joints: nearest neighbor.
    bool sample(int64_t t, Sample& out, SampleInfo* info = nullptr) const {
        std::lock_guard<std::mutex> lock(mutex);
        if (info) { info->curCount = count; }
        if (count == 0) return false;

        int oldestIdx = (writeIdx - count + CAPACITY) % CAPACITY;
        int newestIdx = (writeIdx - 1 + CAPACITY) % CAPACITY;
        if (info) {
            info->oldestNs = buffer[oldestIdx].bootTimeNs;
            info->newestNs = buffer[newestIdx].bootTimeNs;
        }

        if (count == 1 || t <= buffer[oldestIdx].bootTimeNs) {
            out = buffer[oldestIdx];
            if (info) { info->clampedLow = true; info->alpha = 0.0; }
            return true;
        }
        if (t >= buffer[newestIdx].bootTimeNs) {
            out = buffer[newestIdx];
            if (info) { info->clampedHigh = true; info->alpha = 1.0; }
            return true;
        }

        // Find bracketing samples [a, b] s.t. a.t <= t < b.t.
        int aIdx = oldestIdx;
        int bIdx = newestIdx;
        for (int i = 1; i < count; i++) {
            int idx = (oldestIdx + i) % CAPACITY;
            if (buffer[idx].bootTimeNs > t) {
                bIdx = idx;
                aIdx = (oldestIdx + i - 1) % CAPACITY;
                break;
            }
        }
        const Sample& a = buffer[aIdx];
        const Sample& b = buffer[bIdx];

        double denom = (double)(b.bootTimeNs - a.bootTimeNs);
        double alpha = denom > 0.0 ? (double)(t - a.bootTimeNs) / denom : 0.0;
        if (alpha < 0.0) alpha = 0.0;
        if (alpha > 1.0) alpha = 1.0;
        if (info) { info->alpha = alpha; }

        out.bootTimeNs = t;
        out.poseValid = a.poseValid && b.poseValid;

        // Position: linear interpolation.
        for (int i = 0; i < 3; i++) {
            out.headPos[i] = (float)(a.headPos[i] + alpha * (b.headPos[i] - a.headPos[i]));
        }

        // Orientation: slerp (x, y, z, w convention).
        float q0[4] = { a.headQuat[0], a.headQuat[1], a.headQuat[2], a.headQuat[3] };
        float q1[4] = { b.headQuat[0], b.headQuat[1], b.headQuat[2], b.headQuat[3] };
        float dot = q0[0]*q1[0] + q0[1]*q1[1] + q0[2]*q1[2] + q0[3]*q1[3];
        if (dot < 0.0f) {
            for (int i = 0; i < 4; i++) q1[i] = -q1[i];
            dot = -dot;
        }
        float qr[4];
        if (dot > 0.9995f) {
            for (int i = 0; i < 4; i++) qr[i] = q0[i] + (float)alpha * (q1[i] - q0[i]);
        } else {
            float theta0 = acosf(dot);
            float sinTheta0 = sinf(theta0);
            float theta = theta0 * (float)alpha;
            float s0 = cosf(theta) - dot * sinf(theta) / sinTheta0;
            float s1 = sinf(theta) / sinTheta0;
            for (int i = 0; i < 4; i++) qr[i] = s0 * q0[i] + s1 * q1[i];
        }
        float n = sqrtf(qr[0]*qr[0] + qr[1]*qr[1] + qr[2]*qr[2] + qr[3]*qr[3]);
        if (n > 0.0f) {
            for (int i = 0; i < 4; i++) qr[i] /= n;
        }
        memcpy(out.headQuat, qr, sizeof(qr));

        // Hand joints: LERP for positions/radii, SLERP for orientations.
        // Interpolate when both bracket samples have the hand active;
        // fall back to the nearest sample when only one has it.
        float fa = (float)alpha;

        auto lerpHand = [fa](bool& outAct,
                              float outJ[26][3], float outR[26], float outQ[26][4],
                              bool aAct, const float aJ[26][3], const float aR[26], const float aQ[26][4],
                              bool bAct, const float bJ[26][3], const float bR[26], const float bQ[26][4]) {
            if (aAct && bAct) {
                outAct = true;
                for (int j = 0; j < 26; ++j) {
                    for (int k = 0; k < 3; ++k)
                        outJ[j][k] = aJ[j][k] + fa * (bJ[j][k] - aJ[j][k]);
                    outR[j] = aR[j] + fa * (bR[j] - aR[j]);
                    // SLERP (same logic as head pose above)
                    float q0[4] = { aQ[j][0], aQ[j][1], aQ[j][2], aQ[j][3] };
                    float q1[4] = { bQ[j][0], bQ[j][1], bQ[j][2], bQ[j][3] };
                    float d = q0[0]*q1[0] + q0[1]*q1[1] + q0[2]*q1[2] + q0[3]*q1[3];
                    if (d < 0.0f) { for (int kk = 0; kk < 4; ++kk) q1[kk] = -q1[kk]; d = -d; }
                    if (d > 0.9995f) {
                        for (int kk = 0; kk < 4; ++kk) outQ[j][kk] = q0[kk] + fa * (q1[kk] - q0[kk]);
                    } else {
                        float t0 = acosf(d), st0 = sinf(t0), t = t0 * fa;
                        float s0 = cosf(t) - d * sinf(t) / st0;
                        float s1 = sinf(t) / st0;
                        for (int kk = 0; kk < 4; ++kk) outQ[j][kk] = s0 * q0[kk] + s1 * q1[kk];
                    }
                    float n = sqrtf(outQ[j][0]*outQ[j][0] + outQ[j][1]*outQ[j][1] +
                                   outQ[j][2]*outQ[j][2] + outQ[j][3]*outQ[j][3]);
                    if (n > 0.0f) { for (int kk = 0; kk < 4; ++kk) outQ[j][kk] /= n; }
                }
            } else if (aAct) {
                outAct = true;
                memcpy(outJ, aJ, sizeof(float) * 26 * 3);
                memcpy(outR, aR, sizeof(float) * 26);
                memcpy(outQ, aQ, sizeof(float) * 26 * 4);
            } else if (bAct) {
                outAct = true;
                memcpy(outJ, bJ, sizeof(float) * 26 * 3);
                memcpy(outR, bR, sizeof(float) * 26);
                memcpy(outQ, bQ, sizeof(float) * 26 * 4);
            }
        };

        lerpHand(out.leftActive, out.leftJoints, out.leftRadii, out.leftQuats,
                 a.leftActive, a.leftJoints, a.leftRadii, a.leftQuats,
                 b.leftActive, b.leftJoints, b.leftRadii, b.leftQuats);
        lerpHand(out.rightActive, out.rightJoints, out.rightRadii, out.rightQuats,
                 a.rightActive, a.rightJoints, a.rightRadii, a.rightQuats,
                 b.rightActive, b.rightJoints, b.rightRadii, b.rightQuats);
        return true;
    }
};

struct engine;  // forward declaration for g_engine
static struct engine* g_engine = nullptr;  // global engine pointer (set in android_main)

// Forward declarations: defined after engine struct (which has full type info).
static void saveAlignedSensorData(int64_t rgbTimestampNs);
static void feedOverlayCameraParams(const SXR::FrameData* data);

struct CameraAccessExtension{
    const AppCommon::base_engine* engine;
    JavaVM* vm;
    jobject activityObject;
    PFN_xrConvertTimespecTimeToTimeKHR xrConvertTimespecTimeToTimeKHR;
    PFN_xrConvertTimeToTimespecTimeKHR xrConvertTimeToTimespecTimeKHR;

    // Static instance pointer for C-style callback
    static CameraAccessExtension* sInstance;

    // Static wrapper for use as function pointer
    static XrTime staticBoottimeToXrTime(uint64_t boottime_ns) {
        if (sInstance) return sInstance->boottimeToXrTime(boottime_ns);
        return static_cast<XrTime>(boottime_ns);
    }
    static int64_t staticXrtimeToboottime(XrTime xrtime) {
        if (sInstance) return sInstance->xrtimeToboottime(xrtime);
        return static_cast<int64_t>(xrtime);
    }

    // ========== New callback-based camera API (dynamic loading) ==========
    SxrCameraApi api{};  // API function table for dynamic loading
    SxrCameraContext* cameraContext{nullptr};
    bool camerasInitialized{false};

    // RGB camera frame ready flag (for display thread to know when to render)
    std::atomic<bool> rgbFrameReady{false};

    // Camera/Encoder paused state
    std::atomic<bool> isPaused{false};
    bool cameraGroupsOpen{false};  // track whether camera groups are open

    // Callback drain synchronization
    std::atomic<int> inFlightCallbacks{0};
    std::mutex callbackDrainMutex;
    std::condition_variable callbackDrainCV;

    // ========== 独立EGL上下文（每个相机组一个，按上下文串行访问）==========
    struct CameraGLContext {
        EGLDisplay display = EGL_NO_DISPLAY;
        EGLContext context = EGL_NO_CONTEXT;
        EGLSurface surface = EGL_NO_SURFACE;  // Pbuffer for offscreen rendering
        std::atomic<bool> initialized{false};
        std::mutex useMutex;

        bool init(const AppCommon::base_engine* engine) {
            if (initialized) return true;
            EGLDisplay mainDisplay = engine->display;
            EGLContext mainContext = engine->context;
            EGLConfig config = engine->config;
            if (mainDisplay == EGL_NO_DISPLAY || mainContext == EGL_NO_CONTEXT) return false;

            EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
            context = eglCreateContext(mainDisplay, config, mainContext, contextAttribs);
            if (context == EGL_NO_CONTEXT) {
                LOGE("Failed to create camera EGL context: 0x%x", eglGetError());
                return false;
            }

            EGLint pbufferAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
            surface = eglCreatePbufferSurface(mainDisplay, config, pbufferAttribs);
            if (surface == EGL_NO_SURFACE) {
                LOGE("Failed to create Pbuffer surface: 0x%x", eglGetError());
                eglDestroyContext(mainDisplay, context);
                context = EGL_NO_CONTEXT;
                return false;
            }

            display = mainDisplay;
            initialized = true;
            return true;
        }

        void cleanup() {
            if (!initialized) return;
            if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT) {
                eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                if (surface != EGL_NO_SURFACE) {
                    eglDestroySurface(display, surface);
                    surface = EGL_NO_SURFACE;
                }
                if (context != EGL_NO_CONTEXT) {
                    eglDestroyContext(display, context);
                    context = EGL_NO_CONTEXT;
                }
            }
            display = EGL_NO_DISPLAY;
            initialized = false;
        }

        bool makeCurrentUnlocked() {
            if (!initialized) return false;
            return eglMakeCurrent(display, surface, surface, context);
        }

        void releaseCurrentUnlocked() {
            if (display != EGL_NO_DISPLAY) {
                eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            }
        }

        bool makeCurrent() {
            std::lock_guard<std::mutex> lock(useMutex);
            return makeCurrentUnlocked();
        }

        void releaseCurrent() {
            std::lock_guard<std::mutex> lock(useMutex);
            releaseCurrentUnlocked();
        }
    };

    struct ScopedCameraGLContextCurrent {
        CameraGLContext& ctx;
        std::unique_lock<std::mutex> lock;
        bool current{false};

        explicit ScopedCameraGLContextCurrent(CameraGLContext& ctx)
            : ctx(ctx), lock(ctx.useMutex) {
            current = ctx.makeCurrentUnlocked();
        }

        ~ScopedCameraGLContextCurrent() {
            if (current) {
                ctx.releaseCurrentUnlocked();
            }
        }

        bool isCurrent() const { return current; }
    };
    CameraGLContext rgbCtx;
    CameraGLContext trackingCtx;
    CameraGLContext ctrlCtx;

    // 显示纹理（主线程使用，相机线程通过FBO渲染更新）
    GLuint rgbDisplayTextures[2] = {0, 0};  // [0]=left, [1]=right
    GLuint rgbDisplayFBOs[2] = {0, 0};      // FBOs for YUV->RGBA conversion

    // 编码器停止标志（防止停止后立即重新初始化）
    std::atomic<bool> encodersStopped{true};//default do not encode
    std::atomic<bool> stopInProgress{false};//true while async stopEncoder() is running
    std::atomic<uint64_t> pendingEncodeTimestamp{0};
    OverlaySnapshot overlaySnap;  // snapshot at RGB frame time for overlay projection
    std::atomic<bool> encodingEnabled{false};//用户按键切换编码状态
    std::atomic<bool> snapshotRequested{false};//快照请求标志（intent或按键触发）
    std::atomic<bool> ; // 发送控制命令到

    // Dataset recording: encoder output directory (set when recording starts)
    std::string encoderBaseDir;

    // Camera params saved flags (reset on each new recording session)
    std::atomic<bool> cameraParamsSavedRgb{false};
    std::atomic<bool> cameraParamsSavedTracking{false};
    std::atomic<bool> cameraParamsSavedCtrl{false};

    // Snapshot pixel cache: camera threads write (in their GL context), main thread reads
    struct SnapshotBuffer {
        std::mutex mutex;
        std::vector<uint8_t> pixels; // RGBA
        uint32_t width = 0, height = 0;
        std::atomic<bool> ready{false};
    };
    SnapshotBuffer snapshotRgbLeft, snapshotRgbRight;
    SnapshotBuffer snapshotCv[4]; // [0]=TL, [1]=TR, [2]=CL, [3]=CR

    // Tracking camera (callback-based, CPU access)
    struct TrackingFrameData {
        SXR::FrameInfo frameInfo;
        std::vector<uint8_t> pixelData;
        std::vector<uint8_t> rgbaData;  // pre-expanded RGBA for display
    };
    std::mutex trackingFrameMutex;
    TrackingFrameData trackingFrames[2];  // [0]=left, [1]=right (from TRACKING group)
    TrackingFrameData ctrlFrames[2];      // [0]=left, [1]=right (from CTRL group)
    std::atomic<bool> trackingFrameReady{false};
    std::atomic<bool> ctrlFrameReady{false};

    // CV camera display textures (for projection layer rendering)
    // Order: [0]=CV-TL(tracking-left), [1]=CV-TR(tracking-right),
    //        [2]=CV-BL(ctrl-left), [3]=CV-BR(ctrl-right)
    GLuint cvDisplayTextures[4] = {0, 0, 0, 0};
    GLuint cvDisplayFBOs[4] = {0, 0, 0, 0};  // FBOs for GPU-side hwBuffer->texture copy
    GLuint cvDisplayVBOs[2] = {0, 0};         // per-group VBOs: [0]=tracking, [1]=ctrl (avoids cross-thread glBufferSubData)
    uint32_t cvFrameWidths[4] = {0, 0, 0, 0};
    uint32_t cvFrameHeights[4] = {0, 0, 0, 0};
    uint32_t cvLastUploadedFrameId[4] = {0, 0, 0, 0};  // track which frame was last uploaded

    // FPS tracking for all cameras
    struct FpsTracker {
        uint64_t lastFrameTime{0};
        uint32_t frameCount{0};
        float currentFps{0.0f};
        void update(uint64_t timestamp) {
            frameCount++;
            if (lastFrameTime == 0) { lastFrameTime = timestamp; return; }
            uint64_t elapsed = timestamp - lastFrameTime;
            if (elapsed >= 1000000000ULL) { // 1 second in ns
                currentFps = (float)frameCount * 1000000000ULL / elapsed;
                frameCount = 0;
                lastFrameTime = timestamp;
            }
        }
    };
    FpsTracker cvFpsTrackers[4];  // CV camera FPS trackers
    FpsTracker rgbFpsTrackers[2]; // RGB camera FPS trackers
    float getRgbFps(int idx) const { return rgbFpsTrackers[idx].currentFps; }
    float getCvFps(int idx) const { return cvFpsTrackers[idx].currentFps; }

    // RGB frame dimensions (populated in callback)
    uint32_t rgbFrameWidths[2] = {0, 0};
    uint32_t rgbFrameHeights[2] = {0, 0};

    // Camera encoders
    SXR::CameraEncoder* grayCameraEncoders[SXR::CAME_MAX];  // Legacy: individual grayscale encoders
    SXR::CameraEncoder* trackingEncoder{nullptr};  // Combined tracking camera encoder (left+right)
    SXR::CameraEncoder* ctrlEncoder{nullptr};      // Combined ctrl camera encoder (left+right)
    SXR::CameraEncoder* rgbEncoder{nullptr};       // RGB camera encoder (side-by-side)

    // Encoder surface for zero-copy rendering (Surface mode, RGB only)
    SXR::EncoderSurface* rgbEncoderSurface{nullptr};

    // FBO and texture for side-by-side stitching
    GLuint rgbSbsFBO{0};
    GLuint rgbSbsTexture{0};

    // Grayscale encoder surfaces for zero-copy rendering (Surface mode)
    SXR::EncoderSurface* trackingEncoderSurface{nullptr};
    SXR::EncoderSurface* ctrlEncoderSurface{nullptr};

    // Y8 to YUV shader for grayscale encoding
    GLuint grayscaleEncoderShaderProgram{0};
    GLuint grayscaleEncoderVBO{0};
    GLuint grayscaleEncoderVAO{0};

    // Cached uniform/attrib locations for grayscale shader (avoid per-frame lookups)
    GLint grayShader_uGrayscaleTexture{0};
    GLint grayShader_aPosition{0};
    GLint grayShader_aTexCoord{0};

    // Persistent EGLImage for CV hwBuffer (per-group to avoid race between tracking/ctrl threads)
    EGLImageKHR cvPersistentEGLImage[2]{EGL_NO_IMAGE_KHR, EGL_NO_IMAGE_KHR};  // [0]=tracking, [1]=ctrl
    GLuint cvPersistentExtTex[2]{0, 0};

    // Separate Y8 textures for each grayscale encoder (NOT shared, each thread uses its own)
    GLuint trackingY8Texture{0};
    GLuint ctrlY8Texture{0};

    // Simple shader for rendering camera texture to encoder surface
    GLuint encoderShaderProgram{0};
    GLuint encoderVBO{0};
    GLuint encoderVAO{0};

    // Simple blit shader for FBO→encoder surface (uses sampler2D, not samplerExternalOES)
    GLuint sbsCopyShaderProgram{0};

    // Initialize encoder shader (YUV to RGB color space conversion)
    void initEncoderShader() {
        const char* vertexShaderSource = R"(
            #version 300 es
            in vec2 aPosition;
            in vec2 aTexCoord;
            out vec2 vTexCoord;
            void main() {
                gl_Position = vec4(aPosition, 0.0, 1.0);
                vTexCoord = aTexCoord;
            }
        )";

        const char* fragmentShaderSource = R"(
            #version 300 es
            #extension GL_OES_EGL_image_external_essl3 : require
            precision highp float;
            in vec2 vTexCoord;
            uniform samplerExternalOES uTexture;
            out vec4 fragColor;

            void main() {
                // Flip Y coordinate for correct orientation
                vec2 texCoord = vec2(vTexCoord.x, 1.0 - vTexCoord.y);
                vec4 color = texture(uTexture, texCoord);
                fragColor = color;
            }
        )";

        // Compile vertex shader
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
        glCompileShader(vertexShader);

        // Compile fragment shader
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
        glCompileShader(fragmentShader);

        // Link program
        encoderShaderProgram = glCreateProgram();
        glAttachShader(encoderShaderProgram, vertexShader);
        glAttachShader(encoderShaderProgram, fragmentShader);
        glLinkProgram(encoderShaderProgram);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        // Create fullscreen quad VBO
        float vertices[] = {
            // position     texcoord
            -1.0f, -1.0f,   0.0f, 0.0f,
             1.0f, -1.0f,   1.0f, 0.0f,
            -1.0f,  1.0f,   0.0f, 1.0f,
             1.0f,  1.0f,   1.0f, 1.0f,
        };

        glGenBuffers(1, &encoderVBO);
        glBindBuffer(GL_ARRAY_BUFFER, encoderVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

        glGenVertexArrays(1, &encoderVAO);
        glBindVertexArray(encoderVAO);
        glBindBuffer(GL_ARRAY_BUFFER, encoderVBO);
        GLint posLoc = glGetAttribLocation(encoderShaderProgram, "aPosition");
        GLint texLoc = glGetAttribLocation(encoderShaderProgram, "aTexCoord");
        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(texLoc);
        glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);

        LOGI("Encoder shader initialized");
    }

    // Initialize SBS copy shader (simple sampler2D blit for FBO→encoder surface)
    void initSbsCopyShader() {
        if (sbsCopyShaderProgram != 0) return;

        const char* vs = R"(
            #version 300 es
            in vec2 aPosition;
            in vec2 aTexCoord;
            out vec2 vTexCoord;
            void main() {
                gl_Position = vec4(aPosition, 0.0, 1.0);
                vTexCoord = aTexCoord;
            }
        )";

        const char* fs = R"(
            #version 300 es
            precision highp float;
            in vec2 vTexCoord;
            uniform sampler2D uTexture;
            out vec4 fragColor;
            void main() {
                fragColor = texture(uTexture, vTexCoord);
            }
        )";

        GLuint v = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(v, 1, &vs, nullptr);
        glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(f, 1, &fs, nullptr);
        glCompileShader(f);

        sbsCopyShaderProgram = glCreateProgram();
        glAttachShader(sbsCopyShaderProgram, v);
        glAttachShader(sbsCopyShaderProgram, f);
        glLinkProgram(sbsCopyShaderProgram);
        glDeleteShader(v);
        glDeleteShader(f);

        LOGI("SBS copy shader initialized");
    }

    // Initialize grayscale encoder shader (Y8 to YUV conversion)
    void initGrayscaleEncoderShader() {
        const char* vertexShaderSource = R"(
            #version 300 es
            in vec4 aPosition;
            in vec2 aTexCoord;
            out vec2 vTexCoord;
            void main() {
                gl_Position = aPosition;
                vTexCoord = aTexCoord;
            }
        )";

        // Use samplerExternalOES for camera buffer, but extract only Y (luminance) for grayscale
        const char* fragmentShaderSource = R"(
            #version 300 es
            #extension GL_OES_EGL_image_external_essl3 : require
            precision highp float;
            in vec2 vTexCoord;
            uniform samplerExternalOES uGrayscaleTexture;
            out vec4 fragColor;

            void main() {
                // Sample from external texture
                vec4 color = texture(uGrayscaleTexture, vTexCoord);
                // For grayscale camera, the image is monochrome
                // Use luminance (Y) from YUV conversion result
                // Y = 0.299*R + 0.587*G + 0.114*B (BT.601)
                float y = 0.299 * color.r + 0.587 * color.g + 0.114 * color.b;
                // Output grayscale
                fragColor = vec4(vec3(y), 1.0);
            }
        )";

        // Compile vertex shader
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
        glCompileShader(vertexShader);

        // Check vertex shader compilation
        GLint success;
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
            LOGE("Grayscale encoder vertex shader compilation failed: %s", infoLog);
            glDeleteShader(vertexShader);
            return;
        }

        // Compile fragment shader
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
        glCompileShader(fragmentShader);

        // Check fragment shader compilation
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
            LOGE("Grayscale encoder fragment shader compilation failed: %s", infoLog);
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            return;
        }

        // Link program
        grayscaleEncoderShaderProgram = glCreateProgram();
        glAttachShader(grayscaleEncoderShaderProgram, vertexShader);
        glAttachShader(grayscaleEncoderShaderProgram, fragmentShader);
        glLinkProgram(grayscaleEncoderShaderProgram);

        // Check link status
        glGetProgramiv(grayscaleEncoderShaderProgram, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(grayscaleEncoderShaderProgram, 512, nullptr, infoLog);
            LOGE("Grayscale encoder shader program link failed: %s", infoLog);
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            glDeleteProgram(grayscaleEncoderShaderProgram);
            grayscaleEncoderShaderProgram = 0;
            return;
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        // Create fullscreen quad VBO
        float vertices[] = {
            // position     texcoord
            -1.0f, -1.0f,   0.0f, 1.0f,  // Flip Y for correct orientation
             1.0f, -1.0f,   1.0f, 1.0f,
            -1.0f,  1.0f,   0.0f, 0.0f,
             1.0f,  1.0f,   1.0f, 0.0f,
        };

        glGenBuffers(1, &grayscaleEncoderVBO);
        glBindBuffer(GL_ARRAY_BUFFER, grayscaleEncoderVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        glGenVertexArrays(1, &grayscaleEncoderVAO);
        glBindVertexArray(grayscaleEncoderVAO);

        GLint posLoc = glGetAttribLocation(grayscaleEncoderShaderProgram, "aPosition");
        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

        GLint texLoc = glGetAttribLocation(grayscaleEncoderShaderProgram, "aTexCoord");
        glEnableVertexAttribArray(texLoc);
        glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // Cache uniform/attrib locations to avoid per-frame lookups
        grayShader_uGrayscaleTexture = glGetUniformLocation(grayscaleEncoderShaderProgram, "uGrayscaleTexture");
        grayShader_aPosition = glGetAttribLocation(grayscaleEncoderShaderProgram, "aPosition");
        grayShader_aTexCoord = glGetAttribLocation(grayscaleEncoderShaderProgram, "aTexCoord");

        LOGI("Grayscale encoder shader initialized");
    }

    // Render texture to encoder surface (called from GL thread)
    void renderTextureToEncoderSurface(SXR::EncoderSurface* encoderSurface, GLuint textureId, int width, int height) {
        if (!encoderSurface || !encoderSurface->isInitialized()) {
            return;
        }

        // Save current EGL context
        EGLDisplay prevDisplay = eglGetCurrentDisplay();
        EGLSurface prevDrawSurface = eglGetCurrentSurface(EGL_DRAW);
        EGLSurface prevReadSurface = eglGetCurrentSurface(EGL_READ);
        EGLContext prevContext = eglGetCurrentContext();

        // Make encoder surface current
        if (!encoderSurface->makeCurrent()) {
            LOGE("Failed to make encoder surface current");
            return;
        }

        // Clear and render
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Use shader (shaders are shared between contexts)
        glUseProgram(encoderShaderProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);
        glUniform1i(glGetUniformLocation(encoderShaderProgram, "uTexture"), 0);

        // Draw fullscreen quad without VAO (VAOs are not shared between contexts)
        // Use direct vertex attribute setup
        float vertices[] = {
            // position     texcoord
            -1.0f, -1.0f,   0.0f, 0.0f,
             1.0f, -1.0f,   1.0f, 0.0f,
            -1.0f,  1.0f,   0.0f, 1.0f,
             1.0f,  1.0f,   1.0f, 1.0f,
        };

        GLint posLoc = glGetAttribLocation(encoderShaderProgram, "aPosition");
        GLint texLoc = glGetAttribLocation(encoderShaderProgram, "aTexCoord");

        glBindBuffer(GL_ARRAY_BUFFER, encoderVBO);
        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(texLoc);
        glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(posLoc);
        glDisableVertexAttribArray(texLoc);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // Swap buffers to submit frame to encoder
        encoderSurface->swapBuffers();

        // Restore previous EGL context
        if (prevDisplay != EGL_NO_DISPLAY && prevContext != EGL_NO_CONTEXT) {
            eglMakeCurrent(prevDisplay, prevDrawSurface, prevReadSurface, prevContext);
        }
    }

    void initTimeConversion() {
        sInstance = this;
        XrResult res;
        res = xrGetInstanceProcAddr(engine->state.xrInstance,"xrConvertTimespecTimeToTimeKHR",
                                    reinterpret_cast<PFN_xrVoidFunction*>(&xrConvertTimespecTimeToTimeKHR));
        if(res != XR_SUCCESS)
        {
            LOGE("get xrConvertTimespecTimeToTimeKHR function failed!");
        }

        res = xrGetInstanceProcAddr(engine->state.xrInstance, "xrConvertTimeToTimespecTimeKHR",
                                    reinterpret_cast<PFN_xrVoidFunction*>(&xrConvertTimeToTimespecTimeKHR));
        if (res != XR_SUCCESS)
        {
            LOGE("get xrConvertTimeToTimespecTimeKHR function failed!");
        }

        g_boottimeToXrTimeFn = &CameraAccessExtension::staticBoottimeToXrTime;
        g_xrtimeToboottimeFn = &CameraAccessExtension::staticXrtimeToboottime;
    }

    // Cleanup all camera GL contexts and related resources
    void cleanupAllGLContexts() {
        // Save current context to restore later
        EGLDisplay prevDisplay = eglGetCurrentDisplay();
        EGLSurface prevDraw = eglGetCurrentSurface(EGL_DRAW);
        EGLSurface prevRead = eglGetCurrentSurface(EGL_READ);
        EGLContext prevContext = eglGetCurrentContext();

        // Cleanup RGB context resources
        if (rgbCtx.initialized) {
            {
                ScopedCameraGLContextCurrent rgbCurrent(rgbCtx);
                if (rgbCurrent.isCurrent()) {
                    for (int i = 0; i < 2; i++) {
                        if (rgbDisplayTextures[i] != 0) { glDeleteTextures(1, &rgbDisplayTextures[i]); rgbDisplayTextures[i] = 0; }
                        if (rgbDisplayFBOs[i] != 0) { glDeleteFramebuffers(1, &rgbDisplayFBOs[i]); rgbDisplayFBOs[i] = 0; }
                    }
                    if (rgbEncoderSurface) { rgbEncoderSurface->release(); delete rgbEncoderSurface; rgbEncoderSurface = nullptr; }
                    if (rgbEncoder) { rgbEncoder->stop(); delete rgbEncoder; rgbEncoder = nullptr; }
                    if (rgbSbsFBO) { glDeleteFramebuffers(1, &rgbSbsFBO); rgbSbsFBO = 0; }
                    if (rgbSbsTexture) { glDeleteTextures(1, &rgbSbsTexture); rgbSbsTexture = 0; }
                    if (encoderShaderProgram != 0) { glDeleteProgram(encoderShaderProgram); encoderShaderProgram = 0; }
                    if (sbsCopyShaderProgram != 0) { glDeleteProgram(sbsCopyShaderProgram); sbsCopyShaderProgram = 0; }
                    if (encoderVBO != 0) { glDeleteBuffers(1, &encoderVBO); encoderVBO = 0; }
                    if (encoderVAO != 0) { glDeleteVertexArrays(1, &encoderVAO); encoderVAO = 0; }
                } else {
                    LOGW("cleanupAllGLContexts: failed to make RGB context current: 0x%x", eglGetError());
                }
            }
            rgbCtx.cleanup();
        }

        // Cleanup Tracking/CTRL context resources
        if (trackingCtx.initialized || ctrlCtx.initialized) {
            // Use tracking context for CV cleanup (resources are shared)
            ScopedCameraGLContextCurrent trackingCurrent(trackingCtx);
            if (trackingCurrent.isCurrent()) {
                if (cvPersistentEGLImage[0] != EGL_NO_IMAGE_KHR) {
                    glext::eglDestroyImageKHR(trackingCtx.display, cvPersistentEGLImage[0]);
                    cvPersistentEGLImage[0] = EGL_NO_IMAGE_KHR;
                }
                if (cvPersistentEGLImage[1] != EGL_NO_IMAGE_KHR) {
                    glext::eglDestroyImageKHR(trackingCtx.display, cvPersistentEGLImage[1]);
                    cvPersistentEGLImage[1] = EGL_NO_IMAGE_KHR;
                }
                if (cvPersistentExtTex[0] != 0) {
                    glDeleteTextures(1, &cvPersistentExtTex[0]);
                    cvPersistentExtTex[0] = 0;
                }
                if (cvPersistentExtTex[1] != 0) {
                    glDeleteTextures(1, &cvPersistentExtTex[1]);
                    cvPersistentExtTex[1] = 0;
                }
                for (int i = 0; i < 4; i++) {
                    if (cvDisplayTextures[i] != 0) { glDeleteTextures(1, &cvDisplayTextures[i]); cvDisplayTextures[i] = 0; }
                    if (cvDisplayFBOs[i] != 0) { glDeleteFramebuffers(1, &cvDisplayFBOs[i]); cvDisplayFBOs[i] = 0; }
                }
                if (cvDisplayVBOs[0] != 0) { glDeleteBuffers(1, &cvDisplayVBOs[0]); cvDisplayVBOs[0] = 0; }
                if (cvDisplayVBOs[1] != 0) { glDeleteBuffers(1, &cvDisplayVBOs[1]); cvDisplayVBOs[1] = 0; }
                if (grayscaleEncoderShaderProgram != 0) { glDeleteProgram(grayscaleEncoderShaderProgram); grayscaleEncoderShaderProgram = 0; }
                if (grayscaleEncoderVBO != 0) { glDeleteBuffers(1, &grayscaleEncoderVBO); grayscaleEncoderVBO = 0; }
                if (grayscaleEncoderVAO != 0) { glDeleteVertexArrays(1, &grayscaleEncoderVAO); grayscaleEncoderVAO = 0; }
            } else {
                LOGW("cleanupAllGLContexts: failed to make tracking context current: 0x%x", eglGetError());
            }
        }
        trackingCtx.cleanup();
        ctrlCtx.cleanup();

        // Cleanup encoders
        if (trackingEncoder) { trackingEncoder->stop(); delete trackingEncoder; trackingEncoder = nullptr; }
        if (ctrlEncoder) { ctrlEncoder->stop(); delete ctrlEncoder; ctrlEncoder = nullptr; }
        if (trackingEncoderSurface) { trackingEncoderSurface->release(); delete trackingEncoderSurface; trackingEncoderSurface = nullptr; }
        if (ctrlEncoderSurface) { ctrlEncoderSurface->release(); delete ctrlEncoderSurface; ctrlEncoderSurface = nullptr; }

        // Restore previous context
        if (prevDisplay != EGL_NO_DISPLAY && prevContext != EGL_NO_CONTEXT) {
            eglMakeCurrent(prevDisplay, prevDraw, prevRead, prevContext);
        }
        LOGI("All camera GL contexts cleaned up");
    }

    // Save camera intrinsics/extrinsics to JSON (called once per group per recording session)
    void saveCameraParams(const SXR::FrameData* data, const char* groupName,
                          std::atomic<bool>& savedFlag) {
        if (savedFlag.load() || encoderBaseDir.empty()) return;

        const char* eyeNames[2] = {"left", "right"};
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6);
        oss << "{\n  \"group\": \"" << groupName << "\",\n  \"cameras\": [\n";

        for (int i = 0; i < 2; i++) {
            const auto& f = data->frames[i];
            if (i > 0) oss << ",\n";
            oss << "    {\n"
                << "      \"eye\": \"" << eyeNames[i] << "\",\n"
                << "      \"width\": " << f.width << ",\n"
                << "      \"height\": " << f.height << ",\n"
                << "      \"intrinsics\": {\n"
                << "        \"focalX\": " << f.focalX << ",\n"
                << "        \"focalY\": " << f.focalY << ",\n"
                << "        \"centerX\": " << f.centerX << ",\n"
                << "        \"centerY\": " << f.centerY << ",\n"
                << "        \"radialDistortion\": [";
            for (int d = 0; d < 8; d++) {
                if (d > 0) oss << ", ";
                oss << f.radialDistortion[d];
            }
            oss << "]\n      },\n"
                << "      \"extrinsics\": {\n"
                << "        \"position\": [" << f.position[0] << ", " << f.position[1] << ", " << f.position[2] << "],\n"
                << "        \"rotation\": [" << f.rotation[0] << ", " << f.rotation[1] << ", " << f.rotation[2] << ", " << f.rotation[3] << "]\n"
                << "      }\n"
                << "    }";
        }

        oss << "\n  ]\n}\n";

        std::string filePath = encoderBaseDir + "/camera_params_" + groupName + ".json";
        std::ofstream ofs(filePath);
        if (ofs.is_open()) {
            ofs << oss.str();
            ofs.close();
            savedFlag = true;
            LOGI("Camera params saved: %s", filePath.c_str());
        } else {
            LOGE("Failed to save camera params: %s", filePath.c_str());
        }
    }

    // Initialize encoders and surfaces (called in RGB callback with rgbCtx current)
    void initEncodersAndSurfaces(int width, int height) {
        if (rgbEncoder) {
            return;  // Already initialized
        }

        int sbsWidth = width * 2;
        LOGI("Initializing RGB SBS encoder with Surface mode: %dx%d", sbsWidth, height);

        // Initialize encoder shader if not done
        if (encoderShaderProgram == 0) {
            initEncoderShader();
        }
        if (sbsCopyShaderProgram == 0) {
            initSbsCopyShader();
        }

        // Create single SBS encoder (2W x H, 8Mbps, rgb.mp4)
        rgbEncoder = new SXR::CameraEncoder(sbsWidth, height, 30, 8000000, "rgb.mp4", encoderBaseDir);
        if (!rgbEncoder->start()) {
            LOGE("RGB encoder start failed");
            delete rgbEncoder;
            rgbEncoder = nullptr;
            return;
        }

        // Create encoder surface
        rgbEncoderSurface = new SXR::EncoderSurface();
        ANativeWindow* window = rgbEncoder->getInputSurface();
        if (!window || !rgbEncoderSurface->init(window, rgbCtx.display, rgbCtx.context)) {
            LOGE("RGB encoder surface init failed");
            delete rgbEncoderSurface;
            rgbEncoderSurface = nullptr;
            rgbEncoder->stop();
            delete rgbEncoder;
            rgbEncoder = nullptr;
            return;
        }
        LOGI("RGB SBS encoder initialized: %dx%d", sbsWidth, height);

        // Create stitch FBO and texture (2W x H)
        glGenFramebuffers(1, &rgbSbsFBO);
        glGenTextures(1, &rgbSbsTexture);
        glBindTexture(GL_TEXTURE_2D, rgbSbsTexture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, sbsWidth, height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, rgbSbsFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rgbSbsTexture, 0);
        GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
            LOGE("RGB SBS FBO not complete: 0x%x", fboStatus);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // Static callback for RGB camera frames
    static void onRGBFrame(void* userData, const SXR::FrameData* data) {
        auto* ext = static_cast<CameraAccessExtension*>(userData);
        if (!data || !ext) return;

        ext->inFlightCallbacks.fetch_add(1);
        if (ext->isPaused.load()) {
            ext->inFlightCallbacks.fetch_sub(1);
            ext->callbackDrainCV.notify_all();
            return;
        }

        auto t0 = std::chrono::steady_clock::now();

        // Init RGB-specific GL context
        if (!ext->rgbCtx.init(ext->engine)) {
            LOGE("Failed to init RGB GL context");
            ext->inFlightCallbacks.fetch_sub(1);
            ext->callbackDrainCV.notify_all();
            return;
        }

        ScopedCameraGLContextCurrent rgbCurrent(ext->rgbCtx);
        if (!rgbCurrent.isCurrent()) {
            LOGE("Failed to make RGB context current: 0x%x", eglGetError());
            ext->inFlightCallbacks.fetch_sub(1);
            ext->callbackDrainCV.notify_all();
            return;
        }

        ext->handleRGBFrame(data);

        ext->inFlightCallbacks.fetch_sub(1);
        ext->callbackDrainCV.notify_all();

        auto t1 = std::chrono::steady_clock::now();
        auto durUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        static std::atomic<uint32_t> sRgbFrameLogCounter{0};
        uint32_t logCount = sRgbFrameLogCounter.fetch_add(1);
        if (logCount % 300 == 0) {
            LOGI("onRGBFrame timing: total=%ld us", durUs);
        }
    }

    // Static callback for TRACKING camera frames
    static void onTrackingFrame(void* userData, const SXR::FrameData* data) {
        auto* ext = static_cast<CameraAccessExtension*>(userData);
        if (!data || !ext) return;

        ext->inFlightCallbacks.fetch_add(1);
        if (ext->isPaused.load()) {
            ext->inFlightCallbacks.fetch_sub(1);
            ext->callbackDrainCV.notify_all();
            return;
        }

        auto t0 = std::chrono::steady_clock::now();

        // Init tracking-specific GL context
        if (!ext->trackingCtx.init(ext->engine)) {
            LOGE("Failed to init tracking GL context");
            ext->inFlightCallbacks.fetch_sub(1);
            ext->callbackDrainCV.notify_all();
            return;
        }

        ScopedCameraGLContextCurrent trackingCurrent(ext->trackingCtx);
        if (!trackingCurrent.isCurrent()) {
            LOGE("Failed to make tracking context current: 0x%x", eglGetError());
            ext->inFlightCallbacks.fetch_sub(1);
            ext->callbackDrainCV.notify_all();
            return;
        }

        ext->handleTrackingFrame(data);

        ext->inFlightCallbacks.fetch_sub(1);
        ext->callbackDrainCV.notify_all();

        auto t1 = std::chrono::steady_clock::now();
        auto durUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        static std::atomic<uint32_t> sTrackFrameLogCounter{0};
        uint32_t logCount = sTrackFrameLogCounter.fetch_add(1);
        if (logCount % 300 == 0) {
            LOGI("onTrackingFrame timing: total=%ld us", durUs);
        }
    }

    // Static callback for CTRL camera frames
    static void onCtrlFrame(void* userData, const SXR::FrameData* data) {
        auto* ext = static_cast<CameraAccessExtension*>(userData);
        if (!data || !ext) return;

        ext->inFlightCallbacks.fetch_add(1);
        if (ext->isPaused.load()) {
            ext->inFlightCallbacks.fetch_sub(1);
            ext->callbackDrainCV.notify_all();
            return;
        }

        auto t0 = std::chrono::steady_clock::now();

        // Init ctrl-specific GL context
        if (!ext->ctrlCtx.init(ext->engine)) {
            LOGE("Failed to init ctrl GL context");
            ext->inFlightCallbacks.fetch_sub(1);
            ext->callbackDrainCV.notify_all();
            return;
        }

        ScopedCameraGLContextCurrent ctrlCurrent(ext->ctrlCtx);
        if (!ctrlCurrent.isCurrent()) {
            LOGE("Failed to make ctrl context current: 0x%x", eglGetError());
            ext->inFlightCallbacks.fetch_sub(1);
            ext->callbackDrainCV.notify_all();
            return;
        }

        ext->handleCtrlFrame(data);

        ext->inFlightCallbacks.fetch_sub(1);
        ext->callbackDrainCV.notify_all();

        auto t1 = std::chrono::steady_clock::now();
        auto durUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        static std::atomic<uint32_t> sCtrlFrameLogCounter{0};
        uint32_t logCount = sCtrlFrameLogCounter.fetch_add(1);
        if (logCount % 300 == 0) {
            LOGI("onCtrlFrame timing: total=%ld us", durUs);
        }
    }

    // Handle RGB camera frame (render directly in callback, release hwBuffer immediately)
    // Caller ensures rgbCtx is current, no mutex needed
    void handleRGBFrame(const SXR::FrameData* data) {
        // Save camera params on first frame of recording session
        saveCameraParams(data, "rgb", cameraParamsSavedRgb);

        // Feed camera params to hand overlay renderer
        if (g_engine) {
            feedOverlayCameraParams(data);
        }

        // Get frame dimensions and lazy init encoders
        int frameWidth = 0, frameHeight = 0;
        if (data->hwBuffer[0]) {
            AHardwareBuffer_Desc desc;
            AHardwareBuffer_describe(data->hwBuffer[0], &desc);
            frameWidth = desc.width;
            frameHeight = desc.height;

            // Initialize display shader (needed for YUV->RGBA conversion regardless of encoding)
            if (encoderShaderProgram == 0) {
                initEncoderShader();
            }

            // Lazy init encoders (must be done in GL context)
            if (!rgbEncoder && !encodersStopped.load()) {
                initEncodersAndSurfaces(frameWidth, frameHeight);
            }
        }

        // Capture snapshot flag once for this frame (both eyes must behave consistently)
        bool doSnapshot = snapshotRequested.load();

        // Save previous FBO for restoration
        GLint prevFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

        // Temporary textures and EGLImages for each eye (need to keep alive until SBS render)
        GLuint eyeTex[2] = {0, 0};
        EGLImageKHR eyeEglImage[2] = {EGL_NO_IMAGE_KHR, EGL_NO_IMAGE_KHR};
        int texWidth = 0, texHeight = 0;

        // Create temporary GL textures from AHardwareBuffer for each eye
        for (int i = 0; i < 2; i++) {
            if (!data->hwBuffer[i]) {
                LOGI("RGB cam %d: hwBuffer is null, skipping", i);
                continue;
            }

            // Populate RGB frame dimensions for info panel
            rgbFrameWidths[i] = data->frames[i].width;
            rgbFrameHeights[i] = data->frames[i].height;

            glGenTextures(1, &eyeTex[i]);
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, eyeTex[i]);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            EGLClientBuffer clientBuffer = glext::eglGetNativeClientBufferANDROID(data->hwBuffer[i]);
            if (!clientBuffer) {
                LOGE("Failed to get EGL client buffer for camera %d", i);
                glDeleteTextures(1, &eyeTex[i]);
                eyeTex[i] = 0;
                continue;
            }

            EGLint eglImageAttributes[] = {
                EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
                EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_LINEAR,
                EGL_NONE
            };
            eyeEglImage[i] = glext::eglCreateImageKHR(rgbCtx.display, EGL_NO_CONTEXT,
                                                     EGL_NATIVE_BUFFER_ANDROID,
                                                     clientBuffer, eglImageAttributes);
            if (eyeEglImage[i] == EGL_NO_IMAGE_KHR) {
                LOGE("Failed to create EGLImage for camera %d: 0x%x", i, eglGetError());
                glDeleteTextures(1, &eyeTex[i]);
                eyeTex[i] = 0;
                continue;
            }

            glext::glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)eyeEglImage[i]);

            AHardwareBuffer_Desc desc;
            AHardwareBuffer_describe(data->hwBuffer[i], &desc);
            texWidth = desc.width;
            texHeight = desc.height;
        }

        if (texWidth == 0 || texHeight == 0) {
            // No valid frames
            return;
        }

        // Setup shader state (shared across all draw calls)
        glUseProgram(encoderShaderProgram);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(encoderShaderProgram, "uTexture"), 0);
        glBindBuffer(GL_ARRAY_BUFFER, encoderVBO);
        GLint posLoc = glGetAttribLocation(encoderShaderProgram, "aPosition");
        GLint texLoc = glGetAttribLocation(encoderShaderProgram, "aTexCoord");
        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(texLoc);
        glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        // --- SBS encoding: render left+right to stitch FBO ---
        if (rgbSbsFBO != 0 && rgbEncoderSurface != nullptr && !encodersStopped.load()) {
            glBindFramebuffer(GL_FRAMEBUFFER, rgbSbsFBO);
            glViewport(0, 0, texWidth * 2, texHeight);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            for (int i = 0; i < 2; i++) {
                if (eyeTex[i] == 0) continue;
                glViewport(i * texWidth, 0, texWidth, texHeight);
                glBindTexture(GL_TEXTURE_EXTERNAL_OES, eyeTex[i]);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
            // Use mid-exposure time (start_of_exposure + exposure/2) as the
            // authoritative timestamp for both encoder PTS and CSV rows.
            // Mid-exposure is the best temporal representation of the image content.
            if (!encodersStopped.load()) {
                uint64_t midExposureNs = data->frames[0].timestamp + data->frames[0].exposure / 2;
                pendingEncodeTimestamp.store(midExposureNs);
                // Save aligned sensor data (head pose + hand tracking) here in the
                // camera callback to minimize pipeline delay between frame arrival
                // and CSV write. This function does no GL work — only ring buffer
                // sampling and async queue writes — so it is thread-safe.
                saveAlignedSensorData((int64_t)midExposureNs);
            }
        }

        // --- Display preview: render each eye to its own display texture ---
        for (int i = 0; i < 2; i++) {
            if (eyeTex[i] == 0) continue;

            if (rgbDisplayTextures[i] == 0) {
                glGenTextures(1, &rgbDisplayTextures[i]);
                glBindTexture(GL_TEXTURE_2D, rgbDisplayTextures[i]);
                glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, texWidth, texHeight);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                glGenFramebuffers(1, &rgbDisplayFBOs[i]);
                glBindFramebuffer(GL_FRAMEBUFFER, rgbDisplayFBOs[i]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, rgbDisplayTextures[i], 0);
                GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (status != GL_FRAMEBUFFER_COMPLETE) {
                    LOGE("RGB display FBO %d not complete: 0x%x", i, status);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            glBindFramebuffer(GL_FRAMEBUFFER, rgbDisplayFBOs[i]);
            glViewport(0, 0, texWidth, texHeight);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, eyeTex[i]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            // Cache pixels for snapshot
            if (doSnapshot) {
                auto& buf = (i == 0) ? snapshotRgbLeft : snapshotRgbRight;
                std::lock_guard<std::mutex> lock(buf.mutex);
                buf.width = texWidth;
                buf.height = texHeight;
                buf.pixels.resize(texWidth * texHeight * 4);
                std::vector<uint8_t> raw(texWidth * texHeight * 4);
                glReadPixels(0, 0, texWidth, texHeight, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
                int rowBytes = texWidth * 4;
                for (uint32_t y = 0; y < texHeight; y++) {
                    memcpy(buf.pixels.data() + y * rowBytes,
                           raw.data() + (texHeight - 1 - y) * rowBytes,
                           rowBytes);
                }
                buf.ready = true;
            }
        }

        // Restore previous FBO and cleanup
        glDisableVertexAttribArray(posLoc);
        glDisableVertexAttribArray(texLoc);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);

        for (int i = 0; i < 2; i++) {
            if (eyeTex[i]) glDeleteTextures(1, &eyeTex[i]);
            if (eyeEglImage[i] != EGL_NO_IMAGE_KHR) glext::eglDestroyImageKHR(rgbCtx.display, eyeEglImage[i]);
        }

        // Mark frame ready
        rgbFrameReady = true;
        rgbFpsTrackers[0].update(data->frames[0].timestamp);
        rgbFpsTrackers[1].update(data->frames[1].timestamp);
    }

    // Initialize grayscale encoder with Surface mode
    void initGrayscaleEncoder(SXR::CameraGroup group, int width, int height, CameraGLContext& ctx) {
        const char* groupName = (group == SXR::CameraGroup::TRACKING) ? "tracking" : "ctrl";
        int combinedWidth = width * 2;  // Side-by-side layout
        LOGI("Initializing grayscale encoder for %s: %dx%d @ 60fps (Surface mode)",
             groupName, combinedWidth, height);

        // Create Surface mode encoder
        auto* encoder = new SXR::CameraEncoder(groupName, combinedWidth, height,
                                                60, SXR::EncoderMode::SURFACE, encoderBaseDir);
        if (!encoder->start()) {
            LOGE("Failed to start grayscale encoder for %s", groupName);
            delete encoder;
            return;
        }

        // Create EncoderSurface
        auto* surface = new SXR::EncoderSurface();
        if (!surface->init(encoder->getInputSurface(), ctx.display, ctx.context)) {
            LOGE("Failed to initialize EncoderSurface for %s", groupName);
            surface->release();
            delete surface;
            encoder->stop();
            delete encoder;
            return;
        }

        // Make the encoder surface's context current to create texture
        if (!surface->makeCurrent()) {
            LOGE("Failed to make encoder surface current for %s", groupName);
            surface->release();
            delete surface;
            encoder->stop();
            delete encoder;
            return;
        }

        // Create texture for this encoder (each encoder has its own texture)
        // NOTE: Grayscale camera buffers are actually YUV format, not R8!
        // Use GL_TEXTURE_EXTERNAL_OES for YUV camera buffers - this is the standard way on Android
        // The GPU will automatically handle YUV->RGB conversion when sampling
        GLuint y8Texture;
        glGenTextures(1, &y8Texture);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, y8Texture);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
        LOGI("Created Y8 texture %u for %s encoder", y8Texture, groupName);

        // Store references
        if (group == SXR::CameraGroup::TRACKING) {
            trackingEncoder = encoder;
            trackingEncoderSurface = surface;
            trackingY8Texture = y8Texture;
        } else {
            ctrlEncoder = encoder;
            ctrlEncoderSurface = surface;
            ctrlY8Texture = y8Texture;
        }

        LOGI("Grayscale encoder initialized for %s with Y8 texture %u", groupName, y8Texture);
    }

    // Render grayscale Y8 buffer to encoder surface using hardware acceleration
    // Render grayscale buffer to encoder surface
    void renderGrayscaleToEncoder(SXR::EncoderSurface* surface, GLuint y8Texture,
                                     AHardwareBuffer* hwBuffer,
                                     int width, int height, EGLDisplay display,
                                     int64_t timestampNs) {
        if (!surface || !hwBuffer || !grayscaleEncoderShaderProgram || !y8Texture) {
            return;
        }

        // Save current EGL context
        EGLDisplay prevDisplay = eglGetCurrentDisplay();
        EGLSurface prevDraw = eglGetCurrentSurface(EGL_DRAW);
        EGLSurface prevRead = eglGetCurrentSurface(EGL_READ);
        EGLContext prevContext = eglGetCurrentContext();

        // Make encoder surface current
        if (!surface->makeCurrent()) {
            LOGE("renderGrayscaleToEncoder: failed to make current");
            return;
        }

        // Bind AHardwareBuffer to texture via EGLImage
        EGLClientBuffer clientBuffer = glext::eglGetNativeClientBufferANDROID(hwBuffer);
        if (!clientBuffer) {
            if (prevDisplay != EGL_NO_DISPLAY && prevContext != EGL_NO_CONTEXT) {
                eglMakeCurrent(prevDisplay, prevDraw, prevRead, prevContext);
            }
            return;
        }

        EGLint attrs[] = {
            EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
            EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_LINEAR,
            EGL_NONE
        };
        EGLImageKHR eglImage = glext::eglCreateImageKHR(
            display, EGL_NO_CONTEXT,
            EGL_NATIVE_BUFFER_ANDROID,
            clientBuffer, attrs);

        if (eglImage == EGL_NO_IMAGE_KHR) {
            if (prevDisplay != EGL_NO_DISPLAY && prevContext != EGL_NO_CONTEXT) {
                eglMakeCurrent(prevDisplay, prevDraw, prevRead, prevContext);
            }
            return;
        }

        glBindTexture(GL_TEXTURE_EXTERNAL_OES, y8Texture);
        glext::glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, eglImage);

        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(grayscaleEncoderShaderProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, y8Texture);
        glUniform1i(grayShader_uGrayscaleTexture, 0);

        glBindBuffer(GL_ARRAY_BUFFER, grayscaleEncoderVBO);
        glEnableVertexAttribArray(grayShader_aPosition);
        glVertexAttribPointer(grayShader_aPosition, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(grayShader_aTexCoord);
        glVertexAttribPointer(grayShader_aTexCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(grayShader_aPosition);
        glDisableVertexAttribArray(grayShader_aTexCoord);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glext::eglDestroyImageKHR(display, eglImage);
        surface->setPresentationTime(timestampNs);
        surface->swapBuffers();

        // Restore previous EGL context
        if (prevDisplay != EGL_NO_DISPLAY && prevContext != EGL_NO_CONTEXT) {
            eglMakeCurrent(prevDisplay, prevDraw, prevRead, prevContext);
        }
    }

    // Handle TRACKING camera frame — called from onTrackingFrame with trackingCtx current
    void handleTrackingFrame(const SXR::FrameData* data) {
        saveCameraParams(data, "tracking", cameraParamsSavedTracking);
        handleCVFrame(data, SXR::CameraGroup::TRACKING, trackingCtx, 0,
                      &trackingEncoder, trackingEncoderSurface, trackingY8Texture);
        trackingFrameReady = true;
        cvFpsTrackers[0].update(data->frames[0].timestamp);
        cvFpsTrackers[1].update(data->frames[1].timestamp);
    }

    // Handle CTRL camera frame — called from onCtrlFrame with ctrlCtx current
    void handleCtrlFrame(const SXR::FrameData* data) {
        saveCameraParams(data, "ctrl", cameraParamsSavedCtrl);
        handleCVFrame(data, SXR::CameraGroup::CTRL, ctrlCtx, 2,
                      &ctrlEncoder, ctrlEncoderSurface, ctrlY8Texture);
        ctrlFrameReady = true;
        cvFpsTrackers[2].update(data->frames[0].timestamp);
        cvFpsTrackers[3].update(data->frames[1].timestamp);
    }

    // Common CV frame handler for TRACKING and CTRL
    void handleCVFrame(const SXR::FrameData* data, SXR::CameraGroup group,
                       CameraGLContext& ctx, int baseIdx,
                       SXR::CameraEncoder** targetEncoder,
                       SXR::EncoderSurface* targetSurface,
                       GLuint targetY8Texture) {
        // Group-local index: 0=tracking, 1=ctrl — used to isolate per-group EGL resources
        const int gi = baseIdx / 2;

        // Initialize encoder shader if needed
        if (encoderShaderProgram == 0) {
            initEncoderShader();
        }

        uint32_t width = data->frames[0].width;
        uint32_t height = data->frames[0].height;

        // Lazy initialize encoder on first frame
        if (!*targetEncoder && !encodersStopped.load()) {
            initGrayscaleEncoder(group, width, height, ctx);
        }

        if (!data->hwBuffer[0]) return;

        // Lazy create VBO for CV rendering (per-group to avoid cross-thread glBufferSubData)
        if (cvDisplayVBOs[gi] == 0) {
            glGenBuffers(1, &cvDisplayVBOs[gi]);
            glBindBuffer(GL_ARRAY_BUFFER, cvDisplayVBOs[gi]);
            float defaultVerts[] = {
                -1.0f, -1.0f,   0.0f, 1.0f,
                 1.0f, -1.0f,   1.0f, 1.0f,
                -1.0f,  1.0f,   0.0f, 0.0f,
                 1.0f,  1.0f,   1.0f, 0.0f,
            };
            glBufferData(GL_ARRAY_BUFFER, sizeof(defaultVerts), defaultVerts, GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        // Initialize grayscale display shader if needed
        if (grayscaleEncoderShaderProgram == 0) {
            initGrayscaleEncoderShader();
        }

        // Reuse persistent external OES texture — create once per group, rebind per frame
        if (cvPersistentExtTex[gi] == 0) {
            glGenTextures(1, &cvPersistentExtTex[gi]);
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, cvPersistentExtTex[gi]);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        // Destroy previous EGLImage and create new one for this frame's hwBuffer
        if (cvPersistentEGLImage[gi] != EGL_NO_IMAGE_KHR) {
            glext::eglDestroyImageKHR(ctx.display, cvPersistentEGLImage[gi]);
            cvPersistentEGLImage[gi] = EGL_NO_IMAGE_KHR;
        }

        EGLClientBuffer clientBuffer = glext::eglGetNativeClientBufferANDROID(data->hwBuffer[0]);
        EGLint attrs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_LINEAR, EGL_NONE};
        cvPersistentEGLImage[gi] = glext::eglCreateImageKHR(ctx.display, EGL_NO_CONTEXT,
                                                         EGL_NATIVE_BUFFER_ANDROID, clientBuffer, attrs);
        if (cvPersistentEGLImage[gi] == EGL_NO_IMAGE_KHR) {
            LOGE("CV frame: failed to create EGLImage: 0x%x", eglGetError());
            return;
        }
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, cvPersistentExtTex[gi]);
        glext::glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)cvPersistentEGLImage[gi]);

        // Render left half and right half to separate display textures
        glUseProgram(grayscaleEncoderShaderProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, cvPersistentExtTex[gi]);
        glUniform1i(grayShader_uGrayscaleTexture, 0);

        for (int i = 0; i < 2; i++) {
            int idx = baseIdx + i;

            // Lazy create display texture + FBO
            if (cvDisplayTextures[idx] == 0 ||
                cvFrameWidths[idx] != width || cvFrameHeights[idx] != height) {
                if (cvDisplayTextures[idx] != 0) {
                    glDeleteTextures(1, &cvDisplayTextures[idx]);
                }
                if (cvDisplayFBOs[idx] != 0) {
                    glDeleteFramebuffers(1, &cvDisplayFBOs[idx]);
                }
                glGenTextures(1, &cvDisplayTextures[idx]);
                glBindTexture(GL_TEXTURE_2D, cvDisplayTextures[idx]);
                glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                glGenFramebuffers(1, &cvDisplayFBOs[idx]);
                glBindFramebuffer(GL_FRAMEBUFFER, cvDisplayFBOs[idx]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, cvDisplayTextures[idx], 0);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                cvFrameWidths[idx] = width;
                cvFrameHeights[idx] = height;
            }

            // Render left half (i=0: U=0..0.5) or right half (i=1: U=0.5..1.0)
            GLint prevFBO;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, cvDisplayFBOs[idx]);
            glViewport(0, 0, width, height);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            float uMin = (i == 0) ? 0.0f : 0.5f;
            float uMax = (i == 0) ? 0.5f : 1.0f;
            float verts[] = {
                -1.0f, -1.0f,  uMin, 1.0f,
                 1.0f, -1.0f,  uMax, 1.0f,
                -1.0f,  1.0f,  uMin, 0.0f,
                 1.0f,  1.0f,  uMax, 0.0f,
            };
            glBindBuffer(GL_ARRAY_BUFFER, cvDisplayVBOs[gi]);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

            glEnableVertexAttribArray(grayShader_aPosition);
            glVertexAttribPointer(grayShader_aPosition, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(grayShader_aTexCoord);
            glVertexAttribPointer(grayShader_aTexCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glDisableVertexAttribArray(grayShader_aPosition);
            glDisableVertexAttribArray(grayShader_aTexCoord);

            glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
            cvLastUploadedFrameId[idx] = data->frames[i].frameId;
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // Cache pixels for snapshot if requested (read in this GL context to avoid cross-thread GL access)
        if (snapshotRequested.load()) {
            for (int i = 0; i < 2; i++) {
                int idx = baseIdx + i;
                auto& buf = snapshotCv[idx];
                glBindFramebuffer(GL_FRAMEBUFFER, cvDisplayFBOs[idx]);
                std::lock_guard<std::mutex> lock(buf.mutex);
                buf.width = width;
                buf.height = height;
                buf.pixels.resize(width * height * 4);
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buf.pixels.data());
                buf.ready = true;
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // Render to encoder surface (for recording)
        // NOTE: This does context switch to encoder surface and back — main overhead
        if (targetSurface && *targetEncoder && targetY8Texture && !stopInProgress.load()) {
            int64_t frameTimestampNs = data->frames[0].timestamp;
            (*targetEncoder)->submitNsTimestamp(frameTimestampNs);
            renderGrayscaleToEncoder(targetSurface, targetY8Texture,
                                     data->hwBuffer[0],
                                     width * 2, height, ctx.display,
                                     frameTimestampNs);
        }
    }

    // Initialize cameras using dynamic loading API
    bool initCameras(JavaVM* jvm, jobject activity) {
        vm = jvm;
        activityObject = activity;

        // Initialize the API table by loading the library dynamically
        if (sxr_camera_api_init(&api, NULL) != 0) {
            LOGE("Failed to load sxr_camera library (libsxr_camera_client.so)");
            return false;
        }

        // Verify API is valid
        if (!sxr_camera_api_is_valid(&api)) {
            LOGE("SxrCamera API is not valid after initialization");
            sxr_camera_api_deinit(&api);
            return false;
        }

        cameraContext = sxr_camera_create(&api, vm, activityObject);
        if (!cameraContext) {
            LOGE("Failed to create SxrCameraContext");
            sxr_camera_api_deinit(&api);
            return false;
        }

        // Open RGB camera group
        int ret = sxr_camera_open_group(&api, cameraContext, SXR::CameraGroup::RGB,
                                        onRGBFrame, this);
        if (ret != 0) {
            LOGE("Failed to open RGB camera group: %d", ret);
        } else {
            LOGI("RGB camera group opened successfully");
        }

        // Open TRACKING camera group
        ret = sxr_camera_open_group(&api, cameraContext, SXR::CameraGroup::TRACKING,
                                    onTrackingFrame, this);
        if (ret != 0) {
            LOGE("Failed to open TRACKING camera group: %d", ret);
        } else {
            LOGI("TRACKING camera group opened successfully");
        }

        // Open CTRL camera group
        ret = sxr_camera_open_group(&api, cameraContext, SXR::CameraGroup::CTRL,
                                    onCtrlFrame, this);
        if (ret != 0) {
            LOGE("Failed to open CTRL camera group: %d", ret);
        } else {
            LOGI("CTRL camera group opened successfully");
        }

        // Initialize legacy grayscale encoders (not used, kept for compatibility)
        for (int i = 0; i < SXR::CAME_MAX; i++) {
            grayCameraEncoders[i] = nullptr;
        }

        // RGB/Tracking/CTRL encoders will be initialized lazily when first frame arrives
        // This allows us to get actual frame dimensions from AHardwareBuffer

        camerasInitialized = true;
        cameraGroupsOpen = true;
        LOGI("Cameras initialized successfully with dynamic loading API");
        return true;
    }

    void Update() {
        // Consider app active only when both:
        // 1. Android lifecycle says Resumed (standard Android backgrounding)
        // 2. OpenXR session is FOCUSED (XR runtime focus management)
        // On some XR devices, switching apps does NOT trigger Android lifecycle
        // events, so we must also check the OpenXR session state.
        bool appActive = engine->state.Resumed &&
                         engine->sessionState == XR_SESSION_STATE_FOCUSED;
        if (appActive) {
            resume();
        } else {
            pause();
        }
    }

    // Pause camera processing and encoding (called when app goes to background)
    void pause() {
        bool wasPaused = isPaused.exchange(true);
        if (!wasPaused) {
            LOGI("CameraAccessExtension: pausing...");

            // Close camera groups FIRST to stop new callbacks from being triggered
            closeCameraGroups();

            // Wait for in-flight callbacks to drain (with timeout)
            {
                std::unique_lock<std::mutex> lk(callbackDrainMutex);
                callbackDrainCV.wait_for(lk, std::chrono::milliseconds(500),
                    [this] { return inFlightCallbacks.load() == 0; });
                if (inFlightCallbacks.load() > 0) {
                    LOGW("pause: %d callbacks still in flight after timeout",
                         inFlightCallbacks.load());
                }
            }

            // Stop encoders to finalize MP4 files properly
            stopEncoders();
            LOGI("CameraAccessExtension: paused, cameras closed");
        }
    }

    // Resume camera processing and encoding (called when app comes to foreground)
    void resume() {
        bool wasPaused = isPaused.exchange(false);
        if (wasPaused) {
            LOGI("CameraAccessExtension: resuming...");
            // Re-open camera groups
            openCameraGroups();
            // Restart encoders
            startEncoders();
            LOGI("CameraAccessExtension: resumed, cameras reopened");
        }
    }

    // Close all camera groups (release camera hardware)
    void closeCameraGroups() {
        if (!camerasInitialized || !cameraContext || !cameraGroupsOpen) return;
        LOGI("Closing camera groups...");
        sxr_camera_close_group(&api, cameraContext, SXR::CameraGroup::RGB);
        sxr_camera_close_group(&api, cameraContext, SXR::CameraGroup::TRACKING);
        sxr_camera_close_group(&api, cameraContext, SXR::CameraGroup::CTRL);
        cameraGroupsOpen = false;
        LOGI("Camera groups closed");
    }

    // Re-open all camera groups (resume camera streaming)
    void openCameraGroups() {
        if (!camerasInitialized || !cameraContext || cameraGroupsOpen) return;
        LOGI("Re-opening camera groups...");
        int ret = sxr_camera_open_group(&api, cameraContext, SXR::CameraGroup::RGB,
                                        onRGBFrame, this);
        if (ret != 0) {
            LOGE("Failed to re-open RGB camera group: %d", ret);
        }

        ret = sxr_camera_open_group(&api, cameraContext, SXR::CameraGroup::TRACKING,
                                    onTrackingFrame, this);
        if (ret != 0) {
            LOGE("Failed to re-open TRACKING camera group: %d", ret);
        }

        ret = sxr_camera_open_group(&api, cameraContext, SXR::CameraGroup::CTRL,
                                    onCtrlFrame, this);
        if (ret != 0) {
            LOGE("Failed to re-open CTRL camera group: %d", ret);
        }

        cameraGroupsOpen = true;
        LOGI("Camera groups re-opened");
    }

    // Stop encoders (can be called from pause or cleanup)
    void stopEncoders() {
        // Stop RGB encoder
        if (rgbEncoder) {
            rgbEncoder->stop();
        }

        // Stop tracking encoder
        if (trackingEncoder) {
            trackingEncoder->stop();
        }

        // Stop ctrl encoder
        if (ctrlEncoder) {
            ctrlEncoder->stop();
        }

        // Stop legacy grayscale encoders
        for (int i = 0; i < SXR::CAME_MAX; i++) {
            if (grayCameraEncoders[i]) {
                grayCameraEncoders[i]->stop();
            }
        }

        LOGI("All encoders stopped");
    }

    // Start encoders (can be called from resume)
    void startEncoders() {
        // RGB encoders will be lazily initialized in handleRGBFrame
        // Grayscale encoders are initialized in initCameras
        // Just reset the ready flags
        rgbFrameReady = false;
        trackingFrameReady = false;
        ctrlFrameReady = false;
        LOGI("Encoders ready to start");
    }

    void stopEncoder() {
        // Mark stop in progress to block new recording starts
        stopInProgress = true;
        // Set flag first to prevent camera thread from entering SBS block
        encodersStopped = true;
        LOGI("stopEncoder: setting encodersStopped=true");

        // Stop RGB encoder first (signal EOS + join output thread), then release surface
        if (rgbEncoder) {
            rgbEncoder->stop();
            delete rgbEncoder;
            rgbEncoder = nullptr;
        }
        if (rgbEncoderSurface) {
            rgbEncoderSurface->release();
            delete rgbEncoderSurface;
            rgbEncoderSurface = nullptr;
        }

        // Delete RGB GL resources on the correct context (rgbCtx)
        if (rgbCtx.initialized && (rgbSbsFBO || rgbSbsTexture)) {
            ScopedCameraGLContextCurrent rgbCurrent(rgbCtx);
            if (rgbCurrent.isCurrent()) {
                if (rgbSbsFBO) {
                    glDeleteFramebuffers(1, &rgbSbsFBO);
                    rgbSbsFBO = 0;
                }
                if (rgbSbsTexture) {
                    glDeleteTextures(1, &rgbSbsTexture);
                    rgbSbsTexture = 0;
                }
            } else {
                LOGW("stopEncoder: failed to make RGB context current for cleanup: 0x%x", eglGetError());
            }
        }

        // Stop legacy grayscale encoders
        for (int i = 0; i < SXR::CAME_MAX; i++) {
            if (grayCameraEncoders[i]) {
                grayCameraEncoders[i]->stop();
                delete grayCameraEncoders[i];
                grayCameraEncoders[i] = nullptr;
            }
        }

        // Stop tracking encoder
        if (trackingEncoder) {
            trackingEncoder->stop();
            delete trackingEncoder;
            trackingEncoder = nullptr;
        }

        // Stop ctrl encoder
        if (ctrlEncoder) {
            ctrlEncoder->stop();
            delete ctrlEncoder;
            ctrlEncoder = nullptr;
        }

        // Release grayscale encoder surfaces
        if (trackingEncoderSurface) {
            trackingEncoderSurface->release();
            delete trackingEncoderSurface;
            trackingEncoderSurface = nullptr;
        }
        if (ctrlEncoderSurface) {
            ctrlEncoderSurface->release();
            delete ctrlEncoderSurface;
            ctrlEncoderSurface = nullptr;
        }

        // Delete per-encoder Y8 textures
        if (trackingY8Texture) {
            glDeleteTextures(1, &trackingY8Texture);
            trackingY8Texture = 0;
        }
        if (ctrlY8Texture) {
            glDeleteTextures(1, &ctrlY8Texture);
            ctrlY8Texture = 0;
        }

        stopInProgress = false;
        LOGI("stopEncoder: completed, stopInProgress=false");
    }

    void cleanupCameras() {
        if (!camerasInitialized) return;

        // Stop camera callbacks immediately
        isPaused = true;

        // Close camera groups (safe to call even if already closed)
        closeCameraGroups();

        // Wait for in-flight callbacks to drain (with timeout)
        {
            std::unique_lock<std::mutex> lk(callbackDrainMutex);
            callbackDrainCV.wait_for(lk, std::chrono::milliseconds(500),
                [this] { return inFlightCallbacks.load() == 0; });
            if (inFlightCallbacks.load() > 0) {
                LOGW("cleanupCameras: %d callbacks still in flight after timeout, proceeding anyway",
                     inFlightCallbacks.load());
            }
        }

        // Destroy camera context
        if (cameraContext) {
            sxr_camera_destroy(&api, cameraContext);
            cameraContext = nullptr;
        }

        // Unload the library and cleanup API
        sxr_camera_api_deinit(&api);

        // Cleanup encoders
        stopEncoder();

        // Cleanup shared EGL context (including displayTextures and encoder surfaces)
        cleanupAllGLContexts();

        camerasInitialized = false;
        LOGI("Cameras cleaned up");
    }

    // Convert XrTime to boottime nanoseconds
    // XrTime → monotonic (via xrConvertTimeToTimespecTimeKHR) → boottime (+ offset)
    int64_t xrtimeToboottime(XrTime xrtime) {
        static int64_t boottime_to_mono_offset = 0;
        static bool offset_calculated = false;

        if (!offset_calculated) {
            struct timespec boot_ts, mono_ts;
            clock_gettime(CLOCK_BOOTTIME, &boot_ts);
            clock_gettime(CLOCK_MONOTONIC, &mono_ts);
            int64_t boottime_ns_now = (int64_t)boot_ts.tv_sec * 1000000000LL + boot_ts.tv_nsec;
            int64_t mono_ns_now = (int64_t)mono_ts.tv_sec * 1000000000LL + mono_ts.tv_nsec;
            boottime_to_mono_offset = boottime_ns_now - mono_ns_now;
            offset_calculated = true;
            LOGI("xrtimeToboottime: boottime-mono offset = %ld ns", (long)boottime_to_mono_offset);
        }

        // Step 1: XrTime → monotonic timespec
        int64_t mono_ns;
        if (xrConvertTimeToTimespecTimeKHR) {
            struct timespec ts;
            XrResult res = xrConvertTimeToTimespecTimeKHR(engine->state.xrInstance, xrtime, &ts);
            if (XR_SUCCEEDED(res)) {
                mono_ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
            } else {
                LOGE("xrConvertTimeToTimespecTimeKHR failed: %d", res);
                mono_ns = (int64_t)xrtime;
            }
        } else {
            mono_ns = (int64_t)xrtime;
        }

        // Step 2: monotonic → boottime
        return mono_ns + boottime_to_mono_offset;
    }

    // Convert boottime nanoseconds to XrTime
    // boottime → monotonic (- offset) → XrTime (via xrConvertTimespecTimeToTimeKHR)
    XrTime boottimeToXrTime(uint64_t boottime_ns) {
        // Reuse the same offset calculated by xrtimeToboottime
        static int64_t boottime_to_mono_offset = 0;
        static bool offset_calculated = false;

        if (!offset_calculated) {
            struct timespec boot_ts, mono_ts;
            clock_gettime(CLOCK_BOOTTIME, &boot_ts);
            clock_gettime(CLOCK_MONOTONIC, &mono_ts);
            int64_t boottime_ns_now = (int64_t)boot_ts.tv_sec * 1000000000LL + boot_ts.tv_nsec;
            int64_t mono_ns_now = (int64_t)mono_ts.tv_sec * 1000000000LL + mono_ts.tv_nsec;
            boottime_to_mono_offset = boottime_ns_now - mono_ns_now;
            offset_calculated = true;
            LOGI("boottimeToXrTime: boottime-mono offset = %ld ns", (long)boottime_to_mono_offset);
        }

        // Step 1: boottime → monotonic
        int64_t mono_ns = (int64_t)boottime_ns - boottime_to_mono_offset;

        // Step 2: monotonic → XrTime
        if (!xrConvertTimespecTimeToTimeKHR) {
            return static_cast<XrTime>(mono_ns);
        }

        struct timespec ts;
        ts.tv_sec = mono_ns / 1000000000LL;
        ts.tv_nsec = mono_ns % 1000000000LL;

        XrTime xrTime;
        XrResult res = xrConvertTimespecTimeToTimeKHR(engine->state.xrInstance, &ts, &xrTime);
        if (XR_FAILED(res)) {
            LOGE("xrConvertTimespecTimeToTimeKHR failed: %d", res);
            return static_cast<XrTime>(mono_ns);
        }
        return xrTime;
    }
};
/**
 * Shared state for our app.
 */
struct engine : public AppCommon::base_engine {
    // render target width
    uint32_t width;

    // render target height
    uint32_t height;

    // <sample count, stereo swapchain>
    std::unordered_map<uint32_t, StereoSwapchain> swapchainMap;

    // cube geometry
    QtiGL::Geometry cube;

    // cube shader
    QtiGL::Shader *cubeShader;

    // cube texture
    GLuint cubeTexture;
    // cube texture
    GLuint testTexture;
    // cube position
    std::vector<glm::mat4> cubeMatrices;

    // cube colors
    std::vector<glm::vec3> cubeColors;

    // max sample count
    GLint maxSampleCount;

    // current sample count
    GLint currentSampleCount;
    const uint32_t quadImageWidth = 500;
    const uint32_t quadImageHeight = 422;

    AppCommon::Swapchain quadSwapchain;
    AppCommon::Swapchain quadSwapchain2;
    XrExtent2Df quadLayerSize = {.width = 2.f, .height = 2.f};
    XrPosef  quadLayerPose = {.orientation = {.x = 0.f,.y=0.f,.z=0.f,.w = 1.f},
            .position = {.x = 0.f,.y=-2.1f,.z=-5.f}};

    GLuint depthBuffer; // common depth buffer... should be alright as we assume

    bool useControllerMode = false;
    bool useProjectHand = false;

    CameraAccessExtension mCameraAccessExtension{this};
    HandTrackerLogic mHandTrackerLogic{this};
    HandOverlayRenderer handOverlay;
    DatasetRecorder mDatasetRecorder;
    DatasetExporter mDatasetExporter;
    ControllerPoseSaver mControllerPoseSaver;
    uint64_t controllerFrameCounter = 0;
    std::unique_ptr<Input> inputPtr;
    bool dpadCenterPressed = false;
    bool prevRecordingToggle = false;
    AlignedSensorSnapshot alignedSnapshot;
    PoseHandSampleRing poseHandRing;
    engine()
            : width(0), height(0), cubeShader(nullptr), cubeTexture(0),
              maxSampleCount(4), currentSampleCount(1)
    {
    }
};

// Save aligned head pose and hand tracking data with the RGB frame timestamp.
// Called from CameraAccessExtension::handleRGBFrame (camera callback thread).
//
// Samples the ring buffer at the camera frame's mid-exposure time
// (start_of_exposure + exposure/2, CLOCK_BOOTTIME) to recover time-aligned
// head pose (slerp) and hand joints (lerp). Both overlaySnap (for encoder
// overlay) and CSV output use the same time-aligned data so that recorded
// timestamps match the actual sensor data.
static void saveAlignedSensorData(int64_t rgbTimestampNs) {
    if (!g_engine || !g_engine->mDatasetRecorder.isRecording() || g_engine->useControllerMode) {
        return;
    }

    // Snapshot the alignedSnapshot once for rgbFrameCount and fallback.
    AlignedSensorSnapshot snap;
    {
        std::lock_guard<std::mutex> lock(g_engine->alignedSnapshot.mutex);
        snap.copyFrom(g_engine->alignedSnapshot);
    }

    // Sample the ring buffer at the camera frame's start_of_exposure timestamp
    // (CLOCK_BOOTTIME). This corrects the lag between predictedDisplayTime (where
    // pose+hand data was captured) and the actual camera frame time.
    PoseHandSampleRing::Sample rs;
    PoseHandSampleRing::SampleInfo info;
    bool ringOk = g_engine->poseHandRing.sample(rgbTimestampNs, rs, &info);

    // Diagnostic: throttle to ~1 Hz at 30 fps
    static int s_logCnt = 0;
    if ((s_logCnt++ % 30) == 0) {
        LOGI("ring sample: rgbTs=%lld count=%d window=[%lld..%lld] dtOld=%.1fms dtNew=%.1fms alpha=%.3f clamp=%c%c ok=%d",
             (long long)rgbTimestampNs, info.curCount,
             (long long)info.oldestNs, (long long)info.newestNs,
             (rgbTimestampNs - info.oldestNs) / 1e6,
             (rgbTimestampNs - info.newestNs) / 1e6,
             info.alpha,
             info.clampedLow ? 'L' : '-',
             info.clampedHigh ? 'H' : '-',
             (int)ringOk);
    }

    bool useRing = ringOk && rs.poseValid;

    // --- Overlay snapshot for encoder hand projection ---
    {
        std::lock_guard<std::mutex> lock(g_engine->mCameraAccessExtension.overlaySnap.mutex);
        auto& os = g_engine->mCameraAccessExtension.overlaySnap;
        if (useRing) {
            os.headValid = true;
            memcpy(os.headPos, rs.headPos, sizeof(os.headPos));
            memcpy(os.headQuat, rs.headQuat, sizeof(os.headQuat));
            os.leftActive = rs.leftActive;
            os.rightActive = rs.rightActive;
            memcpy(os.leftJoints, rs.leftJoints, sizeof(os.leftJoints));
            memcpy(os.rightJoints, rs.rightJoints, sizeof(os.rightJoints));
        } else {
            // Ring not warmed up yet — fall back to the most recent snapshot.
            os.copyFrom(snap);
        }
    }

    // --- Save head pose (time-aligned via ring buffer) ---
    {
        XrPosef pose{};
        if (useRing) {
            pose.position.x = rs.headPos[0];
            pose.position.y = rs.headPos[1];
            pose.position.z = rs.headPos[2];
            pose.orientation.x = rs.headQuat[0];
            pose.orientation.y = rs.headQuat[1];
            pose.orientation.z = rs.headQuat[2];
            pose.orientation.w = rs.headQuat[3];
        } else if (snap.headPose.valid) {
            // Ring not warmed up — fall back to most recent snapshot.
            pose.position.x = snap.headPose.pos[0];
            pose.position.y = snap.headPose.pos[1];
            pose.position.z = snap.headPose.pos[2];
            pose.orientation.x = snap.headPose.quat[0];
            pose.orientation.y = snap.headPose.quat[1];
            pose.orientation.z = snap.headPose.quat[2];
            pose.orientation.w = snap.headPose.quat[3];
        }
        g_engine->mDatasetRecorder.saveHeadPose(rgbTimestampNs, pose);
    }

    // --- Save hand tracking (time-aligned via ring buffer) ---
    FrameData fd{};
    fd.frameNumber = snap.rgbFrameCount;
    fd.timestamp = rgbTimestampNs;

    if (useRing) {
        fd.hasLeftHand = rs.leftActive;
        fd.hasRightHand = rs.rightActive;
        if (rs.leftActive) {
            fd.leftHand.isActive = true;
            fd.leftHand.jointCount = XR_HAND_JOINT_COUNT_EXT;
            for (int j = 0; j < XR_HAND_JOINT_COUNT_EXT; j++) {
                fd.leftHand.joints[j].radius = rs.leftRadii[j];
                fd.leftHand.joints[j].position[0] = rs.leftJoints[j][0];
                fd.leftHand.joints[j].position[1] = rs.leftJoints[j][1];
                fd.leftHand.joints[j].position[2] = rs.leftJoints[j][2];
                fd.leftHand.joints[j].orientation[0] = rs.leftQuats[j][0];
                fd.leftHand.joints[j].orientation[1] = rs.leftQuats[j][1];
                fd.leftHand.joints[j].orientation[2] = rs.leftQuats[j][2];
                fd.leftHand.joints[j].orientation[3] = rs.leftQuats[j][3];
            }
        }
        if (rs.rightActive) {
            fd.rightHand.isActive = true;
            fd.rightHand.jointCount = XR_HAND_JOINT_COUNT_EXT;
            for (int j = 0; j < XR_HAND_JOINT_COUNT_EXT; j++) {
                fd.rightHand.joints[j].radius = rs.rightRadii[j];
                fd.rightHand.joints[j].position[0] = rs.rightJoints[j][0];
                fd.rightHand.joints[j].position[1] = rs.rightJoints[j][1];
                fd.rightHand.joints[j].position[2] = rs.rightJoints[j][2];
                fd.rightHand.joints[j].orientation[0] = rs.rightQuats[j][0];
                fd.rightHand.joints[j].orientation[1] = rs.rightQuats[j][1];
                fd.rightHand.joints[j].orientation[2] = rs.rightQuats[j][2];
                fd.rightHand.joints[j].orientation[3] = rs.rightQuats[j][3];
            }
        }
    } else {
        // Ring not warmed up — fall back to most recent snapshot.
        fd.hasLeftHand = snap.leftHand.active;
        fd.hasRightHand = snap.rightHand.active;
        if (snap.leftHand.active) {
            fd.leftHand.isActive = true;
            fd.leftHand.jointCount = XR_HAND_JOINT_COUNT_EXT;
            for (int j = 0; j < XR_HAND_JOINT_COUNT_EXT; j++) {
                fd.leftHand.joints[j].radius = snap.leftHand.radii[j];
                fd.leftHand.joints[j].position[0] = snap.leftHand.joints[j][0];
                fd.leftHand.joints[j].position[1] = snap.leftHand.joints[j][1];
                fd.leftHand.joints[j].position[2] = snap.leftHand.joints[j][2];
                fd.leftHand.joints[j].orientation[0] = snap.leftHand.quats[j][0];
                fd.leftHand.joints[j].orientation[1] = snap.leftHand.quats[j][1];
                fd.leftHand.joints[j].orientation[2] = snap.leftHand.quats[j][2];
                fd.leftHand.joints[j].orientation[3] = snap.leftHand.quats[j][3];
            }
        }
        if (snap.rightHand.active) {
            fd.rightHand.isActive = true;
            fd.rightHand.jointCount = XR_HAND_JOINT_COUNT_EXT;
            for (int j = 0; j < XR_HAND_JOINT_COUNT_EXT; j++) {
                fd.rightHand.joints[j].radius = snap.rightHand.radii[j];
                fd.rightHand.joints[j].position[0] = snap.rightHand.joints[j][0];
                fd.rightHand.joints[j].position[1] = snap.rightHand.joints[j][1];
                fd.rightHand.joints[j].position[2] = snap.rightHand.joints[j][2];
                fd.rightHand.joints[j].orientation[0] = snap.rightHand.quats[j][0];
                fd.rightHand.joints[j].orientation[1] = snap.rightHand.quats[j][1];
                fd.rightHand.joints[j].orientation[2] = snap.rightHand.quats[j][2];
                fd.rightHand.joints[j].orientation[3] = snap.rightHand.quats[j][3];
            }
        }
    }
    g_engine->mHandTrackerLogic.rawDateSave->SaveFrame(fd);

    {
        std::lock_guard<std::mutex> lock(g_engine->alignedSnapshot.mutex);
        g_engine->alignedSnapshot.rgbFrameCount++;
    }
}

// Feed camera params to hand overlay renderer (needs complete engine type)
static void feedOverlayCameraParams(const SXR::FrameData* data) {
    if (!g_engine) return;
    for (int i = 0; i < 2; i++) {
        const auto& f = data->frames[i];
        static bool logged = false;
        if (!logged) {
            LOGI("feedOverlayCameraParams[%d]: %ux%u focal=[%.1f,%.1f] center=[%.1f,%.1f] "
                 "dist=[%.4f,%.4f,%.4f,%.4f] pos=[%.4f,%.4f,%.4f] quat=[%.4f,%.4f,%.4f,%.4f]",
                 i, f.width, f.height, f.focalX, f.focalY, f.centerX, f.centerY,
                 f.radialDistortion[0], f.radialDistortion[1], f.radialDistortion[2], f.radialDistortion[3],
                 f.position[0], f.position[1], f.position[2],
                 f.rotation[0], f.rotation[1], f.rotation[2], f.rotation[3]);
            if (i == 1) logged = true;
        }
        g_engine->handOverlay.updateCameraParams(i, f.focalX, f.focalY,
                                                   f.centerX, f.centerY,
                                                   f.radialDistortion,
                                                   f.position, f.rotation,
                                                   f.width, f.height);
    }
    // Per-eye UV pixel offsets for fine-tuning (default 0).
    // Projection now matches the Python reference; offsets are
    // only needed for residual calibration.
    static bool offsetsSet = false;
    if (!offsetsSet) {
        g_engine->handOverlay.setUVOffset(0, 0.0f, 0.0f);
        g_engine->handOverlay.setUVOffset(1, 0.0f, 0.0f);
        offsetsSet = true;
        LOGI("HandOverlay UV offsets set: L=(0,0) R=(0,0)");
    }
}

// JNI native methods for intent control (needs complete engine type)
// g_engine is declared near top of file (before CameraAccessExtension)

CameraAccessExtension* CameraAccessExtension::sInstance = nullptr;


extern "C" JNIEXPORT void JNICALL
Java_com_ssnwt_helloxr_VrNativeActivity_nativeRequestSnapshot(JNIEnv *env, jobject thiz) {
    if (g_engine) {
        g_engine->mCameraAccessExtension.snapshotRequested = true;
        LOGI("Snapshot requested via intent");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ssnwt_helloxr_VrNativeActivity_nativeStartRecording(JNIEnv *env, jobject thiz) {
    if (g_engine && !g_engine->mDatasetRecorder.isRecording()) {
        // Wait for any async stop (encoder + recorder) to complete before starting
        int waitCount = 0;
        while (g_engine->mCameraAccessExtension.stopInProgress.load() ||
               g_engine->mDatasetRecorder.isRecording()) {
            usleep(10000); // 10ms
            if (++waitCount % 100 == 0) {
                LOGW("nativeStartRecording: waiting for stopEncoder to finish (%d ms)", waitCount * 10);
            }
            if (waitCount > 300) { // 3s timeout
                LOGE("nativeStartRecording: timed out waiting for stopEncoder");
                return;
            }
        }
        LOGI("Start recording via intent");
        g_engine->mDatasetRecorder.start();
        g_engine->mCameraAccessExtension.encoderBaseDir = g_engine->mDatasetRecorder.getDatasetDir();
        g_engine->mCameraAccessExtension.encodingEnabled = true;
        g_engine->mCameraAccessExtension.encodersStopped = false;
        g_engine->mCameraAccessExtension.cameraParamsSavedRgb = false;
        g_engine->mCameraAccessExtension.cameraParamsSavedTracking = false;
        g_engine->mCameraAccessExtension.cameraParamsSavedCtrl = false;
        {
            std::lock_guard<std::mutex> lock(g_engine->alignedSnapshot.mutex);
            g_engine->alignedSnapshot.headPose.valid = false;
            g_engine->alignedSnapshot.leftHand.active = false;
            g_engine->alignedSnapshot.rightHand.active = false;
            g_engine->alignedSnapshot.rgbFrameCount = 0;
        }
        if (g_engine->useControllerMode) {
            g_engine->mControllerPoseSaver.StartSession(
                g_engine->mDatasetRecorder.getControllerPoseCsvPath());
        } else {
            g_engine->mHandTrackerLogic.rawDateSave->StartNewSession(
                g_engine->mDatasetRecorder.getHandTrackingCsvPath());
            g_engine->mDatasetRecorder.writeCaptureStatusJson(
                "recording", g_engine->mHandTrackerLogic.rawDateSave);
        }
        ttsSpeak("开始录制");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ssnwt_helloxr_VrNativeActivity_nativeStopRecording(JNIEnv *env, jobject thiz) {
    if (g_engine && g_engine->mDatasetRecorder.isRecording()) {
        LOGI("Stop recording via intent (async)");
        g_engine->mCameraAccessExtension.encodingEnabled = false;
        if (!g_engine->useControllerMode) {
            g_engine->mDatasetRecorder.writeCaptureStatusJson(
                "finalizing", g_engine->mHandTrackerLogic.rawDateSave);
        }
        // Stop encoder first, then stop recorder in the same async thread.
        // This ordering guarantees that saveAlignedSensorData (called from the
        // render thread during encoder submission) completes before the recorder
        // is torn down, so head_pose / hand_tracking CSV rows stay 1:1 with mett.
        std::thread([]() {
            g_engine->mCameraAccessExtension.stopEncoder();
            g_engine->mCameraAccessExtension.encoderBaseDir.clear();

            g_engine->mDatasetRecorder.stop();
            if (g_engine->useControllerMode) {
                g_engine->mControllerPoseSaver.StopSession();
            } else {
                g_engine->mHandTrackerLogic.rawDateSave->StopSession();
                g_engine->mDatasetRecorder.writeCaptureStatusJson(
                    "complete", g_engine->mHandTrackerLogic.rawDateSave);
            }
            ttsSpeak("录制已保存");
            LOGI("Intent: Async encoder + recorder stop completed");
        }).detach();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ssnwt_helloxr_VrNativeActivity_nativeStartExporter(
        JNIEnv *env, jobject thiz, jstring exportPath) {
    if (!g_engine) {
        LOGW("nativeStartExporter: g_engine is null");
        return;
    }
    if (!exportPath) {
        LOGW("nativeStartExporter: exportPath is null");
        return;
    }

    const char* exportPathChars = env->GetStringUTFChars(exportPath, nullptr);
    std::string exportPathStr = exportPathChars;
    std::string datasetPathStr = std::string(storagePath) + "/dataset";
    env->ReleaseStringUTFChars(exportPath, exportPathChars);

    LOGI("nativeStartExporter: datasetPath=%s exportPath=%s",
         datasetPathStr.c_str(), exportPathStr.c_str());

    g_engine->mDatasetExporter.stop();
    g_engine->mDatasetExporter.init(datasetPathStr);
    g_engine->mDatasetExporter.start(exportPathStr);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ssnwt_helloxr_VrNativeActivity_nativeStopExporter(
        JNIEnv *env, jobject thiz) {
    if (!g_engine) {
        LOGW("nativeStopExporter: g_engine is null");
        return;
    }

    LOGI("nativeStopExporter");
    g_engine->mDatasetExporter.stop();
}

// CameraInfoPanel implementation (needs complete engine type)
void CameraInfoPanel::update(struct engine* engine) {
    auto& cam = engine->mCameraAccessExtension;

    // Only rebuild texture every 30 frames (FPS changes ~1/sec at 30fps)
    frameCounter++;
    if (frameCounter % 30 != 0 && texture != 0) return;

    const char* names[6] = {"RGB-L", "RGB-R", "CV-TL", "CV-TR", "CV-BL", "CV-BR"};
    uint32_t resW[6] = {cam.rgbFrameWidths[0], cam.rgbFrameWidths[1], cam.cvFrameWidths[0], cam.cvFrameWidths[1],
                        cam.cvFrameWidths[2], cam.cvFrameWidths[3]};
    uint32_t resH[6] = {cam.rgbFrameHeights[0], cam.rgbFrameHeights[1], cam.cvFrameHeights[0], cam.cvFrameHeights[1],
                        cam.cvFrameHeights[2], cam.cvFrameHeights[3]};
    float fps[6] = {cam.getRgbFps(0), cam.getRgbFps(1),
                    cam.getCvFps(0), cam.getCvFps(1), cam.getCvFps(2), cam.getCvFps(3)};

    // Build 6-line text
    std::string allText;
    for (int i = 0; i < 6; i++) {
        char buf[128];
        if (resW[i] > 0) {
            snprintf(buf, sizeof(buf), "%s %ux%u %.0ffps", names[i], resW[i], resH[i], fps[i]);
        } else {
            snprintf(buf, sizeof(buf), "%s %.0ffps", names[i], fps[i]);
        }
        if (i > 0) allText += '\n';
        allText += buf;
    }

    if (allText == cachedText && texture != 0) return;
    cachedText = allText;

    // Generate multi-line text texture
    const int scale = 2;
    int charW = 8 * scale;
    int charH = 8 * scale;
    int lineSpacing = 2 * scale;

    // Find max line width
    int maxLineLen = 0;
    int lineCount = 0;
    size_t pos = 0;
    while (pos < allText.length()) {
        size_t eol = allText.find('\n', pos);
        int len = (eol == std::string::npos) ? (int)(allText.length() - pos) : (int)(eol - pos);
        if (len > maxLineLen) maxLineLen = len;
        lineCount++;
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    if (lineCount == 0) return;

    uint32_t imgW = (uint32_t)(maxLineLen * charW + 8 * scale);  // padding each side
    uint32_t imgH = (uint32_t)(lineCount * (charH + lineSpacing) - lineSpacing + 4 * scale);

    std::vector<uint8_t> pixels(imgW * imgH * 4, 0);
    // Fill with semi-transparent black background
    for (uint32_t p = 0; p < imgW * imgH; p++) {
        pixels[p * 4 + 3] = 180;  // alpha
    }

    // Render each line using the bitmap font
    pos = 0;
    int lineIdx = 0;
    while (pos < allText.length()) {
        size_t eol = allText.find('\n', pos);
        std::string line = (eol == std::string::npos) ? allText.substr(pos) : allText.substr(pos, eol - pos);

        int baseX = 4 * scale;  // left padding
        int baseY = 2 * scale + lineIdx * (charH + lineSpacing);

        for (size_t ci = 0; ci < line.length(); ci++) {
            int ch = (unsigned char)line[ci];
            if (ch < 32 || ch > 127) ch = 32;
            const uint8_t* glyph = FONT8X8[ch - 32];
            int cx = baseX + (int)ci * charW;
            for (int gy = 0; gy < 8; gy++) {
                uint8_t row = glyph[gy];
                for (int gx = 0; gx < 8; gx++) {
                    if (row & (0x80 >> gx)) {
                        // Fill scale x scale block for each glyph pixel
                        for (int sy = 0; sy < scale; sy++) {
                            for (int sx = 0; sx < scale; sx++) {
                                int px = cx + gx * scale + sx;
                                int py = baseY + gy * scale + sy;
                                if (px < (int)imgW && py < (int)imgH) {
                                    int idx = (py * imgW + px) * 4;
                                    pixels[idx + 0] = 0;      // R
                                    pixels[idx + 1] = 255;    // G
                                    pixels[idx + 2] = 0;      // B
                                    pixels[idx + 3] = 255;    // A
                                }
                            }
                        }
                    }
                }
            }
        }

        lineIdx++;
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }

    // Upload texture
    if (texture) glDeleteTextures(1, &texture);
    glGenTextures(1, &texture);
    if (texture == 0) return;  // GL context lost
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_SRGB8_ALPHA8, imgW, imgH);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, imgW, imgH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    texWidth = imgW;
    texHeight = imgH;

    // Create geometry: wide flat quad below cameras
    // Flip V coords (0↔1) to counteract vertex shader's (1-y) flip
    float worldW = 2.5f;
    float worldH = worldW * (float)imgH / (float)imgW;
    if (geometry) delete geometry;

    VertexLayoutPos3Uv2 verts[4];
    uint32_t indices[6] = {0, 2, 1, 1, 2, 3};
    verts[0] = {{-worldW/2,  worldH/2, 0}, {0, 1}};
    verts[1] = {{ worldW/2,  worldH/2, 0}, {1, 1}};
    verts[2] = {{-worldW/2, -worldH/2, 0}, {0, 0}};
    verts[3] = {{ worldW/2, -worldH/2, 0}, {1, 0}};
    QtiGL::ProgramAttribute attribs[2] = {
        {QtiGL::kPosition,  3, GL_FLOAT, false, sizeof(VertexLayoutPos3Uv2), 0},
        {QtiGL::kTexcoord0, 2, GL_FLOAT, false, sizeof(VertexLayoutPos3Uv2), 12}
    };
    geometry = new QtiGL::Geometry();
    geometry->Initialize(attribs, 2, indices, 6, verts, 4 * sizeof(VertexLayoutPos3Uv2), 4);
    if (geometry->GetVaoId() == 0) {  // GL context lost during init
        delete geometry;
        geometry = nullptr;
        return;
    }
}

/**
 * Initializes OpenXR
 */
static int engine_init_openxr(struct engine *engine)
{
    AppCommon::app_query_layers_and_extensions(engine);

    XrInstanceCreateInfoAndroidKHR instanceCreateInfoAndroidKHR;
    instanceCreateInfoAndroidKHR.type =
            XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR;
    instanceCreateInfoAndroidKHR.next = nullptr;
    instanceCreateInfoAndroidKHR.applicationVM =
            (void *)engine->app->activity->vm;
    instanceCreateInfoAndroidKHR.applicationActivity =
            (void *)engine->app->activity->clazz;

    // TODO: should not hard-code these ideally
    const char *const enabledExtensions[] = {
            XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
            XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME,
            XR_EXT_HAND_TRACKING_EXTENSION_NAME,
            XR_MSFT_HAND_TRACKING_MESH_EXTENSION_NAME,
            XR_QCOM_HAND_TRACKING_GESTURE_EXTENSION_NAME,
            "XR_EXT_hand_interaction",
            "XR_EXT_palm_pose",
            "XR_MSFT_hand_interaction"};
    std::vector<const char*> enabledApiLayerNames;
    enabledApiLayerNames.push_back("XR_APILAYER_QCOM_retina_tracking");
    enabledApiLayerNames.push_back("XR_APILAYER_QCOM_handtracking");
    XrInstanceCreateInfo instanceCreateInfo = {
            .type = XR_TYPE_INSTANCE_CREATE_INFO,
            .next = &instanceCreateInfoAndroidKHR,
            .createFlags = 0,
            .applicationInfo =
                    {
                            .applicationName = "OpenXR MSAA Sample",
                            .engineName = "",
                            .applicationVersion = 1,
                            .engineVersion = 0,
                            .apiVersion = XR_CURRENT_API_VERSION,
                    },
            .enabledApiLayerCount = static_cast<uint32_t>(enabledApiLayerNames.size()),
            .enabledApiLayerNames = enabledApiLayerNames.data(),
            .enabledExtensionCount =
                    sizeof(enabledExtensions) / sizeof(*enabledExtensions),
            .enabledExtensionNames = enabledExtensions,
    };
    AppCommon::app_create_instance(&instanceCreateInfo, engine);

    AppCommon::app_get_system_prop(engine);

    AppCommon::app_enum_view_configuration(engine);
    engine->width = engine->state.viewConfigs[0].recommendedImageRectWidth;
    engine->height =
            engine->state.viewConfigs[0].recommendedImageRectHeight;

    // Create XR session
    assert(!engine->state.xrSession);
    XrGraphicsBindingOpenGLESAndroidKHR gfxBinding = {
            .type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
            .next = nullptr,
            .display = engine->display,
            .config = engine->config,
            .context = engine->context};
    XrSessionCreateInfo createInfo = {.type = XR_TYPE_SESSION_CREATE_INFO,
                                      .next = &gfxBinding,
                                      .systemId = engine->state.xrSysId};
    AppCommon::app_create_session(&createInfo, engine);

    // initialize space
    XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{
            XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    referenceSpaceCreateInfo.poseInReferenceSpace = idPose;
    referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    AppCommon::app_create_space(&referenceSpaceCreateInfo, engine);

    XrReferenceSpaceCreateInfo viewSpaceInfo{
            XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    viewSpaceInfo.poseInReferenceSpace = idPose;
    viewSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    AppCommon::app_create_space(&viewSpaceInfo, engine);

    // Create RootSpace for hand tracking
    PFN_xrCreateRootSpaceQCOM createRootSpaceQCOM{};
    XrResult result = xrGetInstanceProcAddr(engine->state.xrInstance,"xrCreateRootSpaceQCOM",reinterpret_cast<PFN_xrVoidFunction*>(&createRootSpaceQCOM));
    if (result == XR_SUCCESS) {
        XrRootSpaceCreateInfoQCOM rootSpaceCreateInfo{};
        rootSpaceCreateInfo.poseInSpace.orientation.w = 1.0f;
        rootSpaceCreateInfo.poseInSpace.position.x = 0.0f;
        rootSpaceCreateInfo.poseInSpace.position.y = 0.0f;
        rootSpaceCreateInfo.poseInSpace.position.z = 0.0f;
        rootSpaceCreateInfo.poseInSpace.orientation.x = 0.0f;
        rootSpaceCreateInfo.poseInSpace.orientation.y = 0.0f;
        rootSpaceCreateInfo.poseInSpace.orientation.z = 0.0f;
        result = createRootSpaceQCOM(engine->state.xrSession, &rootSpaceCreateInfo, &engine->state.xrRootSpace);
        if (result == XR_SUCCESS) {
            engine->useRootSpace = true;
            LOGI("createRootSpaceQCOM succeed, useRootSpace=true");
        }
    }

    engine->inputPtr = std::make_unique<Input>();
    engine->inputPtr->Init(engine->state.xrInstance, engine->state.xrSession,
                           engine->useRootSpace ? engine->state.xrRootSpace : engine->state.xrLocalSpace);

    return engine_init_xr_swapchains(engine);

}

/**
 * Shutdown OpenXR
 */
static void engine_shutdown_openxr(struct engine *engine)
{
    XrResult result = AppCommon::app_destroy_space(engine->state.xrLocalSpace);
    if (XR_SUCCESS != result) {
        LOGW("Destroy XR local space failed: %d", result);
        assert(0);
    }

    result = AppCommon::app_destroy_space(engine->state.xrViewSpace);
    if (XR_SUCCESS != result) {
        LOGW("Destroy XR view space failed: %d", result);
        assert(0);
    }

    result = AppCommon::app_destroy_session(engine->state.xrSession);
    if (XR_SUCCESS != result) {
        LOGW("Destroy XR session failed: %d", result);
        assert(0);
    }

    result = AppCommon::app_destroy_instance(engine);
    if (XR_SUCCESS != result) {
        LOGW("Destroy XR instance failed: %d", result);
        assert(0);
    }
}

/**
 * Create XR swapchains
 */
static int engine_init_xr_swapchains(struct engine *engine) {
    AppCommon::app_enum_sc_format(engine);

    // Looking for multisample extension
    PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEIMGPROC
            glFramebufferTexture2DMultisampleEXT = nullptr;
    glFramebufferTexture2DMultisampleEXT =
            (PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEIMGPROC) eglGetProcAddress(
                    "glFramebufferTexture2DMultisampleEXT");
    if (!glFramebufferTexture2DMultisampleEXT) {
        LOGW("Couldn't get function pointer to "
             "glFramebufferTexture2DMultisampleEXT()!");
        assert(0);
        return 1;
    }

    PFNGLRENDERBUFFERSTORAGEMULTISAMPLEIMGPROC
            glRenderbufferStorageMultisampleEXT =
            (PFNGLRENDERBUFFERSTORAGEMULTISAMPLEIMGPROC)
                    eglGetProcAddress(
                            "glRenderbufferStorageMultisampleEXT");
    if (!glRenderbufferStorageMultisampleEXT) {
        LOGE("Couldn't get function pointer to "
             "glRenderbufferStorageMultisampleEXT()!");
        return false;
    }

    // Create swapchain with different sample count and cached in map
    uint32_t samples = engine->currentSampleCount;
    {
        StereoSwapchain stereoSwapchain;
        stereoSwapchain.eyeSwapchain.resize(engine->state.viewCount);
        std::vector<uint32_t> swapchainLengths;
        swapchainLengths.resize(engine->state.viewCount);
        uint32_t maxSwapchainLength = 0;

        for (uint32_t eye = 0; eye < engine->state.viewCount; ++eye) {
            auto &swapchain = stereoSwapchain.eyeSwapchain[eye];
            XrSwapchainCreateInfo swapchainCreateInfo = {
                    .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                    .usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                                  XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                    .createFlags = 0,
                    .format = GL_SRGB8_ALPHA8,
                    .sampleCount = samples,
                    .width = engine->width,
                    .height = engine->height,
                    .faceCount = 1,
                    .arraySize = 1,
                    .mipCount = 1,
                    .next = nullptr,
            };

            XrResult result = xrCreateSwapchain(engine->state.xrSession,
                                                &swapchainCreateInfo,
                                                &swapchain.xrSwapchain);
            if (XR_FAILED(result)) {
                LOGW("xrCreateSwapchain failed");
                assert(0);
                return 1;
            }

            result = xrEnumerateSwapchainImages(
                    swapchain.xrSwapchain, 0, &swapchainLengths[eye], nullptr);
            if (XR_FAILED(result)) {
                LOGW("xrEnumerateSwapchainImages failed");
                assert(0);
                return 1;
            }

            if (swapchainLengths[eye] > maxSwapchainLength)
                maxSwapchainLength = swapchainLengths[eye];

            swapchain.xrImages.resize(swapchainLengths[eye],
                                      {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
            swapchain.fbos.resize(swapchainLengths[eye]);
            swapchain.dbos.resize(swapchainLengths[eye]);

            result = xrEnumerateSwapchainImages(
                    swapchain.xrSwapchain, swapchainLengths[eye],
                    &swapchainLengths[eye],
                    (XrSwapchainImageBaseHeader *) &swapchain.xrImages[0]);

            if (XR_SUCCESS != result) {
                LOGW("xrEnumerateSwapchainImages failed");
                assert(0);
                return 1;
            }

            for (uint32_t index = 0; index < swapchainLengths[eye]; ++index) {
                // Create depth buffer
                GL(glGenRenderbuffers(1, &swapchain.dbos[index]));
                GL(glBindRenderbuffer(GL_RENDERBUFFER, swapchain.dbos[index]));
                GL(glRenderbufferStorageMultisampleEXT(
                        GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT16,
                        engine->width, engine->height));
                GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

                // Create MSAA fbo
                GL(glGenFramebuffers(1, &swapchain.fbos[index]));
                GL(glBindFramebuffer(GL_FRAMEBUFFER, swapchain.fbos[index]));
                GL(glFramebufferRenderbuffer(
                        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                        swapchain.dbos[index]));
                LOGI("glFramebufferTexture2DMultisampleEXT index:%d sample: %d",
                     index, samples);
                GL(glFramebufferTexture2DMultisampleEXT(
                        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                        swapchain.xrImages[index].image, 0, samples));

                GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                // check frame buffer status
                if (status != GL_FRAMEBUFFER_COMPLETE) {
                    LOGE("framebuffer is incomplete, status: %d! Error code %d",
                         status, glGetError());
                    assert(0);
                    return 1;
                }
            }
        }

        LOGI("Insert to table, sample :%d", samples);
        engine->swapchainMap[samples] = std::move(stereoSwapchain);
    }

// Projection
    {
        XrSwapchainCreateInfo swapchainCreateInfo = {
                .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                .usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                              XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                .createFlags = 0,
                .format = GL_SRGB8_ALPHA8,//GL_SRGB8_ALPHA8,//GL_RGBA8,
                .sampleCount = 1,
                .width = engine->quadImageWidth,
                .height = engine->quadImageHeight,
                .faceCount = 1,
                .arraySize = 1,
                .mipCount = 1,
                .next = nullptr,
        };

        AppCommon::app_create_swapchain(
                &swapchainCreateInfo, &engine->state.xrSession, &engine->quadSwapchain);

        AppCommon::app_create_swapchain(
                &swapchainCreateInfo, &engine->state.xrSession, &engine->quadSwapchain2);
    }
    GL();
    // Init depth buffer
    glGenTextures(1, &engine->depthBuffer);
    glBindTexture(GL_TEXTURE_2D, engine->depthBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16,
                 engine->quadImageWidth,
                 engine->quadImageHeight, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (engine->depthBuffer == 0) {
        LOGE("Failed to init depth buffer");
        assert(0);
    }
    GL();
    LOGI("render_gles,Init depth buffer");
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_PROTECTED_EXT, GL_TRUE);
    return 0;
}

/**
 * Destroy XR swapchains
 */
static int engine_destroy_xr_swapchains(struct engine *engine)
{
    for (auto it = engine->swapchainMap.begin();
         it != engine->swapchainMap.end(); ++it) {
        for (auto &swapchain : it->second.eyeSwapchain) {
            GL(glDeleteFramebuffers(swapchain.fbos.size(),
                                    swapchain.fbos.data()));
            GL(glDeleteRenderbuffers(swapchain.dbos.size(),
                                     swapchain.dbos.data()));
            if (XR_FAILED(xrDestroySwapchain(swapchain.xrSwapchain))) {
                LOGW("xrDestroySwapchain failed");
                assert(0);
                return 1;
            }
        }
    }
    engine->swapchainMap.clear();

    return 0;
}

/**
 * Initialize an EGL context for the current display.
 */
static int engine_init_display(struct engine *engine)
{
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLint numConfig;

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(display != EGL_NO_DISPLAY);

    EGLBoolean res = eglInitialize(display, nullptr, nullptr);
    assert(res == EGL_TRUE);

    EGLint const configAttrs[] = {
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            // This is tricky, you can't use glGetIntegerv until egl context was
            // created. Maybe create a temp context and get max sample count
            // first.
            EGL_SAMPLES, EGL_SAMPLE_COUNT, EGL_NONE};

    res = eglChooseConfig(display, configAttrs, &config, 1, &numConfig);
    LOGI("numConfig: %d", numConfig);
    assert(res == EGL_TRUE);

    EGLint const contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3,
                                     EGL_PROTECTED_CONTENT_EXT, false,
                                     EGL_NONE};
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    assert(context != EGL_NO_CONTEXT);

    res = eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
    assert(res == EGL_TRUE);

    engine->config = config;
    engine->context = context;
    engine->display = display;

    return 0;
}
uint32_t GetDepthTexture(uint32_t colorTexture,int TextureWidth,int TextureHeight)
{
    // If a depth-stencil view has already been created for this back-buffer, use it.
    auto depthBufferIt = m_colorToDepthMap.find(colorTexture);
    if (depthBufferIt != m_colorToDepthMap.end())
    {
        return depthBufferIt->second;
    }
    GLuint glType = GL_TEXTURE_2D;
    GL(glBindTexture(glType, colorTexture));

    uint32_t depthTexture;
    GL(glGenTextures(1, &depthTexture));
    GL(glBindTexture(glType, depthTexture));
    GL(glTexParameteri(glType, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    GL(glTexParameteri(glType, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    GL(glTexParameteri(glType, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL(glTexParameteri(glType, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    if (glType == GL_TEXTURE_2D_ARRAY)
    {
        GL(glTexStorage3D(glType, 1, GL_DEPTH_COMPONENT24, TextureWidth, TextureHeight, 2));
    }
    else
    {
        GL(glTexImage2D(glType, 0, GL_DEPTH_COMPONENT24, TextureWidth, TextureHeight, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr));
    }

    m_colorToDepthMap.insert(std::make_pair(colorTexture, depthTexture));
    return depthTexture;
}

// Upload CV camera textures to GPU (only when new frames arrive)
static void ensureCvTextures(struct engine *engine) {
    // Textures are now uploaded directly by GPU in handleCVFrame callback.
    // No CPU-side upload needed. Just check if textures are ready.
    auto& cam = engine->mCameraAccessExtension;
    (void)cam;
}

// Composite all camera frames (4 grayscale) into a single texture
static void compositeAllCameras(struct engine *engine, uint32_t targetTexture) {
    auto& cam = engine->mCameraAccessExtension;

    // Check if tracking cameras have frames available
    bool hasTracking = cam.trackingFrameReady;
    bool hasCtrl = cam.ctrlFrameReady;

    if (!hasTracking && !hasCtrl) {
        // No camera frames available yet, clear with black
        glBindTexture(GL_TEXTURE_2D, targetTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, engine->quadImageWidth, engine->quadImageHeight,
                       GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }

    // Allocate buffer for composite texture
    uint32_t* compositeData = (uint32_t*)malloc(engine->quadImageWidth * engine->quadImageHeight * sizeof(uint32_t));
    if (!compositeData) {
        LOGE("Failed to allocate composite buffer");
        return;
    }
    memset(compositeData, 0, engine->quadImageWidth * engine->quadImageHeight * sizeof(uint32_t));

    // Create a 2x2 grid layout for cameras:
    // +------------------+------------------+
    // |   GRAY_LEFT      |  GRAY_RIGHT      |
    // |      (0)         |       (1)        |
    // +------------------+------------------+
    // |   GRAY_LEFT_UP   |  GRAY_RIGHT_UP   |
    // |      (2)         |       (3)        |
    // +------------------+------------------+

    int gridCols = 2;
    int gridRows = 2;
    int cellWidth = engine->quadImageWidth / gridCols;
    int cellHeight = engine->quadImageHeight / gridRows;

    // Helper lambda to copy tracking frame to grid position
    auto copyTrackingToGrid = [&](const CameraAccessExtension::TrackingFrameData& frame,
                                   int gridCol, int gridRow) {
        if (frame.pixelData.empty()) return;

        uint32_t camWidth = frame.frameInfo.width;
        uint32_t camHeight = frame.frameInfo.height;
        if (camWidth == 0 || camHeight == 0) return;

        int startX = gridCol * cellWidth;
        int startY = gridRow * cellHeight;

        for (int y = 0; y < cellHeight && y < (int)camHeight; y++) {
            for (int x = 0; x < cellWidth && x < (int)camWidth; x++) {
                int srcX = x * camWidth / cellWidth;
                int srcY = y * camHeight / cellHeight;
                uint8_t pix = frame.pixelData[srcY * camWidth + srcX];
                uint32_t rgba = (0xFF000000 | (pix<<16) | (pix<<8) | pix);
                int destX = startX + x;
                int destY = startY + (cellHeight - 1 - y); // Flip Y
                compositeData[destY * engine->quadImageWidth + destX] = rgba;
            }
        }
    };

    // Copy tracking frames (GRAY_LEFT=0, GRAY_RIGHT=1)
    if (hasTracking) {
        std::lock_guard<std::mutex> lock(cam.trackingFrameMutex);
        copyTrackingToGrid(cam.trackingFrames[0], 0, 0);  // GRAY_LEFT - top left
        copyTrackingToGrid(cam.trackingFrames[1], 1, 0);  // GRAY_RIGHT - top right
    }

    // Copy ctrl frames (GRAY_LEFT_UP=2, GRAY_RIGHT_UP=3)
    if (hasCtrl) {
        std::lock_guard<std::mutex> lock(cam.trackingFrameMutex);
        copyTrackingToGrid(cam.ctrlFrames[0], 0, 1);  // GRAY_LEFT_UP - bottom left
        copyTrackingToGrid(cam.ctrlFrames[1], 1, 1);  // GRAY_RIGHT_UP - bottom right
    }

    // Upload composite texture
    glBindTexture(GL_TEXTURE_2D, targetTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, engine->quadImageWidth, engine->quadImageHeight,
                   GL_RGBA, GL_UNSIGNED_BYTE, compositeData);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOGE("GL Error uploading composite texture: 0x%x", err);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    free(compositeData);
}

static void draw(struct engine *engine,uint32_t imgIndex){
    GL(glBindFramebuffer(GL_FRAMEBUFFER,
                      engine->quadSwapchain.glFramebuffers[imgIndex]));
    uint32_t colorTexture = engine->quadSwapchain.xrImages[imgIndex].image;
    GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                              colorTexture, 0));
    auto depthBufferIt = GetDepthTexture(colorTexture,engine->quadImageWidth,engine->quadImageHeight);
    GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                              depthBufferIt, 0));

    GLint scissor[4];
    scissor[0] = 0;
    scissor[1] = 0;
    scissor[2] = engine->quadImageWidth;
    scissor[3] = engine->quadImageHeight;
    GL(glEnable(GL_SCISSOR_TEST));
    GL(glViewport(0, 0, engine->quadImageWidth, engine->quadImageHeight));
    GL(glScissor(scissor[0], scissor[1], scissor[2], scissor[3]));
    GL(glClearColor(0.f, 0.f, 0.f, 0.f));
    GL(glClear(GL_COLOR_BUFFER_BIT| GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));
    GL(glDisable(GL_SCISSOR_TEST));
    GL(glFrontFace(GL_CW));
    GL(glEnable(GL_CULL_FACE));
    GL(glCullFace(GL_FRONT));
    GL(glEnable(GL_DEPTH_TEST));

    mNotificationShader->Bind();
    // LOGI("render_gles,srcTex:%d",engine->testTexture);
    mNotificationShader->SetUniformSampler("srcTex", engine->testTexture, GL_TEXTURE_2D, 0);
    mNotificationMesh.Submit();
    mNotificationShader->Unbind();
    GL();
    GL(glBindFramebuffer(GL_FRAMEBUFFER,0));
}
static void  engine_draw_layer(struct engine *engine){
    XrSwapchainImageAcquireInfo swapchainImageAcquireInfo = {
            .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, .next = nullptr};
    uint32_t bufferIndex;
    XrResult result = xrAcquireSwapchainImage(engine->quadSwapchain.xrSwapchain,
                                              &swapchainImageAcquireInfo, &bufferIndex);

    if (XR_SUCCESS != result) {
        LOGW("xrAcquireSwapchainImage failed, %d", result);
    }

    XrSwapchainImageWaitInfo swapchainImageWaitInfo = {
            .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
            .next = nullptr,
            .timeout = 1000};
    result = xrWaitSwapchainImage(engine->quadSwapchain.xrSwapchain,
                                  &swapchainImageWaitInfo);

    if (XR_SUCCESS != result) {
        LOGW("xrWaitSwapchainImage failed %d", result);
    }

    // Composite all camera frames to the quad swapchain
    uint32_t colorTexture = engine->quadSwapchain.xrImages[bufferIndex].image;
    compositeAllCameras(engine, colorTexture);

    XrSwapchainImageReleaseInfo swapchainImageReleaseInfo = {
            .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, .next = nullptr};
    result = xrReleaseSwapchainImage(engine->quadSwapchain.xrSwapchain,
                                     &swapchainImageReleaseInfo);

    if (XR_SUCCESS != result) {
        LOGW("xrReleaseSwapchainImage failed %d", result);
    }
}
static void  engine_draw_layer2(struct engine *engine){
    XrSwapchainImageAcquireInfo swapchainImageAcquireInfo = {
            .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, .next = nullptr};
    uint32_t bufferIndex;
    XrResult result = xrAcquireSwapchainImage(engine->quadSwapchain2.xrSwapchain,
                                              &swapchainImageAcquireInfo, &bufferIndex);

    if (XR_SUCCESS != result) {
        LOGW("xrAcquireSwapchainImage failed, %d", result);
    }

    XrSwapchainImageWaitInfo swapchainImageWaitInfo = {
            .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
            .next = nullptr,
            .timeout = 1000};
    result = xrWaitSwapchainImage(engine->quadSwapchain2.xrSwapchain,
                                  &swapchainImageWaitInfo);

    if (XR_SUCCESS != result) {
        LOGW("xrWaitSwapchainImage failed %d", result);
    }

//    draw(engine,bufferIndex);
    uint32_t colorTexture = engine->quadSwapchain2.xrImages[bufferIndex].image;
//    GL_APICALL void GL_APIENTRY glCopyTexSubImage2D (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
    glCopyImageSubData(engine->testTexture,GL_TEXTURE_2D,0,0,0,0,colorTexture,GL_TEXTURE_2D,0,0,0,0,engine->quadImageWidth,
                       engine->quadImageHeight,1);

    XrSwapchainImageReleaseInfo swapchainImageReleaseInfo = {
            .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, .next = nullptr};
    result = xrReleaseSwapchainImage(engine->quadSwapchain2.xrSwapchain,
                                     &swapchainImageReleaseInfo);

    if (XR_SUCCESS != result) {
        LOGW("xrReleaseSwapchainImage failed %d", result);
    }
}
/**
 * Render scene.
 */
static void engine_draw_frame(struct engine *engine,
                              const uint32_t viewIndex,
                              const uint32_t imgIndex,
                              const XrView &xrView)
{
    assert(engine->display);
    auto &stereoSwapchain = engine->swapchainMap[engine->currentSampleCount];
    auto &swapchain = stereoSwapchain.eyeSwapchain[viewIndex];
    GL(glBindFramebuffer(GL_FRAMEBUFFER, swapchain.fbos[imgIndex]));

    GL(glEnable(GL_SCISSOR_TEST));
    GL(glEnable(GL_DEPTH_TEST));
    GL(glEnable(GL_CULL_FACE));
    GL(glDepthFunc(GL_LESS));
    GL(glDepthMask(GL_TRUE));

    GL(glViewport(0, 0, engine->width, engine->height));
    GL(glScissor(0, 0, engine->width, engine->height));
    GL(glClearColor(0.1f, 0.1f, 0.1f, 0.0f));
    GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    glm::mat4 eyeProjMat, eyeViewMat;

    XrMatrix4x4f result;
    XrMatrix4x4f_CreateProjectionFov(&result, GRAPHICS_OPENGL_ES, xrView.fov,
                                     0.05f, 100.f);
    AppCommon::array2matrix(result, eyeProjMat);
    glm::mat4 rot = glm::mat4_cast(
            glm::fquat(xrView.pose.orientation.w, xrView.pose.orientation.x,
                       xrView.pose.orientation.y, xrView.pose.orientation.z));
    glm::mat4 trans =
            glm::translate(glm::mat4(1.0f), glm::vec3(xrView.pose.position.x,
                                                      xrView.pose.position.y,
                                                      xrView.pose.position.z));
    eyeViewMat = trans * rot;
    eyeViewMat = glm::inverse(eyeViewMat);

    engine->cubeShader->Bind();
    engine->cubeShader->SetUniformMat4("projectionMatrix", eyeProjMat);
    engine->cubeShader->SetUniformMat4("viewMatrix", eyeViewMat);
//    if(!engine->mCameraAccessExtension.frames.empty()){
//        engine->cubeShader->SetUniformSampler("srcTex", engine->mCameraAccessExtension.frames[0].shareTexture->getBindTexture(),
//                                              GL_TEXTURE_2D, 0);
//    }
//    engine->cubeShader->SetUniformSampler("srcTex", engine->testTexture,
//                                          GL_TEXTURE_2D, 0);
    glm::vec3 eyePos =
            glm::vec3(-eyeViewMat[3][0], -eyeViewMat[3][1], -eyeViewMat[3][2]);
    engine->cubeShader->SetUniformVec3("eyePos", eyePos);

    // Render cube scene
//    for (size_t i = 0; i < engine->cubeMatrices.size(); ++i) {
//        engine->cubeShader->SetUniformMat4("modelMatrix",
//                                           engine->cubeMatrices[i]);
//        engine->cubeShader->SetUniformVec3("modelColor", engine->cubeColors[i]);
//        engine->cube.Submit();
//    }

    glm::vec3 color = glm::vec3(1.0f,1.0f,1.0f);
    engine->cubeShader->SetUniformVec3("modelColor", color);

    // Controller model rendering (simple cube)
    if (engine->useControllerMode && engine->inputPtr) {
        glm::vec3 leftColor = glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 rightColor = glm::vec3(1.0f, 0.0f, 0.0f);
        if (engine->inputPtr->IsControllerActive(0)) {
            auto& p = engine->inputPtr->mControllerPose[0];
            glm::mat4 rotMat = glm::mat4_cast(glm::fquat(
                p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z));
            glm::mat4 transMat = glm::translate(glm::vec3(
                p.position.x, p.position.y, p.position.z));
            glm::mat4 modelMat = transMat * rotMat * glm::scale(glm::vec3(0.05f));
            engine->cubeShader->SetUniformVec3("modelColor", leftColor);
            engine->cubeShader->SetUniformMat4("modelMatrix", modelMat);
            engine->cube.Submit();
        }
        if (engine->inputPtr->IsControllerActive(1)) {
            auto& p = engine->inputPtr->mControllerPose[1];
            glm::mat4 rotMat = glm::mat4_cast(glm::fquat(
                p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z));
            glm::mat4 transMat = glm::translate(glm::vec3(
                p.position.x, p.position.y, p.position.z));
            glm::mat4 modelMat = transMat * rotMat * glm::scale(glm::vec3(0.05f));
            engine->cubeShader->SetUniformVec3("modelColor", rightColor);
            engine->cubeShader->SetUniformMat4("modelMatrix", modelMat);
            engine->cube.Submit();
        }
    }

    // Render RGB camera frames (use display textures updated by callback thread)
    engine->cubeShader->SetUniform1i("useTexture", 1);
    engine->cubeShader->SetUniformVec3("modelColor", color);
    if(engine->mCameraAccessExtension.rgbFrameReady &&
       engine->mCameraAccessExtension.rgbDisplayTextures[0] != 0 &&
       engine->mCameraAccessExtension.rgbDisplayTextures[1] != 0){
        //left
        {
            glm::mat4 matleft = glm::translate(glm::vec3(-1.0f, .0f, -5))
                                * glm::scale(glm::vec3(2.f));
            engine->cubeShader->SetUniformMat4("modelMatrix", matleft);
            engine->cubeShader->SetUniformSampler("srcTex",
                                                  engine->mCameraAccessExtension.rgbDisplayTextures[0],
                                                  GL_TEXTURE_2D, 0);
            mNotificationMesh.Submit();
        }
        //right
        {
            glm::mat4 matright = glm::translate(glm::vec3(1.0f, .0f, -5))
                                 * glm::scale(glm::vec3(2.f));
            engine->cubeShader->SetUniformMat4("modelMatrix", matright);
            engine->cubeShader->SetUniformSampler("srcTex",
                                                  engine->mCameraAccessExtension.rgbDisplayTextures[1],
                                                  GL_TEXTURE_2D, 0);
            mNotificationMesh.Submit();
        }
    }


    // Render CV camera frames in 4 corners at same depth as RGB
    // Update CV display textures from pixel data
    ensureCvTextures(engine);
    engine->cubeShader->SetUniform1i("useTexture", 1);  // RGB texture mode (grayscale expanded to RGBA)
    {
        // Corner positions for CV cameras (z=-5, same as RGB)
        // CV-TL: top-left, CV-TR: top-right, CV-BL: bottom-left, CV-BR: bottom-right
        struct CvCamPos {
            glm::vec3 pos;
            float scale;
            const char* name;
        };
        // CV cameras on left/right of RGB, stacked vertically, aligned with RGB height
        // CV scale_y=1.0, scale_x=1.33 (4:3 ratio), two stacked = height 2.0 = RGB height
        CvCamPos cvCams[4] = {
            {glm::vec3(-2.67f, -0.5f, -5), 1.0f, "CV-TL"},  // left-bottom
            {glm::vec3( 2.67f, -0.5f, -5), 1.0f, "CV-TR"},  // right-bottom
            {glm::vec3(-2.67f,  0.5f, -5), 1.0f, "CV-BL"},  // left-top
            {glm::vec3( 2.67f,  0.5f, -5), 1.0f, "CV-BR"},  // right-top
        };
        // Texture index: direct mapping [TL, TR, BL, BR]
        static const int texMap[4] = {0, 1, 2, 3};
        for (int i = 0; i < 4; i++) {
            int ti = texMap[i];
            if (engine->mCameraAccessExtension.cvDisplayTextures[ti] == 0) continue;
            // Use actual aspect ratio (640x480 = 4:3) for non-uniform scaling
            uint32_t cw = engine->mCameraAccessExtension.cvFrameWidths[ti];
            uint32_t ch = engine->mCameraAccessExtension.cvFrameHeights[ti];
            float aspect = (cw > 0 && ch > 0) ? (float)cw / (float)ch : 1.0f;
            float sx = cvCams[i].scale * aspect;  // wider for 4:3
            float sy = cvCams[i].scale;
            glm::mat4 mat = glm::translate(cvCams[i].pos)
                          * glm::scale(glm::vec3(sx, sy, 1.0f));
            engine->cubeShader->SetUniformMat4("modelMatrix", mat);
            engine->cubeShader->SetUniformSampler("srcTex",
                                                  engine->mCameraAccessExtension.cvDisplayTextures[ti],
                                                  GL_TEXTURE_2D, 0);
            mNotificationMesh.Submit();
        }
    }
//    mNotificationMesh.Submit()

    // Render camera info panel below cameras
    engine->cubeShader->SetUniform1i("useTexture", 1);
    gInfoPanel.update(engine);
    gInfoPanel.render(engine->cubeShader);

    engine->cubeShader->Unbind();
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

//    engine_draw_layer(engine);
//    engine_draw_layer2(engine);
}

static std::string read_text_file(const std::string &file)
{
    std::stringstream ss;
    std::ifstream ifs(file);
    if (ifs) {
        ss << ifs.rdbuf();
    } else {
        LOGW("Read %s failed", file.c_str());
    }

    return ss.str();
}

static void engine_create_cube(QtiGL::Geometry &geometry, float width)
{
    // Create attributes of the cube
    unsigned int numElementsPerVert = 8;
    int stride = (int)(numElementsPerVert * sizeof(float));

    std::vector<QtiGL::ProgramAttribute> attribs = {
            {QtiGL::kPosition, 3, GL_FLOAT, false, stride, 0},
            {QtiGL::kNormal, 3, GL_FLOAT, false, stride, 3 * sizeof(float)},
            {QtiGL::kTexcoord0, 2, GL_FLOAT, false, stride, 6 * sizeof(float)}};

    float halfWidth = width / 2.0f;

    // Create vertex data of the cube with position and normal data
    float cubeVerts[] = {
            // Front
            halfWidth, halfWidth, halfWidth, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
            -halfWidth, halfWidth, halfWidth, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
            -halfWidth, -halfWidth, halfWidth, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            halfWidth, -halfWidth, halfWidth, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
            // Right
            halfWidth, halfWidth, halfWidth, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            halfWidth, -halfWidth, halfWidth, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            halfWidth, -halfWidth, -halfWidth, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            halfWidth, halfWidth, -halfWidth, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
            // Top
            halfWidth, halfWidth, halfWidth, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
            halfWidth, halfWidth, -halfWidth, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
            -halfWidth, halfWidth, -halfWidth, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            -halfWidth, halfWidth, halfWidth, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
            // Left
            -halfWidth, halfWidth, halfWidth, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
            -halfWidth, halfWidth, -halfWidth, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            -halfWidth, -halfWidth, -halfWidth, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            -halfWidth, -halfWidth, halfWidth, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            // Bottom
            -halfWidth, -halfWidth, -halfWidth, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
            halfWidth, -halfWidth, -halfWidth, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
            halfWidth, -halfWidth, halfWidth, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,
            -halfWidth, -halfWidth, halfWidth, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,
            // Back
            halfWidth, -halfWidth, -halfWidth, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
            -halfWidth, -halfWidth, -halfWidth, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f,
            -halfWidth, halfWidth, -halfWidth, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,
            halfWidth, halfWidth, -halfWidth, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f

    };
    int numCubeVerts = 24;

    // Create index data of the cube
    unsigned int cubeIndices[] = {// Front
                                  0, 1, 2, 2, 3, 0,
                                  // Right
                                  4, 5, 6, 6, 7, 4,
                                  // Top
                                  8, 9, 10, 10, 11, 8,
                                  // Left
                                  12, 13, 14, 14, 15, 12,
                                  // Bottom
                                  16, 17, 18, 18, 19, 16,
                                  // Back
                                  20, 21, 22, 22, 23, 20

    };
    int numCubeIndices = 36;

    geometry.Initialize(attribs.data(), attribs.size(), cubeIndices,
                        numCubeIndices, cubeVerts,
                        numCubeVerts * numElementsPerVert * sizeof(float),
                        numCubeVerts);
}
void glInit() {
    VertexLayoutPos3Uv2 verts[4];

    uint32_t quadIndices[6] = {0, 2, 1, 1, 2, 3};
    int32_t indexCount = sizeof(quadIndices) / sizeof(uint32_t);

    verts[0].position[0] = -0.5f;
    verts[0].position[1] = 0.5f;
    verts[0].position[2] = 0.f;
    verts[0].texCoord[0] = 0.f;
    verts[0].texCoord[1] = 0.f;

    verts[1].position[0] = 0.5f;
    verts[1].position[1] = 0.5f;
    verts[1].position[2] = 0.f;
    verts[1].texCoord[0] = 1.f;
    verts[1].texCoord[1] = 0.f;

    verts[2].position[0] = -0.5f;
    verts[2].position[1] = -0.5f;
    verts[2].position[2] = 0.f;
    verts[2].texCoord[0] = 0.f;
    verts[2].texCoord[1] = 1.f;

    verts[3].position[0] = 0.5f;
    verts[3].position[1] = -0.5f;
    verts[3].position[2] = 0.f;
    verts[3].texCoord[0] = 1.f;
    verts[3].texCoord[1] = 1.f;

    QtiGL::ProgramAttribute MeshAttribsPos3Uv2[2] =
            {
                    //  Index               Size    Type        Normalized      Stride                      Offset
                    {QtiGL::kPosition,  3, GL_FLOAT, false, sizeof(VertexLayoutPos3Uv2), 0},
                    {QtiGL::kTexcoord0, 2, GL_FLOAT, false, sizeof(VertexLayoutPos3Uv2), 12}
            };

    mNotificationMesh.Initialize(
            &MeshAttribsPos3Uv2[0], 2,
            &quadIndices[0], indexCount,
            &verts[0], 4 * sizeof(VertexLayoutPos3Uv2), 4);


    const char *vNotiShaderGlsl = R"_(#version 320 es
    precision highp float;
    precision mediump int;
    layout (location = 0) in vec3 position;
    layout (location = 3) in vec2 texcoord0;
    out vec3 PSVertexColor;
    out vec2 v_Coords;
    void main() {
       gl_Position = vec4(position.x,position.y,0, 1.0);
       v_Coords = texcoord0.xy;
    }
    )_";


    const char *fNotiShaderGlsl = R"_(#version 320 es
    precision highp float;
    precision mediump int;
    uniform sampler2D srcTex;
    in vec2 v_Coords;
    out vec4 FragColor;
    void main() {
       	FragColor = texture(srcTex,v_Coords);
//        FragColor = vec4(v_Coords.x,v_Coords.y,0, 1);
    }
    )_";

    mNotificationShader = new QtiGL::Shader;
    mNotificationShader->Initialize(1, &vNotiShaderGlsl,
                                    1, &fNotiShaderGlsl,
                                    "VS", "FS");

    char texFilePath[512];
    const char *pModelTexFile = "white.ktx";
    sprintf(texFilePath, "%s/%s", storagePath,
            pModelTexFile);

    GLenum texTarget;
    QtiGL::KtxTexture texHelper;
    int texBuffSize;
    char *pTexBuffer =
            (char *) AppCommon::get_file_buffer(texFilePath, &texBuffSize);
    if (pTexBuffer == nullptr) {
        return;
    }

    QtiGL::TKTXHeader *pOutHeader = nullptr;
    QtiGL::TKTXErrorCode resultCode = texHelper.LoadKtxFromBuffer(
            pTexBuffer, texBuffSize, &quadTexture, &texTarget,
            pOutHeader, false);

    if (resultCode != QtiGL::KTX_SUCCESS || 0 == quadTexture) {
        free(pTexBuffer);
        return;
    }

    quadTextureWidth = texHelper.GetWidht();
    quadTextureHeight = texHelper.GetHeight();

    free(pTexBuffer);

    LOGW("render_gles::Init over");
}
// type = 0 不重复纹理 1重复纹理
GLuint GetTexutre(int width, int height,int nrComponents, void *pTexUint,int type = 0)
{
    XR_DEBUG("pTexUint %p", pTexUint);
    LOGE("nrComponents:%d",nrComponents);
    if (pTexUint == NULL)
            XR_DEBUG("texture pTexUint is NULL");
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    unsigned int texture = -1;
    GL(glGenTextures(1, &texture));
    GL(glActiveTexture(GL_TEXTURE0));
    GL(glBindTexture(GL_TEXTURE_2D, texture));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_GREEN));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_BLUE));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ALPHA));
//    if(type==0)
//    {
//        //不重复
//        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//    }
//    else
//    {
//        //重复纹理
//        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
//        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
//    }
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0));


    GLenum format = GL_RGBA;
    if (nrComponents == 1)
    {
        format = GL_RED;
    }
    else if (nrComponents == 3)
    {
        format = GL_RGB;
    }
    else if (nrComponents == 4)
    {
        format = GL_RGBA;
    }
//    GL(glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pTexUint));
    GL(glTexStorage2D(GL_TEXTURE_2D,1,GL_SRGB8_ALPHA8,width,height));
    GL(glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pTexUint));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
    return texture;
}
/**
 * Init resources for rendering scene
 */
static int engine_init_scene_resources(struct engine *engine)
{
    std::string externalDir =
            std::string(engine->app->activity->externalDataPath);

    // load shader
    {
        std::string vsFilePath = externalDir + "/model_v.glsl";
        std::string fsFilePath = externalDir + "/model_f.glsl";
        // load shader sources
        std::string vsSource = read_text_file(vsFilePath);
        if (vsSource.length() <= 0) {
            return 1;
        }

        std::string fsSource = read_text_file(fsFilePath);
        if (fsSource.length() <= 0) {
            return 1;
        }

        engine->cubeShader = new QtiGL::Shader();
        std::vector<const char *> vs = {vsSource.c_str()};
        std::vector<const char *> fs = {fsSource.c_str()};
        if (!engine->cubeShader->Initialize(vs.size(), vs.data(), fs.size(),
                                            fs.data(), vsFilePath.c_str(),
                                            fsFilePath.c_str())) {
            return 1;
        }
    }
    //load png
    {
        LOGI("load png");
        std::string textureFilePath = externalDir + "/happy.jpg";
        std::ifstream ifs(textureFilePath);
        if (!ifs) {
            return false;
        }

        std::streampos texBuffSize = ifs.tellg();
        ifs.seekg(0, std::ios::end);
        texBuffSize = ifs.tellg() - texBuffSize;
        ifs.seekg(0);

        std::vector<char> data(texBuffSize);
        ifs.read(data.data(), data.size());
        ifs.close();
        auto imgid = 0;
        int width, height, nrChannels;
        unsigned char *contnet = stbi_load_from_memory(reinterpret_cast<unsigned char *>(data.data()), data.size(), &width, &height, &nrChannels, 4);
        if (contnet) {
            LOGI("getAssets::Init2++ stbi_load ok width：%d,height:%d", width, height);
            auto imgid = 0;
            imgid = GetTexutre(width, height,nrChannels,contnet,0);

            if (imgid > 0) {
                LOGI("getAssets Load %d", imgid);
                engine->testTexture = imgid;
            }
        } else {
            LOGI("getAssets::Failed to load texture");
        }
    }
    // load texture
    {
        LOGI("load texture");
        std::string textureFilePath = externalDir + "/src.ktx";
        std::ifstream ifs(textureFilePath);
        if (!ifs) {
            return false;
        }

        std::streampos texBuffSize = ifs.tellg();
        ifs.seekg(0, std::ios::end);
        texBuffSize = ifs.tellg() - texBuffSize;
        ifs.seekg(0);

        std::vector<char> data(texBuffSize);
        ifs.read(data.data(), data.size());
        ifs.close();

        GLenum texTarget;
        QtiGL::KtxTexture texHelper;
        QtiGL::TKTXHeader pOutHeader;
        QtiGL::TKTXErrorCode result = texHelper.LoadKtxFromBuffer(
                data.data(), data.size(), &engine->cubeTexture, &texTarget,
                &pOutHeader, false);
        LOGI("texture width: %d, height: %d", pOutHeader.pixelWidth,
             pOutHeader.pixelHeight);

        if (result != QtiGL::KTX_SUCCESS || 0 == engine->cubeTexture) {
            return 1;
        }
    }

    // cube geometry
    engine_create_cube(engine->cube, 0.3f);

    // Init 2D hand overlay renderer
    engine->handOverlay.init();

    // Create cube sea around origin
    float xPos = -(CUBE_COUNT / 2);
    float yPos = -(CUBE_COUNT / 2);
    float zPos = -(CUBE_COUNT / 2);

    // rotate 45 degrees along y axis so we can see the edge
    glm::mat4 rotationMat =
            glm::rotate(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    for (int z = 0; z < CUBE_COUNT; ++z) {
        for (int y = 0; y < CUBE_COUNT; ++y) {
            for (int x = 0; x < CUBE_COUNT; ++x) {
                engine->cubeMatrices.push_back(
                        glm::translate(
                                glm::mat4(1.0f),
                                glm::vec3(xPos + x, yPos + y, zPos + z)) *
                        rotationMat);
                engine->cubeColors.push_back(CUBE_COLORS[y]);
            }
        }
    }
    glInit();
    return 0;
}

/**
 * Destroys resources for rendering scene
 */
static void engine_destroy_scene_resources(struct engine *engine)
{
    engine->cube.Destroy();

    engine->cubeShader->Destroy();
    delete engine->cubeShader;
    engine->cubeShader = nullptr;

    glDeleteTextures(1, &engine->cubeTexture);
    engine->cubeTexture = 0;
}

// Generate timestamp string for filenames
static std::string getTimestampString() {
    auto now = std::chrono::system_clock::now();
    auto time_t_val = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_r(&time_t_val, &tm_buf);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
    return ss.str();
}

// Read RGBA pixels from a GL texture (flipped vertically for PNG)
static bool readTexturePixels(GLuint textureId, int width, int height,
                               std::vector<uint8_t>& outPixels) {
    if (textureId == 0 || width == 0 || height == 0) return false;

    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("readTexturePixels: FBO incomplete for texture %u", textureId);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glDeleteFramebuffers(1, &fbo);
        return false;
    }

    std::vector<uint8_t> raw(width * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());

    // Flip vertically (OpenGL origin is bottom-left, PNG is top-left)
    int rowBytes = width * 4;
    outPixels.resize(raw.size());
    for (int y = 0; y < height; y++) {
        memcpy(outPixels.data() + y * rowBytes,
               raw.data() + (height - 1 - y) * rowBytes,
               rowBytes);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glDeleteFramebuffers(1, &fbo);
    return true;
}

// Save a stereo pair (left + right) as a single side-by-side PNG
static bool saveStereoPairAsPng(GLuint leftTex, int leftW, int leftH,
                                 GLuint rightTex, int rightW, int rightH,
                                 const std::string& filePath) {
    std::vector<uint8_t> leftPixels, rightPixels;
    bool hasLeft = readTexturePixels(leftTex, leftW, leftH, leftPixels);
    bool hasRight = readTexturePixels(rightTex, rightW, rightH, rightPixels);

    if (!hasLeft && !hasRight) return false;

    if (hasLeft && hasRight && leftW == rightW && leftH == rightH) {
        // Both eyes available, same size: stitch horizontally [left | right]
        int combinedW = leftW * 2;
        std::vector<uint8_t> combined(combinedW * leftH * 4);
        int srcRowBytes = leftW * 4;
        int dstRowBytes = combinedW * 4;
        for (int y = 0; y < leftH; y++) {
            memcpy(combined.data() + y * dstRowBytes,
                   leftPixels.data() + y * srcRowBytes, srcRowBytes);
            memcpy(combined.data() + y * dstRowBytes + srcRowBytes,
                   rightPixels.data() + y * srcRowBytes, srcRowBytes);
        }
        return ImageSaver::Instance().saveImage(filePath, combined, combinedW, leftH);
    } else if (hasLeft) {
        // Only left eye
        return ImageSaver::Instance().saveImage(filePath, leftPixels, leftW, leftH);
    } else {
        // Only right eye
        return ImageSaver::Instance().saveImage(filePath, rightPixels, rightW, rightH);
    }
}

// Read RGBA pixels from a GL texture and save as PNG
static bool saveTextureAsPng(GLuint textureId, int width, int height,
                              const std::string& filePath) {
    std::vector<uint8_t> pixels;
    if (!readTexturePixels(textureId, width, height, pixels)) return false;
    return ImageSaver::Instance().saveImage(filePath, pixels, width, height);
}

// Save a stereo pair from cached snapshot buffers (thread-safe, no GL access needed)
static bool saveSnapshotStereoPair(CameraAccessExtension::SnapshotBuffer& leftBuf,
                                    CameraAccessExtension::SnapshotBuffer& rightBuf,
                                    const std::string& filePath) {
    bool hasLeft = false, hasRight = false;
    std::vector<uint8_t> leftPixels, rightPixels;
    int leftW = 0, leftH = 0, rightW = 0, rightH = 0;

    {
        std::lock_guard<std::mutex> lock(leftBuf.mutex);
        if (leftBuf.ready && !leftBuf.pixels.empty()) {
            leftPixels = leftBuf.pixels;
            leftW = leftBuf.width;
            leftH = leftBuf.height;
            hasLeft = true;
            leftBuf.ready = false;
        }
    }
    {
        std::lock_guard<std::mutex> lock(rightBuf.mutex);
        if (rightBuf.ready && !rightBuf.pixels.empty()) {
            rightPixels = rightBuf.pixels;
            rightW = rightBuf.width;
            rightH = rightBuf.height;
            hasRight = true;
            rightBuf.ready = false;
        }
    }

    if (!hasLeft || !hasRight) return false;

    if (leftW == rightW && leftH == rightH) {
        int combinedW = leftW * 2;
        std::vector<uint8_t> combined(combinedW * leftH * 4);
        int srcRowBytes = leftW * 4;
        int dstRowBytes = combinedW * 4;
        for (int y = 0; y < leftH; y++) {
            memcpy(combined.data() + y * dstRowBytes,
                   leftPixels.data() + y * srcRowBytes, srcRowBytes);
            memcpy(combined.data() + y * dstRowBytes + srcRowBytes,
                   rightPixels.data() + y * srcRowBytes, srcRowBytes);
        }
        return ImageSaver::Instance().saveImage(filePath, combined, combinedW, leftH);
    }
    return false;
}

static int32_t handle_input(struct android_app *app, AInputEvent *event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        int32_t keyCode = AKeyEvent_getKeyCode(event);
        int32_t action = AKeyEvent_getAction(event);
        if (action == AKEY_EVENT_ACTION_DOWN) {
            struct engine* e = static_cast<struct engine*>(app->userData);
            if (keyCode == AKEYCODE_VOLUME_UP) {
                e->mCameraAccessExtension.snapshotRequested = true;
                return 1;
            }
            if (keyCode == AKEYCODE_DPAD_CENTER || keyCode == AKEYCODE_ENTER) {
                e->dpadCenterPressed = true;
                return 1;
            }
            if (keyCode == AKEYCODE_VOLUME_DOWN) {

                return 1;
            }
        }
    }
    return 0;
}

void android_main(struct android_app *state)
{
    struct engine engine;

    state->userData = &engine;
    state->onAppCmd = AppCommon::app_handle_cmd;
    state->onInputEvent = handle_input;
    g_engine = &engine;
    engine.app = state;

    if (engine_init_display(&engine) != 0) {
        LOGW("Failed to create EGL resources");
        return;
    }

    glGetIntegerv(GL_MAX_SAMPLES, &engine.maxSampleCount);
    LOGI("Max sample count: %d", engine.maxSampleCount);
	if(engine.maxSampleCount < 4)
	{
		engine.currentSampleCount = engine.maxSampleCount;
        LOGW("maxSampleCount < 4. Render quality may be impacted ... ");
	}


    if (engine_init_scene_resources(&engine) != 0) {
        LOGW("Failed to load scene resources!  Exiting");
        return;
    }

    AppCommon::app_wait_window((AppCommon::base_engine *)&engine);
    engine_init_openxr(&engine);
    engine.mCameraAccessExtension.initTimeConversion();
    engine.mDatasetRecorder.init(storagePath);

    // Initialize cameras using new callback-based API
    JavaVM* vm = engine.app->activity->vm;
    jobject activity = engine.app->activity->clazz;

    // Cache for TTS JNI bridge
    g_javaVm = vm;
    JNIEnv* ttsEnv = nullptr;
    vm->GetEnv(reinterpret_cast<void**>(&ttsEnv), JNI_VERSION_1_6);
    if (ttsEnv && activity) {
        g_activity = ttsEnv->NewGlobalRef(activity);
    }
    NativeLoggerInit(storagePath);

    if (engine.mCameraAccessExtension.initCameras(vm, activity)) {
        LOGI("All cameras initialized successfully");
    } else {
        LOGE("Failed to initialize cameras");
    }

    // Start image saver worker thread
    ImageSaver::Instance().start();

    // Initialize hand tracking
    engine.useControllerMode = readUseControllerProperty();
    engine.useProjectHand = readProjectHandProperty();
    LOGI("Input mode: %s, project_hand: %s",
         engine.useControllerMode ? "controller" : "hand tracking",
         engine.useProjectHand ? "on" : "off");
    if (!engine.useControllerMode) {
        engine.mHandTrackerLogic.Init();
    } else {
        engine.mControllerPoseSaver.Init(storagePath);
    }

    while (1) {
        for (;;) {
            int events;
            struct android_poll_source* source;
            // If the timeout is zero, returns immediately without blocking.
            // If the timeout is negative, waits indefinitely until an event appears.
            const int timeoutMilliseconds =
                    (!engine.state.Resumed && !engine.ready) ? -1 : 0;
            if (ALooper_pollOnce(timeoutMilliseconds, nullptr, &events, (void**)&source) < 0) {
                break;
            }

            // Check if the system requested us to exit
            if (state->destroyRequested != 0) {
                LOGI("App destroy requested, exiting main loop");
                goto cleanup;
            }

            // Process this event.
            if (source != nullptr) {
                source->process(state, source);
            }
        }
        AppCommon::app_poll_events(&engine);

        // Always handle camera pause/resume, even when XR session is not ready.
        // This ensures cameras are stopped when the app goes to background,
        // regardless of OpenXR session state transitions (e.g. STOPPING).
        engine.mCameraAccessExtension.Update();

        if (!engine.ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        XrFrameState frameState = {.type = XR_TYPE_FRAME_STATE,
                                   .next = nullptr};
        XrFrameWaitInfo frameWaitInfo = {.type = XR_TYPE_FRAME_WAIT_INFO,
                                         .next = nullptr};
        XrResult result = xrWaitFrame(engine.state.xrSession, &frameWaitInfo,
                                      &frameState);
        if (XR_FAILED(result)) {
            LOGW("xrWaitFrame failed");
            continue;
        }


        XrFrameBeginInfo frameBeginInfo = {.type = XR_TYPE_FRAME_BEGIN_INFO,
                                           .next = nullptr};
        result = xrBeginFrame(engine.state.xrSession, &frameBeginInfo);

        if (XR_FAILED(result)) {
            LOGW("xrBeginFrame failed");
            continue;
        }

        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t viewCapacityInput = (uint32_t)engine.state.m_views.size();
        uint32_t viewCountOutput;

        XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        viewLocateInfo.viewConfigurationType =
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        viewLocateInfo.displayTime = frameState.predictedDisplayTime;
        viewLocateInfo.space = engine.useRootSpace ? engine.state.xrRootSpace : engine.state.xrLocalSpace;
        result = xrLocateViews(engine.state.xrSession, &viewLocateInfo,
                               &viewState, viewCapacityInput, &viewCountOutput,
                               engine.state.m_views.data());
        if (XR_FAILED(result)) {
            LOGW("xrLocateViews failed");
        }
        // Use current CLOCK_BOOTTIME as the single clock source for all sensor
        // queries and dataset timestamps. predictedDisplayTime is a future
        // prediction meant to reduce rendering latency; for dataset recording
        // we want timestamps that faithfully represent when data was captured.
        // XrTime (for OpenXR APIs) is derived from the same boottime sample.
        int64_t sensorBoottimeNs;
        XrTime sensorXrTime = frameState.predictedDisplayTime;  // fallback
        {
            struct timespec nowBoot;
            clock_gettime(CLOCK_BOOTTIME, &nowBoot);
            sensorBoottimeNs = (int64_t)nowBoot.tv_sec * 1000000000LL + nowBoot.tv_nsec;
            if (g_boottimeToXrTimeFn) {
                sensorXrTime = g_boottimeToXrTimeFn((uint64_t)sensorBoottimeNs);
            }
        }

        engine.inputPtr->UpdateInput(sensorXrTime);

        if (!engine.useControllerMode) {
            engine.mHandTrackerLogic.Update(sensorXrTime);
        } else if (engine.mControllerPoseSaver.IsSessionActive()) {
            ControllerPoseRecord rec{};
            rec.frameNumber = engine.controllerFrameCounter;
            rec.timestamp = sensorBoottimeNs;

            rec.leftActive = engine.inputPtr->IsControllerActive(0);
            if (rec.leftActive) {
                auto& p = engine.inputPtr->mControllerPose[0];
                rec.leftPos[0] = p.position.x;
                rec.leftPos[1] = p.position.y;
                rec.leftPos[2] = p.position.z;
                rec.leftQuat[0] = p.orientation.x;
                rec.leftQuat[1] = p.orientation.y;
                rec.leftQuat[2] = p.orientation.z;
                rec.leftQuat[3] = p.orientation.w;
            }

            rec.rightActive = engine.inputPtr->IsControllerActive(1);
            if (rec.rightActive) {
                auto& p = engine.inputPtr->mControllerPose[1];
                rec.rightPos[0] = p.position.x;
                rec.rightPos[1] = p.position.y;
                rec.rightPos[2] = p.position.z;
                rec.rightQuat[0] = p.orientation.x;
                rec.rightQuat[1] = p.orientation.y;
                rec.rightQuat[2] = p.orientation.z;
                rec.rightQuat[3] = p.orientation.w;
            }

            engine.mControllerPoseSaver.SaveFrame(rec);
            engine.controllerFrameCounter++;
        }

        // Locate device/IMU pose (view space in root/local space).
        // Camera extrinsics are relative to the device frame, not individual eyes.
        XrSpaceLocation deviceLocation{XR_TYPE_SPACE_LOCATION};
        XrSpace refSpace = engine.useRootSpace ? engine.state.xrRootSpace
                                                : engine.state.xrLocalSpace;
        XrResult locateResult = xrLocateSpace(engine.state.xrViewSpace, refSpace,
                                               sensorXrTime,
                                               &deviceLocation);
        bool devicePoseValid = XR_SUCCEEDED(locateResult) &&
            (deviceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
            (deviceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);

        // Update shared sensor snapshot with device pose and hand tracking
        if (!engine.useControllerMode) {
            std::lock_guard<std::mutex> lock(engine.alignedSnapshot.mutex);
            // Device/IMU pose (not eye pose — camera extrinsics are in device frame)
            if (devicePoseValid) {
                auto& dp = deviceLocation.pose;
                engine.alignedSnapshot.headPose.pos[0] = dp.position.x;
                engine.alignedSnapshot.headPose.pos[1] = dp.position.y;
                engine.alignedSnapshot.headPose.pos[2] = dp.position.z;
                engine.alignedSnapshot.headPose.quat[0] = dp.orientation.x;
                engine.alignedSnapshot.headPose.quat[1] = dp.orientation.y;
                engine.alignedSnapshot.headPose.quat[2] = dp.orientation.z;
                engine.alignedSnapshot.headPose.quat[3] = dp.orientation.w;
                engine.alignedSnapshot.headPose.valid = true;
            }
            // Hand tracking
            auto& snap = engine.alignedSnapshot;
            snap.leftHand.active = engine.mHandTrackerLogic.LeftHandIsActive;
            snap.rightHand.active = engine.mHandTrackerLogic.RightHandIsActive;
            for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++) {
                if (snap.leftHand.active) {
                    auto& j = engine.mHandTrackerLogic.LeftHandJointLocations[i];
                    snap.leftHand.joints[i][0] = j.pose.position.x;
                    snap.leftHand.joints[i][1] = j.pose.position.y;
                    snap.leftHand.joints[i][2] = j.pose.position.z;
                    snap.leftHand.quats[i][0] = j.pose.orientation.x;
                    snap.leftHand.quats[i][1] = j.pose.orientation.y;
                    snap.leftHand.quats[i][2] = j.pose.orientation.z;
                    snap.leftHand.quats[i][3] = j.pose.orientation.w;
                    snap.leftHand.radii[i] = j.radius;
                }
                if (snap.rightHand.active) {
                    auto& j = engine.mHandTrackerLogic.RightHandJointLocations[i];
                    snap.rightHand.joints[i][0] = j.pose.position.x;
                    snap.rightHand.joints[i][1] = j.pose.position.y;
                    snap.rightHand.joints[i][2] = j.pose.position.z;
                    snap.rightHand.quats[i][0] = j.pose.orientation.x;
                    snap.rightHand.quats[i][1] = j.pose.orientation.y;
                    snap.rightHand.quats[i][2] = j.pose.orientation.z;
                    snap.rightHand.quats[i][3] = j.pose.orientation.w;
                    snap.rightHand.radii[i] = j.radius;
                }
            }
        }

        // Push timestamped sample to ring buffer so the camera callback can
        // recover time-aligned head pose + hand joints at start_of_exposure.
        if (!engine.useControllerMode && devicePoseValid) {
            PoseHandSampleRing::Sample rs;
            rs.bootTimeNs = sensorBoottimeNs;
            rs.poseValid = true;
            // Device/IMU pose (camera extrinsics are in device frame)
            auto& dp = deviceLocation.pose;
            rs.headPos[0] = dp.position.x;
            rs.headPos[1] = dp.position.y;
            rs.headPos[2] = dp.position.z;
            rs.headQuat[0] = dp.orientation.x;
            rs.headQuat[1] = dp.orientation.y;
            rs.headQuat[2] = dp.orientation.z;
            rs.headQuat[3] = dp.orientation.w;
            rs.leftActive = engine.mHandTrackerLogic.LeftHandIsActive;
            rs.rightActive = engine.mHandTrackerLogic.RightHandIsActive;
            if (rs.leftActive) {
                for (int j = 0; j < XR_HAND_JOINT_COUNT_EXT; ++j) {
                    const auto& loc = engine.mHandTrackerLogic.LeftHandJointLocations[j];
                    rs.leftJoints[j][0] = loc.pose.position.x;
                    rs.leftJoints[j][1] = loc.pose.position.y;
                    rs.leftJoints[j][2] = loc.pose.position.z;
                    rs.leftRadii[j] = loc.radius;
                    rs.leftQuats[j][0] = loc.pose.orientation.x;
                    rs.leftQuats[j][1] = loc.pose.orientation.y;
                    rs.leftQuats[j][2] = loc.pose.orientation.z;
                    rs.leftQuats[j][3] = loc.pose.orientation.w;
                }
            }
            if (rs.rightActive) {
                for (int j = 0; j < XR_HAND_JOINT_COUNT_EXT; ++j) {
                    const auto& loc = engine.mHandTrackerLogic.RightHandJointLocations[j];
                    rs.rightJoints[j][0] = loc.pose.position.x;
                    rs.rightJoints[j][1] = loc.pose.position.y;
                    rs.rightJoints[j][2] = loc.pose.position.z;
                    rs.rightRadii[j] = loc.radius;
                    rs.rightQuats[j][0] = loc.pose.orientation.x;
                    rs.rightQuats[j][1] = loc.pose.orientation.y;
                    rs.rightQuats[j][2] = loc.pose.orientation.z;
                    rs.rightQuats[j][3] = loc.pose.orientation.w;
                }
            }
            engine.poseHandRing.push(rs);
        }

        // Toggle recording on rising edge of B button or DPAD_CENTER
        bool curToggle = engine.inputPtr->mRightBPressed || engine.dpadCenterPressed;
        engine.dpadCenterPressed = false;
        if (curToggle && !engine.prevRecordingToggle) {
            if (!engine.mDatasetRecorder.isRecording()) {
                // Wait for any async stop (encoder + recorder) to complete before starting
                int waitCount = 0;
                while (engine.mCameraAccessExtension.stopInProgress.load() ||
                       engine.mDatasetRecorder.isRecording()) {
                    usleep(10000); // 10ms
                    if (++waitCount % 100 == 0) {
                        LOGW("Right B start: waiting for stopEncoder (%d ms)", waitCount * 10);
                    }
                    if (waitCount > 300) break; // 3s timeout
                }
                LOGI("Starting dataset recording (right B)...");
                engine.mDatasetRecorder.start();
                engine.mCameraAccessExtension.encoderBaseDir = engine.mDatasetRecorder.getDatasetDir();
                engine.mCameraAccessExtension.encodingEnabled = true;
                engine.mCameraAccessExtension.encodersStopped = false;
                engine.mCameraAccessExtension.cameraParamsSavedRgb = false;
                engine.mCameraAccessExtension.cameraParamsSavedTracking = false;
                engine.mCameraAccessExtension.cameraParamsSavedCtrl = false;
                {
                    std::lock_guard<std::mutex> lock(engine.alignedSnapshot.mutex);
                    engine.alignedSnapshot.headPose.valid = false;
                    engine.alignedSnapshot.leftHand.active = false;
                    engine.alignedSnapshot.rightHand.active = false;
                    engine.alignedSnapshot.rgbFrameCount = 0;
                }
                engine.poseHandRing.clear();
                if (engine.useControllerMode) {
                    engine.mControllerPoseSaver.StartSession(
                        engine.mDatasetRecorder.getControllerPoseCsvPath());
                } else {
                    engine.mHandTrackerLogic.rawDateSave->StartNewSession(
                        engine.mDatasetRecorder.getHandTrackingCsvPath());
                    engine.mDatasetRecorder.writeCaptureStatusJson(
                        "recording", engine.mHandTrackerLogic.rawDateSave);
                }
                ttsSpeak("开始录制");
            } else {
                LOGI("Stopping dataset recording (right B, async)...");
                engine.mCameraAccessExtension.encodingEnabled = false;
                if (!engine.useControllerMode) {
                    engine.mDatasetRecorder.writeCaptureStatusJson(
                        "finalizing", engine.mHandTrackerLogic.rawDateSave);
                }
                std::thread([&engine]() {
                    engine.mCameraAccessExtension.stopEncoder();
                    engine.mCameraAccessExtension.encoderBaseDir.clear();

                    engine.mDatasetRecorder.stop();
                    if (engine.useControllerMode) {
                        engine.mControllerPoseSaver.StopSession();
                    } else {
                        engine.mHandTrackerLogic.rawDateSave->StopSession();
                        engine.mDatasetRecorder.writeCaptureStatusJson(
                            "complete", engine.mHandTrackerLogic.rawDateSave);
                    }
                    ttsSpeak("录制已保存");
                    LOGI("Right B: Async encoder + recorder stop completed");
                }).detach();
            }
        }
        engine.prevRecordingToggle = curToggle;

        // Handle snapshot: non-blocking — check each frame, save when data is ready
        {
            bool req = engine.mCameraAccessExtension.snapshotRequested.load();
            bool lOk = engine.mCameraAccessExtension.snapshotRgbLeft.ready.load();
            bool rOk = engine.mCameraAccessExtension.snapshotRgbRight.ready.load();
            if (req && lOk && rOk) {

            // Both eyes ready — camera callback has cached pixels
            engine.mCameraAccessExtension.snapshotRequested.exchange(false);
            LOGI("Snapshot: saving");

            std::string ts = getTimestampString();
            std::string basePath = std::string(storagePath) + "/images";
            mkdir(basePath.c_str(), 0777);

            bool anySaved = false;

            // RGB cameras: left + right stitched horizontally
            {
                std::string path = basePath + "/rgb_" + ts + ".png";
                if (saveSnapshotStereoPair(engine.mCameraAccessExtension.snapshotRgbLeft,
                                           engine.mCameraAccessExtension.snapshotRgbRight, path)) {
                    anySaved = true;
                }
            }

            // Tracking cameras: left(0) + right(1) stitched horizontally
            {
                std::string path = basePath + "/tracking_" + ts + ".png";
                if (saveSnapshotStereoPair(engine.mCameraAccessExtension.snapshotCv[0],
                                           engine.mCameraAccessExtension.snapshotCv[1], path)) {
                    anySaved = true;
                }
            }

            // Ctrl cameras: left(2) + right(3) stitched horizontally
            {
                std::string path = basePath + "/ctrl_" + ts + ".png";
                if (saveSnapshotStereoPair(engine.mCameraAccessExtension.snapshotCv[2],
                                           engine.mCameraAccessExtension.snapshotCv[3], path)) {
                    anySaved = true;
                }
            }

            ttsSpeak(anySaved ? "图片已保存" : "图片保存失败");
            } // end if (req && lOk && rOk)
        } // end snapshot scope

        XrCompositionLayerProjectionView
                projectionViews[engine.state.viewCount];
        auto &stereoSwapchain = engine.swapchainMap[engine.currentSampleCount];
        for (uint32_t i = 0; i < engine.state.viewCount; ++i) {
            auto &swapchain = stereoSwapchain.eyeSwapchain[i];
            XrSwapchainImageAcquireInfo swapchainImageAcquireInfo = {
                    .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
                    .next = nullptr};
            uint32_t bufferIndex;
            result = xrAcquireSwapchainImage(swapchain.xrSwapchain,
                                             &swapchainImageAcquireInfo,
                                             &bufferIndex);

            if (XR_FAILED(result)) {
                LOGW("xrAcquireSwapchainImage failed");
            }

            XrSwapchainImageWaitInfo swapchainImageWaitInfo = {
                    .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
                    .next = nullptr,
                    .timeout = 1000};
            result = xrWaitSwapchainImage(swapchain.xrSwapchain,
                                          &swapchainImageWaitInfo);

            if (XR_FAILED(result)) {
                LOGW("xrWaitSwapchainImage failed");
            }

            // NOTE: since xrLocateViews is not implemented (neither is
            // tracking) yet... we hard-code some things in the following
            projectionViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projectionViews[i].next = nullptr;
            projectionViews[i].pose = engine.state.m_views[i].pose;
            projectionViews[i].fov = engine.state.m_views[i].fov;
            projectionViews[i].subImage.swapchain = swapchain.xrSwapchain;
            projectionViews[i].subImage.imageArrayIndex = 0;
            projectionViews[i].subImage.imageRect.offset.x = 0;
            projectionViews[i].subImage.imageRect.offset.y = 0;
            projectionViews[i].subImage.imageRect.extent.width = engine.width;
            projectionViews[i].subImage.imageRect.extent.height = engine.height;

            // Draw scene
            engine_draw_frame(&engine, i, bufferIndex, engine.state.m_views[i]);

            XrSwapchainImageReleaseInfo swapchainImageReleaseInfo = {
                    .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
                    .next = nullptr};
            result = xrReleaseSwapchainImage(swapchain.xrSwapchain,
                                             &swapchainImageReleaseInfo);

            if (XR_FAILED(result)) {
                LOGW("xrReleaseSwapchainImage failed");
            }
        }

        glFlush();

        // Submit pending RGB SBS frame to encoder.
        // saveAlignedSensorData is now called in the camera callback (handleRGBFrame)
        // to minimize pipeline delay between frame arrival and CSV write.
        {
            uint64_t ts = engine.mCameraAccessExtension.pendingEncodeTimestamp.exchange(0);
            if (ts != 0 &&
                engine.mCameraAccessExtension.rgbEncoderSurface != nullptr &&
                !engine.mCameraAccessExtension.encodersStopped.load() &&
                !engine.mCameraAccessExtension.stopInProgress.load()) {

                auto* encSurf = engine.mCameraAccessExtension.rgbEncoderSurface;
                auto* encoder = engine.mCameraAccessExtension.rgbEncoder;

                int sbsW = engine.mCameraAccessExtension.rgbFrameWidths[0] * 2;
                int sbsH = engine.mCameraAccessExtension.rgbFrameHeights[0];
                if (sbsW > 0 && sbsH > 0) {
                    encSurf->makeCurrent();

                    glViewport(0, 0, sbsW, sbsH);
                    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);

                    // Draw SBS texture fullscreen
                    glUseProgram(engine.mCameraAccessExtension.sbsCopyShaderProgram);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, engine.mCameraAccessExtension.rgbSbsTexture);
                    glUniform1i(glGetUniformLocation(engine.mCameraAccessExtension.sbsCopyShaderProgram, "uTexture"), 0);

                    glBindBuffer(GL_ARRAY_BUFFER, engine.mCameraAccessExtension.encoderVBO);
                    GLint cpPosLoc = glGetAttribLocation(engine.mCameraAccessExtension.sbsCopyShaderProgram, "aPosition");
                    GLint cpTexLoc = glGetAttribLocation(engine.mCameraAccessExtension.sbsCopyShaderProgram, "aTexCoord");
                    glEnableVertexAttribArray(cpPosLoc);
                    glVertexAttribPointer(cpPosLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
                    glEnableVertexAttribArray(cpTexLoc);
                    glVertexAttribPointer(cpTexLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
                    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                    glDisableVertexAttribArray(cpPosLoc);
                    glDisableVertexAttribArray(cpTexLoc);

                    // Compute overlay projection using RGB-frame-time snapshot, then render
                    if (engine.useProjectHand) {
                        auto& os = engine.mCameraAccessExtension.overlaySnap;
                        std::lock_guard<std::mutex> lock(os.mutex);
                        if (os.headValid) {
                            engine.handOverlay.computeProjection(
                                os.leftActive, os.leftJoints,
                                os.rightActive, os.rightJoints,
                                os.headPos, os.headQuat);
                            int halfW = sbsW / 2;
                            engine.handOverlay.render(0, 0, halfW, sbsH);       // left eye
                            engine.handOverlay.render(1, halfW, halfW, sbsH);   // right eye
                        }
                    }

                    encSurf->setPresentationTime(ts);
                    encoder->submitNsTimestamp(ts);
                    encSurf->swapBuffers();

                    // Restore main render context
                    eglMakeCurrent(engine.display, engine.surface, engine.surface, engine.context);
                }
            }
        }

        XrCompositionLayerProjection projectionLayer = {
                .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
                .next = nullptr,
                .layerFlags =
                        XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT |
                                XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT,
                .space = engine.useRootSpace ? engine.state.xrRootSpace : engine.state.xrLocalSpace,
                .viewCount = engine.state.viewCount,
                .views = projectionViews,
        };

        const XrCompositionLayerBaseHeader *const layers[] = {
                (const XrCompositionLayerBaseHeader *const) & projectionLayer,
        };

        XrFrameEndInfo frameEndInfo = {
                .type = XR_TYPE_FRAME_END_INFO,
                .displayTime = frameState.predictedDisplayTime,
                .layerCount = sizeof(layers) / sizeof(layers[0]),
                .layers = layers,
                .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
                .next = nullptr};

        result = xrEndFrame(engine.state.xrSession, &frameEndInfo);

        if (XR_FAILED(result)) {
            LOGW("xrEndFrame failed");
        }
    }

cleanup:
    LOGI("Shutting down...");
    // Cleanup cameras first (stops streaming, releases camera hardware)
    engine.mCameraAccessExtension.cleanupCameras();

    // Stop image saver worker thread
    ImageSaver::Instance().shutdown();
    NativeLoggerShutdown();

    // Release JNI global reference
    if (g_javaVm && g_activity) {
        JNIEnv* env = nullptr;
        g_javaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (env) {
            env->DeleteGlobalRef(g_activity);
        }
        g_activity = nullptr;
        g_javaVm = nullptr;
    }

    // Destroy OpenXR resources
    engine_destroy_xr_swapchains(&engine);
    engine_destroy_scene_resources(&engine);
    gInfoPanel.cleanup();
    engine_shutdown_openxr(&engine);

    LOGI("Shutdown complete");
}
