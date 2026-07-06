#include "request.h"
#include "task-types.h"

#include <cassert>
#include <cmath>
#include <string>

static void assert_close(float actual, float expected) {
    assert(std::fabs(actual - expected) < 1e-6f);
}

int main() {
    AceRequest r;

    assert(request_parse_json(&r, "{\"caption\":\"x\",\"solver\":\"storm\"}"));
    assert(r.solver == SOLVER_STORM);
    assert(r.stork_substeps == STORK_SUBSTEPS_DEFAULT);
    assert_close(r.storm_stiffness_threshold, 0.15f);
    assert_close(r.storm_hysteresis_margin, 0.05f);
    assert_close(r.storm_ema_alpha, 0.30f);
    assert(r.storm_cache_depth == 5);
    assert(r.storm_rk_order == "auto");
    assert_close(r.storm_calib_frac, 0.12f);
    assert(r.storm_adaptive_sub_step);
    assert_close(r.storm_sub_step_threshold, 0.0f);
    assert(r.storm_sub_step_max_depth == 2);
    assert(r.storm_look_back_enabled);
    assert_close(r.storm_look_back_lambda, 0.35f);
    assert_close(r.storm_look_back_snr_power, 1.5f);
    assert(!r.storm_enable_restarts);
    assert(r.storm_restart_steps.empty());
    assert_close(r.storm_restart_noise_scale, 0.5f);
    assert_close(r.storm_restart_s_noise, 1.0f);
    assert(r.storm_restart_seed == 42);
    assert(r.storm_restart_flush_cache);
    assert(r.storm_restart_aligned_noise);
    assert(!r.storm_force_pure_euler);
    assert(!r.storm_verbose);

    std::string json = request_to_json(&r, true);
    assert(json.find("\"solver\": \"storm\"") != std::string::npos);

    assert(request_parse_json(&r, "{\"caption\":\"x\",\"infer_method\":\"ode\"}"));
    assert(r.solver == SOLVER_EULER);

    assert(request_parse_json(&r, "{\"caption\":\"x\",\"infer_method\":\"sde\"}"));
    assert(r.solver == SOLVER_SDE);

    assert(request_parse_json(&r, "{\"caption\":\"x\",\"infer_method\":\"rk4\"}"));
    assert(r.solver == "rk4");

    assert(request_parse_json(
        &r,
        "{\"caption\":\"x\",\"solver\":\"storm\",\"infer_method\":\"sde\",\"storm_rk_order\":\"4\","
        "\"storm_restart_steps\":\"10,20\",\"storm_enable_restarts\":true}"));
    assert(r.solver == SOLVER_STORM);
    assert(r.infer_method == INFER_SDE);
    assert(r.storm_rk_order == "4");
    assert(r.storm_restart_steps == "10,20");
    assert(r.storm_enable_restarts);

    return 0;
}
