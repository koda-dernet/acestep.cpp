// test-flac: hand-rolled FLAC encoder correctness via dr_flac round-trip.
//
// FLAC is lossless, so the bar is exact: every stream flacenc produces must
// decode (with the vendored dr_flac) to PCM that is bit-identical to the
// quantized input, at both 16- and 24-bit, across signal shapes that hit
// every subframe type (constant, verbatim-ish noise, fixed orders) and every
// stereo decorrelation mode. Also verifies the MD5 helper against RFC 1321
// vectors and the audio-io magic-byte routing.

#include "audio-io.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// same quantizer as flac_encode: clamp + scale + clamp to int range
static int32_t quant(float x, int bits) {
    float scale = (bits == 16) ? 32767.0f : 8388607.0f;
    if (!std::isfinite(x)) {
        x = 0.0f;
    }
    x = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
    int32_t v    = (int32_t) (x * scale);
    int32_t smin = -(1 << (bits - 1)), smax = (1 << (bits - 1)) - 1;
    return v < smin ? smin : (v > smax ? smax : v);
}

static void md5_hex(const uint8_t d[16], char out[33]) {
    for (int i = 0; i < 16; i++) {
        snprintf(out + i * 2, 3, "%02x", d[i]);
    }
}

static void test_md5() {
    {
        flac_md5 m;
        uint8_t  d[16];
        m.finish(d);
        char hex[33];
        md5_hex(d, hex);
        assert(strcmp(hex, "d41d8cd98f00b204e9800998ecf8427e") == 0);
    }
    {
        flac_md5 m;
        m.update((const uint8_t *) "abc", 3);
        uint8_t d[16];
        m.finish(d);
        char hex[33];
        md5_hex(d, hex);
        assert(strcmp(hex, "900150983cd24fb0d6963f7d28e17f72") == 0);
    }
    // multi-block update crossing the 64-byte boundary
    {
        const char * s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        flac_md5     m;
        m.update((const uint8_t *) s, strlen(s));
        uint8_t d[16];
        m.finish(d);
        char hex[33];
        md5_hex(d, hex);
        assert(strcmp(hex, "8215ef0796a20bcaaae116d3876c664a") == 0);
    }
    printf("[FLAC-Test] MD5 vectors OK\n");
}

// encode -> dr_flac decode -> bit-exact compare
static void roundtrip(const char * name, const std::vector<float> & planar, int T, int sr, int bits) {
    std::string flac = flac_encode(planar.data(), T, sr, bits);
    assert(!flac.empty());

    drflac * f = drflac_open_memory(flac.data(), flac.size(), NULL);
    if (!f) {
        printf("[FLAC-Test] %s (%d-bit): dr_flac REJECTED the stream\n", name, bits);
        assert(false);
    }
    assert((int) f->totalPCMFrameCount == T);
    assert(f->channels == 2);
    assert((int) f->sampleRate == sr);
    assert((int) f->bitsPerSample == bits);

    std::vector<int32_t> got((size_t) T * 2);
    drflac_uint64        n = drflac_read_pcm_frames_s32(f, (drflac_uint64) T, got.data());
    drflac_close(f);
    assert((int) n == T);

    for (int t = 0; t < T; t++) {
        // dr_flac returns s32 shifted to the top; shift back to native depth
        int32_t l = got[(size_t) t * 2 + 0] >> (32 - bits);
        int32_t r = got[(size_t) t * 2 + 1] >> (32 - bits);
        int32_t el = quant(planar[(size_t) t], bits);
        int32_t er = quant(planar[(size_t) T + t], bits);
        if (l != el || r != er) {
            printf("[FLAC-Test] %s (%d-bit): MISMATCH at t=%d: got (%d,%d) want (%d,%d)\n", name, bits, t, l, r, el,
                   er);
            assert(false);
        }
    }

    double ratio = (double) flac.size() / ((double) T * 2 * (bits / 8));
    printf("[FLAC-Test] %s (%d-bit): OK, %d samples, %.1f%% of raw\n", name, bits, T, ratio * 100.0);
}

int main() {
    test_md5();

    const int sr = 48000;

    // silence: constant subframes, M/S all-zero
    {
        int                T = 9000;  // 2 full frames + partial
        std::vector<float> a((size_t) T * 2, 0.0f);
        roundtrip("silence", a, T, sr, 16);
        roundtrip("silence", a, T, sr, 24);
    }

    // DC offset: constant, non-zero, L != R
    {
        int                T = 4096;  // exactly one frame
        std::vector<float> a((size_t) T * 2);
        for (int t = 0; t < T; t++) {
            a[(size_t) t]     = 0.25f;
            a[(size_t) T + t] = -0.125f;
        }
        roundtrip("dc", a, T, sr, 16);
        roundtrip("dc", a, T, sr, 24);
    }

    // sine: smooth, fixed predictors should bite; L=R exercises mid/side
    {
        int                T = 48000;
        std::vector<float> a((size_t) T * 2);
        for (int t = 0; t < T; t++) {
            float v           = 0.7f * sinf(2.0f * 3.14159265f * 440.0f * (float) t / (float) sr);
            a[(size_t) t]     = v;
            a[(size_t) T + t] = v;
        }
        roundtrip("sine-mono", a, T, sr, 16);
        roundtrip("sine-mono", a, T, sr, 24);
    }

    // sweep with phase-inverted right channel: stresses the side channel's
    // bps+1 samples and left/right-side assignments
    {
        int                T = 60000;
        std::vector<float> a((size_t) T * 2);
        for (int t = 0; t < T; t++) {
            float ph          = 2.0f * 3.14159265f * (20.0f + 8000.0f * (float) t / (float) T) * (float) t / (float) sr;
            float v           = 0.9f * sinf(ph);
            a[(size_t) t]     = v;
            a[(size_t) T + t] = -v;
        }
        roundtrip("sweep-inverted", a, T, sr, 16);
        roundtrip("sweep-inverted", a, T, sr, 24);
    }

    // white noise: near-incompressible, exercises verbatim fallback and
    // high Rice parameters
    {
        int                T = 20000;
        std::vector<float> a((size_t) T * 2);
        uint32_t           s = 0x12345678;
        for (size_t i = 0; i < a.size(); i++) {
            s    = s * 1664525u + 1013904223u;
            a[i] = ((float) (s >> 8) / 8388608.0f) - 1.0f;
        }
        roundtrip("noise", a, T, sr, 16);
        roundtrip("noise", a, T, sr, 24);
    }

    // hard-clipped square with out-of-range and NaN inputs: quantizer edge
    {
        int                T = 10000;
        std::vector<float> a((size_t) T * 2);
        for (int t = 0; t < T; t++) {
            float v           = ((t / 100) % 2 == 0) ? 1.5f : -1.5f;  // clamps to +-1
            a[(size_t) t]     = v;
            a[(size_t) T + t] = (t % 997 == 0) ? NAN : -v;
        }
        roundtrip("square-clip-nan", a, T, sr, 16);
        roundtrip("square-clip-nan", a, T, sr, 24);
    }

    // tiny streams: T smaller than any predictor order / one sample
    {
        std::vector<float> a1 = { 0.5f, -0.5f };  // T=1 planar
        roundtrip("one-sample", a1, 1, sr, 16);
        std::vector<float> a3 = { 0.1f, 0.2f, 0.3f, -0.1f, -0.2f, -0.3f };  // T=3
        roundtrip("three-samples", a3, 3, sr, 24);
    }

    // audio-io magic routing: encode, then decode through audio_read_buf
    {
        int                T = 6000;
        std::vector<float> a((size_t) T * 2);
        for (int t = 0; t < T; t++) {
            float v           = 0.5f * sinf(2.0f * 3.14159265f * 220.0f * (float) t / (float) sr);
            a[(size_t) t]     = v;
            a[(size_t) T + t] = 0.5f * v;
        }
        std::string flac = flac_encode(a.data(), T, sr, 16);
        int         Td = 0, srd = 0;
        float *     dec = audio_read_buf((const uint8_t *) flac.data(), flac.size(), &Td, &srd);
        assert(dec);
        assert(Td == T);
        assert(srd == sr);
        // spot-check float samples match the quantized originals within 1 LSB
        for (int t = 0; t < T; t += 97) {
            float want = (float) quant(a[(size_t) t], 16) / 2147483648.0f * 65536.0f;
            assert(fabsf(dec[t] - want) < 2.0f / 32768.0f);
        }
        free(dec);
        printf("[FLAC-Test] audio_read_buf magic routing OK\n");
    }

    printf("[FLAC-Test] All tests passed\n");
    return 0;
}
