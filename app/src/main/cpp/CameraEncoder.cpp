#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/stat.h>
#include "CameraEncoder.h"


namespace SXR {
    // Constructor for RGB cameras (Surface mode)
    CameraEncoder::CameraEncoder(int width, int height, int frameRate, int bitRate,
                                  const std::string& outputName, const std::string& baseDir)
        : mCameraId(-1), mWidth(width), mHeight(height), mFrameRate(frameRate),
          mBitRate(bitRate), mType(EncoderType::RGB), mMode(EncoderMode::SURFACE),
          mOutputName(outputName), mBaseDir(baseDir) {
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
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, mFrameRate);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);

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

        // Set output path based on encoder type
        const std::string baseDir = mBaseDir.empty()
            ? "/sdcard/Android/data/com.ssnwt.helloxr/files"
            : mBaseDir;
        mkdir(baseDir.c_str(), 0777);
        if (mType == EncoderType::RGB) {
            mOutputPath = baseDir + "/" + mOutputName;
        } else {
            mOutputPath = baseDir + "/" + mGroupName + ".mp4";
        }
        LOGI("Output path: %s, size: %dx%d, fps: %d, mode: %s",
             mOutputPath.c_str(), mWidth, mHeight, mFrameRate,
             mMode == EncoderMode::SURFACE ? "Surface" : "Buffer");
        unlink(mOutputPath.c_str());
        int fd = open(mOutputPath.c_str(), O_CREAT | O_RDWR, 0666);
        mMuxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);

        // Add text track for timestamps
        AMediaFormat* textFormat = AMediaFormat_new();
        AMediaFormat_setString(textFormat, AMEDIAFORMAT_KEY_MIME, "application/x-subrip");
        AMediaFormat_setString(textFormat, AMEDIAFORMAT_KEY_LANGUAGE, "und");
        AMediaFormat_setInt32(textFormat, AMEDIAFORMAT_KEY_IS_FORCED_SUBTITLE, 0);
        AMediaFormat_setInt32(textFormat, AMEDIAFORMAT_KEY_IS_AUTOSELECT, 0);
        mTextTrackIndex = AMediaMuxer_addTrack(mMuxer, textFormat);
        AMediaFormat_delete(textFormat);
    }

    bool CameraEncoder::start() {
        initEncoder();

        if (mMode == EncoderMode::SURFACE && !mInputSurface) {
            LOGE("Failed to create input surface");
            return false;
        }

        if (!mCodec) {
            LOGE("Failed to create codec");
            return false;
        }

        mRunning = true;
        mOutputThread = std::thread(&CameraEncoder::outputLoop, this);

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
        if (!mRunning) {
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

        if (mMuxer) {
            AMediaMuxer_stop(mMuxer);
            AMediaMuxer_delete(mMuxer);
            LOGI("Muxer stopped and deleted for %s", mOutputPath.c_str());
            mMuxer = nullptr;
        }

        LOGI("CameraEncoder stopped for %s", mOutputPath.c_str());
    }

    ANativeWindow* CameraEncoder::getInputSurface() {
        return mInputSurface;
    }

    void CameraEncoder::signalEndOfInputStream() {
        if (mMode == EncoderMode::SURFACE && mCodec && mInputSurface) {
            AMediaCodec_signalEndOfInputStream(mCodec);
        }
    }

    void CameraEncoder::submitNsTimestamp(int64_t timestampNs) {
        std::lock_guard<std::mutex> lock(mNsMutex);
        mNsQueue.push(timestampNs);
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

        // Process remaining output buffers until we get EOS
        LOGI("Processing remaining buffers for %s", nameStr);
        int maxIterations = 100;
        while (!eosReceived && maxIterations-- > 0) {
            eosReceived = processOutputBuffer();
        }

        LOGI("Output loop exited for %s (EOS=%s)", nameStr, eosReceived ? "true" : "false");
    }

    // Returns true if EOS was received
    bool CameraEncoder::processOutputBuffer() {
        AMediaCodecBufferInfo info;
        ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(mCodec, &info, 5000);

        if (outIndex >= 0) {
            size_t outSize;
            uint8_t* outBuf = AMediaCodec_getOutputBuffer(mCodec, outIndex, &outSize);

            if (!mMuxerStarted && outBuf) {
                AMediaFormat* newFormat = AMediaCodec_getOutputFormat(mCodec);
                mTrackIndex = AMediaMuxer_addTrack(mMuxer, newFormat);
                AMediaMuxer_start(mMuxer);
                mMuxerStarted = true;
                LOGI("Muxer started for %s", mOutputPath.c_str());
            }

            bool isConfig = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;

            if (mMuxerStarted && outBuf) {
                AMediaMuxer_writeSampleData(mMuxer, mTrackIndex, outBuf, &info);
                mLastPtsUs = info.presentationTimeUs;

                // Write ns timestamp to text track (skip codec config buffers)
                if (!isConfig) {
                    int64_t nsTimestamp = 0;
                    {
                        std::lock_guard<std::mutex> lock(mNsMutex);
                        if (!mNsQueue.empty()) {
                            nsTimestamp = mNsQueue.front();
                            mNsQueue.pop();
                        }
                    }
                    if (nsTimestamp != 0) {
                        std::string text = std::to_string(nsTimestamp);
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
            if (!mMuxerStarted) {
                mTrackIndex = AMediaMuxer_addTrack(mMuxer, newFormat);
                AMediaMuxer_start(mMuxer);
                mMuxerStarted = true;
                LOGI("Muxer started (format change) for %s", mOutputPath.c_str());
            }
            return false;
        } else if (outIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            return false;
        }
        return false;
    }
}
