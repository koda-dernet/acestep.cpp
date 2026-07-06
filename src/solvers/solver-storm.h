#pragma once
// solver-storm.h: adapter from acestep.cpp's per-step solver registry to the
// vendored STORM sampler core in vendor/storm_sampler/storm_sampler_core.hpp.

#include "solver-interface.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

static inline bool _storm_adapter_has_nan_inf(const float * data, int n) {
    for (int i = 0; i < n; i++) {
        if (!std::isfinite(data[i])) return true;
    }
    return false;
}

static inline int _storm_adapter_n_calib(const SolverState & state) {
    int n_steps = state.total_steps > 0 ? state.total_steps : 1;
    return (std::max)(2, (std::min)(5, (int) ((float) n_steps * state.storm_config.calib_frac)));
}

static inline void _storm_adapter_trim_cache(SolverState & state) {
    while ((int) state.storm.v_cache.size() > state.storm_config.cache_depth) {
        state.storm.v_cache.erase(state.storm.v_cache.begin());
    }
}

static inline void _storm_adapter_push_cache(SolverState & state, const float * v, int n, float sigma) {
    storm::CacheEntry entry;
    entry.v.assign(v, v + n);
    entry.sigma = sigma;
    state.storm.v_cache.push_back(std::move(entry));
    _storm_adapter_trim_cache(state);
}

static void _storm_adapter_init_lookback(SolverState & state, const float * x, int n) {
    state.storm.x_prev_lb.resize(n);
    std::mt19937                          rng(42);
    std::normal_distribution<float>       nd(0.0f, state.storm.sigma_max * 0.1f);
    for (int i = 0; i < n; i++) {
        state.storm.x_prev_lb[i] = x[i] + nd(rng);
    }
    state.storm.lb_initialized = true;
}

static bool _storm_adapter_is_restart_step(const SolverState & state) {
    return state.storm_config.restart_steps.find(state.step_index) != state.storm_config.restart_steps.end();
}

static void _storm_adapter_apply_restart(float *              xt,
                                         int                  n,
                                         float                sigma_curr,
                                         float                sigma_next,
                                         SolverState &        state,
                                         const storm::ModelFn & model_fn) {
    if (!state.storm_config.enable_restarts || !_storm_adapter_is_restart_step(state) || sigma_next <= 0.0f) return;

    float s_res = sigma_next + (sigma_curr - sigma_next) * state.storm_config.restart_noise_scale;
    float n_amt = std::sqrt((std::max)(0.0f, s_res * s_res - sigma_next * sigma_next) + 1e-8f);

    std::vector<float> noise(n);
    std::mt19937       rng_r((unsigned) (state.storm_config.restart_seed + state.step_index * 1000));
    std::normal_distribution<float> nd(0.0f, 1.0f);

    if (state.storm_config.restart_aligned_noise && state.storm.v_cache.size() >= 2) {
        std::vector<float> v_mean(n, 0.0f);
        float              wsum = 0.0f;
        int                depth = (int) state.storm.v_cache.size();
        for (int i = 0; i < depth; i++) {
            float w = std::pow(0.5f, (float) (depth - 1 - i));
            wsum += w;
            for (int j = 0; j < n; j++) {
                v_mean[j] += state.storm.v_cache[i].v[j] * w;
            }
        }
        for (int j = 0; j < n; j++) v_mean[j] /= wsum;

        float              v_norm = storm::detail::vec_norm(v_mean.data(), (size_t) n) + 1e-8f;
        std::vector<float> v_dir(n);
        for (int j = 0; j < n; j++) v_dir[j] = v_mean[j] / v_norm;

        std::vector<float> raw(n);
        for (int j = 0; j < n; j++) raw[j] = nd(rng_r) * n_amt * state.storm_config.restart_s_noise;

        float proj = storm::detail::vec_dot(raw.data(), v_dir.data(), (size_t) n);
        for (int j = 0; j < n; j++) noise[j] = raw[j] - proj * v_dir[j];

        float rn = storm::detail::vec_norm(raw.data(), (size_t) n);
        float an = storm::detail::vec_norm(noise.data(), (size_t) n) + 1e-8f;
        for (int j = 0; j < n; j++) noise[j] *= rn / an;
        if (state.storm_config.verbose) {
            fprintf(stderr, "[STORM] ALIGNED RESTART @ step %d (noise perpendicular to v_principal)\n",
                    state.step_index);
        }
    } else {
        for (int j = 0; j < n; j++) noise[j] = nd(rng_r) * n_amt * state.storm_config.restart_s_noise;
        if (state.storm_config.verbose) {
            fprintf(stderr, "[STORM] RESTART @ step %d\n", state.step_index);
        }
    }

    std::vector<float> x_renoise(n), v_res(n);
    for (int j = 0; j < n; j++) x_renoise[j] = xt[j] + noise[j];
    model_fn(x_renoise.data(), s_res, v_res.data(), (size_t) n, nullptr);

    float dt_res = sigma_next - s_res;
    for (int j = 0; j < n; j++) xt[j] = x_renoise[j] + dt_res * v_res[j];

    if (state.storm_config.restart_flush_cache) {
        state.storm.v_cache.clear();
        state.storm.baseline.prev_mode_dpm = false;
        if (state.storm_config.verbose) {
            fprintf(stderr, "[STORM] Cache flushed after restart.\n");
        }
    }
    state.injected_noise_this_step = true;
}

static void solver_storm_step(float *       xt,
                              const float * vt,
                              float         t_curr,
                              float         t_prev,
                              int           n,
                              SolverState & state,
                              SolverModelFn model_fn,
                              float *       vt_buf) {
    if (!state.storm.initialized) {
        state.storm.initialized = true;
        state.storm.sigma_max   = t_curr;
        if (state.storm_config.look_back_enabled) {
            _storm_adapter_init_lookback(state, xt, n);
        }
    }

    std::vector<float> x_before(xt, xt + n);
    std::vector<float> x_next(n);
    std::vector<float> v_curr(vt, vt + n);
    std::vector<float> v_step(n);
    int                actual_order = 1;
    const char *       mode = "STORK";
    float              cos_sim = 0.0f;
    bool               have_cos = false;

    storm::ModelFn model_cached = [&](const float * x_in, float sigma, float * v_out, size_t n_in, void *) {
        if (std::fabs(sigma - t_curr) < 1e-7f) {
            std::memcpy(v_out, v_curr.data(), n_in * sizeof(float));
            return;
        }
        if (model_fn && vt_buf) {
            model_fn(x_in, sigma);
            std::memcpy(v_out, vt_buf, n_in * sizeof(float));
            return;
        }
        std::fill(v_out, v_out + n_in, 0.0f);
    };

    if (state.storm_config.force_pure_euler) {
        float dt = t_prev - t_curr;
        for (int i = 0; i < n; i++) x_next[i] = xt[i] + dt * v_curr[i];
        v_step = v_curr;
        mode = "EULER";
        state.storm.baseline.prev_mode_dpm = false;
    } else {
        bool stiff = true;
        if (!state.storm.v_cache.empty()) {
            stiff = storm::compute_stiffness(v_curr.data(), state.storm.v_cache, state.step_index,
                                             state.storm.baseline, state.storm_config.stiffness_threshold,
                                             state.storm_config.ema_alpha, _storm_adapter_n_calib(state),
                                             (size_t) n, &cos_sim);
            have_cos = true;
        }

        if (state.storm.baseline.prev_mode_dpm && !stiff) {
            if (state.storm.baseline.last_ratio >
                state.storm.baseline.last_threshold + state.storm_config.hysteresis_margin) {
                stiff = true;
            }
        }

        if (stiff) {
            if (state.storm_config.adaptive_sub_step && !state.storm.v_cache.empty() && model_fn && vt_buf) {
                std::vector<float> x_work(xt, xt + n);
                actual_order = storm::sub_step_stork(state.storm.v_cache, x_work.data(), t_curr, t_prev,
                                                     model_cached, nullptr, v_step.data(), state.storm_config,
                                                     (size_t) n, 0);
                x_next = std::move(x_work);
                _storm_adapter_trim_cache(state);
            } else {
                actual_order = storm::stork_step(state.storm.v_cache, xt, t_curr, t_prev, model_cached, nullptr,
                                                 x_next.data(), v_step.data(), state.storm_config.rk_order,
                                                 (size_t) n);
            }
            mode = "STORK";
        } else {
            storm::dpmpp3m_step(state.storm.v_cache, xt, t_curr, t_prev, model_cached, nullptr, x_next.data(),
                                v_step.data(), (size_t) n);
            actual_order = 3;
            mode = "DPM++";
        }

        if (_storm_adapter_has_nan_inf(x_next.data(), n)) {
            fprintf(stderr, "[STORM] NaN/Inf at step %d. Flushing cache.\n", state.step_index);
            float dt = t_prev - t_curr;
            for (int i = 0; i < n; i++) x_next[i] = xt[i] + dt * v_curr[i];
            v_step = v_curr;
            state.storm.v_cache.clear();
            state.storm.baseline.prev_mode_dpm = false;
            actual_order = 1;
            mode = "EULER";
        }

        state.storm.baseline.prev_mode_dpm = (std::strcmp(mode, "DPM++") == 0);
    }

    _storm_adapter_push_cache(state, v_step.data(), n, t_curr);

    float lb_lam = 0.0f;
    if (state.storm_config.look_back_enabled && state.storm.lb_initialized && !state.storm.x_prev_lb.empty()) {
        lb_lam = storm::look_back_smooth(x_next.data(), state.storm.x_prev_lb.data(), t_curr,
                                         state.storm.sigma_max, state.storm_config.look_back_lambda,
                                         state.storm_config.look_back_snr_power, (size_t) n);
        state.storm.x_prev_lb = std::move(x_before);
    }

    std::memcpy(xt, x_next.data(), n * sizeof(float));

    _storm_adapter_apply_restart(xt, n, t_curr, t_prev, state, model_cached);

    if (state.storm_config.verbose) {
        if (have_cos) {
            fprintf(stderr,
                    "[STORM] Step %02d: %-5s RK%d | Ratio: %.3f | Threshold: %.3f | cos_sim: %.4f | LB: %.4f\n",
                    state.step_index, mode, actual_order, state.storm.baseline.last_ratio,
                    state.storm.baseline.last_threshold, cos_sim, lb_lam);
        } else {
            fprintf(stderr, "[STORM] Step %02d: %-5s RK%d | LB: %.4f\n",
                    state.step_index, mode, actual_order, lb_lam);
        }
    }
}
