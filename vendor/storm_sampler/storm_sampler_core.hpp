/**
 * storm_sampler_core.hpp
 * STORM -- Stabilized Taylor Oscillation with Runge-Kutta Memory
 *
 * © 2026 Alexander Allan (MDMAchine) | A&E Concepts
 * GPL v3 -- Public version.
 *
 * Version: 2.1.0
 * PARITY: Exact algorithmic parity with storm_sampler_core.py v2.0.0 / dev node v1.5
 *
 * [acestep.cpp patch] DEVIATION from upstream: the AB3/AB4/AB5 history
 * coefficients in stork_step and dpmpp3m_step were replaced with exact
 * nonuniform Adams-Bashforth weights (detail::ab_weights). The upstream
 * closed forms are inconsistent (weights do not sum to 1), which
 * systematically over-integrates every order>=3 step and injects wideband
 * noise into the samples. Orders 1-2, stiffness logic, look-back, sub-steps
 * and restarts are unchanged.
 *
 * Changelog:
 *   2.1.0 -- EMA-weighted velocity for aligned restarts
 *   2.0.0 -- RK2/3/4/5 adaptive multi-order (single NFE, cached derivatives)
 *            Adaptive sub-stepping (manifold fracture defense, HPC-style)
 *            SDE restarts: isotropic + velocity-aligned Langevin noise
 *            Adaptive calibration steps (CALIB_FRAC of total steps)
 *            Batch-safe reduce_dims projection in velocity-aligned restart
 *   1.2.0 -- Look-Back SNR smoother
 *   1.0.0 -- Initial release
 *
 * model_fn signature:
 *   void model_fn(const float* x, float sigma, float* v_out, size_t n, void* user_data)
 *   NOTE: model_fn must return velocity v = (x - denoised) / sigma
 */

#pragma once

#include <cmath>
#include <cstring>
#include <functional>
#include <vector>
#include <set>
#include <string>
#include <cstdio>
#include <algorithm>
#include <random>

namespace storm {

using ModelFn    = std::function<void(const float*, float, float*, size_t, void*)>;
using CallbackFn = std::function<void(int, const float*, float, size_t)>;

// ─────────────────────────────────────────────
// CONFIG
// ─────────────────────────────────────────────
struct Config {
    float  stiffness_threshold   = 0.15f;
    float  hysteresis_margin     = 0.05f;
    float  ema_alpha             = 0.30f;
    int    cache_depth           = 5;
    float  calib_frac            = 0.12f;
    std::string rk_order         = "auto";  // "auto"|"2"|"3"|"4"|"5"
    bool   adaptive_sub_step     = true;
    float  sub_step_threshold    = 0.0f;
    int    sub_step_max_depth    = 2;
    bool   look_back_enabled     = true;
    float  look_back_lambda      = 0.35f;
    float  look_back_snr_power   = 1.5f;
    bool   enable_restarts       = false;
    std::set<int> restart_steps  = {};
    float  restart_noise_scale   = 0.5f;
    float  restart_s_noise       = 1.0f;
    int    restart_seed          = 42;
    bool   restart_flush_cache   = true;
    bool   restart_aligned_noise = true;
    bool   force_pure_euler      = false;
    bool   verbose               = false;
    CallbackFn callback          = nullptr;
};

struct StiffnessBaseline {
    float  ema            = 0.0f;
    float  sum            = 0.0f;
    int    count          = 0;
    float  last_ratio     = 0.0f;
    float  last_threshold = 0.15f;
    bool   prev_mode_dpm  = false;
};

struct CacheEntry {
    std::vector<float> v;
    float              sigma = 0.0f;
};

// ─────────────────────────────────────────────
// INTERNAL HELPERS
// ─────────────────────────────────────────────
namespace detail {

inline float vec_norm(const float* v, size_t n) {
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) s += (double)v[i]*v[i];
    return (float)std::sqrt(s);
}

inline float vec_sub_norm(const float* a, const float* b, size_t n) {
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) { double d=(double)a[i]-b[i]; s+=d*d; }
    return (float)std::sqrt(s);
}

inline float vec_dot(const float* a, const float* b, size_t n) {
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) s += (double)a[i]*b[i];
    return (float)s;
}

inline bool has_nan_inf(const float* v, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (std::isnan(v[i]) || std::isinf(v[i])) return true;
    return false;
}

inline float clampf(float x, float lo, float hi) { return std::max(lo, std::min(hi, x)); }

inline int rk_order_int(const std::string& order, int n_cache) {
    if (order == "auto") return std::min(n_cache + 1, 5);
    int o = std::stoi(order);
    return std::min(o, n_cache + 1);
}

// [acestep.cpp patch] Exact nonuniform Adams-Bashforth weights.
//
// Upstream v2.1.0 shipped hand-derived AB3/AB4/AB5 coefficients that are
// inconsistent: integrating a CONSTANT velocity field does not return
// x + dt*v (uniform-grid weight sums are 7/6 for AB3, 13/12 for AB4 instead
// of 1). Every order>=3 step therefore over-integrates by ~8-17% and smears
// the error across stale velocities, which audibly injects wideband noise.
//
// This helper computes the exact weights by moment matching: find c_j with
//   sum_j c_j * u_j^p = dt^p / (p+1)   for p = 0..k-1,
// where u_j = sigma_j - sigma_curr are the history node offsets (u_0 = 0).
// Then x_next = x + dt * sum_j c_j v_j integrates the degree-(k-1)
// interpolant of the velocity history exactly. On a uniform grid this
// reproduces the classic AB weights (23/12, -4/3, 5/12 for AB3).
// Solved by Gauss-Jordan with partial pivoting; returns false when the
// node set is degenerate (repeated sigmas) so callers can drop an order.
inline bool ab_weights(const double* u, int k, double dt, double* c) {
    double A[5][6];
    for (int p = 0; p < k; ++p) {
        for (int j = 0; j < k; ++j) A[p][j] = std::pow(u[j], (double)p);
        A[p][k] = std::pow(dt, (double)p) / (double)(p + 1);
    }
    for (int col = 0; col < k; ++col) {
        int piv = col;
        for (int r = col + 1; r < k; ++r)
            if (std::fabs(A[r][col]) > std::fabs(A[piv][col])) piv = r;
        if (std::fabs(A[piv][col]) < 1e-12) return false;
        if (piv != col)
            for (int cc = 0; cc <= k; ++cc) std::swap(A[piv][cc], A[col][cc]);
        for (int r = 0; r < k; ++r) {
            if (r == col) continue;
            double f = A[r][col] / A[col][col];
            for (int cc = col; cc <= k; ++cc) A[r][cc] -= f * A[col][cc];
        }
    }
    for (int j = 0; j < k; ++j) c[j] = A[j][k] / A[j][j];
    return true;
}

} // namespace detail

// ─────────────────────────────────────────────
// LOOK-BACK SMOOTHER
// Parity: storm_sampler_core.py::look_back_smooth
// ─────────────────────────────────────────────
inline float look_back_smooth(float* x, const float* x_prev, float sigma_curr,
                               float sigma_max, float lambda_base, float snr_power, size_t n) {
    if (!x_prev) return 0.0f;
    float ratio = std::min(sigma_curr / std::max(sigma_max, 1e-8f), 1.0f);
    float lam   = lambda_base * std::pow(ratio, snr_power);
    for (size_t i = 0; i < n; ++i)
        x[i] = (1.0f - lam) * x[i] + lam * x_prev[i];
    return lam;
}

// ─────────────────────────────────────────────
// STIFFNESS DETECTION (adaptive calib)
// Parity: storm_sampler_core.py::compute_stiffness
// ─────────────────────────────────────────────
inline bool compute_stiffness(const float* v_curr, const std::vector<CacheEntry>& v_cache,
                               int step_idx, StiffnessBaseline& bl,
                               float threshold, float ema_alpha, int n_calib, size_t n,
                               float* cos_sim_out) {
    if (v_cache.empty()) { if (cos_sim_out) *cos_sim_out = 0.0f; return true; }

    const float* v_prev  = v_cache.back().v.data();
    float norm_delta     = detail::vec_sub_norm(v_curr, v_prev, n);
    float norm_curr      = detail::vec_norm(v_curr, n) + 1e-8f;
    float raw_ratio      = norm_delta / norm_curr;

    float prev_ema  = (bl.count == 0) ? raw_ratio : bl.ema;
    float smoothed  = ema_alpha * raw_ratio + (1.0f - ema_alpha) * prev_ema;
    bl.ema          = smoothed;

    // Cosine similarity
    float dot  = detail::vec_dot(v_curr, v_prev, n);
    float nc   = detail::vec_norm(v_curr, n);
    float np_  = detail::vec_norm(v_prev, n);
    float cs   = dot / (nc * np_ + 1e-8f);
    if (cos_sim_out) *cos_sim_out = cs;

    if (step_idx < n_calib) {
        bl.sum   += smoothed;
        bl.count += 1;
        bl.last_ratio = smoothed;
        return true;
    }

    float bmean  = bl.sum / (float)std::max(bl.count, 1);
    float adap   = threshold * (bmean / 0.15f);
    adap         = detail::clampf(adap, 0.05f, 0.50f);
    bool stiff   = smoothed > adap;
    bl.last_ratio     = smoothed;
    bl.last_threshold = adap;
    return stiff;
}

// ─────────────────────────────────────────────
// STORK MULTI-ORDER (RK2/3/4/5)
// Parity: storm_sampler_core.py::stork_step
// ─────────────────────────────────────────────
inline int stork_step(const std::vector<CacheEntry>& v_cache, const float* x, float sigma_curr,
                       float sigma_next, const ModelFn& model_fn, void* ud,
                       float* x_next, float* v_curr_out, const std::string& order, size_t n) {
    float dt = sigma_next - sigma_curr;
    model_fn(x, sigma_curr, v_curr_out, n, ud);

    int n_cache = (int)v_cache.size();
    int actual_order = (n_cache >= 1) ? std::min(detail::rk_order_int(order, n_cache), 5) : 1;
    actual_order = std::max(actual_order, 1);

    if (n_cache < 1 || actual_order <= 1) {
        for (size_t i=0;i<n;++i) x_next[i] = x[i] + dt*v_curr_out[i];
        return 1;
    }

    const float* vp0     = v_cache.back().v.data();
    float sigma_prev     = v_cache.back().sigma;
    float dot0           = detail::vec_dot(v_curr_out, vp0, n);
    float nc0            = detail::vec_norm(v_curr_out, n);
    float np0            = detail::vec_norm(vp0, n);
    float damping        = detail::clampf(dot0 / (nc0*np0+1e-8f), 0.0f, 1.0f);
    float denom          = sigma_curr - sigma_prev;

    if (std::abs(denom) < 1e-8f) {
        for (size_t i=0;i<n;++i) x_next[i]=x[i]+dt*v_curr_out[i];
        return 2;
    }
    float alpha = (sigma_next - sigma_curr) / denom;

    auto AB2 = [&]() {
        for (size_t i=0;i<n;++i) {
            float ve = v_curr_out[i] + (alpha*damping)*(v_curr_out[i]-vp0[i]);
            x_next[i] = x[i] + dt*(0.5f*v_curr_out[i] + 0.5f*ve);
        }
    };

    if (actual_order == 2) { AB2(); return 2; }

    // [acestep.cpp patch] AB3/AB4/AB5 via exact nonuniform Adams-Bashforth
    // weights (see detail::ab_weights). Upstream's closed-form coefficients
    // were inconsistent and over-integrated every order>=3 step. Degenerate
    // node spacing drops down one order at a time, ending at AB2.
    if (actual_order >= 3 && n_cache >= 2) {
        int want = std::min(actual_order, 1 + n_cache);
        for (int k = std::min(want, 5); k >= 3; --k) {
            double u[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
            bool   ok   = true;
            for (int j = 1; j < k && ok; ++j) {
                u[j] = (double)v_cache[n_cache - j].sigma - (double)sigma_curr;
                for (int q = 0; q < j; ++q)
                    if (std::fabs(u[j] - u[q]) < 1e-8) { ok = false; break; }
            }
            double c[5];
            if (!ok || !detail::ab_weights(u, k, (double)dt, c)) continue;
            for (size_t i = 0; i < n; ++i) {
                double vp = c[0] * (double)v_curr_out[i];
                for (int j = 1; j < k; ++j) vp += c[j] * (double)v_cache[n_cache - j].v[i];
                x_next[i] = x[i] + dt * (v_curr_out[i] + damping * ((float)vp - v_curr_out[i]));
            }
            return k;
        }
    }
    AB2(); return 2;
}

// ─────────────────────────────────────────────
// ADAPTIVE SUB-STEPPING
// Parity: storm_sampler_core.py::sub_step_stork
// ─────────────────────────────────────────────
inline int sub_step_stork(std::vector<CacheEntry>& v_cache, float* x,
                           float sigma_curr, float sigma_next, const ModelFn& model_fn, void* ud,
                           float* v_curr_out, const Config& cfg, size_t n,
                           int depth = 0);

inline int sub_step_stork(std::vector<CacheEntry>& v_cache, float* x,
                           float sigma_curr, float sigma_next, const ModelFn& model_fn, void* ud,
                           float* v_curr_out, const Config& cfg, size_t n, int depth) {
    if (depth >= cfg.sub_step_max_depth) {
        std::vector<float> xn(n);
        int ord = stork_step(v_cache, x, sigma_curr, sigma_next, model_fn, ud,
                             xn.data(), v_curr_out, cfg.rk_order, n);
        std::memcpy(x, xn.data(), n*sizeof(float));
        return ord;
    }

    if (!v_cache.empty()) {
        std::vector<float> v_probe(n);
        model_fn(x, sigma_curr, v_probe.data(), n, ud);
        const float* v_prev = v_cache.back().v.data();
        float dot     = detail::vec_dot(v_probe.data(), v_prev, n);
        float nc      = detail::vec_norm(v_probe.data(), n);
        float np_     = detail::vec_norm(v_prev, n);
        float cos_sim = dot / (nc*np_+1e-8f);

        if (cos_sim < cfg.sub_step_threshold) {
            float sigma_mid = (sigma_curr + sigma_next) * 0.5f;
            if (cfg.verbose)
                std::printf("[STORM] 🔀 SUB-STEP depth=%d | cos_sim=%.4f | σ %.4f→%.4f→%.4f\n",
                            depth, cos_sim, sigma_curr, sigma_mid, sigma_next);

            std::vector<float> x_mid(x, x+n);
            int o1 = sub_step_stork(v_cache, x_mid.data(), sigma_curr, sigma_mid,
                                    model_fn, ud, v_curr_out, cfg, n, depth+1);
            CacheEntry e1; e1.v.assign(v_curr_out, v_curr_out+n); e1.sigma=sigma_curr;
            v_cache.push_back(std::move(e1));
            while((int)v_cache.size()>cfg.cache_depth) v_cache.erase(v_cache.begin());

            int o2 = sub_step_stork(v_cache, x_mid.data(), sigma_mid, sigma_next,
                                    model_fn, ud, v_curr_out, cfg, n, depth+1);
            std::memcpy(x, x_mid.data(), n*sizeof(float));
            return std::max(o1,o2);
        }
    }

    std::vector<float> xn(n);
    int ord = stork_step(v_cache, x, sigma_curr, sigma_next, model_fn, ud,
                         xn.data(), v_curr_out, cfg.rk_order, n);
    std::memcpy(x, xn.data(), n*sizeof(float));
    return ord;
}

// ─────────────────────────────────────────────
// DPM++3M
// Parity: storm_sampler_core.py::dpmpp3m_step
// ─────────────────────────────────────────────
inline void dpmpp3m_step(const std::vector<CacheEntry>& v_cache, const float* x,
                          float sigma_curr, float sigma_next, const ModelFn& model_fn, void* ud,
                          float* x_next, float* v_curr_out, size_t n) {
    float dt = sigma_next - sigma_curr;
    model_fn(x, sigma_curr, v_curr_out, n, ud);
    int nc = (int)v_cache.size();

    if (nc >= 2) {
        const float* v1=v_cache[nc-1].v.data(); float s1=v_cache[nc-1].sigma;
        const float* v2=v_cache[nc-2].v.data(); float s2=v_cache[nc-2].sigma;
        // [acestep.cpp patch] exact nonuniform AB3 weights (see detail::ab_weights);
        // upstream's closed-form coefficients summed to 7/6 on uniform grids.
        double u[3] = {0.0, (double)s1 - (double)sigma_curr, (double)s2 - (double)sigma_curr};
        double c[3];
        bool ok = std::fabs(u[1]) >= 1e-8 && std::fabs(u[2] - u[1]) >= 1e-8 &&
                  std::fabs(u[2]) >= 1e-8 && detail::ab_weights(u, 3, (double)dt, c);
        if (!ok) {
            for (size_t i=0;i<n;++i) x_next[i]=x[i]+dt*v_curr_out[i];
        } else {
            for (size_t i=0;i<n;++i)
                x_next[i]=x[i]+dt*(float)(c[0]*(double)v_curr_out[i]+c[1]*(double)v1[i]+c[2]*(double)v2[i]);
        }
    } else if (nc >= 1) {
        const float* v1=v_cache.back().v.data(); float s1=v_cache.back().sigma;
        float h=sigma_curr-s1;
        if (std::abs(h)<1e-8f) {
            for (size_t i=0;i<n;++i) x_next[i]=x[i]+dt*v_curr_out[i];
        } else {
            for (size_t i=0;i<n;++i)
                x_next[i]=x[i]+dt*(v_curr_out[i]+(dt/(2.0f*h))*(v_curr_out[i]-v1[i]));
        }
    } else {
        for (size_t i=0;i<n;++i) x_next[i]=x[i]+dt*v_curr_out[i];
    }
}

// ─────────────────────────────────────────────
// STORM SAMPLER -- Full inference loop
// Parity: storm_sampler_core.py::storm_sampler
// ─────────────────────────────────────────────
inline void run(const ModelFn& model_fn, float* x, const float* sigmas,
                size_t n, int n_steps, void* user_data, const Config& cfg = Config{}) {

    std::vector<CacheEntry>  v_cache;
    StiffnessBaseline        bl{};
    std::vector<float>       v_curr(n), x_next(n), v_probe(n);
    float                    sigma_max = sigmas[0];
    int                      n_calib   = std::max(2, std::min(5, (int)(n_steps*cfg.calib_frac)));

    if (cfg.verbose)
        std::printf("[STORM] Schedule: %d steps | Calib: %d | RK: %s | Cache: %d\n",
                    n_steps, n_calib, cfg.rk_order.c_str(), cfg.cache_depth);

    // Seed x_prev for step-0 look-back
    std::vector<float> x_prev_lb;
    if (cfg.look_back_enabled) {
        x_prev_lb.resize(n);
        std::mt19937 rng(42);
        std::normal_distribution<float> nd(0.0f, sigma_max * 0.1f);
        for (size_t i=0;i<n;++i) x_prev_lb[i] = x[i] + nd(rng);
    }

    for (int i = 0; i < n_steps; ++i) {
        float sigma_curr = sigmas[i];
        float sigma_next = sigmas[i+1];

        if (sigma_next == 0.0f) {
            model_fn(x, sigma_curr, v_curr.data(), n, user_data);
            float dt = sigma_next - sigma_curr;
            for (size_t j=0;j<n;++j) x[j] += dt*v_curr[j];
            if (cfg.verbose) std::printf("[STORM] Step %02d: FINAL (Euler terminal)\n", i);
            break;
        }

        std::vector<float> x_prev_lb_before;
        if (cfg.look_back_enabled) x_prev_lb_before.assign(x, x+n);

        // ── EULER BYPASS ──
        if (cfg.force_pure_euler) {
            model_fn(x, sigma_curr, v_curr.data(), n, user_data);
            float dt=sigma_next-sigma_curr;
            for (size_t j=0;j<n;++j) x[j]+=dt*v_curr[j];
            CacheEntry e; e.v.assign(v_curr.begin(),v_curr.end()); e.sigma=sigma_curr;
            v_cache.push_back(std::move(e));
            while((int)v_cache.size()>cfg.cache_depth) v_cache.erase(v_cache.begin());
            bl.prev_mode_dpm=false;
        } else {
            // ── STIFFNESS DETECTION ──
            bool stiff; float cos_sim_val=0.0f;
            bool have_probe=false;
            if (!v_cache.empty()) {
                model_fn(x, sigma_curr, v_probe.data(), n, user_data);
                have_probe=true;
                stiff = compute_stiffness(v_probe.data(), v_cache, i, bl,
                                          cfg.stiffness_threshold, cfg.ema_alpha, n_calib, n, &cos_sim_val);
            } else { stiff=true; }

            // Hysteresis
            if (bl.prev_mode_dpm && !stiff)
                if (bl.last_ratio > bl.last_threshold + cfg.hysteresis_margin) stiff=true;

            // Cached model wrapper
            ModelFn model_cached = [&](const float* xi, float si, float* vo, size_t ni, void* ud2) {
                if (have_probe && std::abs(si-sigma_curr)<1e-7f)
                    std::memcpy(vo, v_probe.data(), ni*sizeof(float));
                else
                    model_fn(xi, si, vo, ni, ud2);
            };

            std::string mode; int actual_order;
            if (stiff) {
                if (cfg.adaptive_sub_step && !v_cache.empty()) {
                    // Sub-step path modifies x in-place
                    std::vector<float> xc(x,x+n);
                    actual_order = sub_step_stork(v_cache, xc.data(), sigma_curr, sigma_next,
                                                  model_cached, user_data, v_curr.data(), cfg, n, 0);
                    std::memcpy(x_next.data(), xc.data(), n*sizeof(float));
                } else {
                    actual_order = stork_step(v_cache, x, sigma_curr, sigma_next,
                                             model_cached, user_data, x_next.data(), v_curr.data(),
                                             cfg.rk_order, n);
                }
                mode = "STORK";
            } else {
                dpmpp3m_step(v_cache, x, sigma_curr, sigma_next,
                             model_cached, user_data, x_next.data(), v_curr.data(), n);
                mode="DPM++"; actual_order=3;
            }

            if (cfg.verbose) {
                const char* sp = (stiff && bl.prev_mode_dpm) ? " -> CURVATURE SPIKE" : "";
                std::printf("[STORM] Step %02d: %-5s RK%d | Ratio: %.3f | Threshold: %.3f%s\n",
                            i, mode.c_str(), actual_order, bl.last_ratio, bl.last_threshold, sp);
            }

            // NaN guard
            if (detail::has_nan_inf(x_next.data(), n)) {
                std::printf("[STORM] NaN/Inf at step %d. Flushing cache.\n", i);
                model_fn(x, sigma_curr, v_curr.data(), n, user_data);
                float dt=sigma_next-sigma_curr;
                for (size_t j=0;j<n;++j) x_next[j]=x[j]+dt*v_curr[j];
                v_cache.clear(); bl.prev_mode_dpm=false;
            }

            CacheEntry e; e.v.assign(v_curr.begin(),v_curr.end()); e.sigma=sigma_curr;
            v_cache.push_back(std::move(e));
            while((int)v_cache.size()>cfg.cache_depth) v_cache.erase(v_cache.begin());
            bl.prev_mode_dpm = (mode=="DPM++");
            std::memcpy(x, x_next.data(), n*sizeof(float));
        }

        // ── LOOK-BACK ──
        if (cfg.look_back_enabled && !x_prev_lb.empty()) {
            float lam = look_back_smooth(x, x_prev_lb.data(), sigma_curr, sigma_max,
                                         cfg.look_back_lambda, cfg.look_back_snr_power, n);
            if (cfg.verbose) std::printf("[STORM] LookBack λ=%.4f @ σ=%.3f\n", lam, sigma_curr);
        }
        if (!x_prev_lb_before.empty()) x_prev_lb = x_prev_lb_before;

        // ── SDE RESTARTS ──
        if (cfg.enable_restarts && sigma_next > 0.0f && cfg.restart_steps.count(i)) {
            float s_res  = sigma_next + (sigma_curr - sigma_next) * cfg.restart_noise_scale;
            float n_amt  = std::sqrt(std::max(0.0f, s_res*s_res - sigma_next*sigma_next) + 1e-8f);

            std::vector<float> noise(n);
            std::mt19937 rng_r((unsigned)(cfg.restart_seed + i*1000));
            std::normal_distribution<float> nd(0.0f, 1.0f);

            if (cfg.restart_aligned_noise && v_cache.size() >= 2) {
                // Velocity-aligned Langevin -- noise ⊥ principal velocity direction
                // EMA-weighted velocity: recent vectors get highest weight (0.5^(depth-1-i))
                std::vector<float> v_mean(n, 0.0f);
                float _wsum = 0.0f;
                int _depth = (int)v_cache.size();
                for (int _i = 0; _i < _depth; ++_i) {
                    float _w = std::pow(0.5f, (float)(_depth - 1 - _i));
                    _wsum += _w;
                    for (size_t j=0;j<n;++j) v_mean[j] += v_cache[_i].v[j] * _w;
                }
                for (size_t j=0;j<n;++j) v_mean[j] /= _wsum;
                float vn = detail::vec_norm(v_mean.data(), n) + 1e-8f;
                std::vector<float> v_dir(n);
                for (size_t j=0;j<n;++j) v_dir[j]=v_mean[j]/vn;

                std::vector<float> raw(n);
                for (size_t j=0;j<n;++j) raw[j]=nd(rng_r)*n_amt*cfg.restart_s_noise;
                // Batch-safe projection: sum across spatial dims (no batch dim in C++)
                float proj = detail::vec_dot(raw.data(), v_dir.data(), n);
                for (size_t j=0;j<n;++j) noise[j] = raw[j] - proj*v_dir[j];
                float rn=detail::vec_norm(raw.data(),n), an=detail::vec_norm(noise.data(),n)+1e-8f;
                for (size_t j=0;j<n;++j) noise[j] *= rn/an;
                if (cfg.verbose) std::printf("[STORM] ♻️  ALIGNED RESTART @ step %d (noise ⊥ v_principal)\n", i);
            } else {
                for (size_t j=0;j<n;++j) noise[j]=nd(rng_r)*n_amt*cfg.restart_s_noise;
                if (cfg.verbose) std::printf("[STORM] ♻️  RESTART @ step %d\n", i);
            }

            std::vector<float> x_renoise(n), v_res(n);
            for (size_t j=0;j<n;++j) x_renoise[j]=x[j]+noise[j];
            model_fn(x_renoise.data(), s_res, v_res.data(), n, user_data);
            float dt_res=sigma_next-s_res;
            for (size_t j=0;j<n;++j) x[j]=x_renoise[j]+dt_res*v_res[j];

            if (cfg.restart_flush_cache) {
                v_cache.clear(); bl.prev_mode_dpm=false;
                if (cfg.verbose) std::printf("[STORM] Cache flushed after restart.\n");
            }
        }

        if (cfg.callback) cfg.callback(i, x, sigma_next, n);
    }
}

} // namespace storm
