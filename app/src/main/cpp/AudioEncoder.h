#pragma once

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaMuxer.h>
#include <aaudio/AAudio.h>
#include <android/log.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>
#include <vector>

#define AUDIO_ENCODER_TAG "AudioEncoder"
#define AE_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, AUDIO_ENCODER_TAG, __VA_ARGS__))
#define AE_LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, AUDIO_ENCODER_TAG, __VA_ARGS__))
#define AE_LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, AUDIO_ENCODER_TAG, __VA_ARGS__))

class AudioEncoder {
public:
    AudioEncoder(int sampleRate = 44100, int bitRate = 96000, int channelCount = 1);
    ~AudioEncoder();

    bool start(const std::string& outputPath);
    void stop();

    bool isRecording() const { return mRunning.load(); }

private:
    void initEncoder(const std::string& outputPath);
    void recordingLoop();   // AAudio read → PCM queue
    void inputLoop();       // PCM queue → AMediaCodec input
    void outputLoop();      // AMediaCodec output → AMediaMuxer + TimedText

    struct PcmFrame {
        int64_t ptsUs;
        std::vector<uint8_t> data;
    };

    int mSampleRate;
    int mBitRate;
    int mChannelCount;

    int64_t mBoottimeBaseNs = 0;

    AMediaCodec* mCodec = nullptr;
    AMediaMuxer* mMuxer = nullptr;
    AAudioStream* mRecordingStream = nullptr;
    int mAudioTrackIndex = -1;
    int mTextTrackIndex = -1;
    bool mMuxerStarted = false;

    std::thread mRecordingThread;
    std::thread mInputThread;
    std::thread mOutputThread;
    std::atomic<bool> mRunning{false};
    std::mutex mMutex;

    std::queue<PcmFrame> mPcmQueue;
    std::mutex mQueueMutex;
    std::condition_variable mQueueCV;
    std::atomic<bool> mRecordingDone{false};
    static constexpr size_t kMaxQueueFrames = 64;

    int64_t mLastPtsUs = 0;
    int64_t mTotalFramesRead = 0;
    int64_t mTotalFramesDropped = 0;
    std::string mOutputPath;
};
