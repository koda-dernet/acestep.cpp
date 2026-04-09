<script lang="ts">
	import { slide } from 'svelte/transition';
	import { easeEmphasizedDecel } from 'm3-svelte';
	import {
		Button, Icon, Chip,
		TextFieldOutlined,
		TextFieldOutlinedMultiline,
		SelectOutlined
	} from 'm3-svelte';
	import iconFolderOpen from '@ktibow/iconset-material-symbols/folder-open';
	import iconDownload from '@ktibow/iconset-material-symbols/download';
	import iconRestartAlt from '@ktibow/iconset-material-symbols/restart-alt';
	import iconCasino from '@ktibow/iconset-material-symbols/casino';
	import iconAutoAwesome from '@ktibow/iconset-material-symbols/auto-awesome';
	import iconFormatAlignLeft from '@ktibow/iconset-material-symbols/format-align-left';
	import iconExpandMore from '@ktibow/iconset-material-symbols/expand-more';
	import iconChevronLeft from '@ktibow/iconset-material-symbols/chevron-left';
	import iconChevronRight from '@ktibow/iconset-material-symbols/chevron-right';
	import iconCheck from '@ktibow/iconset-material-symbols/check';
	import iconRemove from '@ktibow/iconset-material-symbols/remove';
	import NumericTextFieldOutlined from './NumericTextFieldOutlined.svelte';
	import AceTextFieldOutlinedMultiline from './AceTextFieldOutlinedMultiline.svelte';
	import { app, toast, setRequest } from '../lib/state.svelte.js';
	import { rollDice } from '../lib/dice.js';
	import {
		lmGenerate,
		lmInspire,
		lmFormat,
		synthGenerate,
		synthGenerateWithAudio,
		understandAudio
	} from '../lib/api.js';
	import { putSong } from '../lib/db.js';
	import {
		TASK_COVER,
		TASK_COVER_NOFSQ,
		TASK_REPAINT,
		TASK_LEGO,
		TASK_EXTRACT,
		TASK_COMPLETE,
		TRACK_NAMES
	} from '../lib/config.js';
	import type { AceRequest, Song } from '../lib/types.js';

	let busy = $state(false);
	let fileInput: HTMLInputElement;

	let d = $derived(app.props?.default);
	let ditModels = $derived(app.props?.models.dit ?? []);
	let lmModels = $derived(app.props?.models.lm ?? []);
	let loraList = $derived(app.props?.loras ?? []);
	let loraStale = $derived(!!app.request.lora && !loraList.includes(String(app.request.lora)));
	let taskType = $derived(app.request.task_type || '');
	let dp = $derived(
		app.props?.presets
			? String(app.request.synth_model || '').includes('turbo')
				? app.props.presets.turbo
				: app.props.presets.sft
			: null
	);
	let needsTrack = $derived(
		taskType === TASK_LEGO || taskType === TASK_EXTRACT || taskType === TASK_COMPLETE
	);
	let singleTrack = $derived(taskType === TASK_LEGO || taskType === TASK_EXTRACT);

	// DiT input indicators
	let hasCodes = $derived(!!app.request.audio_codes?.trim() && app.srcSongId == null);
	let hasSrc = $derived(app.srcSongId != null);
	let hasRange = $derived(app.srcRangeStart >= 0 && app.srcRangeEnd > app.srcRangeStart);
	let hasRef = $derived(app.refSongId != null);

	// track selection: radio for lego/extract, multi for complete
	let selectedTracks: Set<string> = $state(new Set());

	function toggleTrack(name: string) {
		let next = new Set(selectedTracks);
		if (next.has(name)) {
			next.delete(name);
		} else {
			if (singleTrack) next.clear();
			next.add(name);
		}
		selectedTracks = next;
	}

	// sync set to request string (preserve TRACK_NAMES order)
	$effect(() => {
		app.request.track = TRACK_NAMES.filter((n: string) => selectedTracks.has(n)).join(' | ');
	});

	// clear tracks when task has no use for them, trim to 1 for radio modes
	$effect(() => {
		if (!needsTrack) {
			if (selectedTracks.size > 0) selectedTracks = new Set();
		} else if (singleTrack && selectedTracks.size > 1) {
			selectedTracks = new Set([...selectedTracks].slice(0, 1));
		}
	});

	function reset() {
		app.name = '';
		setRequest({ caption: '' });
		app.pendingRequests = [];
		app.pendingIndex = 0;
		selectedTracks = new Set();
	}

	function exportJson() {
		const json = JSON.stringify(buildRequest(), null, 2);
		const blob = new Blob([json], { type: 'application/json' });
		const url = URL.createObjectURL(blob);
		const a = document.createElement('a');
		a.href = url;
		const safe = app.name.replace(/[^a-zA-Z0-9 _-]/g, '') || 'request';
		a.download = `${safe}.json`;
		a.click();
		URL.revokeObjectURL(url);
	}

	function importJson() {
		fileInput.click();
	}

	function onFileSelected(e: Event) {
		const input = e.target as HTMLInputElement;
		const file = input.files?.[0];
		if (!file) return;
		// reset so the same file can be re-opened
		input.value = '';

		const ext = file.name.split('.').pop()?.toLowerCase() || '';

		// JSON: load request into form (existing behavior)
		if (ext === 'json') {
			file
				.text()
				.then((text) => {
					setRequest(JSON.parse(text) as AceRequest);
					app.name = file.name.replace(/\.json$/i, '') || 'Imported';
					app.pendingRequests = [];
					app.pendingIndex = 0;
				})
				.catch(() => {
					toast('Invalid JSON file');
				});
			return;
		}

		// MP3 or WAV: send to /understand, populate form + create song card
		if (ext === 'mp3' || ext === 'wav') {
			importAudio(file, ext);
			return;
		}

		toast('Unsupported file type: ' + ext);
	}

	// import audio file via /understand endpoint.
	// creates a song card with the original audio and fills the form
	// with the returned metadata so it matches existing generated songs.
	async function importAudio(file: File, ext: string) {
		busy = true;
		try {
			toast('Understanding audio...', 4000, true);
			const blob = new Blob([await file.arrayBuffer()], {
				type: ext === 'wav' ? 'audio/wav' : 'audio/mpeg'
			});
			const result = await understandAudio(
				blob,
				app.request.lm_model as string,
				app.request.synth_model as string
			);

			setRequest(result);
			app.pendingRequests = [];
			app.pendingIndex = 0;

			// derive a clean name from the filename (strip extension)
			const name = file.name.replace(/\.(mp3|wav)$/i, '') || 'Imported';
			app.name = name;

			// create a song card so the audio is playable immediately
			const song: Song = {
				name: name,
				format: ext,
				created: Date.now(),
				caption: result.caption || '',
				seed: Number(result.seed) || 0,
				duration: Number(result.duration) || 0,
				request: { ...result },
				audio: blob
			};
			song.id = await putSong(song);
			app.songs.unshift(song);

			toast('Imported: ' + name, 4000, true);
		} catch (e: unknown) {
			toast(e instanceof Error ? e.message : String(e));
		} finally {
			busy = false;
		}
	}

	// convert string or number to number, return undefined if empty/NaN
	function num(v: unknown): number | undefined {
		if (v == null || v === '') return undefined;
		const n = Number(v);
		return isNaN(n) ? undefined : n;
	}

	// snapshot app.request into a clean AceRequest with proper types.
	// bind:value guarantees app.request always matches the DOM.
	function buildRequest(): AceRequest {
		const r = app.request;
		const out: AceRequest = { caption: String(r.caption || '') };
		if (r.lyrics) out.lyrics = String(r.lyrics);
		if (r.audio_codes) out.audio_codes = String(r.audio_codes);
		if (r.vocal_language) out.vocal_language = String(r.vocal_language);
		if (r.keyscale) out.keyscale = String(r.keyscale);
		if (r.timesignature) out.timesignature = String(r.timesignature);
		const bpm = num(r.bpm);
		if (bpm != null) out.bpm = bpm;
		const duration = num(r.duration);
		if (duration != null) out.duration = duration;
		const seed = num(r.seed);
		if (seed != null) out.seed = seed;
		const lm_temperature = num(r.lm_temperature);
		if (lm_temperature != null) out.lm_temperature = lm_temperature;
		const lm_cfg_scale = num(r.lm_cfg_scale);
		if (lm_cfg_scale != null) out.lm_cfg_scale = lm_cfg_scale;
		const lm_top_p = num(r.lm_top_p);
		if (lm_top_p != null) out.lm_top_p = lm_top_p;
		const lm_top_k = num(r.lm_top_k);
		if (lm_top_k != null) out.lm_top_k = lm_top_k;
		if (r.lm_negative_prompt) out.lm_negative_prompt = String(r.lm_negative_prompt);
		const inference_steps = num(r.inference_steps);
		if (inference_steps != null) out.inference_steps = inference_steps;
		const guidance_scale = num(r.guidance_scale);
		if (guidance_scale != null) out.guidance_scale = guidance_scale;
		const shift = num(r.shift);
		if (shift != null) out.shift = shift;
		const audio_cover_strength = num(r.audio_cover_strength);
		if (audio_cover_strength != null) out.audio_cover_strength = audio_cover_strength;
		const cover_noise_strength = num(r.cover_noise_strength);
		if (cover_noise_strength != null) out.cover_noise_strength = cover_noise_strength;
		const lm_batch_size = num(r.lm_batch_size);
		if (lm_batch_size != null && lm_batch_size >= 1) out.lm_batch_size = lm_batch_size;
		const synth_batch_size = num(r.synth_batch_size);
		if (synth_batch_size != null && synth_batch_size >= 1) out.synth_batch_size = synth_batch_size;
		if (r.task_type) out.task_type = String(r.task_type);
		if (r.track) out.track = String(r.track);
		if (r.infer_method) out.infer_method = String(r.infer_method);
		if (r.synth_model) out.synth_model = String(r.synth_model);
		if (r.lm_model) out.lm_model = String(r.lm_model);
		if (r.lora && loraList.includes(String(r.lora))) out.lora = String(r.lora);
		const lora_scale = num(r.lora_scale);
		if (lora_scale != null) out.lora_scale = lora_scale;
		return out;
	}

	// save current form edits back into pendingRequests[pendingIndex]
	function savePending() {
		if (app.pendingRequests.length > 0 && app.pendingIndex < app.pendingRequests.length) {
			app.pendingRequests[app.pendingIndex] = buildRequest();
		}
	}

	// load pendingRequests[index] into the form.
	// synth params are form-global, not per-pending: preserve them across switches.
	function loadPending(index: number) {
		const r = app.pendingRequests[index];
		setRequest({
			...r,
			inference_steps: app.request.inference_steps,
			guidance_scale: app.request.guidance_scale,
			shift: app.request.shift,
			seed: app.request.seed,
			audio_cover_strength: app.request.audio_cover_strength,
			cover_noise_strength: app.request.cover_noise_strength,
			synth_batch_size: app.request.synth_batch_size
		});
		app.pendingIndex = index;
	}

	// switch to a different pending composition (saves current edits first)
	function switchPending(delta: number) {
		const next = app.pendingIndex + delta;
		if (next < 0 || next >= app.pendingRequests.length) return;
		savePending();
		loadPending(next);
	}

	// shared: call an LM endpoint and load results into the form.
	// LM enriches: caption, lyrics, bpm, duration, keyscale, timesignature, vocal_language, audio_codes.
	// Everything else is preserved from the current UI state.
	async function lmCall(fn: (req: AceRequest) => Promise<AceRequest[]>) {
		busy = true;
		try {
			const req = buildRequest();
			req.audio_codes = '';
			const results = await fn(req);
			if (results.length > 0) {
				app.pendingRequests = results;
				app.pendingIndex = 0;
				setRequest({
					...results[0],
					inference_steps: app.request.inference_steps,
					guidance_scale: app.request.guidance_scale,
					shift: app.request.shift,
					seed: app.request.seed,
					audio_cover_strength: app.request.audio_cover_strength,
					cover_noise_strength: app.request.cover_noise_strength,
					repaint_strength: app.request.repaint_strength,
					synth_batch_size: app.request.synth_batch_size,
					lm_batch_size: app.request.lm_batch_size,
					lm_temperature: app.request.lm_temperature,
					lm_cfg_scale: app.request.lm_cfg_scale,
					lm_top_p: app.request.lm_top_p,
					lm_top_k: app.request.lm_top_k,
					lm_negative_prompt: app.request.lm_negative_prompt
				});
			}
		} catch (e: unknown) {
			toast(e instanceof Error ? e.message : String(e));
		} finally {
			busy = false;
		}
	}

	// Dice: pick a random example prompt and fill the caption
	function dice() {
		setRequest(rollDice());
	}

	// Inspire: short caption -> fresh metadata + lyrics (no audio codes)
	async function inspire() {
		await lmCall(lmInspire);
	}

	// Format: caption + lyrics -> metadata + lyrics (no audio codes)
	async function format() {
		await lmCall(lmFormat);
	}

	// Compose: send form to LM, store all enriched results for batch synth.
	// The LM preserves user-provided fields and fills the rest independently
	// per batch item. Each result is a complete standalone request.
	async function compose() {
		await lmCall(lmGenerate);
	}

	// POST /synth: send pending requests (or current form) to the server.
	// synth params (batch, seed, steps, CFG, shift) come from the form, not from pending.
	// server groups by request and expands synth_batch_size for GPU batching.
	// webui resolves seeds and predicts the expanded list for SongCard mapping.
	async function synthesize() {
		busy = true;
		try {
			savePending();
			const reqs: AceRequest[] =
				app.pendingRequests.length > 0 ? $state.snapshot(app.pendingRequests) : [buildRequest()];

			// read synth params from the form (global, not per-pending).
			const synthBatch = Math.max(1, Number(app.request.synth_batch_size) || 1);
			const userSeed = num(app.request.seed);
			const hasSeed = userSeed != null && userSeed >= 0;
			const synthParams: Partial<AceRequest> = {};
			const steps = num(app.request.inference_steps);
			if (steps != null) synthParams.inference_steps = steps;
			const cfg = num(app.request.guidance_scale);
			if (cfg != null) synthParams.guidance_scale = cfg;
			const sh = num(app.request.shift);
			if (sh != null) synthParams.shift = sh;
			const acs = num(app.request.audio_cover_strength);
			if (acs != null) synthParams.audio_cover_strength = acs;
			const cns = num(app.request.cover_noise_strength);
			if (cns != null) synthParams.cover_noise_strength = cns;
			const rps = num(app.request.repaint_strength);
			if (rps != null) synthParams.repaint_strength = rps;
			// task_type and track from form
			const t = app.request.task_type || '';
			if (t) synthParams.task_type = t;
			if (app.request.track) synthParams.track = app.request.track;
			// infer_method from form
			const im = app.request.infer_method || '';
			if (im) synthParams.infer_method = im;
			// model routing from form
			if (app.request.synth_model) synthParams.synth_model = app.request.synth_model;
			if (app.request.lora && loraList.includes(String(app.request.lora)))
				synthParams.lora = app.request.lora;
			const loraScale = num(app.request.lora_scale);
			if (loraScale != null) synthParams.lora_scale = loraScale;
			// repaint/lego: inject range from source audio selection (optional for lego)
			if (
				(t === TASK_REPAINT || t === TASK_LEGO) &&
				app.srcRangeStart >= 0 &&
				app.srcRangeEnd > app.srcRangeStart
			) {
				synthParams.repainting_start = app.srcRangeStart;
				synthParams.repainting_end = app.srcRangeEnd;
			}

			// resolve seeds, build server payload and local expanded list for SongCard mapping.
			// server receives synth_batch_size and expands internally (groups by T for GPU batch).
			// webui predicts the same expansion: seed, seed+1, ..., seed+N-1.
			const toSend: AceRequest[] = [];
			const expanded: AceRequest[] = [];
			for (const r of reqs) {
				const base = hasSeed ? userSeed : Math.floor(Math.random() * 0x100000000);
				toSend.push({ ...r, ...synthParams, seed: base, synth_batch_size: synthBatch });
				for (let i = 0; i < synthBatch; i++) {
					expanded.push({ ...r, ...synthParams, seed: base + i });
				}
			}

			// find source audio (cover/lego/repaint) and reference audio (timbre)
			const srcSong = app.srcSongId != null ? app.songs.find((s) => s.id === app.srcSongId) : null;
			const refSong = app.refSongId != null ? app.songs.find((s) => s.id === app.refSongId) : null;

			const blobs =
				srcSong || refSong
					? await synthGenerateWithAudio(
							toSend,
							srcSong?.audio ?? null,
							refSong?.audio ?? null,
							app.format
						)
					: await synthGenerate(toSend, app.format);
			const now = Date.now();
			const baseName = app.name || 'Untitled';
			for (let i = blobs.length - 1; i >= 0; i--) {
				const r = expanded[i];
				const song = {
					name: baseName,
					format: app.format,
					created: now + i,
					caption: r.caption,
					seed: r.seed || 0,
					duration: r.duration || 0,
					request: r,
					audio: blobs[i]
				} as Song;
				song.id = await putSong(song);
				app.songs.unshift(song);
			}
			app.pendingRequests = [];
			app.pendingIndex = 0;
		} catch (e: unknown) {
			toast(e instanceof Error ? e.message : String(e));
		} finally {
			busy = false;
		}
	}

	let panelModels = $state(true);
	let panelAdvLM = $state(false);
	let panelTask = $state(true);
	let panelFlow = $state(true);

	const slideM3 = (node: Element) => slide(node, { duration: 250, easing: easeEmphasizedDecel });

	let lmModelOptions = $derived(lmModels.map((n: string) => ({ text: n, value: n })));
	let ditModelOptions = $derived(ditModels.map((n: string) => ({ text: n, value: n })));
	let loraOptions = $derived([
		{ text: 'Disabled', value: '' },
		...(loraStale ? [{ text: String(app.request.lora), value: String(app.request.lora), disabled: true }] : []),
		...loraList.map((n: string) => ({ text: n, value: n }))
	]);
	let taskOptions = [
		{ text: 'text2music', value: '' },
		{ text: 'cover', value: TASK_COVER },
		{ text: 'cover-nofsq', value: TASK_COVER_NOFSQ },
		{ text: 'repaint', value: TASK_REPAINT },
		{ text: 'lego', value: TASK_LEGO },
		{ text: 'extract', value: TASK_EXTRACT },
		{ text: 'complete', value: TASK_COMPLETE }
	];
	let methodOptions = [
		{ text: 'ODE Euler', value: '' },
		{ text: 'SDE Stochastic', value: 'sde' }
	];
</script>

<form class="form ace-neutral-fields" onsubmit={(e) => e.preventDefault()}>
	<input
		type="file"
		accept=".json,.mp3,.wav"
		bind:this={fileInput}
		onchange={onFileSelected}
		hidden
	/>

	<div class="toolbar">
		<Button variant="outlined" onclick={importJson}>
			<Icon icon={iconFolderOpen} /> Open
		</Button>
		<Button variant="outlined" onclick={exportJson}>
			<Icon icon={iconDownload} /> Save
		</Button>
		<Button variant="outlined" onclick={reset}>
			<Icon icon={iconRestartAlt} /> Reset
		</Button>
	</div>

	<div class="ace-panel">
		<button class="panel-header" type="button" onclick={() => (panelModels = !panelModels)}>
			<span class="chevron" class:open={panelModels}><Icon icon={iconExpandMore} size={18} /></span>
			Models
		</button>
		{#if panelModels}
			<div class="panel-body" transition:slideM3>
				<SelectOutlined
					label="LM model"
					options={lmModelOptions.length > 0 ? lmModelOptions : [{text: 'Loading...', value: ''}]}
					value={app.request.lm_model || ''}
					onchange={(e) => { app.request.lm_model = (e.target as HTMLSelectElement).value; }}
				/>
				<SelectOutlined
					label="DiT model"
					options={ditModelOptions.length > 0 ? ditModelOptions : [{text: 'Loading...', value: ''}]}
					value={app.request.synth_model || ''}
					onchange={(e) => { app.request.synth_model = (e.target as HTMLSelectElement).value; }}
				/>
				<div class="lora-row">
					<div class="lora-select">
						<SelectOutlined
							label="LoRA"
							options={loraOptions}
							value={app.request.lora || ''}
							onchange={(e) => { app.request.lora = (e.target as HTMLSelectElement).value; }}
						/>
					</div>
					<div class="lora-scale">
						<NumericTextFieldOutlined label="Scale" bind:value={app.request.lora_scale} />
					</div>
				</div>
			</div>
		{/if}
	</div>

	<TextFieldOutlined label="Name" bind:value={app.name} />
	<AceTextFieldOutlinedMultiline label="Caption" bind:value={app.request.caption} />
	<AceTextFieldOutlinedMultiline label="Lyrics" bind:value={app.request.lyrics} />

	<div class="grid-2col">
		<TextFieldOutlined label="Language" bind:value={app.request.vocal_language} />
		<NumericTextFieldOutlined label="BPM" bind:value={app.request.bpm} />
		<NumericTextFieldOutlined label="Duration" bind:value={app.request.duration} />
		<TextFieldOutlined label="Key" bind:value={app.request.keyscale} />
		<TextFieldOutlined label="Time sig" bind:value={app.request.timesignature} />
	</div>

	<div class="toolbar ace-primary-strip">
		<Button variant="tonal" disabled={busy} onclick={dice}>
			<Icon icon={iconCasino} /> Dice
		</Button>
		<Button variant="tonal" disabled={busy} onclick={inspire}>
			<Icon icon={iconAutoAwesome} /> Inspire
		</Button>
		<Button variant="tonal" disabled={busy} onclick={format}>
			<Icon icon={iconFormatAlignLeft} /> Format
		</Button>
	</div>

	<div class="ace-panel">
		<button class="panel-header" type="button" onclick={() => (panelAdvLM = !panelAdvLM)}>
			<span class="chevron" class:open={panelAdvLM}><Icon icon={iconExpandMore} size={18} /></span>
			Advanced LM
		</button>
		{#if panelAdvLM}
			<div class="panel-body" transition:slideM3>
				<div class="grid-2col">
					<NumericTextFieldOutlined label="Temperature" bind:value={app.request.lm_temperature} />
					<NumericTextFieldOutlined label="CFG scale" bind:value={app.request.lm_cfg_scale} />
					<NumericTextFieldOutlined label="Top P" bind:value={app.request.lm_top_p} />
					<NumericTextFieldOutlined label="Top K" bind:value={app.request.lm_top_k} />
				</div>
				<TextFieldOutlinedMultiline label="Negative prompt" bind:value={app.request.lm_negative_prompt} />
				<TextFieldOutlinedMultiline label="Audio codes" bind:value={app.request.audio_codes} />
			</div>
		{/if}
	</div>

	<div class="inline-row">
		<div class="batch-field"><NumericTextFieldOutlined label="Batch" bind:value={app.request.lm_batch_size} /></div>
		<div class="spacer"></div>
		<span class="inline-label">Pending</span>
		<div class="pending-nav">
			<Button variant="text" iconType="full" onclick={() => switchPending(-1)}>
				<Icon icon={iconChevronLeft} />
			</Button>
			<span class="pending-count">
				{app.pendingRequests.length > 0 ? app.pendingIndex + 1 : 0} / {app.pendingRequests.length}
			</span>
			<Button variant="text" iconType="full" onclick={() => switchPending(1)}>
				<Icon icon={iconChevronRight} />
			</Button>
		</div>
	</div>

	<div class="ace-primary-strip fill-width">
		<Button variant="filled" disabled={busy} onclick={compose}>Compose</Button>
	</div>

	<div class="ace-panel">
		<button class="panel-header" type="button" onclick={() => (panelTask = !panelTask)}>
			<span class="chevron" class:open={panelTask}><Icon icon={iconExpandMore} size={18} /></span>
			Task
		</button>
		{#if panelTask}
			<div class="panel-body" transition:slideM3>
				<SelectOutlined
					label="Type"
					options={taskOptions}
					value={taskType}
					onchange={(e) => { app.request.task_type = (e.target as HTMLSelectElement).value; }}
				/>
				<div class="chip-row">
					<span class="inline-label">Track</span>
					<div class="chip-wrap">
						{#each TRACK_NAMES as name}
							<Chip
								variant="input"
								icon={selectedTracks.has(name) ? iconCheck : undefined}
								selected={selectedTracks.has(name)}
								disabled={!needsTrack}
								onclick={() => toggleTrack(name)}
							>
								{name}
							</Chip>
						{/each}
					</div>
				</div>
			</div>
		{/if}
	</div>

	<div class="ace-panel">
		<button class="panel-header" type="button" onclick={() => (panelFlow = !panelFlow)}>
			<span class="chevron" class:open={panelFlow}><Icon icon={iconExpandMore} size={18} /></span>
			Flow matching
		</button>
		{#if panelFlow}
			<div class="panel-body" transition:slideM3>
				<div class="grid-2col">
					<NumericTextFieldOutlined label="Steps" bind:value={app.request.inference_steps} />
					<NumericTextFieldOutlined label="Cover str." bind:value={app.request.audio_cover_strength} />
					<NumericTextFieldOutlined label="Cover noise" bind:value={app.request.cover_noise_strength} />
					<NumericTextFieldOutlined label="Repaint str." bind:value={app.request.repaint_strength} />
					<NumericTextFieldOutlined label="CFG scale" bind:value={app.request.guidance_scale} />
					<NumericTextFieldOutlined label="Shift" bind:value={app.request.shift} />
					<NumericTextFieldOutlined label="Seed" bind:value={app.request.seed} />
				</div>
				<SelectOutlined
					label="Method"
					options={methodOptions}
					value={app.request.infer_method || ''}
					onchange={(e) => { app.request.infer_method = (e.target as HTMLSelectElement).value; }}
				/>
			</div>
		{/if}
	</div>

	<div class="inline-row">
		<div class="batch-field"><NumericTextFieldOutlined label="Batch" bind:value={app.request.synth_batch_size} /></div>
		<div class="spacer"></div>
		<span class="inline-label">Format</span>
		<Chip variant="input" selected={app.format === 'mp3'} onclick={() => (app.format = 'mp3')}>MP3</Chip>
		<Chip variant="input" selected={app.format === 'wav'} onclick={() => (app.format = 'wav')}>WAV</Chip>
	</div>

	<div class="chip-row">
		<span class="inline-label">Cond</span>
		<div class="chip-wrap">
			<span class="cond-ind" class:on={hasCodes}>
				<Icon icon={hasCodes ? iconCheck : iconRemove} size={12} />
				LM codes
			</span>
			<span class="cond-ind" class:on={hasSrc}>
				<Icon icon={hasSrc ? iconCheck : iconRemove} size={12} />
				Src audio
			</span>
			<span class="cond-ind" class:on={hasRange}>
				<Icon icon={hasRange ? iconCheck : iconRemove} size={12} />
				Range
			</span>
			<span class="cond-ind" class:on={hasRef}>
				<Icon icon={hasRef ? iconCheck : iconRemove} size={12} />
				Timbre ref
			</span>
		</div>
	</div>

	<div class="ace-primary-strip fill-width">
		<Button variant="filled" disabled={busy} onclick={synthesize}>
			Synthesize
		</Button>
	</div>
</form>

<style>
	.form {
		display: flex;
		flex-direction: column;
		gap: 1.25rem;
		padding-top: 0.25rem;
		/* Outlined field label “chip” matches surface (fixes harsh white cut-out in light mode) */
		--m3v-background: var(--m3c-surface);
	}

	/* Choice chips (Track, Format, …): brand accent */
	.form.ace-neutral-fields :global(button.m3-container.input) {
		--m3c-secondary: var(--ace-brand-secondary);
		--m3c-secondary-container: var(--ace-brand-secondary-container);
		--m3c-on-secondary-container: var(--ace-brand-on-secondary-container);
	}

	/* Outlined text fields only — neutral border + focus (pickers keep theme outline below) */
	.form.ace-neutral-fields :global(.m3-container:has(> input)) {
		--m3c-primary: var(--m3c-on-surface-variant);
		--m3c-primary-container: var(--m3c-surface-container-high);
		--m3c-on-primary-container: var(--m3c-on-surface);
		--m3c-outline: color-mix(in srgb, var(--m3c-on-surface) 30%, var(--m3c-surface) 70%);
	}
	.form.ace-neutral-fields :global(.m3-container:has(> textarea)) {
		--m3c-primary: var(--m3c-on-surface-variant);
		--m3c-primary-container: var(--m3c-surface-container-high);
		--m3c-on-primary-container: var(--m3c-on-surface);
		--m3c-outline: color-mix(in srgb, var(--m3c-on-surface) 30%, var(--m3c-surface) 70%);
	}

	/* SelectOutlined pickers: restore brand accent (hover, focus, menu) */
	.form.ace-neutral-fields :global(.m3-container:has(> select)) {
		--m3c-primary: var(--ace-brand-primary);
		--m3c-primary-container: var(--ace-brand-primary-container);
		--m3c-on-primary-container: var(--ace-brand-on-primary-container);
	}

	/* Accordion panels use app surface (same as Name/Caption); fields stay visually continuous */
	.form.ace-neutral-fields .ace-panel :global(.m3-container:has(> input)),
	.form.ace-neutral-fields .ace-panel :global(.m3-container:has(> textarea)) {
		--m3v-background: var(--m3c-surface);
	}
	.form.ace-neutral-fields .ace-panel :global(.m3-container:has(> input) input),
	.form.ace-neutral-fields .ace-panel :global(.m3-container:has(> textarea) textarea) {
		background-color: var(--m3c-surface) !important;
	}
	.form.ace-neutral-fields .ace-panel :global(.m3-container:has(> select)) {
		--m3v-background: var(--m3c-surface);
	}
	.form.ace-neutral-fields .ace-panel :global(.m3-container:has(> select) select) {
		background-color: var(--m3c-surface) !important;
	}
	.form.ace-neutral-fields .cond-ind.on {
		background: var(--m3c-surface-container-highest);
		color: var(--m3c-on-surface);
	}

	.form .ace-primary-strip {
		--m3c-primary: var(--ace-brand-primary);
		--m3c-primary-container: var(--ace-brand-primary-container);
		--m3c-on-primary-container: var(--ace-brand-on-primary-container);
		--m3c-secondary: var(--ace-brand-secondary);
		--m3c-secondary-container: var(--ace-brand-secondary-container);
		--m3c-on-secondary-container: var(--ace-brand-on-secondary-container);
	}
	.form .ace-primary-strip.fill-width {
		display: flex;
		flex-direction: column;
	}
	.form .ace-primary-strip.fill-width > :global(*) {
		width: 100%;
	}

	/* Toolbar buttons spread evenly */
	.toolbar {
		display: flex;
		gap: 0.5rem;
	}
	.toolbar > :global(*) {
		flex: 1;
	}

	/* Expansion panels: same ground as app / Name field (outline + radius only; no extra green fill) */
	.ace-panel {
		--m3v-background: var(--m3c-surface);
		border: 1px solid var(--m3c-outline-variant);
		border-radius: var(--m3-shape-large);
		background: var(--m3c-surface);
		overflow: hidden;
	}
	.panel-header {
		display: flex;
		width: 100%;
		align-items: center;
		gap: 0.5rem;
		padding: 0.75rem 1rem;
		border: none;
		background: transparent;
		cursor: pointer;
		user-select: none;
		@apply --m3-title-small;
		color: var(--m3c-on-surface);
	}
	.panel-header:hover {
		background: oklch(from var(--m3c-on-surface) l c h / 0.08);
	}
	.chevron {
		display: inline-flex;
		transition: transform 300ms var(--m3-timing-function-emphasized);
	}
	.chevron.open {
		transform: rotate(180deg);
	}
	.panel-body {
		display: flex;
		flex-direction: column;
		gap: 1rem;
		padding: 0 1rem 1rem;
	}

	/* LoRA row: select + scale side by side */
	.lora-row {
		display: flex;
		gap: 0.75rem;
	}
	.lora-select {
		flex: 1;
		min-width: 0;
	}
	.lora-scale {
		width: 5.5rem;
		flex-shrink: 0;
	}

	/* Make text-field / select containers fill their parent, but not chips or buttons */
	.form :global(.m3-container:has(> input, > select, > textarea)) {
		width: 100%;
		min-width: 0;
		align-self: stretch !important;
	}

	/* 2-column grid for small fields */
	.grid-2col {
		display: grid;
		grid-template-columns: 1fr 1fr;
		gap: 1rem;
	}

	/* Inline row for batch/pending/format */
	.inline-row {
		display: flex;
		align-items: center;
		gap: 0.5rem;
	}
	.inline-label {
		@apply --m3-label-large;
		color: var(--m3c-on-surface-variant);
		flex-shrink: 0;
	}
	.batch-field {
		width: 5.5rem;
		flex-shrink: 0;
	}
	.spacer {
		flex: 1;
	}
	.pending-nav {
		display: flex;
		align-items: center;
	}
	.pending-count {
		@apply --m3-label-medium;
		font-family: var(--m3-font-mono);
		color: var(--m3c-on-surface);
	}

	/* Chip rows */
	.chip-row {
		display: flex;
		align-items: start;
		gap: 0.5rem;
	}
	.chip-row > .inline-label {
		padding-top: 0.5rem;
	}
	.chip-wrap {
		display: flex;
		flex-wrap: wrap;
		gap: 0.375rem;
		flex: 1;
		min-width: 0;
	}

	/* Condition indicators */
	.cond-ind {
		display: inline-flex;
		align-items: center;
		gap: 0.25rem;
		padding: 0.25rem 0.625rem;
		border-radius: var(--m3-shape-small);
		@apply --m3-label-small;
		white-space: nowrap;
		background: var(--m3c-surface-container-high);
		color: oklch(from var(--m3c-on-surface-variant) l c h / 0.6);
	}
</style>
