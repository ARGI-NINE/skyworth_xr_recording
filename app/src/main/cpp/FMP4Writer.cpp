#include "FMP4Writer.h"
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#define FMP4_LOGE(...) __android_log_print(ANDROID_LOG_ERROR,"FMP4Writer",__VA_ARGS__)
#define FMP4_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "FMP4Writer",__VA_ARGS__)
#define FMP4_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "FMP4Writer",__VA_ARGS__)
#else
#include <cstdio>
#define FMP4_LOGE(...) do{fprintf(stderr,"E/FMP4Writer: " __VA_ARGS__); fputc('\n',stderr);}while(0)
#define FMP4_LOGW(...) do{fprintf(stderr,"W/FMP4Writer: " __VA_ARGS__); fputc('\n',stderr);}while(0)
#define FMP4_LOGI(...) do{fprintf(stdout,"I/FMP4Writer: " __VA_ARGS__); fputc('\n',stdout);}while(0)
#endif

namespace SXR {

// ---------------------------------------------------------------------------
// Big-endian writers into a byte buffer.
// ---------------------------------------------------------------------------
static void be32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}
static void be16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}
static void be64(std::vector<uint8_t>& b, uint64_t v) {
    be32(b, static_cast<uint32_t>(v >> 32));
    be32(b, static_cast<uint32_t>(v & 0xFFFFFFFFu));
}
static void bytes(std::vector<uint8_t>& b, const void* p, size_t n) {
    const uint8_t* q = static_cast<const uint8_t*>(p);
    b.insert(b.end(), q, q + n);
}
static void fourcc(std::vector<uint8_t>& b, const char tag[4]) {
    b.insert(b.end(), tag, tag + 4);
}

// Wrap a payload as a box: [size(4) type(4) payload].
static std::vector<uint8_t> box(const char type[4], std::vector<uint8_t> payload) {
    std::vector<uint8_t> h;
    be32(h, static_cast<uint32_t>(8 + payload.size()));
    fourcc(h, type);
    h.insert(h.end(), payload.begin(), payload.end());
    return h;
}
// Wrap as a FullBox: [size(4) type(4) version(1) flags(3) payload].
static std::vector<uint8_t> fullbox(const char type[4], uint8_t version,
                                    uint32_t flags, std::vector<uint8_t> payload) {
    std::vector<uint8_t> body;
    body.push_back(version);
    body.push_back(static_cast<uint8_t>((flags >> 16) & 0xFF));
    body.push_back(static_cast<uint8_t>((flags >> 8) & 0xFF));
    body.push_back(static_cast<uint8_t>(flags & 0xFF));
    body.insert(body.end(), payload.begin(), payload.end());
    return box(type, std::move(body));
}

// ---------------------------------------------------------------------------
// HEVC hvcC construction from a length-prefixed csd buffer.
// csd is a sequence of [4-byte BE length][nalu bytes]. We extract the NALUs and
// pack them into NALU arrays grouped by type-class. If parsing yields nothing,
// we fall back to carrying the raw bytes verbatim — still structurally valid.
// ---------------------------------------------------------------------------
struct HvcCInfo {
    uint8_t configVersion = 1;
    uint8_t generalProfileSpace = 0;
    uint8_t generalTierFlag = 0;
    uint8_t generalProfileIdc = 0;
    uint32_t generalProfileCompat = 0;
    uint8_t generalConstraintFlags[6] = {0,0,0,0,0,0};
    uint8_t generalLevelIdc = 0;
    uint8_t minSpatialSegmentation = 0;   // with 4 reserved bits -> 0xF0
    uint8_t parallelismType = 0;          // with 6 reserved bits -> 0xFC
    uint8_t chromaFormat = 1;             // with 6 reserved bits -> 0xFC, 1=4:2:0
    uint8_t bitDepthLuma = 0;             // with 5 reserved bits -> 0xF8
    uint8_t bitDepthChroma = 0;           // with 5 reserved bits -> 0xF8
    uint16_t avgFrameRate = 0;
    uint8_t constantFrameRate = 0;
    uint8_t numTemporalLayers = 1;
    uint8_t temporalIdNested = 1;
    uint8_t lengthSizeMinusOne = 3;       // 4-byte length prefix
};

// Extract NAL units from a csd buffer. Android/MediaCodec csd can be either:
//   - length-prefixed (4-byte BE length per NALU), or
//   - Annex-B (00 00 00 01 start code per NALU).
// We detect which by checking the first 4 bytes: a 00 00 00 01 prefix means
// Annex-B. Otherwise treat as length-prefixed; if parsing yields inconsistent
// sizes we fall back to Annex-B start-code scanning so we never emit garbage.
static bool looksLikeStartCode(const uint8_t* p) {
    return p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x01;
}
static void scanAnnexB(const uint8_t* data, size_t len,
                       std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& out) {
    size_t i = 0;
    while (i + 4 <= len) {
        if (!looksLikeStartCode(data + i)) { ++i; continue; }
        size_t start = i + 4;
        // find next start code
        size_t j = start;
        while (j + 4 <= len) {
            if (looksLikeStartCode(data + j)) break;
            ++j;
        }
        if (j + 4 > len) j = len;  // last NALU runs to end
        if (j > start) {
            const uint8_t* nalu = data + start;
            uint8_t nalType = (nalu[0] >> 1) & 0x3F;
            out.emplace_back(nalType, std::vector<uint8_t>(nalu, nalu + (j - start)));
        }
        i = j;
    }
}
static void extractNalus(const uint8_t* data, size_t len,
                         std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& out) {
    if (len >= 4 && looksLikeStartCode(data)) {
        scanAnnexB(data, len, out);
        return;
    }
    // Length-prefixed parse; validate consistency, else fall back to Annex-B.
    size_t i = 0;
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> tmp;
    bool ok = true;
    while (i + 4 <= len) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 24) |
                     (static_cast<uint32_t>(data[i + 1]) << 16) |
                     (static_cast<uint32_t>(data[i + 2]) << 8) |
                     (static_cast<uint32_t>(data[i + 3]));
        if (n == 0 || i + 4 + n > len) { ok = false; break; }
        const uint8_t* nalu = data + i + 4;
        uint8_t nalType = (nalu[0] >> 1) & 0x3F;
        tmp.emplace_back(nalType, std::vector<uint8_t>(nalu, nalu + n));
        i += 4 + n;
    }
    if (ok && !tmp.empty()) { out = std::move(tmp); return; }
    scanAnnexB(data, len, out);
}

static std::vector<uint8_t> buildHvcC(const uint8_t* csd, size_t csdLen,
                                      int width, int height, int32_t timescale) {
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> nalus;
    extractNalus(csd, csdLen, nalus);

    HvcCInfo info;
    bool haveProfile = false;
    for (auto& nv : nalus) {
        uint8_t t = nv.first;
        const auto& nalu = nv.second;
        if ((t == 32 || t == 33 || t == 34) && !haveProfile && nalu.size() >= 13) {
            // VPS(32)/SPS(33)/PPS(34) share the profile-tier-level prefix.
            const uint8_t* p = nalu.data();
            info.generalProfileSpace  = (p[2] >> 6) & 0x3;
            info.generalTierFlag      = (p[2] >> 5) & 0x1;
            info.generalProfileIdc    = p[2] & 0x1F;
            info.generalProfileCompat = (static_cast<uint32_t>(p[3]) << 24) |
                                        (static_cast<uint32_t>(p[4]) << 16) |
                                        (static_cast<uint32_t>(p[5]) << 8) |
                                        (static_cast<uint32_t>(p[6]));
            for (int k = 0; k < 6; ++k) info.generalConstraintFlags[k] = p[7 + k];
            info.generalLevelIdc = p[13];
            haveProfile = true;
        }
    }

    std::vector<uint8_t> b;
    b.push_back(info.configVersion);
    b.push_back(static_cast<uint8_t>(
        ((info.generalProfileSpace & 0x3) << 6) |
        ((info.generalTierFlag & 0x1) << 5) |
        (info.generalProfileIdc & 0x1F)));
    be32(b, info.generalProfileCompat);
    for (int k = 0; k < 6; ++k) b.push_back(info.generalConstraintFlags[k]);
    b.push_back(info.generalLevelIdc);
    be16(b, 0xF000 | (info.minSpatialSegmentation & 0x0F));
    b.push_back(0xFC | (info.parallelismType & 0x03));
    b.push_back(0xFC | (info.chromaFormat & 0x03));
    b.push_back(0xF8 | (info.bitDepthLuma & 0x07));
    b.push_back(0xF8 | (info.bitDepthChroma & 0x07));
    be16(b, info.avgFrameRate);
    b.push_back(static_cast<uint8_t>(
        ((info.constantFrameRate & 0x3) << 6) |
        ((info.numTemporalLayers & 0x7) << 3) |
        ((info.temporalIdNested & 0x1) << 2) |
        (info.lengthSizeMinusOne & 0x3)));

    // Group NALUs into arrays: VPS(32), SPS(33), PPS(34) each get their own
    // array; any others go into a single misc array. Each array is:
    //   [array_completeness(1bit)|reserved(1bit)|nal_unit_type(6bits)]
    //   numNalus(2 bytes) then per NALU [length(2) + bytes].
    auto emitArray = [&](uint8_t nalType,
                         const std::vector<const std::vector<uint8_t>*>& items) {
        b.push_back(static_cast<uint8_t>(0x80 | (nalType & 0x3F)));  // completeness=1
        be16(b, static_cast<uint16_t>(items.size()));
        for (auto* nalu : items) {
            be16(b, static_cast<uint16_t>(nalu->size()));
            bytes(b, nalu->data(), nalu->size());
        }
    };

    std::vector<const std::vector<uint8_t>*> vps, sps, pps, other;
    for (auto& nv : nalus) {
        if (nv.first == 32) vps.push_back(&nv.second);
        else if (nv.first == 33) sps.push_back(&nv.second);
        else if (nv.first == 34) pps.push_back(&nv.second);
        else other.push_back(&nv.second);
    }

    // Fallback: if no recognized NALUs, carry everything in one type-0 array so
    // the box is still structurally valid and self-describing.
    if (vps.empty() && sps.empty() && pps.empty()) {
        std::vector<const std::vector<uint8_t>*> all;
        for (auto& nv : nalus) all.push_back(&nv.second);
        b.push_back(1);  // numOfArrays
        emitArray(0, all);
    } else {
        uint8_t numArrays = 0;
        if (!vps.empty()) ++numArrays;
        if (!sps.empty()) ++numArrays;
        if (!pps.empty()) ++numArrays;
        if (!other.empty()) ++numArrays;
        b.push_back(numArrays);  // numOfArrays
        if (!vps.empty())   emitArray(32, vps);
        if (!sps.empty())   emitArray(33, sps);
        if (!pps.empty())   emitArray(34, pps);
        if (!other.empty()) emitArray(0, other);
    }
    (void)width; (void)height; (void)timescale;
    return box("hvcC", std::move(b));
}

// ---------------------------------------------------------------------------
// Annex-B -> 4-byte length-prefixed sample conversion.
//
// The on-device HEVC MediaCodec emits Annex-B (start-code-delimited) NALUs in
// every output sample: each sample begins with 00 00 00 01. An MP4 carrying an
// hvcC (lengthSizeMinusOne=3) requires 4-byte-length-prefixed NALUs in mdat, NOT
// start codes. Writing the Annex-B bytes verbatim makes the decoder read the
// leading 00 00 00 01 as a NALU length (=1), desync, and fail to decode.
//
// This helper detects whether a VIDEO sample is Annex-B and, if so, rewrites it
// into a sequence of [4-byte BE length][NALU bytes]. It handles both the 4-byte
// start code 00 00 00 01 and the 3-byte start code 00 00 01 (the latter grows
// the sample by 1 byte per occurrence; the caller uses the returned size for
// trun sample-size and mdat sizing).
//
// Detection is reliable: a length-prefixed HEVC sample never begins with a start
// code (00 00 00 01 would imply a 1-byte NALU, which is impossible — the NALU
// header alone is 2 bytes). So "begins with a start code" => Annex-B.
//
// If the sample is already length-prefixed (or empty), it is passed through
// unchanged (out aliases the input) and the function returns false.
// ---------------------------------------------------------------------------
static bool is3ByteStartCode(const uint8_t* p) {
    return p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01;
}

// Returns true and fills `out` if the sample was Annex-B and converted;
// returns false (out untouched) if the sample is passed through verbatim.
static bool annexBToLengthPrefixed(const uint8_t* data, size_t len,
                                   std::vector<uint8_t>& out) {
    if (len < 3) return false;
    // Annex-B detector: 4-byte or 3-byte start code at the very start.
    const bool startsAnnexB =
        (len >= 4 && looksLikeStartCode(data)) || is3ByteStartCode(data);
    if (!startsAnnexB) return false;

    // Walk the buffer, locating each NALU as [start code][NALU bytes up to the
    // next start code or EOF]. We scan byte-by-byte so both 3- and 4-byte start
    // codes are recognized as boundaries.
    out.clear();
    out.reserve(len + 16);
    size_t i = 0;
    while (i < len) {
        // Find a start code at position i. Skip bytes until one is found.
        bool sc4 = (i + 4 <= len) && looksLikeStartCode(data + i);
        bool sc3 = !sc4 && (i + 3 <= len) && is3ByteStartCode(data + i);
        if (!sc4 && !sc3) { ++i; continue; }
        size_t hdrLen = sc4 ? 4 : 3;
        size_t naluStart = i + hdrLen;
        // Find the next start code (3- or 4-byte) after naluStart.
        size_t j = naluStart;
        while (j < len) {
            bool j4 = (j + 4 <= len) && looksLikeStartCode(data + j);
            bool j3 = !j4 && (j + 3 <= len) && is3ByteStartCode(data + j);
            if (j4 || j3) break;
            ++j;
        }
        size_t naluLen = j - naluStart;
        if (naluLen > 0) {
            be32(out, static_cast<uint32_t>(naluLen));
            bytes(out, data + naluStart, naluLen);
        }
        i = j;
    }
    // If we never emitted anything (e.g., only start codes with no NALU bytes),
    // treat as passthrough rather than producing an empty sample.
    if (out.empty()) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct FMP4Writer::Impl {
    int fd = -1;
    bool started = false;
    bool closed = false;

    bool hasVideo = false;
    bool hasAudio = false;
    bool isAudio = false;   // true after setAudioTrack: track kind this file carries
    int width = 0, height = 0;
    int32_t timescale = 1000000;
    int32_t sampleIntervalUs = 0;   // stts placeholder delta (0 = empty, backward compat)
    int64_t firstPtsUs = -1;    // first video sample PTS (track timescale)
    int64_t lastPtsUs = 0;      // last video sample PTS
    int32_t totalSamples = 0;   // video sample count
    off_t moovFilePos = 0;      // file offset where moov starts
    size_t moovSize = 0;        // moov total byte size
    std::vector<uint8_t> csd0;
    int sampleRate = 0, channels = 0;

    uint32_t seqNumber = 0;   // mfhd sequence number, ++ per fragment (first fragment = 1)
    int64_t prevPtsUs = -1;   // previous sample PTS (timescale units) for duration delta

    bool writeAll(const void* p, size_t n) {
        const uint8_t* q = static_cast<const uint8_t*>(p);
        size_t off = 0;
        while (off < n) {
            ssize_t w = ::write(fd, q + off, n - off);
            if (w < 0) { if (errno == EINTR) continue; FMP4_LOGE("write failed"); return false; }
            off += static_cast<size_t>(w);
        }
        return true;
    }
    bool writeBuf(const std::vector<uint8_t>& b) { return writeAll(b.data(), b.size()); }
};

FMP4Writer::FMP4Writer() : m(new Impl()) {}
FMP4Writer::~FMP4Writer() { close(); delete m; }

bool FMP4Writer::open(const std::string& path) {
    if (m->fd >= 0) { FMP4_LOGE("already open"); return false; }
    m->fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (m->fd < 0) { FMP4_LOGE("open failed: %s", path.c_str()); return false; }
    m->started = false; m->closed = false;
    return true;
}

void FMP4Writer::setVideoTrack(int width, int height, int32_t timescale,
                               const uint8_t* csd0, size_t csd0Len,
                               int32_t sampleIntervalUs) {
    m->hasVideo = true;
    m->width = width; m->height = height; m->timescale = timescale;
    m->sampleIntervalUs = sampleIntervalUs;
    m->csd0.assign(csd0, csd0 + csd0Len);
}

void FMP4Writer::setAudioTrack(int sampleRate, int channels,
                               const uint8_t* csd0, size_t csd0Len) {
    m->hasAudio = true;
    m->isAudio = true;
    m->sampleRate = sampleRate; m->channels = channels;
    // Audio track timescale = sample rate; caller converts pts (us) into this
    // timescale before calling writeSample (ptsTs = ptsUs * sampleRate / 1e6).
    m->timescale = sampleRate;
    m->csd0.assign(csd0, csd0 + csd0Len);
}

// ---------------------------------------------------------------------------
// ftyp
// ---------------------------------------------------------------------------
static std::vector<uint8_t> buildFtyp() {
    std::vector<uint8_t> b;
    fourcc(b, "iso5");   // major brand
    be32(b, 0);          // minor version
    fourcc(b, "iso5");
    fourcc(b, "iso6");
    fourcc(b, "mp41");
    fourcc(b, "dash");
    return box("ftyp", std::move(b));
}

// ---------------------------------------------------------------------------
// moov and children
// ---------------------------------------------------------------------------
static std::vector<uint8_t> buildMvhd(int64_t durationMs) {
    std::vector<uint8_t> b;
    be32(b, 0);            // creation_time
    be32(b, 0);            // modification_time
    be32(b, 1000);         // timescale (movie)
    be32(b, static_cast<uint32_t>(durationMs)); // duration
    be32(b, 0x00010000);   // rate = 1.0 (16.16)
    be16(b, 0x0100);       // volume = 1.0 (8.8)
    be16(b, 0);            // reserved (bit(16))
    be32(b, 0); be32(b, 0);// reserved (unsigned int(32)[2])
    // identity matrix (3x3 fixed-point): 1.0 0 0 / 0 1.0 0 / 0 0 16384.0(=1.0 in 2.30)
    be32(b, 0x00010000); be32(b, 0); be32(b, 0);
    be32(b, 0); be32(b, 0x00010000); be32(b, 0);
    be32(b, 0); be32(b, 0); be32(b, 0x40000000);
    for (int i = 0; i < 6; ++i) be32(b, 0);  // pre_defined
    be32(b, 2);            // next_track_ID
    return fullbox("mvhd", 0, 0, std::move(b));
}

static std::vector<uint8_t> buildTkhd(int width, int height, int64_t durationUs) {
    std::vector<uint8_t> b;
    be32(b, 0);            // creation_time
    be32(b, 0);            // modification_time
    be32(b, 1);            // track_ID
    be32(b, 0);            // reserved
    be32(b, static_cast<uint32_t>(durationUs)); // duration
    be32(b, 0); be32(b, 0);// reserved
    be16(b, 0);            // layer
    be16(b, 0);            // alternate_group
    be16(b, 0);            // volume (0 for video)
    be16(b, 0);            // reserved
    // identity matrix
    be32(b, 0x00010000); be32(b, 0); be32(b, 0);
    be32(b, 0); be32(b, 0x00010000); be32(b, 0);
    be32(b, 0); be32(b, 0); be32(b, 0x40000000);
    be32(b, (static_cast<uint32_t>(width)  << 16));   // width  (16.16)
    be32(b, (static_cast<uint32_t>(height) << 16));   // height (16.16)
    return fullbox("tkhd", 0, 0x0007 /*track_enabled|in_movie|in_preview*/, std::move(b));
}

static std::vector<uint8_t> buildMdhd(int32_t timescale, int64_t duration) {
    std::vector<uint8_t> b;
    be32(b, 0);            // creation_time
    be32(b, 0);            // modification_time
    be32(b, static_cast<uint32_t>(timescale));
    be32(b, static_cast<uint32_t>(duration)); // duration
    be16(b, 0x55C4);       // language = 'und' (packed)
    be16(b, 0);            // pre_defined
    return fullbox("mdhd", 0, 0, std::move(b));
}

static std::vector<uint8_t> buildHdlr(const char* handlerType) {
    std::vector<uint8_t> b;
    be32(b, 0);            // pre_defined
    bytes(b, handlerType, 4);  // exactly 4 bytes
    be32(b, 0); be32(b, 0); be32(b, 0);  // reserved
    const char name[] = "";  // empty, null-terminated
    bytes(b, name, sizeof(name));
    return fullbox("hdlr", 0, 0, std::move(b));
}

static std::vector<uint8_t> buildVmhd() {
    std::vector<uint8_t> b;
    be16(b, 0);            // graphicsmode
    be16(b, 0); be16(b, 0); be16(b, 0);  // opcolor
    return fullbox("vmhd", 0, 1, std::move(b));
}

static std::vector<uint8_t> buildDref() {
    // One self-referencing url entry (flag=1 means data is in this file).
    std::vector<uint8_t> urlBody;  // empty body; flag 1 = no URL string
    std::vector<uint8_t> urlBox = fullbox("url ", 0, 1, std::move(urlBody));
    std::vector<uint8_t> drefBody;
    be32(drefBody, 1);     // entry_count
    drefBody.insert(drefBody.end(), urlBox.begin(), urlBox.end());
    return fullbox("dref", 0, 0, std::move(drefBody));
}

static std::vector<uint8_t> buildDinf() {
    std::vector<uint8_t> dref = buildDref();
    std::vector<uint8_t> dinf;
    dinf.insert(dinf.end(), dref.begin(), dref.end());
    return box("dinf", std::move(dinf));
}

static std::vector<uint8_t> buildStsd(const uint8_t* csd, size_t csdLen,
                                      int width, int height, int32_t timescale) {
    // hvc1 visual sample entry.
    std::vector<uint8_t> hvc1;
    for (int i = 0; i < 6; ++i) hvc1.push_back(0);  // reserved
    be16(hvc1, 1);                  // data_reference_index
    be16(hvc1, 0); be16(hvc1, 0);   // pre_defined + reserved
    for (int i = 0; i < 12; ++i) hvc1.push_back(0); // pre_defined
    be16(hvc1, static_cast<uint16_t>(width));   // width
    be16(hvc1, static_cast<uint16_t>(height));  // height
    be32(hvc1, 0x00480000);         // horizresolution 72dpi
    be32(hvc1, 0x00480000);         // vertresolution  72dpi
    be32(hvc1, 0);                  // reserved
    be16(hvc1, 1);                  // frame_count
    // compressorname: length-prefixed string, 32 bytes total
    hvc1.push_back(0);
    for (int i = 0; i < 31; ++i) hvc1.push_back(0);
    be16(hvc1, 0x0018);             // depth = 24
    be16(hvc1, 0xFFFF);             // pre_defined = -1

    std::vector<uint8_t> hvcc = buildHvcC(csd, csdLen, width, height, timescale);
    hvc1.insert(hvc1.end(), hvcc.begin(), hvcc.end());
    std::vector<uint8_t> hvc1Box = box("hvc1", std::move(hvc1));

    std::vector<uint8_t> stsdBody;
    be32(stsdBody, 1);              // entry_count
    stsdBody.insert(stsdBody.end(), hvc1Box.begin(), hvc1Box.end());
    return fullbox("stsd", 0, 0, std::move(stsdBody));
}

// ---------------------------------------------------------------------------
// AAC audio sample entry: esds box.
// Builds the MPEG-4 descriptor tree (ISO 14496-1):
//   ES_Descriptor(0x03) { ES_ID(u16) flags(u8) ,
//       DecoderConfigDescriptor(0x04) { objectTypeIndication(u8)
//         streamType|upStream|reserved(u8) bufferSizeDB(u24)
//         maxBitrate(u32) avgBitrate(u32) ,
//         DecoderSpecificInfo(0x05) { <AudioSpecificConfig bytes> } },
//       SLConfigDescriptor(0x06) { predefined(u8=0x02) } }
// Each descriptor length is encoded as a variable-length integer (ISO 14496-1
// 8.3.3): bytes with bit7 set = "more follow"; we use the minimal N-byte form.
// ---------------------------------------------------------------------------
static void putVarSize(std::vector<uint8_t>& b, uint32_t size) {
    if (size < 0x80) {
        b.push_back(static_cast<uint8_t>(size & 0x7F));
        return;
    }
    // Collect 7-bit groups big-endian.
    uint8_t tmp[5];
    int n = 0;
    tmp[n++] = static_cast<uint8_t>(size & 0x7F);
    size >>= 7;
    while (size > 0) {
        tmp[n++] = static_cast<uint8_t>(0x80 | (size & 0x7F));
        size >>= 7;
    }
    for (int i = n - 1; i >= 0; --i) b.push_back(tmp[i]);
}

static std::vector<uint8_t> buildEsds(const uint8_t* csd, size_t csdLen,
                                      int /*sampleRate*/, int /*channels*/) {
    // DecoderSpecificInfo (0x05): AudioSpecificConfig verbatim.
    std::vector<uint8_t> dsi;
    dsi.push_back(0x05);                       // tag
    putVarSize(dsi, static_cast<uint32_t>(csdLen));
    bytes(dsi, csd, csdLen);

    // DecoderConfigDescriptor (0x04).
    std::vector<uint8_t> dcdContent;
    dcdContent.push_back(0x40);                // objectTypeIndication = 0x40 (AAC)
    // streamType(6b)=0x05 audio <<2 | upStream(1b)=0 | reserved(1b)=1 -> 0x15
    dcdContent.push_back(0x15);
    dcdContent.push_back(0x00);                // bufferSizeDB (u24) high
    dcdContent.push_back(0x00);                // mid
    dcdContent.push_back(0x00);                // low
    be32(dcdContent, 0);                       // maxBitrate
    be32(dcdContent, 0);                       // avgBitrate
    dcdContent.insert(dcdContent.end(), dsi.begin(), dsi.end());
    std::vector<uint8_t> dcd;
    dcd.push_back(0x04);                       // tag
    putVarSize(dcd, static_cast<uint32_t>(dcdContent.size()));
    dcd.insert(dcd.end(), dcdContent.begin(), dcdContent.end());

    // SLConfigDescriptor (0x06): predefined = 0x02.
    std::vector<uint8_t> slc;
    slc.push_back(0x06);                       // tag
    putVarSize(slc, 1);
    slc.push_back(0x02);                       // predefined

    // ES_Descriptor (0x03).
    std::vector<uint8_t> esdContent;
    be16(esdContent, 1);                       // ES_ID
    esdContent.push_back(0x00);                // flags (no stream dependence/URL/OCR)
    esdContent.insert(esdContent.end(), dcd.begin(), dcd.end());
    esdContent.insert(esdContent.end(), slc.begin(), slc.end());
    std::vector<uint8_t> esd;
    esd.push_back(0x03);                       // tag
    putVarSize(esd, static_cast<uint32_t>(esdContent.size()));
    esd.insert(esd.end(), esdContent.begin(), esdContent.end());

    // esds FullBox: version 0, flags 0, payload = ES_Descriptor.
    return fullbox("esds", 0, 0, std::move(esd));
}

// mp4a audio sample entry (ISO 14496-14/12 §8.5.2 AudioSampleEntry v0):
//   reserved(6) data_reference_index(u16=1)
//   reserved(4+4) channelcount(u16) samplesize(u16=16)
//   pre_defined(u16=0) reserved(u16=0)
//   pre_defined2 = sampling rate as 16.16 fixed (sampleRate<<16)
//   child box: esds
static std::vector<uint8_t> buildMp4a(const uint8_t* csd, size_t csdLen,
                                      int sampleRate, int channels) {
    std::vector<uint8_t> mp4a;
    for (int i = 0; i < 6; ++i) mp4a.push_back(0);  // reserved
    be16(mp4a, 1);                  // data_reference_index
    be32(mp4a, 0); be32(mp4a, 0);   // reserved (two u32)
    be16(mp4a, static_cast<uint16_t>(channels)); // channelcount
    be16(mp4a, 16);                 // samplesize
    be16(mp4a, 0);                  // pre_defined
    be16(mp4a, 0);                  // reserved
    be32(mp4a, static_cast<uint32_t>(sampleRate) << 16);  // 16.16 fixed
    std::vector<uint8_t> esds = buildEsds(csd, csdLen, sampleRate, channels);
    mp4a.insert(mp4a.end(), esds.begin(), esds.end());
    return box("mp4a", std::move(mp4a));
}

static std::vector<uint8_t> buildAudioStsd(const uint8_t* csd, size_t csdLen,
                                           int sampleRate, int channels) {
    std::vector<uint8_t> mp4a = buildMp4a(csd, csdLen, sampleRate, channels);
    std::vector<uint8_t> stsdBody;
    be32(stsdBody, 1);              // entry_count
    stsdBody.insert(stsdBody.end(), mp4a.begin(), mp4a.end());
    return fullbox("stsd", 0, 0, std::move(stsdBody));
}

static std::vector<uint8_t> buildSmhd() {
    std::vector<uint8_t> b;
    be16(b, 0);            // balance (8.8) = 0
    be16(b, 0);            // reserved
    return fullbox("smhd", 0, 0, std::move(b));
}

static std::vector<uint8_t> buildStbl(const uint8_t* csd, size_t csdLen,
                                      int width, int height, int32_t timescale,
                                      bool isAudio, int sampleRate, int channels,
                                      int32_t sampleIntervalUs) {
    std::vector<uint8_t> stbl;
    auto append = [&](const std::vector<uint8_t>& v) { stbl.insert(stbl.end(), v.begin(), v.end()); };
    if (isAudio)
        append(buildAudioStsd(csd, csdLen, sampleRate, channels));
    else
        append(buildStsd(csd, csdLen, width, height, timescale));
    // stts: for video, write a placeholder entry with the target frame interval
    // so tools that only read moov (e.g. Windows Explorer/Media Foundation) can
    // compute a reasonable frame rate. For audio, leave empty as before.
    std::vector<uint8_t> sttsP;
    if (!isAudio && sampleIntervalUs > 0) {
        be32(sttsP, 1);                                     // entry_count = 1
        be32(sttsP, 1);                                     // sample_count = 1
        be32(sttsP, static_cast<uint32_t>(sampleIntervalUs)); // sample_delta
    } else {
        be32(sttsP, 0);                                     // entry_count = 0
    }
    append(fullbox("stts", 0, 0, std::move(sttsP)));
    std::vector<uint8_t> stscP; be32(stscP, 0);
    append(fullbox("stsc", 0, 0, std::move(stscP)));
    std::vector<uint8_t> stszP; be32(stszP, 0); be32(stszP, 0);  // sample_size=0, sample_count=0
    append(fullbox("stsz", 0, 0, std::move(stszP)));
    std::vector<uint8_t> stcoP; be32(stcoP, 0);
    append(fullbox("stco", 0, 0, std::move(stcoP)));
    return box("stbl", std::move(stbl));
}

static std::vector<uint8_t> buildMinf(const uint8_t* csd, size_t csdLen,
                                      int width, int height, int32_t timescale,
                                      bool isAudio, int sampleRate, int channels,
                                      int32_t sampleIntervalUs) {
    std::vector<uint8_t> minf;
    auto append = [&](const std::vector<uint8_t>& v) { minf.insert(minf.end(), v.begin(), v.end()); };
    append(isAudio ? buildSmhd() : buildVmhd());
    append(buildDinf());
    append(buildStbl(csd, csdLen, width, height, timescale, isAudio, sampleRate, channels, sampleIntervalUs));
    return box("minf", std::move(minf));
}

static std::vector<uint8_t> buildMdia(const uint8_t* csd, size_t csdLen,
                                      int width, int height, int32_t timescale,
                                      bool isAudio, int sampleRate, int channels,
                                      int32_t sampleIntervalUs, int64_t durationUs) {
    std::vector<uint8_t> mdia;
    auto append = [&](const std::vector<uint8_t>& v) { mdia.insert(mdia.end(), v.begin(), v.end()); };
    append(buildMdhd(timescale, durationUs));
    append(buildHdlr(isAudio ? "soun" : "vide"));
    append(buildMinf(csd, csdLen, width, height, timescale, isAudio, sampleRate, channels, sampleIntervalUs));
    return box("mdia", std::move(mdia));
}

static std::vector<uint8_t> buildTrak(const uint8_t* csd, size_t csdLen,
                                      int width, int height, int32_t timescale,
                                      bool isAudio, int sampleRate, int channels,
                                      int32_t sampleIntervalUs, int64_t durationUs) {
    std::vector<uint8_t> trak;
    auto append = [&](const std::vector<uint8_t>& v) { trak.insert(trak.end(), v.begin(), v.end()); };
    // tkhd width/height are video-only fields; pass 0 for audio.
    append(buildTkhd(isAudio ? 0 : width, isAudio ? 0 : height, durationUs));
    append(buildMdia(csd, csdLen, width, height, timescale, isAudio, sampleRate, channels, sampleIntervalUs, durationUs));
    return box("trak", std::move(trak));
}

static std::vector<uint8_t> buildMvex() {
    // trex: default sample flags sample_depends_on=2 -> 0x02000000.
    std::vector<uint8_t> trexBody;
    be32(trexBody, 1);            // track_ID
    be32(trexBody, 1);            // default_sample_description_index (1-based into stsd)
    be32(trexBody, 0);            // default_sample_duration
    be32(trexBody, 0);            // default_sample_size
    be32(trexBody, 0x02000000);   // default_sample_flags (sample_depends_on=2)
    std::vector<uint8_t> trex = fullbox("trex", 0, 0, std::move(trexBody));
    std::vector<uint8_t> mvex;
    mvex.insert(mvex.end(), trex.begin(), trex.end());
    return box("mvex", std::move(mvex));
}

static std::vector<uint8_t> buildMoov(const uint8_t* csd, size_t csdLen,
                                      int width, int height, int32_t timescale,
                                      bool isAudio, int sampleRate, int channels,
                                      int32_t sampleIntervalUs, int64_t durationUs) {
    std::vector<uint8_t> moov;
    auto append = [&](const std::vector<uint8_t>& v) { moov.insert(moov.end(), v.begin(), v.end()); };
    append(buildMvhd(durationUs / 1000));  // mvhd timescale=1000, convert us→ms
    append(buildTrak(csd, csdLen, width, height, timescale, isAudio, sampleRate, channels, sampleIntervalUs, durationUs));
    append(buildMvex());
    return box("moov", std::move(moov));
}

bool FMP4Writer::start() {
    if (m->fd < 0) { FMP4_LOGE("start: not open"); return false; }
    if (m->started) { FMP4_LOGE("start: already started"); return false; }
    if (!m->hasVideo && !m->hasAudio) { FMP4_LOGE("start: no track set"); return false; }

    if (!m->writeBuf(buildFtyp())) return false;
    m->moovFilePos = static_cast<off_t>(::lseek(m->fd, 0, SEEK_CUR));
    auto moov = buildMoov(m->csd0.data(), m->csd0.size(),
                           m->width, m->height, m->timescale,
                           m->isAudio, m->sampleRate, m->channels,
                           m->sampleIntervalUs, 0);
    m->moovSize = moov.size();
    if (!m->writeBuf(moov)) return false;
    m->started = true;
    return true;
}

// ---------------------------------------------------------------------------
// Fragment boxes: styp + moof(mfhd + traf(tfhd + tfdt + trun)) + mdat.
// One fragment per sample. With B-frames disabled (decode order == DTS == PTS),
// composition_offset is always 0, so trun version 1 is used purely for the
// 64-bit baseMediaDecodeTime in tfdt; no composition-time table is emitted.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> buildStyp() {
    // Segment Type Box: major brand iso5, minor 0, compatible brands iso5/iso6/msdh.
    std::vector<uint8_t> b;
    fourcc(b, "iso5");   // major brand
    be32(b, 0);          // minor version
    fourcc(b, "iso5");
    fourcc(b, "iso6");
    fourcc(b, "msdh");
    return box("styp", std::move(b));
}

static std::vector<uint8_t> buildMfhd(uint32_t sequenceNumber) {
    std::vector<uint8_t> b;
    be32(b, sequenceNumber);  // sequence_number
    return fullbox("mfhd", 0, 0, std::move(b));
}

// tfhd: track_ID + default-base-is-moof only (no default fields written).
static std::vector<uint8_t> buildTfhd() {
    std::vector<uint8_t> b;
    be32(b, 1);  // track_ID
    // flags 0x020000 = default-base-is-moof. With this flag and none of the
    // default_*_present flags set, only track_ID follows the fullbox header.
    return fullbox("tfhd", 0, 0x020000, std::move(b));
}

// tfdt version 1 (64-bit baseMediaDecodeTime).
static std::vector<uint8_t> buildTfdt(uint64_t baseMediaDecodeTime) {
    std::vector<uint8_t> b;
    be64(b, baseMediaDecodeTime);
    return fullbox("tfdt", 1, 0, std::move(b));
}

// trun version 1, one sample. flags = data-offset(0x000001) |
// sample-duration(0x000100) | sample-size(0x000200) | sample-flags(0x000400)
// => 0x000701. data_offset is written as a placeholder 0 and patched by the
// caller once the moof total size is known. Returns the byte offset, within the
// returned box, of the data_offset field so the caller can patch it in place.
static std::vector<uint8_t> buildTrun(uint32_t sampleDuration, uint32_t sampleSize,
                                      uint32_t sampleFlags, size_t& outDataOffsetPos) {
    std::vector<uint8_t> body;
    body.push_back(1);  // version 1
    // flags 0x000701
    body.push_back(0x00);
    body.push_back(0x07);
    body.push_back(0x01);
    be32(body, 1);              // sample_count
    outDataOffsetPos = body.size();        // data_offset field starts here (within trun payload)
    be32(body, 0);              // data_offset (placeholder; patched by caller)
    be32(body, sampleDuration); // per-sample duration
    be32(body, sampleSize);     // per-sample size
    be32(body, sampleFlags);    // per-sample flags
    // Wrap: box() prepends an 8-byte header, so the data_offset position within
    // the final box is outDataOffsetPos + 8.
    outDataOffsetPos += 8;
    return box("trun", std::move(body));
}

bool FMP4Writer::writeSample(const uint8_t* data, size_t size, int64_t ptsUs, bool isSync) {
    if (!m->started) { FMP4_LOGE("writeSample: not started"); return false; }
    if (m->fd < 0) { FMP4_LOGE("writeSample: no file"); return false; }
    if (size == 0) { FMP4_LOGE("writeSample: empty sample"); return false; }

    // On-device HEVC MediaCodec emits Annex-B (start-code-delimited) NALUs, but
    // an MP4 with an hvcC requires 4-byte-length-prefixed NALUs. Convert VIDEO
    // samples here (audio has no start codes and passes through unchanged). The
    // converted bytes/size are used for BOTH the trun sample size and the mdat
    // payload, so trun and mdat stay consistent.
    std::vector<uint8_t> converted;
    const uint8_t* sampleData = data;
    size_t sampleSize = size;
    if (!m->isAudio) {
        if (annexBToLengthPrefixed(data, size, converted)) {
            sampleData = converted.data();
            sampleSize = converted.size();
        }
    }

    // PTS is already expressed in the track timescale (video: µs == timescale 1e6).
    int64_t pts = ptsUs;

    // Non-monotonic-PTS guard. With B-frames disabled, decode order == DTS == PTS,
    // so PTS must be strictly increasing. A regression here (pts < prevPtsUs)
    // signals B-frame leakage at the encoder: a non-sync sample would then carry
    // a tfdt base that is less than the previous sample's, corrupting decode order.
    // We warn and keep recording (the caller is expected to disable B-frames).
    if (m->prevPtsUs >= 0 && pts < m->prevPtsUs) {
        FMP4_LOGW("non-monotonic PTS: prev=%lld us < cur=%lld us (delta=%lld); "
                  "check encoder B-frame setting",
                  static_cast<long long>(m->prevPtsUs),
                  static_cast<long long>(pts),
                  static_cast<long long>(pts - m->prevPtsUs));
    }

    // Duration: for audio, every AAC packet carries 1024 samples (constant);
    // for video, derive from the PTS delta (first sample gets a 33333 default).
    uint32_t duration;
    if (m->isAudio) {
        duration = 1024u;
    } else {
        duration = (m->prevPtsUs < 0 || pts <= m->prevPtsUs)
                       ? 33333u
                       : static_cast<uint32_t>(pts - m->prevPtsUs);
    }

    // trun per-sample flags (ISO 14496-12 §8.8.3.1), 32-bit word layout:
    //   [31-28] reserved | [27-26] is_leading | [25-24] sample_depends_on
    //   [23-22] sample_is_depended_on | [21-20] sample_has_redundancy
    //   [19-17] sample_padding_value | [16] sample_is_non_sync_sample
    //   [15-0]  sample_degradation_priority
    //   - Sync (I-frame):   sample_depends_on=2 (independent), non_sync=0 -> 0x02000000
    //   - Non-sync (P-frame): sample_depends_on=1 (depends on others), non_sync=1 -> 0x01010000
    // composition_offset stays 0 (no B-frames); tfdt carries the absolute decode time.
    uint32_t sampleFlags = isSync ? 0x02000000u : 0x01010000u;

    // Build moof children, then the moof box, patching data_offset afterwards.
    size_t trunDataOffsetPos = 0;
    std::vector<uint8_t> trun = buildTrun(duration, static_cast<uint32_t>(sampleSize),
                                          sampleFlags, trunDataOffsetPos);

    std::vector<uint8_t> traf;
    auto append = [&](const std::vector<uint8_t>& v) {
        traf.insert(traf.end(), v.begin(), v.end());
    };
    append(buildTfhd());
    append(buildTfdt(static_cast<uint64_t>(pts < 0 ? 0 : pts)));
    append(trun);
    traf = box("traf", std::move(traf));

    std::vector<uint8_t> moofPayload;
    auto appendM = [&](const std::vector<uint8_t>& v) {
        moofPayload.insert(moofPayload.end(), v.begin(), v.end());
    };
    appendM(buildMfhd(++m->seqNumber));
    appendM(traf);

    // moof total size (including the 8-byte box header). data_offset is measured
    // from the base data offset (first byte of moof, per default-base-is-moof)
    // to the first sample byte in mdat. mdat immediately follows moof with an
    // 8-byte header, so: data_offset = moof_total_size + 8.
    const uint32_t moofTotalSize = static_cast<uint32_t>(8 + moofPayload.size());
    const int32_t dataOffset = static_cast<int32_t>(moofTotalSize) + 8;

    // Patch data_offset inside the trun, which we appended last to moofPayload.
    // trun was the final child of traf, so its start within moofPayload is:
    //   (moofPayload.size() - trun.size()).
    // The in-trun data_offset field offset (within the trun box) was returned
    // by buildTrun as trunDataOffsetPos.
    const size_t trunStartInMoofPayload = moofPayload.size() - trun.size();
    const size_t patchPos = trunStartInMoofPayload + trunDataOffsetPos;
    if (patchPos + 4 <= moofPayload.size()) {
        uint32_t v = static_cast<uint32_t>(dataOffset);
        moofPayload[patchPos + 0] = static_cast<uint8_t>((v >> 24) & 0xFF);
        moofPayload[patchPos + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
        moofPayload[patchPos + 2] = static_cast<uint8_t>((v >> 8) & 0xFF);
        moofPayload[patchPos + 3] = static_cast<uint8_t>(v & 0xFF);
    }

    // Emit: styp, moof, mdat(sample bytes).
    if (!m->writeBuf(buildStyp())) return false;
    // Write moof header + patched payload directly.
    std::vector<uint8_t> moofHdr;
    be32(moofHdr, moofTotalSize);
    fourcc(moofHdr, "moof");
    if (!m->writeBuf(moofHdr)) return false;
    if (!m->writeBuf(moofPayload)) return false;
    // mdat
    std::vector<uint8_t> mdatHdr;
    be32(mdatHdr, static_cast<uint32_t>(8 + sampleSize));
    fourcc(mdatHdr, "mdat");
    if (!m->writeBuf(mdatHdr)) return false;
    if (!m->writeAll(sampleData, sampleSize)) return false;

    if (!m->isAudio) {
        if (m->totalSamples == 0) m->firstPtsUs = pts;
        m->lastPtsUs = pts;
        m->totalSamples++;
    }
    m->prevPtsUs = pts;
    return true;
}

bool FMP4Writer::close() {
    if (m->closed) return true;
    // Back-patch moov with real average frame interval and duration so tools
    // that only read moov (e.g. Windows Explorer/Media Foundation) show
    // accurate values. Only video tracks are patched; audio has no stts.
    if (!m->isAudio && m->totalSamples >= 1 && m->moovFilePos > 0 && m->moovSize > 0) {
        int32_t realIntervalUs = m->sampleIntervalUs;  // fallback to constructor hint
        if (m->totalSamples >= 2) {
            int64_t spanUs = m->lastPtsUs - m->firstPtsUs;
            if (spanUs > 0)
                realIntervalUs = static_cast<int32_t>(spanUs / (m->totalSamples - 1));
        }
        int64_t durationUs = m->lastPtsUs;
        auto rebuilt = buildMoov(m->csd0.data(), m->csd0.size(),
                                  m->width, m->height, m->timescale,
                                  m->isAudio, m->sampleRate, m->channels,
                                  realIntervalUs, durationUs);
        if (rebuilt.size() == m->moovSize) {
            ::lseek(m->fd, m->moovFilePos, SEEK_SET);
            m->writeBuf(rebuilt);
            FMP4_LOGI("moov back-patched: avgInterval=%d us, duration=%lld us, "
                      "samples=%d",
                      realIntervalUs, static_cast<long long>(durationUs),
                      m->totalSamples);
        } else {
            FMP4_LOGW("moov size mismatch on close (old=%zu new=%zu); "
                      "skipping patch",
                      m->moovSize, rebuilt.size());
        }
    }
    if (m->fd >= 0) { ::close(m->fd); m->fd = -1; }
    m->closed = true;
    return true;
}

} // namespace SXR
