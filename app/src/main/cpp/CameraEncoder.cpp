#include <unistd.h>
#include <cstring>
#include <sys/stat.h>
#include <unordered_map>
#include <android/api-level.h>   // android_get_device_api_level()
#include "CameraEncoder.h"
#include "OperationCoordinator.h"
#include "SdkStateBridge.h"

// ======== Global output listener (class-level singleton) ========
static SXR::IEncoderOutputListener* s_listener = nullptr;

void SXR::CameraEncoder::setOutputListener(IEncoderOutputListener* listener) {
    s_listener = listener;
}

void SXR::CameraEncoder::requestKeyFrame(AMediaCodec* codec, const std::string& group) {
    if (!codec) return;
    AMediaFormat* params = AMediaFormat_new();
    AMediaFormat_setInt32(params, "request-sync", 1);
    media_status_t status = AMediaCodec_setParameters(codec, params);
    AMediaFormat_delete(params);
    if (status == AMEDIA_OK) {
        LOGI("Keyframe requested for %s", group.c_str());
    } else {
        LOGE("Keyframe request failed for %s, status: %d", group.c_str(), status);
    }
}

namespace SXR {
    // Constructor for RGB cameras (Surface mode)
    CameraEncoder::CameraEncoder(int width, int height, int frameRate, int bitRate,
                                  const std::string& outputName, const std::string& baseDir)
        : mCameraId(-1), mWidth(width), mHeight(height), mFrameRate(frameRate),
          mBitRate(bitRate), mType(EncoderType::RGB), mMode(EncoderMode::SURFACE),
          mGroupName("rgb"), mOutputName(outputName), mBaseDir(baseDir) {
    }

    // Constructor for grayscale cameras (Buffer mode)
    CameraEncoder::CameraEncoder(const std::string& groupName, int width, int height, int frameRate, const std::string& baseDir)
        : mCameraId(-1), mWidth(width), mHeight(height), mFrameRate(frameRate),
          mBitRate(4000000), mType(EncoderType::GRAYSCALE), mMode(EncoderMode::BUFFER),
          mGroupName(groupName), mBaseDir(baseDir) {
    }

    // Constructor for grayscale cameras with explicit mode
    CameraEncoder::CameraEncoder(const std::string& groupName, int width, int height,
                                  int frameRate, EncoderMode mode, const std::string& baseDir)
        : mCameraId(-1), mWidth(width), mHeight(height), mFrameRate(frameRate),
          mBitRate(4000000), mType(EncoderType::GRAYSCALE), mMode(mode),
          mGroupName(groupName), mBaseDir(baseDir) {
    }

    CameraEncoder::~CameraEncoder() {
        stop();
    }


    void CameraEncoder::initEncoder() {
        mCodec = AMediaCodec_createEncoderByType("video/hevc");

        AMediaFormat *format = AMediaFormat_new();
        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/hevc");
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, mWidth);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, mHeight);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, mBitRate);
        // Qualcomm HEVC encoder requires KEY_FRAME_RATE (configure fails
        // without it). This value is a bitrate-allocation hint — actual
        // frame rate is determined by input PTS intervals, not this field.
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, mFrameRate);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);

        // Disable B-frames so output PTS is monotonic (decode order == display
        // order). Guarded by runtime device API, not the compile-time macro,
        // since minSdkVersion may be below 30 but the device runs higher.
        if (android_get_device_api_level() >= 30) {
            AMediaFormat_setInt32(format, "max-bframes", 0);   // AMEDIAFORMAT_KEY_MAX_B_FRAMES
            LOGI("max-bframes=0 set (device API %d)", android_get_device_api_level());
        } else {
            LOGW("device API %d < 30: max-bframes not set; PTS may reorder",
                 android_get_device_api_level());
        }

        if (mMode == EncoderMode::SURFACE) {
            // Color format for Surface mode
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7F000789);  // COLOR_FormatSurface

            // Color space configuration for HD content
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_STANDARD, 1);  // BT709
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_RANGE, 2);      // LIMITED
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_TRANSFER, 3);   // SDR_VIDEO
        } else {
            // Color format for Buffer mode - YUV420 flexible
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7F420888);  // COLOR_FormatYUV420Flexible
        }

        // Configure encoder
        media_status_t status = AMediaCodec_configure(
                mCodec,
                format,
                nullptr,
                nullptr,
                AMEDIACODEC_CONFIGURE_FLAG_ENCODE);

        if (status != AMEDIA_OK) {
            LOGE("AMediaCodec_configure failed: %d", status);
            return;
        }

        if (mMode == EncoderMode::SURFACE) {
            // Create input surface for zero-copy encoding
            status = AMediaCodec_createInputSurface(mCodec, &mInputSurface);
            if (status != AMEDIA_OK || !mInputSurface) {
                LOGE("AMediaCodec_createInputSurface failed: %d", status);
                return;
            }
        }

        AMediaCodec_start(mCodec);

        // When baseDir is empty, skip file output — this encoder is for
        // WebSocket preview streaming only (no MP4 saved to disk).
        if (!mBaseDir.empty()) {
            std::lock_guard<std::mutex> lock(mDiskMutex);
            mPendingBaseDir = mBaseDir;
            mBaseDir.clear();
            mDiskState = DiskState::ARMING;
        } else {
            LOGI("Output: streaming-only (no MP4), size: %dx%d, fps: %d",
                 mWidth, mHeight, mFrameRate);
        }
        mFrameIndex = 0;
        mFirstPtsUs = -1;   // zero-base PTS from this recording's first sample
    }

    bool CameraEncoder::start() {
        EncoderState expected = EncoderState::STOPPED;
        if (!mEncoderState.compare_exchange_strong(expected, EncoderState::STARTING))
            return expected == EncoderState::RUNNING;
        initEncoder();

        if (mMode == EncoderMode::SURFACE && !mInputSurface) {
            LOGE("Failed to create input surface");
            mEncoderState = EncoderState::STOPPED;
            return false;
        }

        if (!mCodec) {
            LOGE("Failed to create codec");
            mEncoderState = EncoderState::STOPPED;
            return false;
        }

        mRunning = true;
        mEncoderState = EncoderState::RUNNING;
        mOutputThread = std::thread(&CameraEncoder::outputLoop, this);
        {
            std::lock_guard<std::mutex> lock(mDiskMutex);
            if (mDiskState == DiskState::ARMING) requestKeyFrame(mCodec, mGroupName);
        }

        const char* typeStr = (mType == EncoderType::RGB) ? "RGB" : "grayscale";
        const char* nameStr = (mType == EncoderType::RGB)
            ? mOutputName.c_str()
            : mGroupName.c_str();
        const char* modeStr = (mMode == EncoderMode::SURFACE) ? "Surface" : "Buffer";
        LOGI("CameraEncoder started with %s mode for %s %s (%dx%d @ %dfps)",
             modeStr, typeStr, nameStr, mWidth, mHeight, mFrameRate);
        return true;
    }

    void CameraEncoder::stop() {
        EncoderState expected = EncoderState::RUNNING;
        if (!mEncoderState.compare_exchange_strong(expected, EncoderState::STOPPING)) {
            return;
        }

        LOGI("CameraEncoder stopping for %s", mOutputPath.c_str());

        // Signal output loop to exit — must set BEFORE join so the while(mRunning) breaks
        mRunning = false;

        if (mMode == EncoderMode::SURFACE) {
            if (mCodec && mInputSurface) {
                media_status_t status = AMediaCodec_signalEndOfInputStream(mCodec);
                LOGI("SignalEndOfInputStream result: %d", status);
            }
        } else {
            if (mCodec) {
                ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(mCodec, 500000);
                if (inputIndex >= 0) {
                    AMediaCodec_queueInputBuffer(mCodec, inputIndex, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    LOGI("Queued EOS buffer for Buffer mode");
                }
            }
        }

        // Wait for output thread — it will exit quickly because mRunning is now false
        if (mOutputThread.joinable()) {
            mOutputThread.join();
        }

        if (mCodec) {
            AMediaCodec_stop(mCodec);
            AMediaCodec_delete(mCodec);
            mCodec = nullptr;
        }

        if (mInputSurface) {
            ANativeWindow_release(mInputSurface);
            mInputSurface = nullptr;
        }

        // Finalize the fragmented mp4. close() is safe even if start() was never
        // reached (no frames emitted) — it just closes the fd.
        LOGI("CameraEncoder stopped for %s", mOutputPath.c_str());
        mEncoderState = EncoderState::STOPPED;
    }

    bool CameraEncoder::armWriter(const std::string& baseDir) {
        if (baseDir.empty() || !mRunning.load()) return false;
        {
            std::lock_guard<std::mutex> lock(mMetaMutex);
            mMetaQueue.clear();
        }
        {
            std::lock_guard<std::mutex> lock(mDiskMutex);
            if (mDiskState != DiskState::DETACHED) return mDiskState == DiskState::WRITING;
            mPendingBaseDir = baseDir;
            mDiskFailed = false;
            mDiskState = DiskState::ARMING;
        }
        requestKeyFrame(mCodec, mGroupName);
        return true;
    }

    bool CameraEncoder::waitWriterArmed(int timeoutMs) {
        std::unique_lock<std::mutex> lock(mDiskMutex);
        mDiskCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
            return mDiskState == DiskState::WRITING || mDiskFailed || !mRunning.load();
        });
        return mDiskState == DiskState::WRITING;
    }

    void CameraEncoder::finalizeWriter() {
        std::unique_lock<std::mutex> lock(mDiskMutex);
        if (mDiskState == DiskState::DETACHED) return;
        mDiskState = DiskState::FINALIZING;
        lock.unlock(); mDiskCv.notify_all(); lock.lock();
        mDiskCv.wait(lock, [this] { return mDiskState == DiskState::DETACHED || !mRunning.load(); });
    }

    bool CameraEncoder::hasWriter() const {
        std::lock_guard<std::mutex> lock(mDiskMutex);
        return mDiskState == DiskState::WRITING;
    }

    bool CameraEncoder::openWriterOnOutputThread() {
        mBaseDir = mPendingBaseDir;
        mOutputPath = mBaseDir + "/" + (mType == EncoderType::RGB ? mOutputName : mGroupName + ".mp4");
        mkdir(mBaseDir.c_str(), 0777); unlink(mOutputPath.c_str());
        if (!mFmp4.open(mOutputPath)) return false;
        const std::string csvPath = mOutputPath.substr(0, mOutputPath.size()-4) + "_metainfo.csv";
        mMetaFile = fopen(csvPath.c_str(), "w");
        if (!mMetaFile) { mFmp4.close(); return false; }
        fprintf(mMetaFile, "frame_index,frame_id,pts_us,exposure_start_utc_ns,exposure_duration_ns,gain,mid_exposure_utc_ns\n");
        mFmp4Started = false; mFirstPtsUs = -1; mFrameIndex = 0;
        return true;
    }

    void CameraEncoder::closeWriterOnOutputThread() {
        mFmp4.close(); mFmp4Started = false;
        if (mMetaFile) { fflush(mMetaFile); fclose(mMetaFile); mMetaFile = nullptr; }
        mBaseDir.clear(); mOutputPath.clear();
    }

    void CameraEncoder::handleDiskCommands() {
        std::lock_guard<std::mutex> lock(mDiskMutex);
        if (mDiskState == DiskState::ARMING && mOutputPath.empty()) {
            if (!openWriterOnOutputThread()) {
                mDiskFailed = true; mDiskState = DiskState::DETACHED; mDiskCv.notify_all();
            }
        } else if (mDiskState == DiskState::FINALIZING) {
            closeWriterOnOutputThread(); mDiskState = DiskState::DETACHED; mDiskCv.notify_all();
        }
    }

    bool CameraEncoder::isVerifiedHevcIdr(const uint8_t* data, size_t size, uint32_t flags) {
        if (!data || size < 2 || (flags & 0x1U) == 0) return false;
        size_t p = 0;
        while (p + 2 <= size) {
            size_t nal = p, next = size;
            if (p + 4 <= size && data[p] == 0 && data[p+1] == 0 && data[p+2] == 0 && data[p+3] == 1) nal = p + 4;
            else if (p + 3 <= size && data[p] == 0 && data[p+1] == 0 && data[p+2] == 1) nal = p + 3;
            else if (p + 4 <= size) {
                uint32_t n=(uint32_t(data[p])<<24)|(uint32_t(data[p+1])<<16)|(uint32_t(data[p+2])<<8)|data[p+3];
                if (n && p + 4 + n <= size) { nal = p + 4; next = p + 4 + n; }
            }
            if (nal < size) { const uint8_t type = (data[nal] >> 1) & 0x3f; if (type == 19 || type == 20) return true; }
            if (next <= p || next == size) break;
            p = next;
        }
        return false;
    }

    ANativeWindow* CameraEncoder::getInputSurface() {
        return mInputSurface;
    }

    void CameraEncoder::signalEndOfInputStream() {
        if (mMode == EncoderMode::SURFACE && mCodec && mInputSurface) {
            AMediaCodec_signalEndOfInputStream(mCodec);
        }
    }

    void CameraEncoder::submitFrameMeta(const FrameMeta& m) {
        std::lock_guard<std::mutex> lock(mMetaMutex);
        mMetaQueue.push_back(m);
    }

    // Feed frame data to encoder (Buffer mode only)
    bool CameraEncoder::feedFrame(const uint8_t* data, size_t size, int64_t timestampNs) {
        if (mMode != EncoderMode::BUFFER || !mCodec || !mRunning) {
            return false;
        }

        // Get input buffer
        ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(mCodec, 10000);  // 10ms timeout
        if (inputIndex < 0) {
            if (inputIndex != AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                LOGE("dequeueInputBuffer failed: %zd", inputIndex);
            }
            return false;
        }

        size_t bufferSize;
        uint8_t* inputBuffer = AMediaCodec_getInputBuffer(mCodec, inputIndex, &bufferSize);
        if (!inputBuffer) {
            LOGE("getInputBuffer failed");
            return false;
        }

        // Copy data to input buffer
        // For grayscale Y8, we need to convert to YUV420 (NV12 or I420)
        // Y8 is just the Y plane, so we fill Y and set UV to 128 (neutral)
        size_t ySize = mWidth * mHeight;
        size_t uvSize = ySize / 2;  // UV plane is 1/2 of Y for NV12
        size_t requiredSize = ySize + uvSize;

        if (bufferSize < requiredSize) {
            LOGE("Buffer too small: %zu < %zu", bufferSize, requiredSize);
            AMediaCodec_queueInputBuffer(mCodec, inputIndex, 0, 0, timestampNs / 1000, 0);
            return false;
        }

        // Copy Y plane
        size_t copySize = (size < ySize) ? size : ySize;
        memcpy(inputBuffer, data, copySize);

        // Fill UV plane with 128 (neutral gray for chroma)
        memset(inputBuffer + ySize, 128, uvSize);

        // Queue the buffer
        int64_t ptsUs = timestampNs / 1000;  // Convert ns to us
        media_status_t status = AMediaCodec_queueInputBuffer(
                mCodec, inputIndex, 0, requiredSize, ptsUs, 0);
        if (status != AMEDIA_OK) {
            LOGE("queueInputBuffer failed: %d", status);
            return false;
        }

        return true;
    }

    // Output processing loop
    void CameraEncoder::outputLoop() {
        const char* nameStr = (mType == EncoderType::RGB)
            ? mOutputName.c_str()
            : mGroupName.c_str();
        LOGI("Output loop started for %s", nameStr);

        bool eosReceived = false;
        while (mRunning && !eosReceived) {
            eosReceived = processOutputBuffer();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // After input admission is closed, EOS is the authoritative last-frame
        // boundary. Do not finalize on a fixed dequeue count.
        LOGI("Processing remaining buffers through EOS for %s", nameStr);
        const auto eosDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!eosReceived) {
            eosReceived = processOutputBuffer();
            if (!eosReceived && std::chrono::steady_clock::now() >= eosDeadline) {
                LOGE("event=encoder_eos_timeout group=%s", nameStr);
                sdk_state_bridge::ReportError("CameraEncoder", "encoder_eos_timeout",
                                              "MediaCodec EOS drain timed out", nameStr);
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mDiskMutex);
            closeWriterOnOutputThread();
            mDiskState = DiskState::DETACHED;
            mDiskCv.notify_all();
        }

        LOGI("event=encoder_eos_drained group=%s last_pts_us=%lld", nameStr,
             static_cast<long long>(mLastPtsUs));
    }

    // Returns true if EOS was received
    bool CameraEncoder::processOutputBuffer() {
        handleDiskCommands();
        AMediaCodecBufferInfo info;
        ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(mCodec, &info, 5000);

        if (outIndex >= 0) {
            size_t outSize;
            uint8_t* outBuf = AMediaCodec_getOutputBuffer(mCodec, outIndex, &outSize);

            bool isConfig = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
            FrameMeta outputMeta{};
            if (!isConfig && outBuf && info.size > 0) {
                std::lock_guard<std::mutex> lk(mMetaMutex);
                for (auto it = mMetaQueue.begin(); it != mMetaQueue.end(); ++it) {
                    const int64_t midUs = it->midExposureBootNs / 1000;
                    const int64_t startUs = it->exposureStartBootNs / 1000;
                    if (midUs == info.presentationTimeUs || startUs == info.presentationTimeUs) {
                        outputMeta = *it; mMetaQueue.erase(it); break;
                    }
                }
                while (mMetaQueue.size() > 240) mMetaQueue.pop_front();
            }

            // Start the writer only on a flag + NAL verified HEVC IDR.
            // codec's csd-0 (HEVC VPS+SPS+PPS) so the moov/stsd is complete.
            // Skip when mBaseDir is empty (streaming-only mode — no file output).
            if (!mFmp4Started && outBuf && !mBaseDir.empty() && !isConfig &&
                isVerifiedHevcIdr(outBuf + info.offset, info.size, info.flags)) {
                AMediaFormat* fmt = AMediaCodec_getOutputFormat(mCodec);
                const uint8_t* csd0 = nullptr;
                size_t csd0Len = 0;
                AMediaFormat_getBuffer(fmt, "csd-0",
                                       reinterpret_cast<void**>(
                                           const_cast<uint8_t**>(&csd0)), &csd0Len);
                mFmp4.setVideoTrack(mWidth, mHeight, 1000000, csd0, csd0Len,
                                     mFrameRate > 0 ? 1000000 / mFrameRate : 0);
                if (mFmp4.start()) {
                    mFmp4Started = true;
                    {
                        std::lock_guard<std::mutex> lock(mDiskMutex);
                        mDiskState = DiskState::WRITING;
                        mDiskCv.notify_all();
                    }
                    LOGI("FMP4Writer started for %s (csd-0 %zu bytes)",
                         mOutputPath.c_str(), csd0Len);
                } else {
                    LOGE("FMP4Writer start() FAILED for %s — samples will be dropped",
                         mOutputPath.c_str());
                    // do NOT set mFmp4Started; subsequent writeSample calls are skipped
                    {
                        std::lock_guard<std::mutex> lock(mDiskMutex);
                        mDiskFailed = true;
                        mDiskCv.notify_all();
                    }
                    sdk_state_bridge::ReportError("CameraEncoder", "writer_start_failed",
                                                  "fMP4 writer start failed", mGroupName.c_str());
                    operation::Coordinator::Instance().HandleRecordStop("writer start failure");
                }
                AMediaFormat_delete(fmt);
            }

            // === Streaming: forward codec config (VPS/SPS/PPS) to listener ===
            if (isConfig && outBuf && info.size > 0) {
                if (s_listener) {
                    s_listener->onEncodedFrame(mGroupName.c_str(),
                        outBuf + info.offset, info.size, info.presentationTimeUs, true);
                }
            }

            // csd is already embedded in moov; skip codec-config buffers and the
            // empty EOS trailer (info.size == 0) so the writer logs stay clean.
            // AMEDIACODEC_BUFFER_FLAG_KEY_FRAME (0x1) is not in the API-30 NDK
            // header (added API 31); use the bit value directly. info.offset is
            // honored: some codecs place sample data at a non-zero offset.
            if (mFmp4Started && outBuf && !isConfig && info.size > 0) {
                bool isKey = (info.flags & 0x1) != 0;

                // info.presentationTimeUs is an absolute CLOCK_BOOTTIME PTS
                // (Surface-mode MediaCodec). Zero-base it from this recording's
                // first sample so the fMP4 timeline starts at 0 — matching the
                // old AMediaMuxer behaviour and what players/tools expect.
                if (mFirstPtsUs < 0) mFirstPtsUs = info.presentationTimeUs;
                const int64_t ptsRel = info.presentationTimeUs - mFirstPtsUs;

                const bool sampleWritten =
                        mFmp4.writeSample(outBuf + info.offset, info.size, ptsRel, isKey);
                if (!sampleWritten) {
                    sdk_state_bridge::ReportError("CameraEncoder", "writer_write_failed",
                                                  "fMP4 sample write failed", mGroupName.c_str());
                    operation::Coordinator::Instance().HandleRecordStop("writer write failure");
                }
                if (sampleWritten) mLastPtsUs = ptsRel;

                // Emit one CSV row per written sample (in encoder output order).
                // Pop the matching FrameMeta staged by the producer thread.
                const FrameMeta& fm = outputMeta;
                if (sampleWritten && mMetaFile) {
                    // pts_us is the zero-based PTS (matches the mp4 sample's PTS
                    // exactly). The UTC columns stay absolute: they come from
                    // FrameMeta (midExposureBoot/exposureStartBoot) plus the
                    // BOOTTIME→REALTIME offset, independent of the muxer PTS.
                    const int64_t startUtc = fm.utcTime;
                    const int64_t midUtc = fm.bootTime + mTimeOffsetNs;
                    fprintf(mMetaFile, "%llu,%u,%lld,%lld,%u,%u,%lld\n",
                            (unsigned long long)mFrameIndex, fm.frameId,
                            (long long)ptsRel,
                            (long long)startUtc, fm.exposure, fm.gain,
                            (long long)midUtc);
                    fflush(mMetaFile);
                    ++mFrameIndex;
                }
            }

            // === Streaming: forward non-config frames to listener ===
            if (outBuf && !isConfig && info.size > 0) {
                if (s_listener) {
                    s_listener->onEncodedFrame(mGroupName.c_str(),
                        outBuf + info.offset, info.size, info.presentationTimeUs, false);
                }
            }

            bool isEOS = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            if (isEOS) {
                LOGI("Received EOS flag for %s", mOutputPath.c_str());
            }

            AMediaCodec_releaseOutputBuffer(mCodec, outIndex, false);
            return isEOS;
        } else if (outIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* newFormat = AMediaCodec_getOutputFormat(mCodec);
            LOGI("Output format changed for %s", mOutputPath.c_str());
            AMediaFormat_delete(newFormat);
            return false;
        } else if (outIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            return false;
        }
        return false;
    }
}
