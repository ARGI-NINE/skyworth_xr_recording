#pragma once

#include <media/NdkMediaCodec.h>
#include <media/NdkImageReader.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <android/log.h>

#include <thread>
#include <atomic>
#include <queue>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include <cstdio>

#include "FMP4Writer.h"
#include "NativeLogger.h"

#define LOG_TAG "CameraEncoder"

#define LOGI(...)  NATIVE_LOGI(LOG_TAG, __VA_ARGS__)
#define LOGW(...)  NATIVE_LOGW(LOG_TAG, __VA_ARGS__)
#define LOGE(...)  NATIVE_LOGE(LOG_TAG, __VA_ARGS__)

namespace SXR {

    // RGB-only encoded output. Grayscale encoders deliberately have no network sink.
    class IRgbEncodedSink {
    public:
        virtual ~IRgbEncodedSink() = default;
        // data:   H.265 encoded sample data (AVCC format)
        // size:   byte length of data
        // ptsUs:  absolute presentation timestamp (CLOCK_BOOTTIME, microseconds)
        // isConfig: true for VPS/SPS/PPS (codec config) frames
        virtual void onRgbEncodedFrame(
            const uint8_t* data, size_t size, int64_t ptsUs, bool isConfig) = 0;
    };

    // Per-frame metadata carried alongside each encoded sample.
    // Driver BOOTTIME exposure fields are converted to UTC for the compatibility
    // CSV by adding the recording-session BOOTTIME-to-REALTIME offset.
    struct FrameMeta {
        int64_t midExposureBootNs;
        int64_t exposureStartBootNs;
        uint32_t exposure;
        uint32_t gain;
        uint32_t frameId;
    };

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
        enum class EncoderState { STOPPED, STARTING, RUNNING, STOPPING };
        // Constructor for RGB cameras with Surface mode
        CameraEncoder(int width, int height, int frameRate, int bitRate,
                      const std::string& outputName, const std::string& baseDir,
                      IRgbEncodedSink* rgbSink);

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

        // Submit per-frame metadata for the current frame (Surface + Buffer mode).
        // Consumed in encoder output order to emit one CSV row per written sample.
        void submitFrameMeta(const FrameMeta& m);

        // Get encoder dimensions
        int getWidth() const { return mWidth; }
        int getHeight() const { return mHeight; }
        EncoderType getType() const { return mType; }
        EncoderMode getMode() const { return mMode; }
        const std::string& getGroupName() const { return mGroupName; }
        void setGroupName(const std::string& g) { mGroupName = g; }
        AMediaCodec* getCodec() const { return mCodec; }

        // Check if encoder uses Surface mode
        bool isSurfaceMode() const { return mMode == EncoderMode::SURFACE; }

        // Set BOOTTIME→REALTIME offset for timestamp conversion (called once per recording session)
        void setTimeOffset(int64_t offsetNs) { mTimeOffsetNs = offsetNs; }

        // Request an IDR keyframe (SPS/PPS + IDR) on the given codec. Used after
        // switching camera groups so consumers get fresh codec config.
        static void requestKeyFrame(AMediaCodec* codec, const std::string& group);

        bool armWriter(const std::string& baseDir);
        bool waitWriterArmed(int timeoutMs);
        void finalizeWriter();
        bool hasWriter() const;

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
        IRgbEncodedSink* mRgbSink = nullptr; // non-owning; RGB encoder is stopped before sink

        AMediaCodec *mCodec = nullptr;
        ANativeWindow *mInputSurface = nullptr;  // Input surface for zero-copy

        // Fragmented-MP4 writer (replaces AMediaMuxer + in-band text track).
        SXR::FMP4Writer mFmp4;
        bool mFmp4Started = false;

        std::thread mOutputThread;
        std::atomic<bool> mRunning{false};
        std::atomic<EncoderState> mEncoderState{EncoderState::STOPPED};

        // Per-frame metadata queue, consumed in encoder output order to write
        // one CSV row per emitted sample. Bounded in practice by the 1:1 drain
        // in the output thread (one pop per written sample; B-frames are
        // disabled so output order == presentation order is preserved), so it
        // does not grow unbounded under normal operation.
        std::mutex mMetaMutex;
        std::deque<FrameMeta> mMetaQueue;

        // Per-stream CSV (one row per encoded sample).
        FILE* mMetaFile = nullptr;
        uint64_t mFrameIndex = 0;

        int64_t mLastPtsUs = 0;

        // First sample's absolute PTS (MediaCodec Surface-mode PTS is absolute
        // CLOCK_BOOTTIME). Each recording zero-bases its PTS from its own first
        // frame so the fMP4 timeline starts at 0, matching AMediaMuxer's
        // behaviour. Reset to -1 in initEncoder().
        int64_t mFirstPtsUs = -1;

        enum class DiskState { DETACHED, ARMING, WRITING, FINALIZING };
        mutable std::mutex mDiskMutex;
        std::condition_variable mDiskCv;
        DiskState mDiskState = DiskState::DETACHED;
        std::string mPendingBaseDir;
        bool mDiskFailed = false;

        void handleDiskCommands();
        void closeWriterOnOutputThread();
        bool openWriterOnOutputThread();
        static bool isVerifiedHevcIdr(const uint8_t* data, size_t size, uint32_t flags);

        // BOOTTIME→REALTIME offset for timestamp conversion
        int64_t mTimeOffsetNs = 0;
    };
}
