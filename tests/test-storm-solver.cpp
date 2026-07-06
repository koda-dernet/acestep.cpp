#include "solvers/solver-registry.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

static void assert_close(float actual, float expected, float eps = 1e-6f) {
    assert(std::fabs(actual - expected) <= eps);
}

static void synthetic_velocity(const float * x, float sigma, float * vt, int n) {
    for (int i = 0; i < n; i++) {
        vt[i] = 0.25f * x[i] + sigma * (0.1f + 0.01f * (float) i);
    }
}

static void parity_velocity(const float * x, float sigma, float * vt, size_t n, void *) {
    for (size_t i = 0; i < n; i++) {
        float idx = (float) i + 1.0f;
        vt[i] = 0.18f * x[i] + 0.03f * std::sin(sigma * idx) + 0.002f * x[i] * x[i];
    }
}

static std::vector<float> run_adapter_like_outer_loop(const std::vector<float> & x0,
                                                      const std::vector<float> & sigmas,
                                                      const storm::Config &      cfg,
                                                      bool *                     restart_flag) {
    int                n = (int) x0.size();
    int                n_steps = (int) sigmas.size() - 1;
    std::vector<float> x = x0;
    std::vector<float> vt(n);
    std::vector<float> vt_buf(n);
    SolverState        state;
    state.total_steps = n_steps;
    state.storm_config = cfg;

    bool saw_restart = false;
    SolverModelFn model_fn = [&](const float * x_in, float sigma) {
        parity_velocity(x_in, sigma, vt_buf.data(), (size_t) n, nullptr);
    };

    for (int step = 0; step < n_steps; step++) {
        parity_velocity(x.data(), sigmas[step], vt.data(), (size_t) n, nullptr);
        if (sigmas[step + 1] == 0.0f) {
            float dt = sigmas[step + 1] - sigmas[step];
            for (int i = 0; i < n; i++) x[i] += dt * vt[i];
            break;
        }
        state.step_index = step;
        state.injected_noise_this_step = false;
        solver_storm_step(x.data(), vt.data(), sigmas[step], sigmas[step + 1], n, state, model_fn, vt_buf.data());
        saw_restart = saw_restart || state.injected_noise_this_step;
    }

    if (restart_flag) *restart_flag = saw_restart;
    return x;
}

static std::vector<float> run_vendored_loop(const std::vector<float> & x0,
                                            const std::vector<float> & sigmas,
                                            const storm::Config &      cfg) {
    std::vector<float> x = x0;
    storm::ModelFn model_fn = [](const float * x_in, float sigma, float * v_out, size_t n, void * user_data) {
        parity_velocity(x_in, sigma, v_out, n, user_data);
    };
    storm::run(model_fn, x.data(), sigmas.data(), x.size(), (int) sigmas.size() - 1, nullptr, cfg);
    return x;
}

static void assert_vector_close(const std::vector<float> & actual,
                                const std::vector<float> & expected,
                                float                      eps = 3e-5f) {
    assert(actual.size() == expected.size());
    for (size_t i = 0; i < actual.size(); i++) {
        assert_close(actual[i], expected[i], eps);
    }
}

static std::vector<float> run_restart_case(bool * restart_flag) {
    const int n = 8;
    const int64_t seeds[1] = { 1234 };
    const float schedule[] = { 1.0f, 0.7f, 0.4f, 0.1f };

    SolverState state;
    state.total_steps = 3;
    state.seeds = seeds;
    state.batch_n = 1;
    state.n_per = n;
    state.storm_config.enable_restarts = true;
    state.storm_config.restart_steps = { 1 };
    state.storm_config.restart_seed = 42;
    state.storm_config.restart_flush_cache = true;

    std::vector<float> x = { 0.3f, -0.2f, 0.5f, -0.4f, 0.9f, -0.7f, 0.1f, 0.2f };
    std::vector<float> vt(n);
    std::vector<float> vt_buf(n);
    bool               saw_restart = false;

    SolverModelFn model_fn = [&](const float * x_in, float sigma) {
        synthetic_velocity(x_in, sigma, vt_buf.data(), n);
    };

    for (int step = 0; step < 3; step++) {
        synthetic_velocity(x.data(), schedule[step], vt.data(), n);
        state.step_index = step;
        state.injected_noise_this_step = false;
        solver_storm_step(x.data(), vt.data(), schedule[step], schedule[step + 1], n, state, model_fn, vt_buf.data());
        if (step == 1 && state.injected_noise_this_step) {
            saw_restart = true;
        }
        for (float v : x) {
            assert(std::isfinite(v));
        }
    }

    *restart_flag = saw_restart;
    return x;
}

int main() {
    assert(solver_lookup("storm") != nullptr);
    assert(solver_lookup("storm:rk=4") == nullptr);

    {
        const int n = 4;
        float     x[n] = { 1.0f, -2.0f, 0.5f, 4.0f };
        float     vt[n] = { 0.25f, -0.5f, 1.0f, -2.0f };

        SolverState state;
        state.total_steps = 1;
        state.storm_config.force_pure_euler = true;
        state.storm_config.look_back_enabled = false;

        solver_storm_step(x, vt, 1.0f, 0.5f, n, state, {}, nullptr);

        assert_close(x[0], 0.875f);
        assert_close(x[1], -1.75f);
        assert_close(x[2], 0.0f);
        assert_close(x[3], 5.0f);
        assert(state.storm.v_cache.size() == 1);
        assert(!state.injected_noise_this_step);
    }

    bool restart_a = false;
    bool restart_b = false;
    std::vector<float> a = run_restart_case(&restart_a);
    std::vector<float> b = run_restart_case(&restart_b);
    assert(restart_a);
    assert(restart_b);
    assert(a.size() == b.size());
    for (size_t i = 0; i < a.size(); i++) {
        assert_close(a[i], b[i], 1e-6f);
    }

    {
        std::vector<float> x0 = { 0.12f, -0.34f, 0.56f, -0.78f, 0.91f, -0.22f, 0.43f, -0.65f };
        std::vector<float> sigmas = { 1.0f, 0.82f, 0.63f, 0.41f, 0.19f, 0.0f };
        storm::Config      cfg;
        std::vector<float> vendored = run_vendored_loop(x0, sigmas, cfg);
        std::vector<float> adapter = run_adapter_like_outer_loop(x0, sigmas, cfg, nullptr);
        assert_vector_close(adapter, vendored);
    }

    {
        std::vector<float> x0 = { 0.21f, -0.13f, 0.37f, -0.49f, 0.53f, -0.61f, 0.72f, -0.84f };
        std::vector<float> sigmas = { 1.0f, 0.78f, 0.52f, 0.31f, 0.12f, 0.0f };
        storm::Config      cfg;
        cfg.enable_restarts = true;
        cfg.restart_steps = { 2 };
        cfg.restart_seed = 42;
        cfg.restart_flush_cache = true;
        bool saw_restart = false;
        std::vector<float> vendored = run_vendored_loop(x0, sigmas, cfg);
        std::vector<float> adapter = run_adapter_like_outer_loop(x0, sigmas, cfg, &saw_restart);
        assert(saw_restart);
        assert_vector_close(adapter, vendored);
    }

    return 0;
}
