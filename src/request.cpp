// request.cpp: AceStep request JSON read/write (yyjson)

#include "request.h"

#include "task-types.h"
#include "yyjson.h"

#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static const yyjson_write_flag WRITE_FLAGS =
    YYJSON_WRITE_PRETTY | YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_FP_TO_FIXED(2);

// Defaults (aligned with Python GenerationParams)
void request_init(AceRequest * r) {
    r->caption = "";
    r->lyrics  = "";

    r->bpm                  = 0;
    r->duration             = 0.0f;
    r->keyscale             = "";
    r->timesignature        = "";
    r->vocal_language       = "";
    r->lm_batch_size        = 1;
    r->synth_batch_size     = 1;
    r->seed                 = -1;
    r->lm_temperature       = 0.85f;
    r->lm_cfg_scale         = 2.0f;
    r->lm_top_p             = 0.9f;
    r->lm_top_k             = 0;
    r->lm_negative_prompt   = "";
    r->lm_seed              = -1;
    r->use_cot_caption      = true;
    r->audio_codes          = "";
    r->inference_steps      = 0;     // 0 = auto (turbo: 8, base/sft: 50)
    r->guidance_scale       = 0.0f;  // 0 = auto (1.0 for all models)
    r->shift                = 0.0f;  // 0 = auto (turbo: 3.0, base/sft: 1.0)
    r->dcw_scaler           = 0.0f;
    r->dcw_high_scaler      = 0.0f;
    r->dcw_mode             = DCW_MODE_LOW;
    r->audio_cover_strength = 1.0f;
    r->cover_noise_strength = 0.0f;
    r->repainting_start     = 0.0f;
    r->repainting_end       = -1.0f;
    r->latent_shift         = 0.0f;
    r->latent_rescale       = 1.0f;
    r->pp_vae_reencode      = false;
    r->custom_timesteps     = "";
    r->schedule_method      = SCHEDULE_LINEAR;
    r->solver               = SOLVER_EULER;
    r->stork_substeps       = STORK_SUBSTEPS_DEFAULT;
    r->storm_stiffness_threshold = 0.15f;
    r->storm_hysteresis_margin   = 0.05f;
    r->storm_ema_alpha           = 0.30f;
    r->storm_cache_depth         = 5;
    r->storm_rk_order            = "auto";
    r->storm_calib_frac          = 0.12f;
    r->storm_adaptive_sub_step   = true;
    r->storm_sub_step_threshold  = 0.0f;
    r->storm_sub_step_max_depth  = 2;
    r->storm_look_back_enabled   = true;
    r->storm_look_back_lambda    = 0.35f;
    r->storm_look_back_snr_power = 1.5f;
    r->storm_enable_restarts     = false;
    r->storm_restart_steps       = "";
    r->storm_restart_noise_scale = 0.5f;
    r->storm_restart_s_noise     = 1.0f;
    r->storm_restart_seed        = 42;
    r->storm_restart_flush_cache = true;
    r->storm_restart_aligned_noise = true;
    r->storm_force_pure_euler    = false;
    r->storm_verbose             = false;
    r->task_type            = TASK_TEXT2MUSIC;
    r->track                = "";
    r->infer_method         = INFER_ODE;
    r->lm_mode              = LM_MODE_NAME_GENERATE;
    r->output_format        = OUTPUT_FORMAT_MP3;
    r->synth_model          = "";
    r->lm_model             = "";
    r->adapter              = "";
    r->adapter_scale        = 1.0f;
    r->adapter_scale_self   = 1.0f;
    r->adapter_scale_cross  = 1.0f;
    r->adapter_scale_mlp    = 1.0f;
    r->vae                  = "";
    r->peak_clip            = 10;
    r->mp3_bitrate          = 128;
}

// helper: get yyjson string as std::string
static inline std::string yy_str(yyjson_val * v) {
    return std::string(yyjson_get_str(v), yyjson_get_len(v));
}

static inline bool yy_bool_value(yyjson_val * v, bool def) {
    if (yyjson_is_bool(v)) {
        return yyjson_get_bool(v);
    }
    if (yyjson_is_str(v)) {
        const char * s = yyjson_get_str(v);
        if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0 || strcmp(s, "yes") == 0 || strcmp(s, "on") == 0) {
            return true;
        }
        if (strcmp(s, "false") == 0 || strcmp(s, "0") == 0 || strcmp(s, "no") == 0 || strcmp(s, "off") == 0) {
            return false;
        }
    }
    return def;
}

// populate AceRequest fields from a yyjson object (must be pre-initialized)
static void request_parse_obj(yyjson_val * obj, AceRequest * r) {
    yyjson_val * v;
    bool saw_solver = false;
    bool saw_infer_method = false;

    // strings
    if ((v = yyjson_obj_get(obj, "caption")) && yyjson_is_str(v)) {
        r->caption = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "lyrics")) && yyjson_is_str(v)) {
        r->lyrics = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "keyscale")) && yyjson_is_str(v)) {
        r->keyscale = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "timesignature")) && yyjson_is_str(v)) {
        r->timesignature = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "vocal_language")) && yyjson_is_str(v)) {
        r->vocal_language = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "audio_codes")) && yyjson_is_str(v)) {
        r->audio_codes = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "lm_negative_prompt")) && yyjson_is_str(v)) {
        r->lm_negative_prompt = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "task_type")) && yyjson_is_str(v) && yyjson_get_len(v) > 0) {
        r->task_type = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "track")) && yyjson_is_str(v)) {
        r->track = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "solver")) && yyjson_is_str(v) && yyjson_get_len(v) > 0) {
        r->solver = yy_str(v);
        saw_solver = true;
    }
    if ((v = yyjson_obj_get(obj, "infer_method")) && yyjson_is_str(v) && yyjson_get_len(v) > 0) {
        r->infer_method = yy_str(v);
        saw_infer_method = true;
    }
    if ((v = yyjson_obj_get(obj, "custom_timesteps")) && yyjson_is_str(v)) {
        r->custom_timesteps = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "schedule_method")) && yyjson_is_str(v)) {
        r->schedule_method = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_rk_order")) && yyjson_is_str(v) && yyjson_get_len(v) > 0) {
        r->storm_rk_order = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_restart_steps")) && yyjson_is_str(v)) {
        r->storm_restart_steps = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "lm_mode")) && yyjson_is_str(v) && yyjson_get_len(v) > 0) {
        r->lm_mode = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "output_format")) && yyjson_is_str(v) && yyjson_get_len(v) > 0) {
        r->output_format = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "synth_model")) && yyjson_is_str(v)) {
        r->synth_model = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "lm_model")) && yyjson_is_str(v)) {
        r->lm_model = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "adapter")) && yyjson_is_str(v)) {
        r->adapter = yy_str(v);
    }
    if ((v = yyjson_obj_get(obj, "vae")) && yyjson_is_str(v)) {
        r->vae = yy_str(v);
    }

    // ints
    if ((v = yyjson_obj_get(obj, "bpm")) && yyjson_is_num(v)) {
        r->bpm = (int) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "lm_batch_size")) && yyjson_is_num(v)) {
        r->lm_batch_size = (int) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "synth_batch_size")) && yyjson_is_num(v)) {
        r->synth_batch_size = (int) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "seed")) && yyjson_is_num(v)) {
        r->seed = (int64_t) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "lm_seed")) && yyjson_is_num(v)) {
        r->lm_seed = (int64_t) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "lm_top_k")) && yyjson_is_num(v)) {
        r->lm_top_k = (int) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "inference_steps")) && yyjson_is_num(v)) {
        r->inference_steps = (int) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "stork_substeps")) && yyjson_is_num(v)) {
        r->stork_substeps = (int) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_cache_depth")) && yyjson_is_num(v)) {
        r->storm_cache_depth = (int) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_sub_step_max_depth")) && yyjson_is_num(v)) {
        r->storm_sub_step_max_depth = (int) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_restart_seed")) && yyjson_is_num(v)) {
        r->storm_restart_seed = (int64_t) yyjson_get_num(v);
    }

    // floats
    if ((v = yyjson_obj_get(obj, "duration")) && yyjson_is_num(v)) {
        r->duration = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "lm_temperature")) && yyjson_is_num(v)) {
        r->lm_temperature = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "lm_cfg_scale")) && yyjson_is_num(v)) {
        r->lm_cfg_scale = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "lm_top_p")) && yyjson_is_num(v)) {
        r->lm_top_p = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "guidance_scale")) && yyjson_is_num(v)) {
        r->guidance_scale = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "shift")) && yyjson_is_num(v)) {
        r->shift = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "dcw_scaler")) && yyjson_is_num(v)) {
        r->dcw_scaler = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "dcw_high_scaler")) && yyjson_is_num(v)) {
        r->dcw_high_scaler = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "dcw_mode")) && yyjson_is_str(v)) {
        r->dcw_mode = yyjson_get_str(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_stiffness_threshold")) && yyjson_is_num(v)) {
        r->storm_stiffness_threshold = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_hysteresis_margin")) && yyjson_is_num(v)) {
        r->storm_hysteresis_margin = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_ema_alpha")) && yyjson_is_num(v)) {
        r->storm_ema_alpha = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_calib_frac")) && yyjson_is_num(v)) {
        r->storm_calib_frac = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_sub_step_threshold")) && yyjson_is_num(v)) {
        r->storm_sub_step_threshold = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_look_back_lambda")) && yyjson_is_num(v)) {
        r->storm_look_back_lambda = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_look_back_snr_power")) && yyjson_is_num(v)) {
        r->storm_look_back_snr_power = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_restart_noise_scale")) && yyjson_is_num(v)) {
        r->storm_restart_noise_scale = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "storm_restart_s_noise")) && yyjson_is_num(v)) {
        r->storm_restart_s_noise = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "audio_cover_strength")) && yyjson_is_num(v)) {
        r->audio_cover_strength = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "cover_noise_strength")) && yyjson_is_num(v)) {
        r->cover_noise_strength = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "repainting_start")) && yyjson_is_num(v)) {
        r->repainting_start = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "repainting_end")) && yyjson_is_num(v)) {
        r->repainting_end = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "latent_shift")) && yyjson_is_num(v)) {
        r->latent_shift = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "latent_rescale")) && yyjson_is_num(v)) {
        r->latent_rescale = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "peak_clip")) && yyjson_is_num(v)) {
        r->peak_clip = (int) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "mp3_bitrate")) && yyjson_is_num(v)) {
        r->mp3_bitrate = (int) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "adapter_scale")) && yyjson_is_num(v)) {
        r->adapter_scale = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "adapter_scale_self")) && yyjson_is_num(v)) {
        r->adapter_scale_self = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "adapter_scale_cross")) && yyjson_is_num(v)) {
        r->adapter_scale_cross = (float) yyjson_get_num(v);
    }
    if ((v = yyjson_obj_get(obj, "adapter_scale_mlp")) && yyjson_is_num(v)) {
        r->adapter_scale_mlp = (float) yyjson_get_num(v);
    }

    // bool
    if ((v = yyjson_obj_get(obj, "use_cot_caption"))) {
        if (yyjson_is_bool(v)) {
            r->use_cot_caption = yyjson_get_bool(v);
        } else if (yyjson_is_str(v)) {
            const char * s     = yyjson_get_str(v);
            r->use_cot_caption = (strcmp(s, "true") == 0 || strcmp(s, "1") == 0);
        }
    }
    if ((v = yyjson_obj_get(obj, "pp_vae_reencode"))) {
        if (yyjson_is_bool(v)) {
            r->pp_vae_reencode = yyjson_get_bool(v);
        } else if (yyjson_is_str(v)) {
            const char * s    = yyjson_get_str(v);
            r->pp_vae_reencode = (strcmp(s, "true") == 0 || strcmp(s, "1") == 0);
        }
    }
    if ((v = yyjson_obj_get(obj, "storm_adaptive_sub_step"))) {
        r->storm_adaptive_sub_step = yy_bool_value(v, r->storm_adaptive_sub_step);
    }
    if ((v = yyjson_obj_get(obj, "storm_look_back_enabled"))) {
        r->storm_look_back_enabled = yy_bool_value(v, r->storm_look_back_enabled);
    }
    if ((v = yyjson_obj_get(obj, "storm_enable_restarts"))) {
        r->storm_enable_restarts = yy_bool_value(v, r->storm_enable_restarts);
    }
    if ((v = yyjson_obj_get(obj, "storm_restart_flush_cache"))) {
        r->storm_restart_flush_cache = yy_bool_value(v, r->storm_restart_flush_cache);
    }
    if ((v = yyjson_obj_get(obj, "storm_restart_aligned_noise"))) {
        r->storm_restart_aligned_noise = yy_bool_value(v, r->storm_restart_aligned_noise);
    }
    if ((v = yyjson_obj_get(obj, "storm_force_pure_euler"))) {
        r->storm_force_pure_euler = yy_bool_value(v, r->storm_force_pure_euler);
    }
    if ((v = yyjson_obj_get(obj, "storm_verbose"))) {
        r->storm_verbose = yy_bool_value(v, r->storm_verbose);
    }

    if (!saw_solver && saw_infer_method) {
        r->solver = legacy_infer_to_solver(r->infer_method);
    }

    // Lyrics is the source of truth for instrumental mode.
    // The DiT was trained with lyrics="[Instrumental]" and language="unknown".
    // Force vocal_language to "unknown" to match the training distribution.
    if (r->lyrics == "[Instrumental]" && r->vocal_language != "unknown") {
        r->vocal_language = "unknown";
    }
}

// Core parser: takes a raw JSON string. Used by the server directly.
bool request_parse_json(AceRequest * r, const char * json) {
    request_init(r);

    yyjson_doc * doc = yyjson_read(json, strlen(json), 0);
    if (!doc) {
        return false;
    }

    yyjson_val * root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    request_parse_obj(root, r);
    yyjson_doc_free(doc);
    return true;
}

// Parse JSON: single object {} or array [{}, ...] into a vector.
bool request_parse_json_array(const char * json, std::vector<AceRequest> * out) {
    yyjson_doc * doc = yyjson_read(json, strlen(json), 0);
    if (!doc) {
        return false;
    }

    yyjson_val * root = yyjson_doc_get_root(doc);

    if (yyjson_is_obj(root)) {
        AceRequest r;
        request_init(&r);
        request_parse_obj(root, &r);
        out->push_back(r);
    } else if (yyjson_is_arr(root)) {
        size_t       idx, max;
        yyjson_val * val;
        yyjson_arr_foreach(root, idx, max, val) {
            if (!yyjson_is_obj(val)) {
                yyjson_doc_free(doc);
                return false;
            }
            AceRequest r;
            request_init(&r);
            request_parse_obj(val, &r);
            out->push_back(r);
        }
    } else {
        yyjson_doc_free(doc);
        return false;
    }

    yyjson_doc_free(doc);
    return !out->empty();
}

// File I/O
static std::string read_file(const char * path) {
    FILE * f = fopen(path, "rb");
    if (!f) {
        return "";
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string buf((size_t) sz, '\0');
    size_t      nr = fread(&buf[0], 1, (size_t) sz, f);
    fclose(f);
    if ((long) nr != sz) {
        buf.resize(nr);
    }
    return buf;
}

// File wrapper: reads JSON from disk, then delegates to request_parse_json.
bool request_parse(AceRequest * r, const char * path) {
    std::string json = read_file(path);
    if (json.empty()) {
        fprintf(stderr, "[Request] ERROR: cannot read %s\n", path);
        return false;
    }

    if (!request_parse_json(r, json.c_str())) {
        fprintf(stderr, "[Request] ERROR: malformed JSON in %s\n", path);
        return false;
    }

    fprintf(stderr, "[Request] Parsed %s\n", path);
    return true;
}

// build a yyjson mutable document from an AceRequest.
// sparse=true: omit fields at their request_init default.
// sparse=false: serialize everything (for /props documentation).
static yyjson_mut_doc * request_build_doc(const AceRequest * r, bool sparse) {
    yyjson_mut_doc * doc  = yyjson_mut_doc_new(NULL);
    yyjson_mut_val * root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    // single source of truth: compare against request_init defaults
    AceRequest def;
    request_init(&def);
    bool all = !sparse;

    // always present
    yyjson_mut_obj_add_str(doc, root, "caption", r->caption.c_str());
    yyjson_mut_obj_add_sint(doc, root, "seed", r->seed);

    // text
    if (all || r->lyrics != def.lyrics) {
        yyjson_mut_obj_add_str(doc, root, "lyrics", r->lyrics.c_str());
    }
    if (all || r->audio_codes != def.audio_codes) {
        yyjson_mut_obj_add_str(doc, root, "audio_codes", r->audio_codes.c_str());
    }

    // metadata
    if (all || r->bpm != def.bpm) {
        yyjson_mut_obj_add_int(doc, root, "bpm", r->bpm);
    }
    if (all || r->duration != def.duration) {
        yyjson_mut_obj_add_real(doc, root, "duration", r->duration);
    }
    if (all || r->keyscale != def.keyscale) {
        yyjson_mut_obj_add_str(doc, root, "keyscale", r->keyscale.c_str());
    }
    if (all || r->timesignature != def.timesignature) {
        yyjson_mut_obj_add_str(doc, root, "timesignature", r->timesignature.c_str());
    }
    if (all || r->vocal_language != def.vocal_language) {
        yyjson_mut_obj_add_str(doc, root, "vocal_language", r->vocal_language.c_str());
    }

    // LM control
    if (all || r->lm_batch_size != def.lm_batch_size) {
        yyjson_mut_obj_add_int(doc, root, "lm_batch_size", r->lm_batch_size);
    }
    if (all || r->lm_temperature != def.lm_temperature) {
        yyjson_mut_obj_add_real(doc, root, "lm_temperature", r->lm_temperature);
    }
    if (all || r->lm_cfg_scale != def.lm_cfg_scale) {
        yyjson_mut_obj_add_real(doc, root, "lm_cfg_scale", r->lm_cfg_scale);
    }
    if (all || r->lm_top_p != def.lm_top_p) {
        yyjson_mut_obj_add_real(doc, root, "lm_top_p", r->lm_top_p);
    }
    if (all || r->lm_top_k != def.lm_top_k) {
        yyjson_mut_obj_add_int(doc, root, "lm_top_k", r->lm_top_k);
    }
    if (all || r->lm_negative_prompt != def.lm_negative_prompt) {
        yyjson_mut_obj_add_str(doc, root, "lm_negative_prompt", r->lm_negative_prompt.c_str());
    }
    if (all || r->lm_seed != def.lm_seed) {
        yyjson_mut_obj_add_sint(doc, root, "lm_seed", r->lm_seed);
    }
    if (all || r->use_cot_caption != def.use_cot_caption) {
        yyjson_mut_obj_add_bool(doc, root, "use_cot_caption", r->use_cot_caption);
    }

    // DiT control (0 = auto-detect from model)
    if (all || r->inference_steps != def.inference_steps) {
        yyjson_mut_obj_add_int(doc, root, "inference_steps", r->inference_steps);
    }
    if (all || r->guidance_scale != def.guidance_scale) {
        yyjson_mut_obj_add_real(doc, root, "guidance_scale", r->guidance_scale);
    }
    if (all || r->shift != def.shift) {
        yyjson_mut_obj_add_real(doc, root, "shift", r->shift);
    }
    if (all || r->dcw_scaler != def.dcw_scaler) {
        yyjson_mut_obj_add_real(doc, root, "dcw_scaler", r->dcw_scaler);
    }
    if (all || r->dcw_high_scaler != def.dcw_high_scaler) {
        yyjson_mut_obj_add_real(doc, root, "dcw_high_scaler", r->dcw_high_scaler);
    }
    if (all || r->dcw_mode != def.dcw_mode) {
        yyjson_mut_obj_add_str(doc, root, "dcw_mode", r->dcw_mode.c_str());
    }
    // solver / schedule_method: always emitted (explicit round trip).
    yyjson_mut_obj_add_str(doc, root, "solver", r->solver.c_str());
    if (all || r->stork_substeps != def.stork_substeps) {
        yyjson_mut_obj_add_int(doc, root, "stork_substeps", r->stork_substeps);
    }
    if (all || r->storm_stiffness_threshold != def.storm_stiffness_threshold) {
        yyjson_mut_obj_add_real(doc, root, "storm_stiffness_threshold", r->storm_stiffness_threshold);
    }
    if (all || r->storm_hysteresis_margin != def.storm_hysteresis_margin) {
        yyjson_mut_obj_add_real(doc, root, "storm_hysteresis_margin", r->storm_hysteresis_margin);
    }
    if (all || r->storm_ema_alpha != def.storm_ema_alpha) {
        yyjson_mut_obj_add_real(doc, root, "storm_ema_alpha", r->storm_ema_alpha);
    }
    if (all || r->storm_cache_depth != def.storm_cache_depth) {
        yyjson_mut_obj_add_int(doc, root, "storm_cache_depth", r->storm_cache_depth);
    }
    if (all || r->storm_rk_order != def.storm_rk_order) {
        yyjson_mut_obj_add_str(doc, root, "storm_rk_order", r->storm_rk_order.c_str());
    }
    if (all || r->storm_calib_frac != def.storm_calib_frac) {
        yyjson_mut_obj_add_real(doc, root, "storm_calib_frac", r->storm_calib_frac);
    }
    if (all || r->storm_adaptive_sub_step != def.storm_adaptive_sub_step) {
        yyjson_mut_obj_add_bool(doc, root, "storm_adaptive_sub_step", r->storm_adaptive_sub_step);
    }
    if (all || r->storm_sub_step_threshold != def.storm_sub_step_threshold) {
        yyjson_mut_obj_add_real(doc, root, "storm_sub_step_threshold", r->storm_sub_step_threshold);
    }
    if (all || r->storm_sub_step_max_depth != def.storm_sub_step_max_depth) {
        yyjson_mut_obj_add_int(doc, root, "storm_sub_step_max_depth", r->storm_sub_step_max_depth);
    }
    if (all || r->storm_look_back_enabled != def.storm_look_back_enabled) {
        yyjson_mut_obj_add_bool(doc, root, "storm_look_back_enabled", r->storm_look_back_enabled);
    }
    if (all || r->storm_look_back_lambda != def.storm_look_back_lambda) {
        yyjson_mut_obj_add_real(doc, root, "storm_look_back_lambda", r->storm_look_back_lambda);
    }
    if (all || r->storm_look_back_snr_power != def.storm_look_back_snr_power) {
        yyjson_mut_obj_add_real(doc, root, "storm_look_back_snr_power", r->storm_look_back_snr_power);
    }
    if (all || r->storm_enable_restarts != def.storm_enable_restarts) {
        yyjson_mut_obj_add_bool(doc, root, "storm_enable_restarts", r->storm_enable_restarts);
    }
    if (all || r->storm_restart_steps != def.storm_restart_steps) {
        yyjson_mut_obj_add_str(doc, root, "storm_restart_steps", r->storm_restart_steps.c_str());
    }
    if (all || r->storm_restart_noise_scale != def.storm_restart_noise_scale) {
        yyjson_mut_obj_add_real(doc, root, "storm_restart_noise_scale", r->storm_restart_noise_scale);
    }
    if (all || r->storm_restart_s_noise != def.storm_restart_s_noise) {
        yyjson_mut_obj_add_real(doc, root, "storm_restart_s_noise", r->storm_restart_s_noise);
    }
    if (all || r->storm_restart_seed != def.storm_restart_seed) {
        yyjson_mut_obj_add_sint(doc, root, "storm_restart_seed", r->storm_restart_seed);
    }
    if (all || r->storm_restart_flush_cache != def.storm_restart_flush_cache) {
        yyjson_mut_obj_add_bool(doc, root, "storm_restart_flush_cache", r->storm_restart_flush_cache);
    }
    if (all || r->storm_restart_aligned_noise != def.storm_restart_aligned_noise) {
        yyjson_mut_obj_add_bool(doc, root, "storm_restart_aligned_noise", r->storm_restart_aligned_noise);
    }
    if (all || r->storm_force_pure_euler != def.storm_force_pure_euler) {
        yyjson_mut_obj_add_bool(doc, root, "storm_force_pure_euler", r->storm_force_pure_euler);
    }
    if (all || r->storm_verbose != def.storm_verbose) {
        yyjson_mut_obj_add_bool(doc, root, "storm_verbose", r->storm_verbose);
    }
    // infer_method remains an explicit legacy round-trip field.
    yyjson_mut_obj_add_str(doc, root, "infer_method", r->infer_method.c_str());
    yyjson_mut_obj_add_str(doc, root, "schedule_method", r->schedule_method.c_str());
    // lm_mode and output_format follow the same rule: enumerations with a
    // guaranteed non-empty value, always explicit in serialized output.
    yyjson_mut_obj_add_str(doc, root, "lm_mode", r->lm_mode.c_str());
    yyjson_mut_obj_add_str(doc, root, "output_format", r->output_format.c_str());

    // batch
    if (all || r->synth_batch_size != def.synth_batch_size) {
        yyjson_mut_obj_add_int(doc, root, "synth_batch_size", r->synth_batch_size);
    }

    // cover/repaint
    if (all || r->audio_cover_strength != def.audio_cover_strength) {
        yyjson_mut_obj_add_real(doc, root, "audio_cover_strength", r->audio_cover_strength);
    }
    if (all || r->cover_noise_strength != def.cover_noise_strength) {
        yyjson_mut_obj_add_real(doc, root, "cover_noise_strength", r->cover_noise_strength);
    }
    if (all || r->repainting_start != def.repainting_start) {
        yyjson_mut_obj_add_real(doc, root, "repainting_start", r->repainting_start);
    }
    if (all || r->repainting_end != def.repainting_end) {
        yyjson_mut_obj_add_real(doc, root, "repainting_end", r->repainting_end);
    }
    if (all || r->latent_shift != def.latent_shift) {
        yyjson_mut_obj_add_real(doc, root, "latent_shift", r->latent_shift);
    }
    if (all || r->latent_rescale != def.latent_rescale) {
        yyjson_mut_obj_add_real(doc, root, "latent_rescale", r->latent_rescale);
    }
    if (all || r->pp_vae_reencode != def.pp_vae_reencode) {
        yyjson_mut_obj_add_bool(doc, root, "pp_vae_reencode", r->pp_vae_reencode);
    }
    if (all || r->custom_timesteps != def.custom_timesteps) {
        yyjson_mut_obj_add_str(doc, root, "custom_timesteps", r->custom_timesteps.c_str());
    }
    // task_type is always emitted: it is the single source of truth for the
    // request and must be explicit in any round trip.
    yyjson_mut_obj_add_str(doc, root, "task_type", r->task_type.c_str());
    if (all || r->track != def.track) {
        yyjson_mut_obj_add_str(doc, root, "track", r->track.c_str());
    }
    if (all || r->peak_clip != def.peak_clip) {
        yyjson_mut_obj_add_int(doc, root, "peak_clip", r->peak_clip);
    }
    if (all || r->mp3_bitrate != def.mp3_bitrate) {
        yyjson_mut_obj_add_int(doc, root, "mp3_bitrate", r->mp3_bitrate);
    }
    if (all || r->synth_model != def.synth_model) {
        yyjson_mut_obj_add_str(doc, root, "synth_model", r->synth_model.c_str());
    }
    if (all || r->lm_model != def.lm_model) {
        yyjson_mut_obj_add_str(doc, root, "lm_model", r->lm_model.c_str());
    }
    if (all || r->adapter != def.adapter) {
        yyjson_mut_obj_add_str(doc, root, "adapter", r->adapter.c_str());
    }
    if (all || r->adapter_scale != def.adapter_scale) {
        yyjson_mut_obj_add_real(doc, root, "adapter_scale", r->adapter_scale);
    }
    if (all || r->adapter_scale_self != def.adapter_scale_self) {
        yyjson_mut_obj_add_real(doc, root, "adapter_scale_self", r->adapter_scale_self);
    }
    if (all || r->adapter_scale_cross != def.adapter_scale_cross) {
        yyjson_mut_obj_add_real(doc, root, "adapter_scale_cross", r->adapter_scale_cross);
    }
    if (all || r->adapter_scale_mlp != def.adapter_scale_mlp) {
        yyjson_mut_obj_add_real(doc, root, "adapter_scale_mlp", r->adapter_scale_mlp);
    }
    if (all || r->vae != def.vae) {
        yyjson_mut_obj_add_str(doc, root, "vae", r->vae.c_str());
    }

    return doc;
}

bool request_write(const AceRequest * r, const char * path) {
    FILE * f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[Request] ERROR: cannot write %s\n", path);
        return false;
    }

    yyjson_mut_doc * doc = request_build_doc(r, true);
    size_t           len;
    char *           json = yyjson_mut_write(doc, WRITE_FLAGS | YYJSON_WRITE_NEWLINE_AT_END, &len);
    yyjson_mut_doc_free(doc);

    fwrite(json, 1, len, f);
    free(json);
    fclose(f);

    fprintf(stderr, "[Request] Wrote %s\n", path);
    return true;
}

std::string request_to_json(const AceRequest * r, bool sparse) {
    yyjson_mut_doc * doc = request_build_doc(r, sparse);
    size_t           len;
    char *           json = yyjson_mut_write(doc, WRITE_FLAGS, &len);
    yyjson_mut_doc_free(doc);

    std::string result(json, len);
    free(json);
    return result;
}

void request_dump(const AceRequest * r, FILE * f) {
    fprintf(f, "[Request] seed=%lld lm_batch=%d synth_batch=%d\n", (long long) r->seed, r->lm_batch_size,
            r->synth_batch_size);
    fprintf(f, "[Request] caption: %.60s%s\n", r->caption.c_str(), r->caption.size() > 60 ? "..." : "");
    fprintf(f, "[Request] lyrics: %zu bytes\n", r->lyrics.size());
    fprintf(f, "[Request] bpm=%d dur=%.0f key=%s ts=%s lang=%s\n", r->bpm, r->duration, r->keyscale.c_str(),
            r->timesignature.c_str(), r->vocal_language.c_str());
    fprintf(f, "[Request] lm: temp=%.2f cfg=%.1f top_p=%.2f top_k=%d\n", r->lm_temperature, r->lm_cfg_scale,
            r->lm_top_p, r->lm_top_k);
    fprintf(f, "[Request] dit: steps=%d guidance=%.1f shift=%.1f\n", r->inference_steps, r->guidance_scale, r->shift);
    if (r->dcw_scaler > 0.0f || r->dcw_high_scaler > 0.0f) {
        fprintf(f, "[Request] dit: dcw_mode=%s scaler=%.3f high_scaler=%.3f\n", r->dcw_mode.c_str(), r->dcw_scaler,
                r->dcw_high_scaler);
    }
    if (r->audio_cover_strength != 1.0f || r->cover_noise_strength != 0.0f) {
        fprintf(f, "[Request] cover: strength=%.2f noise_strength=%.2f\n", r->audio_cover_strength,
                r->cover_noise_strength);
    }
    if (r->repainting_start != 0.0f || r->repainting_end >= 0.0f) {
        fprintf(f, "[Request] repaint: start=%.1f end=%.1f\n", r->repainting_start, r->repainting_end);
    }
    if (r->latent_shift != 0.0f || r->latent_rescale != 1.0f) {
        fprintf(f, "[Request] latent post: shift=%.3f rescale=%.3f\n", r->latent_shift, r->latent_rescale);
    }
    if (r->pp_vae_reencode) {
        fprintf(f, "[Request] pp_vae_reencode: true\n");
    }
    if (!r->custom_timesteps.empty()) {
        fprintf(f, "[Request] custom_timesteps: %s\n", r->custom_timesteps.c_str());
    }
    fprintf(f, "[Request] task_type: %s\n", r->task_type.c_str());
    if (!r->track.empty()) {
        fprintf(f, "[Request] track: %s\n", r->track.c_str());
    }
    fprintf(f, "[Request] solver: %s\n", r->solver.c_str());
    if (r->solver == SOLVER_STORK4) {
        fprintf(f, "[Request] stork_substeps: %d\n", r->stork_substeps);
    }
    if (r->solver == SOLVER_STORM) {
        fprintf(f,
                "[Request] storm: rk=%s stiff=%.3f cache=%d substep=%s lookback=%s restarts=%s\n",
                r->storm_rk_order.c_str(), r->storm_stiffness_threshold, r->storm_cache_depth,
                r->storm_adaptive_sub_step ? "true" : "false", r->storm_look_back_enabled ? "true" : "false",
                r->storm_enable_restarts ? r->storm_restart_steps.c_str() : "false");
    }
    if (r->infer_method != INFER_ODE) {
        fprintf(f, "[Request] legacy infer_method: %s\n", r->infer_method.c_str());
    }
    fprintf(f, "[Request] schedule_method: %s\n", r->schedule_method.c_str());
    fprintf(f, "[Request] lm_mode: %s\n", r->lm_mode.c_str());
    fprintf(f, "[Request] output_format: %s\n", r->output_format.c_str());
    if (r->peak_clip != 10) {
        fprintf(f, "[Request] peak_clip: %d\n", r->peak_clip);
    }
    if (r->output_format == "mp3" && r->mp3_bitrate != 128) {
        fprintf(f, "[Request] mp3_bitrate: %d kbps\n", r->mp3_bitrate);
    }
    if (!r->synth_model.empty()) {
        fprintf(f, "[Request] synth_model: %s\n", r->synth_model.c_str());
    }
    if (!r->lm_model.empty()) {
        fprintf(f, "[Request] lm_model: %s\n", r->lm_model.c_str());
    }
    if (!r->adapter.empty()) {
        fprintf(f, "[Request] adapter: %s (scale=%.2f self=%.2f cross=%.2f mlp=%.2f)\n", r->adapter.c_str(),
                r->adapter_scale, r->adapter_scale_self, r->adapter_scale_cross, r->adapter_scale_mlp);
    }
    if (!r->vae.empty()) {
        fprintf(f, "[Request] vae: %s\n", r->vae.c_str());
    }
    fprintf(f, "[Request] audio_codes: %s\n", r->audio_codes.empty() ? "(none)" : "(present)");
}

void request_resolve_seed(AceRequest * r) {
    if (r->seed < 0) {
        std::random_device rd;
        r->seed = (int64_t) rd();
    }
}

void request_resolve_lm_seed(AceRequest * r) {
    if (r->lm_seed < 0) {
        std::random_device rd;
        r->lm_seed = (int64_t) rd();
    }
}
