#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace SXR {

// Minimal fragmented-MP4 (ISO BMFF) writer. One fragment per sample.
// Portable: POSIX file I/O only; logging guarded for host build/tests.
class FMP4Writer {
public:
    FMP4Writer();
    ~FMP4Writer();

    bool open(const std::string& path);

    // Codec config (csd) embedded in moov stsd. Call before start().
    // Video: csd0 = HEVC VPS+SPS+PPS (length-prefixed NALU), stored as hvcC.
    void setVideoTrack(int width, int height, int32_t timescale,
                       const uint8_t* csd0, size_t csd0Len,
                       int32_t sampleIntervalUs = 0);
    // Audio: csd0 = AAC AudioSpecificConfig, stored as esds inside mp4a.
    void setAudioTrack(int sampleRate, int channels,
                       const uint8_t* csd0, size_t csd0Len);

    // Writes ftyp + moov (up front). Exactly one track must be set.
    bool start();

    // One encoded sample -> one styp+moof+mdat fragment. Video retains one
    // sample until the next PTS (or close()) determines its duration.
    // ptsUs: presentation time, in the track's own timescale (video: µs since
    //        timescale=1e6; audio: sampleRate units, caller converts). isSync: true for I-frame.
    bool writeSample(const uint8_t* data, size_t size, int64_t ptsUs, bool isSync);

    bool close();

private:
    struct Impl;
    Impl* m;  // holds fd, track config, counters; defined in .cpp
};

} // namespace SXR
