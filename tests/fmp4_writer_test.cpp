#include "FMP4Writer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
using namespace SXR;

static std::string run(const char* cmd) {           // capture stdout
    std::string out; char buf[256]; FILE* p = popen(cmd, "r");
    while (p && fgets(buf, sizeof(buf), p)) out += buf;
    if (p) { pclose(p); }
    return out;
}
static int ffprobe_streams(const std::string& f, const char* sel) {
    std::string c = "ffprobe -v error -select_streams " + std::string(sel) +
                    " -show_entries stream=codec_name -of csv=p=0 " + f;
    return run(c.c_str()).empty() ? 0 : 1;          // 1 if a stream exists
}

static uint32_t readBe32(const std::vector<uint8_t>& b, size_t off) {
    return (uint32_t(b[off]) << 24) | (uint32_t(b[off + 1]) << 16) |
           (uint32_t(b[off + 2]) << 8) | uint32_t(b[off + 3]);
}

static uint64_t readBe64(const std::vector<uint8_t>& b, size_t off) {
    return (uint64_t(readBe32(b, off)) << 32) | readBe32(b, off + 4);
}

static std::vector<uint8_t> readFile(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return {};
    fseek(fp, 0, SEEK_END); long size = ftell(fp); rewind(fp);
    std::vector<uint8_t> data(size > 0 ? static_cast<size_t>(size) : 0);
    if (!data.empty() && fread(data.data(), 1, data.size(), fp) != data.size()) data.clear();
    fclose(fp);
    return data;
}

static std::vector<size_t> findTypes(const std::vector<uint8_t>& b, const char type[5]) {
    std::vector<size_t> offsets;
    for (size_t i = 4; i + 4 <= b.size(); ++i)
        if (memcmp(b.data() + i, type, 4) == 0) offsets.push_back(i);
    return offsets;
}

int main() {
    std::string f = "out_empty.mp4";
    // Annex-B HEVC csd: 1 NALU (fake VPS, nalType 32) prefixed with a
    // 00 00 00 01 start code. The writer must accept both Annex-B start-code
    // and 4-byte length-prefixed csd (see length-prefixed fixture below).
    uint8_t csd[] = {0,0,0,1, 0x40,0x01,0x0c,0x01,0xff,0xff,0x01,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x90,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x96,0xac,0x09};
    FMP4Writer w;
    if (!w.open(f)) return 1;
    w.setVideoTrack(640,480,1000000, csd, sizeof(csd));
    if (!w.start()) return 2;
    if (!w.close()) return 3;
    if (ffprobe_streams(f,"v") != 1) { fprintf(stderr,"FAIL: no video stream\n"); return 4; }

    // Also exercise the length-prefixed csd path: the same fake VPS NALU, but
    // prefixed with a 4-byte big-endian length (0x1C = 28 NALU bytes) instead of
    // the Annex-B start code. The writer must accept this form too.
    std::string flp = "out_empty_lp.mp4";
    uint8_t csdLp[] = {0,0,0,0x1C, 0x40,0x01,0x0c,0x01,0xff,0xff,0x01,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x90,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x96,0xac,0x09};
    FMP4Writer wlp;
    if (!wlp.open(flp)) return 5;
    wlp.setVideoTrack(640,480,1000000, csdLp, sizeof(csdLp));
    if (!wlp.start()) return 6;
    if (!wlp.close()) return 7;
    if (ffprobe_streams(flp,"v") != 1) { fprintf(stderr,"FAIL: length-prefixed csd produced no video stream\n"); return 8; }

    printf("PASS task1: container+video stream recognized (Annex-B + length-prefixed csd)\n");

    // ---- Task 2: 5 sync samples -> 5 packets ----
    {
        std::string f = "out_5.mp4";
        // reuse the same Annex-B fake VPS csd as T1
        uint8_t csd[] = {0,0,0,1, 0x40,0x01,0x0c,0x01,0xff,0xff,0x01,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x90,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x96,0xac,0x09};
        uint8_t frame[8] = {0,0,0,4, 0x26,0x01,0xAA,0xBB};   // fake IDR NALU (length-prefixed)
        FMP4Writer w;
        if (!w.open(f)) return 11;
        w.setVideoTrack(640,480,1000000, csd, sizeof(csd));
        if (!w.start()) return 12;
        for (int i=0;i<5;i++) {
            if (!w.writeSample(frame, sizeof(frame), i*33333LL, /*isSync*/true)) return 13;
        }
        if (!w.close()) return 14;
        std::string n = run(("ffprobe -v error -select_streams v -count_packets -show_entries stream=nb_read_packets -of csv=p=0 " + f).c_str());
        if (n != "5\n" && n != "5") { fprintf(stderr,"FAIL task2: got '%s'\n", n.c_str()); return 15; }
        printf("PASS task2: 5 packets\n");
    }

    // ---- Task 3: 1 IDR + 4 P, strictly increasing PTS, 5 packets ----
    {
        std::string f = "out_ip.mp4";
        // reuse the same Annex-B fake VPS csd as T1/T2
        uint8_t csd[] = {0,0,0,1, 0x40,0x01,0x0c,0x01,0xff,0xff,0x01,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x90,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x96,0xac,0x09};
        uint8_t idr[8] = {0,0,0,4, 0x26,0x01,0xAA,0xBB};   // fake IDR (length-prefixed)
        uint8_t pfr[8] = {0,0,0,4, 0x02,0x01,0xCC,0xDD};   // fake non-IDR (P) slice (length-prefixed)
        FMP4Writer w;
        if (!w.open(f)) return 21;
        w.setVideoTrack(640,480,1000000, csd, sizeof(csd));
        if (!w.start()) return 22;
        if (!w.writeSample(idr, 8, 0,        /*isSync*/true))  return 23;
        for (int i=1;i<5;i++)
            if (!w.writeSample(pfr, 8, i*33333LL, /*isSync*/false)) return 24;
        if (!w.close()) return 25;

        // Must be exactly 5 packets.
        std::string nb = run(("ffprobe -v error -select_streams v -count_packets -show_entries stream=nb_read_packets -of csv=p=0 " + f).c_str());
        if (nb != "5\n" && nb != "5") { fprintf(stderr,"FAIL task3: expected 5 packets, got '%s'\n", nb.c_str()); return 26; }

        // pts_time per packet — must be 5 strictly-increasing values.
        std::string pkt = run(("ffprobe -v error -select_streams v -show_entries packet=pts_time -of csv=p=0 " + f).c_str());
        double prev = -1.0; int lines = 0; bool monotonic = true;
        const char* s = pkt.c_str();
        while (*s) {
            const char* nl = s;
            while (*nl && *nl != '\n') ++nl;
            if (nl > s) {
                std::string tok(s, nl);
                double v = strtod(tok.c_str(), nullptr);
                if (!(v > prev)) monotonic = false;
                prev = v; ++lines;
            }
            s = (*nl == '\n') ? nl + 1 : nl;
        }
        if (lines != 5 || !monotonic) { fprintf(stderr,"FAIL task3: pts_time lines=%d monotonic=%d\n", lines, (int)monotonic); return 27; }

        // Only the FIRST packet may be a keyframe (K_); the 4 P-frames must NOT be.
        std::string fl = run(("ffprobe -v error -select_streams v -show_entries packet=flags -of csv=p=0 " + f).c_str());
        // Walk lines: first must contain 'K', the rest must not.
        bool firstOk = false, restOk = true; int idx = 0;
        const char* t = fl.c_str();
        while (*t) {
            const char* nl = t;
            while (*nl && *nl != '\n') ++nl;
            if (nl > t) {
                std::string tok(t, nl);
                bool isK = tok.find('K') != std::string::npos;
                if (idx == 0) firstOk = isK; else if (isK) restOk = false;
                ++idx;
            }
            t = (*nl == '\n') ? nl + 1 : nl;
        }
        if (idx != 5 || !firstOk || !restOk) { fprintf(stderr,"FAIL task3: flags lines=%d firstKey=%d restNonKey=%d\n", idx, (int)firstOk, (int)restOk); return 28; }
        printf("PASS task3: 1 IDR + 4 P, strictly increasing PTS, only first packet K_\n");
    }

    // ---- Task 4: truncated file still parses, plays to last complete moof ----
    {
        std::string f = "out_trunc.mp4";
        // reuse the same Annex-B fake VPS csd as T1/T2/T3
        uint8_t csd[] = {0,0,0,1, 0x40,0x01,0x0c,0x01,0xff,0xff,0x01,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x90,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x96,0xac,0x09};
        uint8_t frame[8] = {0,0,0,4, 0x26,0x01,0xAA,0xBB};   // fake IDR (length-prefixed)
        FMP4Writer w; if(!w.open(f)) return 31;
        w.setVideoTrack(640,480,1000000, csd, sizeof(csd));
        if(!w.start()) return 32;
        for (int i=0;i<20;i++) { if(!w.writeSample(frame, sizeof(frame), i*33333LL, true)) return 33; }
        if(!w.close()) return 34;

        // get full size, then truncate to ~70%
        long sz = 0; { FILE* fp = fopen(f.c_str(),"rb"); if(fp){ fseek(fp,0,SEEK_END); sz=ftell(fp); fclose(fp);} }
        if (sz <= 0) { fprintf(stderr,"FAIL task4: size=%ld\n", sz); return 35; }
        long trunc = (long)(sz * 0.70);
        { int fd = open(f.c_str(), O_RDWR); if(fd>=0){ if(ftruncate(fd, trunc)!=0){} close(fd);} }   // ftruncate via raw fd

        // truncated file must STILL parse with nonzero but fewer packets, no fatal error
        std::string probeErr;
        {
            // run ffprobe capturing combined stderr to detect fatal parse errors
            std::string cmd = "ffprobe -v error " + f + " 2>&1 1>/dev/null";
            probeErr = run(cmd.c_str());
        }
        int pkts = atoi(run(("ffprobe -v error -count_packets -select_streams v -show_entries stream=nb_read_packets -of csv=p=0 " + f).c_str()).c_str());
        // Acceptance: 0 < pkts < 20, and ffprobe did not report a fatal "moov atom not found" / unreadable error.
        if (pkts <= 0 || pkts >= 20) { fprintf(stderr,"FAIL task4: pkts=%d (sz=%ld trunc=%ld) err=[%s]\n", pkts, sz, trunc, probeErr.c_str()); return 36; }
        if (probeErr.find("moov atom not found") != std::string::npos || probeErr.find("Invalid data") != std::string::npos) {
            fprintf(stderr,"FAIL task4: fatal parse err: %s\n", probeErr.c_str()); return 37;
        }
        printf("PASS task4: truncated file playable, %d packets (of 20) after 70%% cut\n", pkts);
    }

    // ---- Task 5: AAC audio track ----
    {
        std::string f = "out_audio.m4a";
        // AAC-LC AudioSpecificConfig for 44100 Hz, 2-channel: 0x12,0x10
        uint8_t asc[] = {0x12, 0x10};
        // fake AAC frame bytes (ADTS-free raw, just needs to be counted by ffprobe)
        uint8_t pkt[8] = {0x21,0x10,0x04,0x60,0x8C,0x1C,0x20,0x00};
        FMP4Writer w; if(!w.open(f)) return 41;
        w.setAudioTrack(44100, 2, asc, sizeof(asc));
        if(!w.start()) return 42;
        int64_t sampleRate = 44100;
        for (int i=0;i<5;i++) {
            int64_t ptsTs = i * 1024;          // audio timescale units (AAC = 1024 samples/pkt)
            if(!w.writeSample(pkt, sizeof(pkt), ptsTs, true)) return 43;
        }
        if(!w.close()) return 44;
        // expect an audio (aac) stream, 5 packets
        std::string codec = run(("ffprobe -v error -select_streams a -show_entries stream=codec_name -of csv=p=0 " + f).c_str());
        if (codec.find("aac") == std::string::npos) { fprintf(stderr,"FAIL task5: no aac stream (got '%s')\n", codec.c_str()); return 45; }
        std::string n = run(("ffprobe -v error -select_streams a -count_packets -show_entries stream=nb_read_packets -of csv=p=0 " + f).c_str());
        if (n != "5\n" && n != "5") { fprintf(stderr,"FAIL task5: packets=%s\n", n.c_str()); return 46; }
        printf("PASS task5: AAC audio stream, 5 packets\n");
    }

    // ---- Task 6: Annex-B input -> length-prefixed mdat output ----
    // The on-device HEVC MediaCodec emits Annex-B (start-code-delimited) NALUs.
    // The writer must convert these to 4-byte-length-prefixed NALUs in mdat so
    // the file is decodable. We feed Annex-B samples and assert each mdat payload
    // begins with a 4-byte length (00 00 00 04) rather than a start code
    // (00 00 00 01). The fake NALUs won't decode, but this proves the FORMAT
    // conversion; the device test proves actual decode.
    {
        std::string f = "out_annexb.mp4";
        // Same Annex-B fake VPS csd as T1.
        uint8_t csd[] = {0,0,0,1, 0x40,0x01,0x0c,0x01,0xff,0xff,0x01,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x90,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x96,0xac,0x09};
        // Annex-B sample: 4-byte start code + a 4-byte fake IDR NALU (nalType 0x26>>1&0x3F = 19 = IDR_W_RADL).
        uint8_t frame[] = {0,0,0,1, 0x26,0x01,0xAA,0xBB};
        FMP4Writer w;
        if (!w.open(f)) return 51;
        w.setVideoTrack(640,480,1000000, csd, sizeof(csd));
        if (!w.start()) return 52;
        for (int i=0;i<3;i++) {
            if (!w.writeSample(frame, sizeof(frame), i*33333LL, /*isSync*/true)) return 53;
        }
        if (!w.close()) return 54;

        // Box-walk the file: for each top-level box, if it is 'mdat' (size>8),
        // read its first 4 payload bytes and assert == 00 00 00 04 (length 4 ==
        // the 4-byte fake NALU after start-code removal) and != 00 00 00 01.
        FILE* fp = fopen(f.c_str(), "rb");
        if (!fp) { fprintf(stderr,"FAIL task6: cannot open %s\n", f.c_str()); return 55; }
        fseek(fp, 0, SEEK_END); long fsize = ftell(fp); rewind(fp);
        std::vector<uint8_t> buf(fsize);
        if (fsize <= 0 || fread(buf.data(), 1, fsize, fp) != (size_t)fsize) {
            fprintf(stderr,"FAIL task6: read error\n"); fclose(fp); return 56;
        }
        fclose(fp);

        int mdatCount = 0; bool allOk = true;
        size_t off = 0;
        while (off + 8 <= (size_t)fsize) {
            uint32_t sz = (uint32_t(buf[off])<<24) | (uint32_t(buf[off+1])<<16) |
                          (uint32_t(buf[off+2])<<8) | uint32_t(buf[off+3]);
            char type[5] = {0};
            memcpy(type, buf.data()+off+4, 4);
            if (sz < 8) break;  // malformed / trailing; stop walking
            bool isMdat = (memcmp(type, "mdat", 4) == 0);
            if (isMdat && (size_t)sz - 8 >= 4) {
                const uint8_t* p = buf.data() + off + 8;
                uint32_t first4 = (uint32_t(p[0])<<24) | (uint32_t(p[1])<<16) |
                                  (uint32_t(p[2])<<8) | uint32_t(p[3]);
                ++mdatCount;
                // Must be length 4 (the converted NALU size), NOT a start code.
                if (first4 != 4u || (p[0]==0 && p[1]==0 && p[2]==0 && p[3]==1)) {
                    fprintf(stderr,"FAIL task6: mdat #%d first4=0x%08x (want 0x00000004)\n", mdatCount, first4);
                    allOk = false;
                }
            }
            off += sz;
        }
        if (mdatCount != 3) { fprintf(stderr,"FAIL task6: found %d mdat boxes (want 3)\n", mdatCount); allOk = false; }
        if (!allOk) return 57;
        printf("PASS task6: Annex-B converted to length-prefixed (3 mdat, each 00 00 00 04)\n");
    }

    // ---- Task 7: fragmented timeline metadata and VFR duration ownership ----
    {
        std::string f = "out_timeline.mp4";
        uint8_t csd[] = {0,0,0,1, 0x40,0x01,0x0c,0x01,0xff,0xff,0x01,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x90,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x96,0xac,0x09};
        uint8_t frame[8] = {0,0,0,4, 0x26,0x01,0xAA,0xBB};
        const int64_t pts[] = {0, 10000, 40000};
        FMP4Writer w;
        if (!w.open(f)) return 61;
        w.setVideoTrack(640, 480, 1000000, csd, sizeof(csd), 16667);
        if (!w.start()) return 62;
        for (int i = 0; i < 3; ++i)
            if (!w.writeSample(frame, sizeof(frame), pts[i], true)) return 63;
        if (!w.close()) return 64;

        std::vector<uint8_t> b = readFile(f);
        auto one = [&](const char type[5], size_t fieldOffset, uint32_t expected) {
            std::vector<size_t> p = findTypes(b, type);
            return p.size() == 1 && p[0] + fieldOffset + 4 <= b.size() &&
                   readBe32(b, p[0] + fieldOffset) == expected;
        };
        // Offsets are relative to the fourcc position (box start + 4).
        if (!one("mvhd", 20, 0) || !one("tkhd", 24, 0) || !one("mdhd", 20, 0) ||
            !one("stts", 8, 0) || !one("stsc", 8, 0) ||
            !one("stsz", 12, 0) || !one("stco", 8, 0)) {
            fprintf(stderr, "FAIL task7: non-empty initialization duration/sample table\n");
            return 65;
        }
        std::vector<size_t> tfdt = findTypes(b, "tfdt");
        std::vector<size_t> trun = findTypes(b, "trun");
        const uint32_t expectedDur[] = {10000, 30000, 30000};
        if (tfdt.size() != 3 || trun.size() != 3) return 66;
        for (size_t i = 0; i < 3; ++i) {
            if (readBe64(b, tfdt[i] + 8) != static_cast<uint64_t>(pts[i]) ||
                readBe32(b, trun[i] + 16) != expectedDur[i]) {
                fprintf(stderr, "FAIL task7: sample %zu tfdt/trun duration mismatch\n", i);
                return 67;
            }
        }
        printf("PASS task7: empty init timeline, VFR durations owned by preceding sample\n");
    }

    return 0;
}
