#pragma once
// flacenc.h: hand-rolled FLAC encoder (RFC 9639 subset). MIT license.
//
// Scope: stereo, 16- or 24-bit, fixed-blocksize (4096) streams built from
// the pipeline's planar float audio. Subframe types CONSTANT, VERBATIM and
// FIXED (orders 0-4) with per-frame stereo decorrelation (L/R, L/S, S/R,
// M/S) and Rice-coded residuals with partition-order search. No LPC: fixed
// predictors compress music to roughly 55-70% of raw PCM, a few percent
// behind libFLAC, with ~600 lines and zero dependencies.
//
// Every produced stream is verified in tests/test-flac.cpp by decoding it
// with the vendored dr_flac and comparing PCM bit-exactly against the
// quantized input — FLAC is lossless, so equality is the correctness bar.
//
// Layout written per stream:
//   "fLaC" magic
//   STREAMINFO metadata block (last-block flag set), incl. MD5 of the
//     unencoded interleaved little-endian PCM
//   frames: 4096-sample blocks, final block possibly shorter

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// MD5 (RFC 1321). STREAMINFO carries an MD5 of the unencoded audio so tools
// like `flac -t` can verify integrity. Round constants are derived at first
// use from sin() exactly as the RFC defines them, which avoids transcribing
// a 64-entry table.
// ---------------------------------------------------------------------------

struct flac_md5 {
    uint32_t a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;
    uint64_t total = 0;      // bytes fed in
    uint8_t  buf[64];
    int      buf_len = 0;

    static const uint32_t * K() {
        static uint32_t k[64];
        static bool     init = false;
        if (!init) {
            for (int i = 0; i < 64; i++) {
                k[i] = (uint32_t) (uint64_t) (std::fabs(std::sin((double) (i + 1))) * 4294967296.0);
            }
            init = true;
        }
        return k;
    }

    static uint32_t rotl(uint32_t x, int s) { return (x << s) | (x >> (32 - s)); }

    void block(const uint8_t * p) {
        static const int S[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                   5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                   4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                   6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
        uint32_t m[16];
        for (int i = 0; i < 16; i++) {
            m[i] = (uint32_t) p[i * 4] | ((uint32_t) p[i * 4 + 1] << 8) | ((uint32_t) p[i * 4 + 2] << 16) |
                   ((uint32_t) p[i * 4 + 3] << 24);
        }
        uint32_t A = a, B = b, C = c, D = d;
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int      g;
            if (i < 16) {
                f = (B & C) | (~B & D);
                g = i;
            } else if (i < 32) {
                f = (D & B) | (~D & C);
                g = (5 * i + 1) & 15;
            } else if (i < 48) {
                f = B ^ C ^ D;
                g = (3 * i + 5) & 15;
            } else {
                f = C ^ (B | ~D);
                g = (7 * i) & 15;
            }
            uint32_t tmp = D;
            D            = C;
            C            = B;
            B            = B + rotl(A + f + K()[i] + m[g], S[i]);
            A            = tmp;
        }
        a += A;
        b += B;
        c += C;
        d += D;
    }

    void update(const uint8_t * p, size_t n) {
        total += n;
        while (n > 0) {
            int take = (int) (n < (size_t) (64 - buf_len) ? n : (size_t) (64 - buf_len));
            memcpy(buf + buf_len, p, (size_t) take);
            buf_len += take;
            p += take;
            n -= (size_t) take;
            if (buf_len == 64) {
                block(buf);
                buf_len = 0;
            }
        }
    }

    void finish(uint8_t out[16]) {
        uint64_t bits   = total * 8;
        uint8_t  pad[72] = { 0x80 };
        size_t   pad_n  = (buf_len < 56) ? (size_t) (56 - buf_len) : (size_t) (120 - buf_len);
        update(pad, pad_n);  // note: update() bumps total, but `bits` is already latched
        uint8_t len[8];
        for (int i = 0; i < 8; i++) {
            len[i] = (uint8_t) (bits >> (8 * i));
        }
        update(len, 8);
        uint32_t h[4] = { a, b, c, d };
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                out[i * 4 + j] = (uint8_t) (h[i] >> (8 * j));
            }
        }
    }
};

// ---------------------------------------------------------------------------
// MSB-first bit writer with byte-aligned CRC-8/CRC-16 taps (FLAC checksums
// cover whole bytes, so CRCs are updated as bytes are flushed).
// ---------------------------------------------------------------------------

struct flac_bitwriter {
    std::string out;
    uint64_t    acc    = 0;
    int         nbits  = 0;
    size_t      crc16_from = 0;  // byte offset where the current frame began
    size_t      crc8_from  = 0;

    void put(uint32_t bits_val, int n) {  // n <= 32
        acc = (acc << n) | ((uint64_t) bits_val & ((n == 32) ? 0xffffffffULL : ((1ULL << n) - 1)));
        nbits += n;
        while (nbits >= 8) {
            nbits -= 8;
            out.push_back((char) ((acc >> nbits) & 0xff));
        }
    }

    void put_signed(int32_t v, int n) { put((uint32_t) v, n); }

    void align_zero() {
        if (nbits > 0) {
            put(0, 8 - nbits);
        }
    }

    // CRC-8, poly x^8+x^2+x+1 (0x07), init 0, over [from, out.size())
    uint8_t crc8(size_t from) const {
        uint8_t crc = 0;
        for (size_t i = from; i < out.size(); i++) {
            crc ^= (uint8_t) out[i];
            for (int b = 0; b < 8; b++) {
                crc = (uint8_t) ((crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1));
            }
        }
        return crc;
    }

    // CRC-16, poly x^16+x^15+x^2+1 (0x8005), init 0, over [from, out.size())
    uint16_t crc16(size_t from) const {
        uint16_t crc = 0;
        for (size_t i = from; i < out.size(); i++) {
            crc ^= (uint16_t) ((uint8_t) out[i] << 8);
            for (int b = 0; b < 8; b++) {
                crc = (uint16_t) ((crc & 0x8000) ? (crc << 1) ^ 0x8005 : (crc << 1));
            }
        }
        return crc;
    }
};

// ---------------------------------------------------------------------------
// Residual helpers
// ---------------------------------------------------------------------------

// fixed-predictor residual, order 0-4. in-place capable (dst may alias src
// only for order 0). Values stay well inside int32 for bps <= 26.
static void flac_fixed_residual(const int32_t * x, int n, int order, int32_t * e) {
    switch (order) {
        case 0:
            for (int i = 0; i < n; i++) e[i] = x[i];
            break;
        case 1:
            for (int i = 1; i < n; i++) e[i - 1] = x[i] - x[i - 1];
            break;
        case 2:
            for (int i = 2; i < n; i++) e[i - 2] = x[i] - 2 * x[i - 1] + x[i - 2];
            break;
        case 3:
            for (int i = 3; i < n; i++) e[i - 3] = x[i] - 3 * x[i - 1] + 3 * x[i - 2] - x[i - 3];
            break;
        default:
            for (int i = 4; i < n; i++) e[i - 4] = x[i] - 4 * x[i - 1] + 6 * x[i - 2] - 4 * x[i - 3] + x[i - 4];
            break;
    }
}

static inline uint32_t flac_zigzag(int32_t v) {
    return ((uint32_t) v << 1) ^ (uint32_t) (v >> 31);
}

// exact Rice cost in bits for one parameter over u[0..n)
static uint64_t flac_rice_cost(const uint32_t * u, int n, int k) {
    uint64_t bits = (uint64_t) n * (uint64_t) (k + 1);
    for (int i = 0; i < n; i++) {
        bits += u[i] >> k;
    }
    return bits;
}

// best Rice parameter for u[0..n), returns cost via *cost_out.
// k capped at 14 so the 4-bit parameter field never needs the escape for
// sane audio; the escape (raw verbatim partition) still guards pathological
// residuals via the caller-side verbatim fallback.
static int flac_best_rice_param(const uint32_t * u, int n, uint64_t * cost_out) {
    int      best_k = 0;
    uint64_t best_c = flac_rice_cost(u, n, 0);
    for (int k = 1; k <= 14; k++) {
        uint64_t c = flac_rice_cost(u, n, k);
        if (c < best_c) {
            best_c = c;
            best_k = k;
        }
    }
    *cost_out = best_c;
    return best_k;
}

// cost estimate (in bits) of the residual section for the best partition
// order; fills params/porder for the encoder pass.
static uint64_t flac_plan_partitions(const uint32_t * u,
                                     int              blocksize,
                                     int              order,
                                     int              max_porder,
                                     std::vector<int> & params,
                                     int *            porder_out) {
    int      n_resid  = blocksize - order;
    uint64_t best_tot = ~0ULL;
    int      best_p   = 0;
    std::vector<int> best_params;

    for (int p = 0; p <= max_porder; p++) {
        int nparts = 1 << p;
        if (blocksize % nparts != 0) {
            break;
        }
        int per = blocksize / nparts;
        if (per <= order) {
            break;  // first partition would be empty or negative
        }
        uint64_t         tot = 6;  // 2-bit method + 4-bit partition order
        std::vector<int> ks((size_t) nparts);
        const uint32_t * up = u;
        bool             ok = true;
        for (int i = 0; i < nparts; i++) {
            int cnt = (i == 0) ? per - order : per;
            if (cnt > n_resid) {
                ok = false;
                break;
            }
            uint64_t c;
            ks[(size_t) i] = flac_best_rice_param(up, cnt, &c);
            tot += 4 + c;
            up += cnt;
        }
        if (!ok) {
            break;
        }
        if (tot < best_tot) {
            best_tot    = tot;
            best_p      = p;
            best_params = ks;
        }
    }

    params      = best_params;
    *porder_out = best_p;
    return best_tot;
}

// write the residual section (coding method 00, 4-bit Rice parameters)
static void flac_write_residual(flac_bitwriter & bw,
                                const int32_t *  e,
                                int              blocksize,
                                int              order,
                                const std::vector<int> & params,
                                int              porder) {
    bw.put(0, 2);                     // coding method: 4-bit Rice
    bw.put((uint32_t) porder, 4);
    int nparts = 1 << porder;
    int per    = blocksize / nparts;
    int idx    = 0;
    for (int i = 0; i < nparts; i++) {
        int cnt = (i == 0) ? per - order : per;
        int k   = params[(size_t) i];
        bw.put((uint32_t) k, 4);
        for (int j = 0; j < cnt; j++) {
            uint32_t u = flac_zigzag(e[idx++]);
            uint32_t q = u >> k;
            while (q >= 32) {         // unary run of zeros in chunks
                bw.put(0, 32);
                q -= 32;
            }
            bw.put(1, (int) q + 1);   // q zeros then a 1
            if (k > 0) {
                bw.put(u & ((1u << k) - 1), k);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Subframe planning: for one channel of one frame, choose CONSTANT / FIXED /
// VERBATIM and precompute everything the writer needs.
// ---------------------------------------------------------------------------

struct flac_subplan {
    int              type;      // 0 = constant, 1 = verbatim, 2 = fixed
    int              order;     // fixed order
    int              porder;    // partition order
    std::vector<int> params;    // per-partition rice params
    std::vector<int32_t> resid; // residual for fixed
    uint64_t         bits;      // total subframe payload cost estimate
};

static void flac_plan_subframe(const int32_t * x, int n, int bps, flac_subplan & sp) {
    // constant?
    bool constant = true;
    for (int i = 1; i < n; i++) {
        if (x[i] != x[0]) {
            constant = false;
            break;
        }
    }
    if (constant) {
        sp.type = 0;
        sp.bits = (uint64_t) bps;
        return;
    }

    uint64_t verb_bits = (uint64_t) n * (uint64_t) bps;

    // pick fixed order by minimal absolute residual sum (standard estimator)
    int      best_order = 0;
    uint64_t best_sum   = ~0ULL;
    std::vector<int32_t> e_try((size_t) n), e_best;
    for (int o = 0; o <= 4 && o < n; o++) {
        flac_fixed_residual(x, n, o, e_try.data());
        uint64_t s = 0;
        for (int i = 0; i < n - o; i++) {
            s += (uint64_t) (e_try[(size_t) i] < 0 ? -(int64_t) e_try[(size_t) i] : (int64_t) e_try[(size_t) i]);
        }
        if (s < best_sum) {
            best_sum   = s;
            best_order = o;
            e_best.assign(e_try.begin(), e_try.begin() + (n - o));
        }
    }

    // exact partition/parameter plan for that order
    std::vector<uint32_t> u((size_t) (n - best_order));
    for (int i = 0; i < n - best_order; i++) {
        u[(size_t) i] = flac_zigzag(e_best[(size_t) i]);
    }
    // partition order limit: dr_flac and spec allow up to 15; 6 covers the
    // practical gains at 4096-sample blocks
    std::vector<int> params;
    int              porder = 0;
    uint64_t rice_bits = flac_plan_partitions(u.data(), n, best_order, 6, params, &porder);
    uint64_t fixed_bits = (uint64_t) best_order * (uint64_t) bps + rice_bits;

    if (fixed_bits >= verb_bits) {
        sp.type = 1;
        sp.bits = verb_bits;
        return;
    }
    sp.type   = 2;
    sp.order  = best_order;
    sp.porder = porder;
    sp.params = std::move(params);
    sp.resid  = std::move(e_best);
    sp.bits   = fixed_bits;
}

static void flac_write_subframe(flac_bitwriter & bw, const int32_t * x, int n, int bps, const flac_subplan & sp) {
    bw.put(0, 1);  // zero pad bit
    if (sp.type == 0) {
        bw.put(0, 6);                       // CONSTANT
        bw.put(0, 1);                       // no wasted bits
        bw.put_signed(x[0], bps);
        return;
    }
    if (sp.type == 1) {
        bw.put(1, 6);                       // VERBATIM
        bw.put(0, 1);
        for (int i = 0; i < n; i++) {
            bw.put_signed(x[i], bps);
        }
        return;
    }
    bw.put((uint32_t) (0x08 | sp.order), 6);  // FIXED, order 0-4
    bw.put(0, 1);
    for (int i = 0; i < sp.order; i++) {
        bw.put_signed(x[i], bps);              // warmup samples
    }
    flac_write_residual(bw, sp.resid.data(), n, sp.order, sp.params, sp.porder);
}

// UTF-8-style coded frame number (fixed blocksize streams code the frame
// index; our index always fits well inside 31 bits)
static void flac_write_frame_number(flac_bitwriter & bw, uint32_t v) {
    if (v < 0x80) {
        bw.put(v, 8);
    } else if (v < 0x800) {
        bw.put(0xC0 | (v >> 6), 8);
        bw.put(0x80 | (v & 0x3F), 8);
    } else if (v < 0x10000) {
        bw.put(0xE0 | (v >> 12), 8);
        bw.put(0x80 | ((v >> 6) & 0x3F), 8);
        bw.put(0x80 | (v & 0x3F), 8);
    } else if (v < 0x200000) {
        bw.put(0xF0 | (v >> 18), 8);
        bw.put(0x80 | ((v >> 12) & 0x3F), 8);
        bw.put(0x80 | ((v >> 6) & 0x3F), 8);
        bw.put(0x80 | (v & 0x3F), 8);
    } else if (v < 0x4000000) {
        bw.put(0xF8 | (v >> 24), 8);
        bw.put(0x80 | ((v >> 18) & 0x3F), 8);
        bw.put(0x80 | ((v >> 12) & 0x3F), 8);
        bw.put(0x80 | ((v >> 6) & 0x3F), 8);
        bw.put(0x80 | (v & 0x3F), 8);
    } else {
        bw.put(0xFC | (v >> 30), 8);
        bw.put(0x80 | ((v >> 24) & 0x3F), 8);
        bw.put(0x80 | ((v >> 18) & 0x3F), 8);
        bw.put(0x80 | ((v >> 12) & 0x3F), 8);
        bw.put(0x80 | ((v >> 6) & 0x3F), 8);
        bw.put(0x80 | (v & 0x3F), 8);
    }
}

// ---------------------------------------------------------------------------
// Encoder entry point.
// Planar stereo float in [-1, 1] -> FLAC stream (16 or 24 bit).
// Quantization matches the WAV writers: clamp, NaN->0, scale to int max.
// Returns empty string on invalid arguments.
// ---------------------------------------------------------------------------

static const int FLAC_BLOCK = 4096;

static std::string flac_encode(const float * planar, int T, int sr, int bits) {
    if (!planar || T <= 0 || (bits != 16 && bits != 24) || sr <= 0 || sr >= (1 << 20)) {
        return "";
    }

    const float scale = (bits == 16) ? 32767.0f : 8388607.0f;
    const int32_t smin = -(1 << (bits - 1));
    const int32_t smax = (1 << (bits - 1)) - 1;

    // quantize both channels once; MD5 and encoding read these ints
    std::vector<int32_t> L((size_t) T), R((size_t) T);
    for (int t = 0; t < T; t++) {
        float lf = planar[t], rf = planar[T + t];
        lf = std::isfinite(lf) ? (lf < -1.0f ? -1.0f : (lf > 1.0f ? 1.0f : lf)) : 0.0f;
        rf = std::isfinite(rf) ? (rf < -1.0f ? -1.0f : (rf > 1.0f ? 1.0f : rf)) : 0.0f;
        int32_t li = (int32_t) (lf * scale);
        int32_t ri = (int32_t) (rf * scale);
        L[(size_t) t] = li < smin ? smin : (li > smax ? smax : li);
        R[(size_t) t] = ri < smin ? smin : (ri > smax ? smax : ri);
    }

    // MD5 of unencoded audio: interleaved samples, little-endian, bits/8 bytes
    flac_md5 md5;
    {
        std::vector<uint8_t> chunk;
        int bytes = bits / 8;
        chunk.resize((size_t) T * 2 * (size_t) bytes);
        uint8_t * p = chunk.data();
        for (int t = 0; t < T; t++) {
            uint32_t l = (uint32_t) L[(size_t) t], r = (uint32_t) R[(size_t) t];
            for (int b = 0; b < bytes; b++) *p++ = (uint8_t) (l >> (8 * b));
            for (int b = 0; b < bytes; b++) *p++ = (uint8_t) (r >> (8 * b));
        }
        md5.update(chunk.data(), chunk.size());
    }
    uint8_t digest[16];
    md5.finish(digest);

    flac_bitwriter bw;
    bw.out.reserve((size_t) T * (size_t) (bits / 8) * 2 / 2);  // ~50% guess

    // stream header + STREAMINFO (last metadata block)
    bw.put(0x664C6143, 32);  // "fLaC"
    bw.put(1, 1);            // last metadata block
    bw.put(0, 7);            // type STREAMINFO
    bw.put(34, 24);          // length
    bw.put(FLAC_BLOCK, 16);  // min blocksize
    bw.put(FLAC_BLOCK, 16);  // max blocksize
    bw.put(0, 24);           // min framesize unknown
    bw.put(0, 24);           // max framesize unknown
    bw.put((uint32_t) sr, 20);
    bw.put(1, 3);            // channels - 1
    bw.put((uint32_t) (bits - 1), 5);
    bw.put(0, 4);            // total samples, top 4 of 36 bits (T < 2^32)
    bw.put((uint32_t) T, 32);
    for (int i = 0; i < 16; i++) {
        bw.put(digest[i], 8);
    }

    std::vector<int32_t> mid((size_t) FLAC_BLOCK), side((size_t) FLAC_BLOCK);

    int frame_idx = 0;
    for (int start = 0; start < T; start += FLAC_BLOCK, frame_idx++) {
        int bs = (T - start < FLAC_BLOCK) ? (T - start) : FLAC_BLOCK;

        const int32_t * l = L.data() + start;
        const int32_t * r = R.data() + start;
        for (int i = 0; i < bs; i++) {
            mid[(size_t) i]  = (l[i] + r[i]) >> 1;
            side[(size_t) i] = l[i] - r[i];
        }

        // plan all four candidate channels, then pick the cheapest pairing
        flac_subplan pl, pr, pm, ps;
        flac_plan_subframe(l, bs, bits, pl);
        flac_plan_subframe(r, bs, bits, pr);
        flac_plan_subframe(mid.data(), bs, bits, pm);
        flac_plan_subframe(side.data(), bs, bits + 1, ps);

        uint64_t c_lr = pl.bits + pr.bits;
        uint64_t c_ls = pl.bits + ps.bits;
        uint64_t c_sr = ps.bits + pr.bits;
        uint64_t c_ms = pm.bits + ps.bits;

        int assign;  // frame header channel assignment code
        if (c_lr <= c_ls && c_lr <= c_sr && c_lr <= c_ms) {
            assign = 0x1;
        } else if (c_ls <= c_sr && c_ls <= c_ms) {
            assign = 0x8;
        } else if (c_sr <= c_ms) {
            assign = 0x9;
        } else {
            assign = 0xA;
        }

        // frame header
        bw.crc8_from  = bw.out.size();
        bw.crc16_from = bw.out.size();
        bw.put(0x3FFE, 14);  // sync
        bw.put(0, 1);        // reserved
        bw.put(0, 1);        // fixed blocksize stream
        bw.put((bs == FLAC_BLOCK) ? 0xC : 0x7, 4);  // 4096 or 16-bit at end
        bw.put(0, 4);        // sample rate: from STREAMINFO
        bw.put((uint32_t) assign, 4);
        bw.put((bits == 16) ? 0x4 : 0x6, 3);
        bw.put(0, 1);        // reserved
        flac_write_frame_number(bw, (uint32_t) frame_idx);
        if (bs != FLAC_BLOCK) {
            bw.put((uint32_t) (bs - 1), 16);
        }
        bw.put(bw.crc8(bw.crc8_from), 8);

        // subframes per assignment
        switch (assign) {
            case 0x1:
                flac_write_subframe(bw, l, bs, bits, pl);
                flac_write_subframe(bw, r, bs, bits, pr);
                break;
            case 0x8:
                flac_write_subframe(bw, l, bs, bits, pl);
                flac_write_subframe(bw, side.data(), bs, bits + 1, ps);
                break;
            case 0x9:
                flac_write_subframe(bw, side.data(), bs, bits + 1, ps);
                flac_write_subframe(bw, r, bs, bits, pr);
                break;
            default:
                flac_write_subframe(bw, mid.data(), bs, bits, pm);
                flac_write_subframe(bw, side.data(), bs, bits + 1, ps);
                break;
        }

        bw.align_zero();
        uint16_t crc = bw.crc16(bw.crc16_from);
        bw.put(crc, 16);
    }

    return bw.out;
}
