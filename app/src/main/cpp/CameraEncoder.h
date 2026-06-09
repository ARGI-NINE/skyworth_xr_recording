#pragma once

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaMuxer.h>
#include <media/NdkImageReader.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <android/log.h>

#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <string>

#define LOG_TAG "CameraEncoder"

#define LOGI(...)  ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGW(...)  ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#define LOGE(...)  ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

namespace SXR {

    // Encoder type
    enum class EncoderType {
        RGB,        // RGB camera (YUV420, left or right)
        GRAYSCALE   // Grayscale camera (Y8, side-by-side left+right)
    };

    // Encoder input mode
    enum class EncoderMode {
        BUFFER,     // Buffer input mode (requires copy)
        SURFACE     // Surface input mode (zero-copy)
    };

    class CameraEncoder {
    public:
        // Constructor for RGB cameras with Surface mode
        CameraEncoder(int width, int height, int frameRate, int bitRate,
                      const std::string& outputName, const std::string& baseDir = "");

        // Constructor for grayscale cameras with group name (Buffer mode)
        CameraEncoder(const std::string& groupName, int width, int height, int frameRate = 60, const std::string& baseDir = "");

        // Constructor for grayscale cameras with explicit mode (Surface or Buffer)
        CameraEncoder(const std::string& groupName, int width, int height,
                      int frameRate, EncoderMode mode, const std::string& baseDir = "");

        ~CameraEncoder();

        bool start();

        void stop();

        // Get the input surface for rendering (Surface mode only)
        // Returns ANativeWindow* that can be used with EGL/OpenGL
        ANativeWindow* getInputSurface();

        // Signal end of input stream (Surface mode only)
        void signalEndOfInputStream();

        // Feed a frame buffer to the encoder (Buffer mode only)
        // For grayscale: data is Y8 format, size = width * height
        // timestampNs is in nanoseconds
        bool feedFrame(const uint8_t* data, size_t size, int64_t timestampNs);

        // Submit nanosecond timestamp for the current frame (Surface mode)
        // The exact ns value is written to the text track in the MP4
        void submitNsTimestamp(int64_t timestampNs);

        // Get encoder dimensions
        int getWidth() const { return mWidth; }
        int getHeight() const { return mHeight; }
        EncoderType getType() const { return mType; }
        EncoderMode getMode() const { return mMode; }
        const std::string& getGroupName() const { return mGroupName; }

        // Check if encoder uses Surface mode
        bool isSurfaceMode() const { return mMode == EncoderMode::SURFACE; }

    private:
        void initEncoder();

        // Output processing loop (runs in separate thread)
        void outputLoop();

        // Process output buffer, returns true if EOS was received
        bool processOutputBuffer();

    private:
        int mCameraId;  // -1 for non-RGB encoders
        int mWidth;
        int mHeight;
        int mFrameRate;
        int mBitRate;
        EncoderType mType;
        EncoderMode mMode;
        std::string mGroupName;  // For grayscale: "tracking" or "ctrl"
        std::string mOutputName; // Output filename (e.g. "rgb.mp4")
        std::string mBaseDir;    // Output directory (empty = use default path)
        std::string mOutputPath;

        AMediaCodec *mCodec = nullptr;
        AMediaMuxer *mMuxer = nullptr;
        ANativeWindow *mInputSurface = nullptr;  // Input surface for zero-copy
        int mTrackIndex = -1;
        int mTextTrackIndex = -1;
        bool mMuxerStarted = false;

        std::thread mOutputThread;
        std::atomic<bool> mRunning{false};

        // Queue of ns timestamps for text track (encoder output is ordered)
        std::mutex mNsMutex;
        std::queue<int64_t> mNsQueue;

        int64_t mLastPtsUs = 0;
    };
}
