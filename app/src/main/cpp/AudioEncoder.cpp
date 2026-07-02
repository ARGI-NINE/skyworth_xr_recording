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

    // Fragmented-MP4 writer: ftyp+moov are written lazily from outputLoop()
    // once the codec reports the audio output format (csd-0 / sample rate).
    if (!mFmp4.open(mOutputPath)) {
        AE_LOGE("FMP4Writer open failed for %s", mOutputPath.c_str());
        AMediaCodec_stop(mCodec);
        AMediaCodec_delete(mCodec);
        mCodec = nullptr;
        return;
    }
    mFmp4Started = false;

    // Open the per-packet metainfo CSV next to the output m4a:
    // audio.m4a -> audio_metainfo.csv
    const std::string csvPath = mOutputPath.substr(0, mOutputPath.size() - 4) + "_metainfo.csv";
    mAudioMetaFile = fopen(csvPath.c_str(), "w");
    if (mAudioMetaFile) {
        fprintf(mAudioMetaFile, "packet_index,pts_us,capture_utc_ns\n");
        fflush(mAudioMetaFile);
    } else {
        AE_LOGE("Failed to open audio metainfo csv: %s", csvPath.c_str());
    }
    mPacketIndex = 0;
    mFirstPtsUs = -1;
    mWarmupFramesRead = 0;

    AE_LOGI("Encoder initialized: %s (sampleRate=%d, bitRate=%d, channels=%d, gain=%.2f)",
            mOutputPath.c_str(), mSampleRate, mBitRate, mChannelCount, mAudioGain);
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
    mFinished = false;

    initEncoder(outputPath);
    if (!mCodec) {
        AE_LOGE("Failed to initialize encoder");
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

        // --- Warm-up gate: discard first ~200ms to avoid AAudio cold-start ---
        // transient. The hardware DMA can emit a loud impulse (~8768 peak at
        // ~71ms) on the first real burst after silence.  Skipping frames here
        // (before queue push) keeps the encoder timeline clean.
        if (mWarmupFramesRead < kWarmupFrames) {
            mWarmupFramesRead += numFrames;
            if (mWarmupFramesRead < kWarmupFrames) {
                continue;  // still in warm-up, drop this chunk silently
            }
            // Warm-up just completed; trim this chunk to only the post-warm-up tail.
            int64_t excess = mWarmupFramesRead - kWarmupFrames;
            int64_t trimFrames = numFrames - excess;
            if (trimFrames > 0 && excess > 0) {
                size_t trimBytes = (size_t)trimFrames * mChannelCount * sizeof(int16_t);
                buffer.erase(buffer.begin(), buffer.begin() + trimBytes);
                dataBytes -= trimBytes;
                numFrames = excess;
                // Adjust ptsUs forward by the trimmed duration.
                ptsUs += trimFrames * 1000000LL / mSampleRate;
            }
        }

        // --- Software gain: compensate for missing AGC in LOW_LATENCY mode ---
        // MediaRecorder's MIC source applies AGC; AAudio LOW_LATENCY bypasses
        // it, resulting in ~3-8 dB lower volume in the output AAC.
        if (mAudioGain != 1.0f) {
            int16_t* samples = reinterpret_cast<int16_t*>(buffer.data());
            size_t numSamples = dataBytes / sizeof(int16_t);
            for (size_t i = 0; i < numSamples; ++i) {
                int32_t amplified = static_cast<int32_t>(samples[i] * mAudioGain);
                // Clamp to int16 range
                if (amplified > 32767) amplified = 32767;
                if (amplified < -32768) amplified = -32768;
                samples[i] = static_cast<int16_t>(amplified);
            }
        }

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

            // Lazily start the FMP4Writer on the first usable output: pull the
            // codec's csd-0 (AAC AudioSpecificConfig) so the moov/stsd is complete.
            if (!mFmp4Started && outBuf) {
                startFmp4FromFormat();
            }

            bool isConfig = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;

            // csd is already embedded in moov via esds; skip codec-config
            // buffers and the empty EOS trailer (info.size == 0). For audio
            // every packet is a sync sample. info.offset is honored: some
            // codecs place sample data at a non-zero offset. ptsTs uses the
            // audio track's sample-rate timescale.
            if (mFmp4Started && outBuf && !isConfig && info.size > 0) {
                // Zero-base the muxer PTS + CSV pts_us from this recording's
                // first AAC sample so the fMP4 audio timeline starts at 0,
                // matching CameraEncoder/video. capture_utc_ns keeps using the
                // ABSOLUTE info.presentationTimeUs (mBoottimeBaseNs is absolute).
                if (mFirstPtsUs < 0) mFirstPtsUs = info.presentationTimeUs;
                const int64_t ptsRel = info.presentationTimeUs - mFirstPtsUs;

                const int64_t ptsTs =
                        ptsRel * (int64_t)mAudioSampleRate / 1000000LL;
                mFmp4.writeSample(outBuf + info.offset, info.size, ptsTs, /*isSync=*/true);
                mLastPtsUs = ptsRel;

                // One CSV row per AAC output sample. capture_utc_ns reuses the
                // same BOOTTIME derivation as the old text track:
                //   mBoottimeBaseNs + absPtsUs*1000  (+ BOOTTIME->REALTIME offset).
                if (mAudioMetaFile) {
                    const int64_t captureUtcNs =
                            mBoottimeBaseNs + info.presentationTimeUs * 1000 + mTimeOffsetNs;
                    fprintf(mAudioMetaFile, "%llu,%lld,%lld\n",
                            (unsigned long long)mPacketIndex,
                            (long long)ptsRel,
                            (long long)captureUtcNs);
                    fflush(mAudioMetaFile);
                    ++mPacketIndex;
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
            if (!mFmp4Started) {
                startFmp4FromFormat();
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

            if (!mFmp4Started && outBuf) {
                startFmp4FromFormat();
            }

            bool isConfig = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;

            if (mFmp4Started && outBuf && !isConfig && info.size > 0) {
                if (mFirstPtsUs < 0) mFirstPtsUs = info.presentationTimeUs;
                const int64_t ptsRel = info.presentationTimeUs - mFirstPtsUs;

                const int64_t ptsTs =
                        ptsRel * (int64_t)mAudioSampleRate / 1000000LL;
                mFmp4.writeSample(outBuf + info.offset, info.size, ptsTs, /*isSync=*/true);
                mLastPtsUs = ptsRel;

                if (mAudioMetaFile) {
                    const int64_t captureUtcNs =
                            mBoottimeBaseNs + info.presentationTimeUs * 1000 + mTimeOffsetNs;
                    fprintf(mAudioMetaFile, "%llu,%lld,%lld\n",
                            (unsigned long long)mPacketIndex,
                            (long long)ptsRel,
                            (long long)captureUtcNs);
                    fflush(mAudioMetaFile);
                    ++mPacketIndex;
                }
            }

            bool isEOS = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            AMediaCodec_releaseOutputBuffer(mCodec, outIndex, false);
            if (isEOS) {
                eosReceived = true;
            }
        } else if (outIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            if (!mFmp4Started) {
                startFmp4FromFormat();
            }
        } else {
            break;
        }
    }

    AE_LOGI("Output loop exited (EOS=%s)", eosReceived ? "true" : "false");
}

bool AudioEncoder::startFmp4FromFormat() {
    if (mFmp4Started) return true;

    AMediaFormat* fmt = AMediaCodec_getOutputFormat(mCodec);
    if (!fmt) {
        AE_LOGE("getOutputFormat returned null; cannot start FMP4Writer");
        return false;
    }

    int32_t sampleRate = mSampleRate;
    int32_t channelCount = mChannelCount;
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
    AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channelCount);

    // csd-0 for AAC = AudioSpecificConfig (raw bytes, not length-prefixed).
    void* csd0Void = nullptr;
    size_t csd0Len = 0;
    AMediaFormat_getBuffer(fmt, "csd-0", &csd0Void, &csd0Len);

    mAudioSampleRate = sampleRate;
    mAudioChannels = channelCount;

    mFmp4.setAudioTrack(sampleRate, channelCount,
                        static_cast<const uint8_t*>(csd0Void), csd0Len);
    const bool ok = mFmp4.start();
    AMediaFormat_delete(fmt);

    if (ok) {
        mFmp4Started = true;
        AE_LOGI("FMP4Writer started for %s (sampleRate=%d, channels=%d, csd-0 %zu bytes)",
                mOutputPath.c_str(), sampleRate, channelCount, csd0Len);
    } else {
        AE_LOGE("FMP4Writer start() FAILED for %s — samples will be dropped",
                mOutputPath.c_str());
    }
    return ok;
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

    // Finalize the fragmented mp4. close() is safe even if start() was never
    // reached (no frames emitted) — it just closes the fd.
    if (mFmp4Started) {
        mFmp4.close();
        AE_LOGI("FMP4Writer closed for %s", mOutputPath.c_str());
    } else {
        mFmp4.close();
    }
    mFmp4Started = false;

    // Close the per-packet metainfo CSV.
    if (mAudioMetaFile) {
        fflush(mAudioMetaFile);
        fclose(mAudioMetaFile);
        mAudioMetaFile = nullptr;
    }

    // Clear any remaining queue
    {
        std::lock_guard<std::mutex> lock(mQueueMutex);
        while (!mPcmQueue.empty()) mPcmQueue.pop();
    }

    AE_LOGI("AudioEncoder stopped: %s (read=%lu, dropped=%lu, packets=%llu)",
            mOutputPath.c_str(),
            (unsigned long)mTotalFramesRead,
            (unsigned long)mTotalFramesDropped,
            (unsigned long long)mPacketIndex);
    mFinished = true;
}
