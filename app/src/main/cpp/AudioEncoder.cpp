#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/stat.h>
#include "AudioEncoder.h"

static int64_t getBoottimeNs() {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

AudioEncoder::AudioEncoder(int sampleRate, int bitRate, int channelCount)
    : mSampleRate(sampleRate), mBitRate(bitRate), mChannelCount(channelCount) {}

AudioEncoder::~AudioEncoder() {
    stop();
}

void AudioEncoder::initEncoder(const std::string& outputPath) {
    mCodec = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    if (!mCodec) {
        AE_LOGE("Failed to create AAC encoder");
        return;
    }

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, mBitRate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, mSampleRate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, mChannelCount);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_AAC_PROFILE, 2);  // AAC-LC

    media_status_t status = AMediaCodec_configure(
            mCodec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    AMediaFormat_delete(format);

    if (status != AMEDIA_OK) {
        AE_LOGE("AMediaCodec_configure failed: %d", status);
        AMediaCodec_delete(mCodec);
        mCodec = nullptr;
        return;
    }

    status = AMediaCodec_start(mCodec);
    if (status != AMEDIA_OK) {
        AE_LOGE("AMediaCodec_start failed: %d", status);
        AMediaCodec_delete(mCodec);
        mCodec = nullptr;
        return;
    }

    mOutputPath = outputPath;
    unlink(mOutputPath.c_str());
    int fd = open(mOutputPath.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        AE_LOGE("Failed to open output file: %s", mOutputPath.c_str());
        AMediaCodec_stop(mCodec);
        AMediaCodec_delete(mCodec);
        mCodec = nullptr;
        return;
    }

    mMuxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    close(fd);

    if (!mMuxer) {
        AE_LOGE("Failed to create muxer");
        AMediaCodec_stop(mCodec);
        AMediaCodec_delete(mCodec);
        mCodec = nullptr;
        return;
    }

    // Add TimedText track FIRST (before audio track)
    AMediaFormat* textFormat = AMediaFormat_new();
    AMediaFormat_setString(textFormat, AMEDIAFORMAT_KEY_MIME, "application/x-subrip");
    AMediaFormat_setString(textFormat, AMEDIAFORMAT_KEY_LANGUAGE, "und");
    AMediaFormat_setInt32(textFormat, AMEDIAFORMAT_KEY_IS_FORCED_SUBTITLE, 0);
    AMediaFormat_setInt32(textFormat, AMEDIAFORMAT_KEY_IS_AUTOSELECT, 0);
    mTextTrackIndex = AMediaMuxer_addTrack(mMuxer, textFormat);
    AMediaFormat_delete(textFormat);

    AE_LOGI("Encoder initialized: %s (sampleRate=%d, bitRate=%d, channels=%d)",
            mOutputPath.c_str(), mSampleRate, mBitRate, mChannelCount);
}

bool AudioEncoder::start(const std::string& outputPath) {
    std::lock_guard<std::mutex> lock(mMutex);

    if (mRunning) {
        AE_LOGW("Already recording");
        return false;
    }

    mBoottimeBaseNs = getBoottimeNs();
    mRecordingDone = false;
    mTotalFramesRead = 0;
    mTotalFramesDropped = 0;

    initEncoder(outputPath);
    if (!mCodec || !mMuxer) {
        AE_LOGE("Failed to initialize encoder");
        if (mCodec) {
            AMediaCodec_stop(mCodec);
            AMediaCodec_delete(mCodec);
            mCodec = nullptr;
        }
        if (mMuxer) {
            AMediaMuxer_delete(mMuxer);
            mMuxer = nullptr;
        }
        return false;
    }

    // Create AAudio input stream
    AAudioStreamBuilder* builder = nullptr;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK || !builder) {
        AE_LOGE("Failed to create AAudio stream builder: %d", result);
        AMediaCodec_stop(mCodec);
        AMediaCodec_delete(mCodec);
        mCodec = nullptr;
        AMediaMuxer_delete(mMuxer);
        mMuxer = nullptr;
        return false;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setSampleRate(builder, mSampleRate);
    AAudioStreamBuilder_setChannelCount(builder, mChannelCount);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);

    result = AAudioStreamBuilder_openStream(builder, &mRecordingStream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK || !mRecordingStream) {
        AE_LOGE("Failed to open AAudio stream: %d", result);
        AMediaCodec_stop(mCodec);
        AMediaCodec_delete(mCodec);
        mCodec = nullptr;
        AMediaMuxer_delete(mMuxer);
        mMuxer = nullptr;
        return false;
    }

    int32_t actualRate = AAudioStream_getSampleRate(mRecordingStream);
    int32_t actualChannels = AAudioStream_getChannelCount(mRecordingStream);
    AE_LOGI("AAudio stream opened: rate=%d, channels=%d", actualRate, actualChannels);

    result = AAudioStream_requestStart(mRecordingStream);
    if (result != AAUDIO_OK) {
        AE_LOGE("Failed to start AAudio stream: %d", result);
        AAudioStream_close(mRecordingStream);
        mRecordingStream = nullptr;
        AMediaCodec_stop(mCodec);
        AMediaCodec_delete(mCodec);
        mCodec = nullptr;
        AMediaMuxer_delete(mMuxer);
        mMuxer = nullptr;
        return false;
    }

    mRunning = true;
    mRecordingThread = std::thread(&AudioEncoder::recordingLoop, this);
    mInputThread = std::thread(&AudioEncoder::inputLoop, this);
    mOutputThread = std::thread(&AudioEncoder::outputLoop, this);

    AE_LOGI("AudioEncoder started: %s", outputPath.c_str());
    return true;
}

// Thread 1: Read PCM from AAudio → push to queue (never blocks on codec)
void AudioEncoder::recordingLoop() {
    AE_LOGI("Recording loop started");

    const int kFramesPerRead = 4096;
    size_t bufferBytes = kFramesPerRead * mChannelCount * sizeof(int16_t);
    std::vector<uint8_t> buffer(bufferBytes);

    while (mRunning) {
        aaudio_result_t numFrames = AAudioStream_read(
                mRecordingStream, buffer.data(), kFramesPerRead, 1000000000LL);

        if (numFrames < 0) {
            AE_LOGE("AAudioStream_read error: %d", numFrames);
            break;
        }
        if (numFrames == 0) {
            continue;
        }

        int64_t ptsUs = 0;
        int64_t framePosition = 0;
        int64_t frameTimeNs = 0;
        aaudio_result_t tsResult = AAudioStream_getTimestamp(
                mRecordingStream, CLOCK_BOOTTIME, &framePosition, &frameTimeNs);

        if (tsResult == AAUDIO_OK && framePosition > 0) {
            int64_t firstFramePos = framePosition - numFrames;
            if (firstFramePos < 0) firstFramePos = 0;
            int64_t elapsedFrames = framePosition - firstFramePos;
            int64_t firstFrameNs = frameTimeNs -
                    (elapsedFrames * 1000000000LL / mSampleRate);
            ptsUs = (firstFrameNs - mBoottimeBaseNs) / 1000;
        } else {
            ptsUs = (getBoottimeNs() - mBoottimeBaseNs) / 1000;
        }
        mTotalFramesRead += numFrames;

        size_t dataBytes = (size_t)numFrames * mChannelCount * sizeof(int16_t);

        // Push to queue (drop oldest if full to keep latency bounded)
        {
            std::lock_guard<std::mutex> lock(mQueueMutex);
            if (mPcmQueue.size() >= kMaxQueueFrames) {
                mPcmQueue.pop();
                mTotalFramesDropped++;
            }
            PcmFrame frame;
            frame.ptsUs = ptsUs;
            frame.data.assign(buffer.data(), buffer.data() + dataBytes);
            mPcmQueue.push(std::move(frame));
        }
        mQueueCV.notify_one();
    }

    mRecordingDone = true;
    mQueueCV.notify_all();
    AE_LOGI("Recording loop exited (read=%lu, dropped=%lu)",
            (unsigned long)mTotalFramesRead, (unsigned long)mTotalFramesDropped);
}

// Thread 2: Pull PCM from queue → feed to codec (blocks until data available)
void AudioEncoder::inputLoop() {
    AE_LOGI("Input loop started");

    const size_t kAacFrameSamples = 1024;
    const size_t kAacFrameBytes = kAacFrameSamples * mChannelCount * sizeof(int16_t);
    const int64_t kAacFrameDurationUs = kAacFrameSamples * 1000000LL / mSampleRate;

    while (mRunning || !mPcmQueue.empty()) {
        PcmFrame frame;
        {
            std::unique_lock<std::mutex> lock(mQueueMutex);
            mQueueCV.wait_for(lock, std::chrono::milliseconds(10), [this] {
                return !mPcmQueue.empty() || mRecordingDone.load();
            });
            if (mPcmQueue.empty()) {
                if (mRecordingDone.load()) break;
                continue;
            }
            frame = std::move(mPcmQueue.front());
            mPcmQueue.pop();
        }

        // Split PCM data into AAC-sized chunks (1024 samples each) and feed individually
        size_t offset = 0;
        int64_t chunkPts = frame.ptsUs;
        while (offset + kAacFrameBytes <= frame.data.size()) {
            ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(mCodec, 1000000);
            if (inputIndex < 0) {
                AE_LOGE("dequeueInputBuffer failed: %zd", inputIndex);
                mTotalFramesDropped++;
                break;
            }

            size_t bufferSize = 0;
            uint8_t* inputBuffer = AMediaCodec_getInputBuffer(mCodec, inputIndex, &bufferSize);
            if (!inputBuffer) {
                AE_LOGE("getInputBuffer failed");
                break;
            }

            size_t copySize = (kAacFrameBytes < bufferSize) ? kAacFrameBytes : bufferSize;
            memcpy(inputBuffer, frame.data.data() + offset, copySize);

            media_status_t status = AMediaCodec_queueInputBuffer(
                    mCodec, inputIndex, 0, copySize, chunkPts, 0);
            if (status != AMEDIA_OK) {
                AE_LOGE("queueInputBuffer failed: %d", status);
            }

            offset += kAacFrameBytes;
            chunkPts += kAacFrameDurationUs;
        }
    }

    // Signal end of stream
    ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(mCodec, 10000000);  // 10s timeout
    if (inputIndex >= 0) {
        AMediaCodec_queueInputBuffer(
                mCodec, inputIndex, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        AE_LOGI("Queued EOS buffer");
    } else {
        AE_LOGE("Failed to queue EOS buffer: %zd", inputIndex);
    }

    AE_LOGI("Input loop exited");
}

void AudioEncoder::outputLoop() {
    AE_LOGI("Output loop started");

    bool eosReceived = false;
    while (mRunning && !eosReceived) {
        AMediaCodecBufferInfo info;
        ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(mCodec, &info, 5000);

        if (outIndex >= 0) {
            size_t outSize;
            uint8_t* outBuf = AMediaCodec_getOutputBuffer(mCodec, outIndex, &outSize);

            if (!mMuxerStarted && outBuf) {
                AMediaFormat* outputFormat = AMediaCodec_getOutputFormat(mCodec);
                mAudioTrackIndex = AMediaMuxer_addTrack(mMuxer, outputFormat);
                AMediaMuxer_start(mMuxer);
                mMuxerStarted = true;
                AE_LOGI("Muxer started, audio track index: %d", mAudioTrackIndex);
            }

            bool isConfig = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;

            if (mMuxerStarted && outBuf) {
                AMediaMuxer_writeSampleData(mMuxer, mAudioTrackIndex, outBuf, &info);
                mLastPtsUs = info.presentationTimeUs;

                if (!isConfig) {
                    int64_t absoluteNs = mBoottimeBaseNs + info.presentationTimeUs * 1000;
                    std::string text = std::to_string(absoluteNs);
                    AMediaCodecBufferInfo textInfo;
                    memset(&textInfo, 0, sizeof(textInfo));
                    textInfo.offset = 0;
                    textInfo.size = text.size();
                    textInfo.presentationTimeUs = info.presentationTimeUs;
                    textInfo.flags = 0;
                    AMediaMuxer_writeSampleData(mMuxer, mTextTrackIndex,
                                                 (const uint8_t*)text.c_str(), &textInfo);
                }
            }

            bool isEOS = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            if (isEOS) {
                AE_LOGI("Received EOS flag");
            }

            AMediaCodec_releaseOutputBuffer(mCodec, outIndex, false);
            if (isEOS) {
                eosReceived = true;
            }
        } else if (outIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            if (!mMuxerStarted) {
                AMediaFormat* newFormat = AMediaCodec_getOutputFormat(mCodec);
                mAudioTrackIndex = AMediaMuxer_addTrack(mMuxer, newFormat);
                AMediaMuxer_start(mMuxer);
                mMuxerStarted = true;
                AE_LOGI("Muxer started (format change), audio track index: %d", mAudioTrackIndex);
            }
        }
    }

    // Process remaining output buffers until EOS
    AE_LOGI("Processing remaining buffers");
    int maxIterations = 100;
    while (!eosReceived && maxIterations-- > 0) {
        AMediaCodecBufferInfo info;
        ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(mCodec, &info, 5000);

        if (outIndex >= 0) {
            size_t outSize;
            uint8_t* outBuf = AMediaCodec_getOutputBuffer(mCodec, outIndex, &outSize);

            if (!mMuxerStarted && outBuf) {
                AMediaFormat* outputFormat = AMediaCodec_getOutputFormat(mCodec);
                mAudioTrackIndex = AMediaMuxer_addTrack(mMuxer, outputFormat);
                AMediaMuxer_start(mMuxer);
                mMuxerStarted = true;
            }

            bool isConfig = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;

            if (mMuxerStarted && outBuf) {
                AMediaMuxer_writeSampleData(mMuxer, mAudioTrackIndex, outBuf, &info);
                mLastPtsUs = info.presentationTimeUs;

                if (!isConfig) {
                    int64_t absoluteNs = mBoottimeBaseNs + info.presentationTimeUs * 1000;
                    std::string text = std::to_string(absoluteNs);
                    AMediaCodecBufferInfo textInfo;
                    memset(&textInfo, 0, sizeof(textInfo));
                    textInfo.offset = 0;
                    textInfo.size = text.size();
                    textInfo.presentationTimeUs = info.presentationTimeUs;
                    textInfo.flags = 0;
                    AMediaMuxer_writeSampleData(mMuxer, mTextTrackIndex,
                                                 (const uint8_t*)text.c_str(), &textInfo);
                }
            }

            bool isEOS = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            AMediaCodec_releaseOutputBuffer(mCodec, outIndex, false);
            if (isEOS) {
                eosReceived = true;
            }
        } else if (outIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            if (!mMuxerStarted) {
                AMediaFormat* newFormat = AMediaCodec_getOutputFormat(mCodec);
                mAudioTrackIndex = AMediaMuxer_addTrack(mMuxer, newFormat);
                AMediaMuxer_start(mMuxer);
                mMuxerStarted = true;
            }
        } else {
            break;
        }
    }

    AE_LOGI("Output loop exited (EOS=%s)", eosReceived ? "true" : "false");
}

void AudioEncoder::stop() {
    if (!mRunning) {
        return;
    }

    AE_LOGI("AudioEncoder stopping: %s", mOutputPath.c_str());

    mRunning = false;

    // Stop AAudio stream to unblock AAudioStream_read
    if (mRecordingStream) {
        AAudioStream_requestStop(mRecordingStream);
    }

    // Wake up inputLoop so it can process remaining queue + EOS
    mQueueCV.notify_all();

    // Join threads in order: recording → input → output
    if (mRecordingThread.joinable()) {
        mRecordingThread.join();
    }
    if (mInputThread.joinable()) {
        mInputThread.join();
    }
    if (mOutputThread.joinable()) {
        mOutputThread.join();
    }

    // Close AAudio stream
    if (mRecordingStream) {
        AAudioStream_close(mRecordingStream);
        mRecordingStream = nullptr;
    }

    // Stop and delete codec
    if (mCodec) {
        AMediaCodec_stop(mCodec);
        AMediaCodec_delete(mCodec);
        mCodec = nullptr;
    }

    // Stop and delete muxer
    if (mMuxer) {
        AMediaMuxer_stop(mMuxer);
        AMediaMuxer_delete(mMuxer);
        AE_LOGI("Muxer stopped and deleted for %s", mOutputPath.c_str());
        mMuxer = nullptr;
    }

    mMuxerStarted = false;
    mAudioTrackIndex = -1;
    mTextTrackIndex = -1;

    // Clear any remaining queue
    {
        std::lock_guard<std::mutex> lock(mQueueMutex);
        while (!mPcmQueue.empty()) mPcmQueue.pop();
    }

    AE_LOGI("AudioEncoder stopped: %s (read=%lu, dropped=%lu)",
            mOutputPath.c_str(),
            (unsigned long)mTotalFramesRead,
            (unsigned long)mTotalFramesDropped);
}
