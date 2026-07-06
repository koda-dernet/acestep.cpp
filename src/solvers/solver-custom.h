#pragma once
// solver-custom.h: small experimental solvers for ACE artifact probing.
//
// These are intentionally conservative:
// - midpoint / ralston add classic 2nd-order RK variants besides Heun.
// - dopri5_late_euler / gl2s_late_euler keep the higher-order solver early,
//   then fade toward Euler in late low-t detail steps.

#include "solver-interface.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

static inline float solver_custom_clamp01(float x) {
    return (std::max)(0.0f, (std::min)(1.0f, x));
}

static inline float solver_custom_smoothstep(float x) {
    x = solver_custom_clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}

static void solver_midpoint_step(float *       xt,
                                 const float * vt,
                                 float         t_curr,
                                 float         t_prev,
                                 int           n,
                                 SolverState & state,
                                 SolverModelFn model_fn,
                                 float *       vt_buf) {
    float dt = t_curr - t_prev;
    if (!model_fn) {
        for (int i = 0; i < n; i++) xt[i] -= vt[i] * dt;
        return;
    }

    if ((int) state.xt_scratch.size() < n) state.xt_scratch.resize(n);
    float * x_mid = state.xt_scratch.data();
    for (int i = 0; i < n; i++) {
        x_mid[i] = xt[i] - 0.5f * dt * vt[i];
    }
    model_fn(x_mid, t_curr - 0.5f * dt);
    for (int i = 0; i < n; i++) {
        xt[i] -= dt * vt_buf[i];
    }
}

static void solver_ralston_step(float *       xt,
                                const float * vt,
                                float         t_curr,
                                float         t_prev,
                                int           n,
                                SolverState & state,
                                SolverModelFn model_fn,
                                float *       vt_buf) {
    float dt = t_curr - t_prev;
    if (!model_fn) {
        for (int i = 0; i < n; i++) xt[i] -= vt[i] * dt;
        return;
    }

    if ((int) state.xt_scratch.size() < n) state.xt_scratch.resize(n);
    float * x_stage = state.xt_scratch.data();
    for (int i = 0; i < n; i++) {
        x_stage[i] = xt[i] - (2.0f / 3.0f) * dt * vt[i];
    }
    std::vector<float> k1(vt, vt + n);
    model_fn(x_stage, t_curr - (2.0f / 3.0f) * dt);
    for (int i = 0; i < n; i++) {
        float v_blend = 0.25f * k1[i] + 0.75f * vt_buf[i];
        xt[i] -= dt * v_blend;
    }
}

static void solver_blend_with_euler(float *       xt,
                                    const float * xt_original,
                                    const float * xt_hi,
                                    const float * vt,
                                    float         dt,
                                    float         t_curr,
                                    int           n) {
    // High-order early, Euler late. Most audible fizz/halo seems to live in
    // fine-detail low-t behavior, so avoid fully trusting high-order there.
    float hi_weight = solver_custom_smoothstep((t_curr - 0.18f) / (0.45f - 0.18f));
    for (int i = 0; i < n; i++) {
        float x_euler = xt_original[i] - dt * vt[i];
        xt[i] = (1.0f - hi_weight) * x_euler + hi_weight * xt_hi[i];
    }
}

static void solver_apply_capped_correction(float *       xt,
                                           const float * xt_original,
                                           const float * xt_hi,
                                           const float * vt,
                                           float         dt,
                                           float         t_curr,
                                           int           n,
                                           float         late_ratio) {
    // Keep the high-order direction, but cap how large its correction can be
    // relative to the plain Euler update near the final low-t detail region.
    double step_sq = 0.0;
    double corr_sq = 0.0;
    for (int i = 0; i < n; i++) {
        float x_euler = xt_original[i] - dt * vt[i];
        float step    = x_euler - xt_original[i];
        float corr    = xt_hi[i] - x_euler;
        step_sq += (double) step * (double) step;
        corr_sq += (double) corr * (double) corr;
    }

    float step_rms = std::sqrt((float) (step_sq / (double) n)) + 1.0e-8f;
    float corr_rms = std::sqrt((float) (corr_sq / (double) n)) + 1.0e-8f;
    float early    = solver_custom_smoothstep((t_curr - 0.14f) / (0.52f - 0.14f));
    float ratio    = late_ratio + (1.0f - late_ratio) * early;
    float scale    = (std::min)(1.0f, ratio * step_rms / corr_rms);

    for (int i = 0; i < n; i++) {
        float x_euler = xt_original[i] - dt * vt[i];
        xt[i] = x_euler + scale * (xt_hi[i] - x_euler);
    }
}

static void solver_dopri5_late_euler_step(float *       xt,
                                          const float * vt,
                                          float         t_curr,
                                          float         t_prev,
                                          int           n,
                                          SolverState & state,
                                          SolverModelFn model_fn,
                                          float *       vt_buf) {
    float dt = t_curr - t_prev;
    if (!model_fn) {
        for (int i = 0; i < n; i++) xt[i] -= vt[i] * dt;
        return;
    }

    std::vector<float> x0(xt, xt + n);
    std::vector<float> x_hi(xt, xt + n);
    std::vector<float> k1(vt, vt + n);
    solver_dopri5_step(x_hi.data(), k1.data(), t_curr, t_prev, n, state, model_fn, vt_buf);
    solver_blend_with_euler(xt, x0.data(), x_hi.data(), k1.data(), dt, t_curr, n);
}

static void solver_gl2s_late_euler_step(float *       xt,
                                        const float * vt,
                                        float         t_curr,
                                        float         t_prev,
                                        int           n,
                                        SolverState & state,
                                        SolverModelFn model_fn,
                                        float *       vt_buf) {
    float dt = t_curr - t_prev;
    if (!model_fn) {
        for (int i = 0; i < n; i++) xt[i] -= vt[i] * dt;
        return;
    }

    std::vector<float> x0(xt, xt + n);
    std::vector<float> x_hi(xt, xt + n);
    std::vector<float> k1(vt, vt + n);
    solver_gl2s_step(x_hi.data(), k1.data(), t_curr, t_prev, n, state, model_fn, vt_buf);
    solver_blend_with_euler(xt, x0.data(), x_hi.data(), k1.data(), dt, t_curr, n);
}

static void solver_dopri5_cap_step(float *       xt,
                                   const float * vt,
                                   float         t_curr,
                                   float         t_prev,
                                   int           n,
                                   SolverState & state,
                                   SolverModelFn model_fn,
                                   float *       vt_buf) {
    float dt = t_curr - t_prev;
    if (!model_fn) {
        for (int i = 0; i < n; i++) xt[i] -= vt[i] * dt;
        return;
    }

    std::vector<float> x0(xt, xt + n);
    std::vector<float> x_hi(xt, xt + n);
    std::vector<float> k1(vt, vt + n);
    solver_dopri5_step(x_hi.data(), k1.data(), t_curr, t_prev, n, state, model_fn, vt_buf);
    solver_apply_capped_correction(xt, x0.data(), x_hi.data(), k1.data(), dt, t_curr, n, 0.28f);
}

static void solver_gl2s_cap_step(float *       xt,
                                 const float * vt,
                                 float         t_curr,
                                 float         t_prev,
                                 int           n,
                                 SolverState & state,
                                 SolverModelFn model_fn,
                                 float *       vt_buf) {
    float dt = t_curr - t_prev;
    if (!model_fn) {
        for (int i = 0; i < n; i++) xt[i] -= vt[i] * dt;
        return;
    }

    std::vector<float> x0(xt, xt + n);
    std::vector<float> x_hi(xt, xt + n);
    std::vector<float> k1(vt, vt + n);
    solver_gl2s_step(x_hi.data(), k1.data(), t_curr, t_prev, n, state, model_fn, vt_buf);
    solver_apply_capped_correction(xt, x0.data(), x_hi.data(), k1.data(), dt, t_curr, n, 0.28f);
}
