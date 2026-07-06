// mirrors AceRequest from request.h
// all fields optional except caption: empty/unset = server applies default
export interface AceRequest {
	caption: string;
	lyrics?: string;
	audio_codes?: string;
	bpm?: number;
	duration?: number;
	keyscale?: string;
	timesignature?: string;
	vocal_language?: string;
	seed?: number;
	lm_batch_size?: number;
	synth_batch_size?: number;
	lm_temperature?: number;
	lm_cfg_scale?: number;
	lm_top_p?: number;
	lm_top_k?: number;
	lm_negative_prompt?: string;
	lm_seed?: number;
	use_cot_caption?: boolean;
	inference_steps?: number;
	guidance_scale?: number;
	shift?: number;
	dcw_scaler?: number;
	dcw_high_scaler?: number;
	dcw_mode?: string;
	audio_cover_strength?: number;
	cover_noise_strength?: number;
	repainting_start?: number;
	repainting_end?: number;
	latent_shift?: number;
	latent_rescale?: number;
	/** When true and server has a PP-VAE GGUF, run neural polish after main VAE decode. */
	pp_vae_reencode?: boolean;
	custom_timesteps?: string;
	/** Timestep spacing: linear, cosine, ddim_uniform, power:p, composite:… (server defaults to linear). */
	schedule_method?: string;
	task_type?: string;
	track?: string;
	solver?: string;
	stork_substeps?: number;
	storm_stiffness_threshold?: number;
	storm_hysteresis_margin?: number;
	storm_ema_alpha?: number;
	storm_cache_depth?: number;
	storm_rk_order?: string;
	storm_calib_frac?: number;
	storm_adaptive_sub_step?: boolean;
	storm_sub_step_threshold?: number;
	storm_sub_step_max_depth?: number;
	storm_look_back_enabled?: boolean;
	storm_look_back_lambda?: number;
	storm_look_back_snr_power?: number;
	storm_enable_restarts?: boolean;
	storm_restart_steps?: string;
	storm_restart_noise_scale?: number;
	storm_restart_s_noise?: number;
	storm_restart_seed?: number;
	storm_restart_flush_cache?: boolean;
	storm_restart_aligned_noise?: boolean;
	storm_force_pure_euler?: boolean;
	storm_verbose?: boolean;
	infer_method?: string;
	peak_clip?: number;
	mp3_bitrate?: number;
	// server routing (not part of C++ AceRequest, parsed separately)
	synth_model?: string;
	lm_model?: string;
	adapter?: string;
	adapter_scale?: number;
	adapter_scale_self?: number;
	adapter_scale_cross?: number;
	adapter_scale_mlp?: number;
	vae?: string;
}

// named entry in a server-side registry (solvers, schedules)
export interface NamedOption {
	name: string;
	display: string;
}

// outcome of the server's most recent adapter merge (GET /props).
// skipped>0 usually means the adapter targets a different base model width;
// ok=false means the merge failed entirely (no tensors applied).
export interface AdapterMergeInfo {
	ok: boolean;
	path: string;
	algo: string;
	merged: number;
	skipped: number;
	scale: number;
	scale_self: number;
	scale_cross: number;
	scale_mlp: number;
}

// GET /props response
export interface AceProps {
	version: string;
	models: {
		lm: string[];
		embedding: string[];
		dit: string[];
		vae: string[];
		'pp-vae'?: string[];
	};
	adapters: string[];
	// served from the C++ registries; absent on older servers
	solvers?: NamedOption[];
	schedules?: NamedOption[];
	tracks?: string[];
	adapter_merge?: AdapterMergeInfo;
	cli: Record<string, string | number>;
	default: AceRequest;
	presets: {
		turbo: { inference_steps: number; guidance_scale: number; shift: number };
		sft: { inference_steps: number; guidance_scale: number; shift: number };
	};
}

// what we store in IndexedDB per song
export interface Song {
	id?: number;
	name: string;
	format: string;
	created: number;
	caption: string;
	seed: number;
	duration: number;
	request: AceRequest;
	audio: Blob;
	// raw f32 [T*64] post-DiT latents that the VAE decoder produces this
	// audio from. Always present for songs from /synth or /vae decode (the
	// server emits them unconditionally). Absent only for songs imported
	// from a raw audio file before Compute VAE latents has been run. When
	// present, the client uploads them instead of audio on subsequent jobs
	// that reuse this song as src or ref, skipping a VAE encode each time.
	latents?: Blob;
	// 4096 normalized peaks [0..1] cached after the first decode, so F5
	// and re-mounts skip decodeAudioData entirely. Downsampled at draw
	// time to whatever canvas width is on screen.
	peaks?: Float32Array;
}
