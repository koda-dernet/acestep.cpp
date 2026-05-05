<script lang="ts">
	import { onMount } from 'svelte';
	import { slide } from 'svelte/transition';
	import {
		easeEmphasizedDecel,
		Button,
		Icon,
		Chip,
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
	import iconClose from '@ktibow/iconset-material-symbols/close';
	import iconStop from '@ktibow/iconset-material-symbols/stop';
	import NumericTextFieldOutlined from './NumericTextFieldOutlined.svelte';
	import AceTextFieldOutlinedMultiline from './AceTextFieldOutlinedMultiline.svelte';
	import { app, toast, setRequest } from '../lib/state.svelte.js';
	import { rollDice } from '../lib/dice.js';
	import {
		lmSubmit,
		lmSubmitInspire,
		lmSubmitFormat,
		synthSubmit,
		synthSubmitWithAudio,
		pollJob,
		jobResultJson,
		jobResultBlobs,
		vaeDecode,
		cancelJob
	} from '../lib/api.js';
	import { putSong, getAllSongs, saveJob, loadJob, loadJobId, clearJob } from '../lib/db.js';
	import {
		TASK_TEXT2MUSIC,
		TASK_COVER,
		TASK_COVER_NOFSQ,
		TASK_REPAINT,
		TASK_LEGO,
		TASK_EXTRACT,
		TASK_COMPLETE,
		INFER_ODE,
		INFER_SDE,
		SCHEDULE_LINEAR,
		SCHEDULE_CUSTOM_SENTINEL,
		DCW_MODE_LOW,
		DCW_MODE_HIGH,
		DCW_MODE_DOUBLE,
		DCW_MODE_PIX,
		TRACK_NAMES
	} from '../lib/config.js';
	import {
		num,
		buildSparse,
		clearSection,
		withCurrentSettings,
		pickSections
	} from '../lib/fields.js';
	import type { AceRequest, Song } from '../lib/types.js';

	let busyLm = $state(false);
	let busySynth = $state(false);
	let busy = $derived(busyLm || busySynth);
	let fileInput: HTMLInputElement;

	let d = $derived(app.props?.default);
	let ditModels = $derived(app.props?.models.dit ?? []);
	let lmModels = $derived(app.props?.models.lm ?? []);
	let vaeModels = $derived(app.props?.models.vae ?? []);
	let adapterList = $derived(app.props?.adapters ?? []);
	let adapterStale = $derived(
		!!app.request.adapter && !adapterList.includes(String(app.request.adapter))
	);
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

	// fill number/enum fields with server defaults (avoids empty inputs / out-of-sync dropdowns)
	$effect(() => {
		if (!d) return;
		if (app.request.lm_batch_size == null) app.request.lm_batch_size = d.lm_batch_size;
		if (app.request.synth_batch_size == null) app.request.synth_batch_size = d.synth_batch_size;
		if (app.request.peak_clip == null) app.request.peak_clip = d.peak_clip;
		if (app.request.task_type == null || app.request.task_type === '')
			app.request.task_type = d.task_type;
		if (app.request.infer_method == null || app.request.infer_method === '')
			app.request.infer_method = d.infer_method;
		if (app.request.schedule_method == null || app.request.schedule_method === '')
			app.request.schedule_method = d.schedule_method ?? SCHEDULE_LINEAR;
		if (app.request.dcw_mode == null || app.request.dcw_mode === '')
			app.request.dcw_mode = d.dcw_mode;
		if (app.request.adapter_scale == null) app.request.adapter_scale = d.adapter_scale;
		if (app.request.adapter_scale_self == null)
			app.request.adapter_scale_self = d.adapter_scale_self;
		if (app.request.adapter_scale_cross == null)
			app.request.adapter_scale_cross = d.adapter_scale_cross;
		if (app.request.adapter_scale_mlp == null) app.request.adapter_scale_mlp = d.adapter_scale_mlp;
	});

	// DiT input indicators
	let hasCodes = $derived(!!app.request.audio_codes?.trim() && app.srcSongId == null);
	let hasSrc = $derived(app.srcSongId != null);
	let hasRange = $derived(app.srcRangeStart != null || app.srcRangeEnd != null);
	let hasRef = $derived(app.refSongId != null);

	// instrumental mode: checked when lyrics and language match the convention.
	// any manual edit to either field naturally unchecks via $derived.
	let instrumental = $derived(
		String(app.request.lyrics || '').trim() === '[Instrumental]' &&
			String(app.request.vocal_language || '').trim() === 'unknown'
	);

	function toggleInstrumental() {
		if (instrumental) {
			app.request.lyrics = '';
			app.request.vocal_language = '';
		} else {
			app.request.lyrics = '[Instrumental]';
			app.request.vocal_language = 'unknown';
		}
	}

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

	// cancel the active pipeline job
	async function cancelPipeline() {
		try {
			if (busySynth) {
				const synthId = loadJobId('synth');
				if (synthId) await cancelJob(synthId);
			} else if (busyLm) {
				const lmId = loadJobId('lm');
				if (lmId) await cancelJob(lmId);
			}
		} catch {}
	}

	// resume polling for any pending jobs persisted in localStorage
	onMount(() => {
		const lmId = loadJobId('lm');
		if (lmId) {
			busyLm = true;
			pollJob(lmId)
				.then(() => jobResultJson(lmId))
				.then((results) => {
					clearJob('lm');
					app.pendingRequests = results;
					app.pendingIndex = 0;
					if (results.length > 0) {
						setRequest(results[0]);
					}
				})
				.catch(() => {
					clearJob('lm');
				})
				.finally(() => {
					busyLm = false;
				});
		}

		const synthJob = loadJob('synth');
		if (synthJob) {
			busySynth = true;
			pollJob(synthJob.id)
				.then(() => jobResultBlobs(synthJob.id))
				.then(async ({ audios, latents }) => {
					clearJob('synth');
					const now = Date.now();
					for (let i = audios.length - 1; i >= 0; i--) {
						const t = synthJob.tracks[i] || {
							caption: '',
							seed: 0,
							duration: 0,
							task: '',
							request: { caption: '' }
						};
						const suffix = [synthJob.variant, t.task].filter((s) => s).join(' ');
						const song: Song = {
							name: suffix ? synthJob.name + ' (' + suffix + ')' : synthJob.name,
							format: synthJob.format,
							created: now + i,
							caption: t.caption,
							seed: t.seed,
							duration: t.duration,
							request: t.request,
							audio: audios[i],
							latents: latents[i]
						};
						await putSong(song);
					}
					app.songs = (await getAllSongs()).reverse();
				})
				.catch(() => {
					clearJob('synth');
				})
				.finally(() => {
					busySynth = false;
				});
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
		const safe = app.name.replace(/[\\/:*?"<>|\x00-\x1f]/g, '') || 'request';
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
		input.value = '';

		const ext = file.name.split('.').pop()?.toLowerCase() || '';

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

		// MP3 or WAV: create song card (audio only, use Scan on the card for metadata)
		if (ext === 'mp3' || ext === 'wav') {
			openAudio(file, ext);
			return;
		}

		if (ext === 'vae') {
			openLatents(file);
			return;
		}

		toast('Unsupported file type: ' + ext);
	}

	async function openAudio(file: File, ext: string) {
		const blob = new Blob([await file.arrayBuffer()], {
			type: ext === 'wav' ? 'audio/wav' : 'audio/mpeg'
		});
		const name = file.name.replace(/\.(mp3|wav)$/i, '') || 'Imported';
		const song: Song = {
			name,
			format: ext,
			created: Date.now(),
			caption: '',
			seed: 0,
			duration: 0,
			request: { caption: '' },
			audio: blob
		};
		song.id = await putSong(song);
		app.songs.unshift(song);
		app.name = name;
		toast('Opened: ' + name, 4000, true);
	}

	async function openLatents(file: File) {
		const buf = await file.arrayBuffer();
		if (buf.byteLength === 0 || buf.byteLength % 256 !== 0) {
			toast('Invalid .vae file: size must be a multiple of 256 bytes (64 channels x f32)');
			return;
		}
		const T = buf.byteLength / 256;
		if (T > 15000) {
			toast('Invalid .vae file: too long (max 15000 frames = 10 min)');
			return;
		}
		const latentsBlob = new Blob([buf], { type: 'application/octet-stream' });
		const name = file.name.replace(/\.vae$/i, '') || 'Imported';
		try {
			const jobId = await vaeDecode(latentsBlob, app.request);
			await pollJob(jobId);
			const { audios } = await jobResultBlobs(jobId);
			if (!audios.length) throw new Error('Decode returned no audio');
			const song: Song = {
				name,
				format: app.format,
				created: Date.now(),
				caption: '',
				seed: 0,
				duration: 0,
				request: { caption: '' },
				audio: audios[0],
				latents: latentsBlob
			};
			song.id = await putSong(song);
			app.songs.unshift(song);
			app.name = name;
			toast('Opened: ' + name, 4000, true);
		} catch (e: unknown) {
			toast(e instanceof Error ? e.message : String(e));
		}
	}

	// snapshot app.request into a clean AceRequest; drop stale adapter
	function buildRequest(): AceRequest {
		const out = buildSparse(app.request);
		if (out.adapter && !adapterList.includes(String(out.adapter))) delete out.adapter;
		return out;
	}

	// save current form edits back into pendingRequests[pendingIndex]
	function savePending() {
		if (app.pendingRequests.length > 0 && app.pendingIndex < app.pendingRequests.length) {
			app.pendingRequests[app.pendingIndex] = buildRequest();
		}
	}

	// load pendingRequests[index] into the form, preserving user settings
	function loadPending(index: number) {
		const r = app.pendingRequests[index];
		setRequest(withCurrentSettings(r, app.request));
		app.pendingIndex = index;
	}

	// switch pending composition (saves current edits first)
	function switchPending(delta: number) {
		const next = app.pendingIndex + delta;
		if (next < 0 || next >= app.pendingRequests.length) return;
		savePending();
		loadPending(next);
	}

	// shared: submit an LM job, poll, and load results into the form
	async function lmCall(fn: (req: AceRequest) => Promise<string>) {
		busyLm = true;
		try {
			const req = buildRequest();
			req.audio_codes = '';
			const jobId = await fn(req);
			saveJob('lm', jobId);
			await pollJob(jobId);
			const results = await jobResultJson(jobId);
			clearJob('lm');
			if (results.length > 0) {
				app.pendingRequests = results;
				app.pendingIndex = 0;
				setRequest(withCurrentSettings(results[0], app.request));
			}
		} catch (e: unknown) {
			toast(e instanceof Error ? e.message : String(e));
		} finally {
			busyLm = false;
		}
	}

	function dice() {
		setRequest(rollDice());
	}

	async function inspire() {
		await lmCall(lmSubmitInspire);
	}

	async function format() {
		await lmCall(lmSubmitFormat);
	}

	async function compose() {
		await lmCall(lmSubmit);
	}

	// POST /synth: send pending requests (or current form) to the server.
	// synth params come from the form (global), not per-pending.
	// server expands synth_batch_size internally; we predict the same expansion for SongCards.
	async function synthesize() {
		busySynth = true;
		try {
			savePending();
			const reqs: AceRequest[] =
				app.pendingRequests.length > 0 ? $state.snapshot(app.pendingRequests) : [buildRequest()];

			const synthBatch = Math.max(1, Number(app.request.synth_batch_size) || 1);
			const userSeed = num(app.request.seed);
			const hasSeed = userSeed != null && userSeed >= 0;

			const synthParams = pickSections(app.request, ['flow', 'advanced', 'toolbar', 'routing']);
			delete synthParams.seed;
			delete synthParams.synth_batch_size;
			if (synthParams.adapter && !adapterList.includes(String(synthParams.adapter)))
				delete synthParams.adapter;

			const toSend: AceRequest[] = [];
			const expanded: AceRequest[] = [];
			for (const r of reqs) {
				const base = hasSeed ? userSeed : Math.floor(Math.random() * 0x100000000);
				toSend.push({ ...r, ...synthParams, seed: base, synth_batch_size: synthBatch });
				for (let i = 0; i < synthBatch; i++) {
					expanded.push({ ...r, ...synthParams, seed: base + i });
				}
			}

			const srcSong = app.srcSongId != null ? app.songs.find((s) => s.id === app.srcSongId) : null;
			const refSong = app.refSongId != null ? app.songs.find((s) => s.id === app.refSongId) : null;

			// extract DiT variant from model filename ("acestep-v15-xl-turbo-Q8_0.gguf" -> "xl-turbo")
			const model = String(app.request.synth_model || '');
			const vm = model.match(/^acestep-v15-(.+?)-(Q\d.*|BF16)\.gguf$/);
			const variant = vm ? vm[1] : '';
			const baseName = app.name || 'Untitled';

			// submit job, poll until done, fetch result. When the source song
			// or timbre reference already carries cached latents, we upload
			// those instead of the audio: the server skips the matching VAE
			// encode entirely.
			const jobId =
				srcSong || refSong
					? await synthSubmitWithAudio(
							toSend,
							srcSong?.latents ? null : (srcSong?.audio ?? null),
							srcSong?.latents ?? null,
							refSong?.latents ? null : (refSong?.audio ?? null),
							refSong?.latents ?? null,
							app.format
						)
					: await synthSubmit(toSend, app.format);
			saveJob('synth', {
				id: jobId,
				name: baseName,
				format: app.format,
				variant,
				tracks: expanded.map((r) => ({
					caption: r.caption || '',
					seed: r.seed || 0,
					duration: r.duration || 0,
					task: r.task_type || 'text2music',
					request: r
				}))
			});
			await pollJob(jobId);
			const { audios, latents } = await jobResultBlobs(jobId);
			clearJob('synth');

			const now = Date.now();
			for (let i = audios.length - 1; i >= 0; i--) {
				const r = expanded[i];
				const task = r.task_type || 'text2music';
				const suffix = [variant, task].filter((s) => s).join(' ');
				const song = {
					name: suffix ? baseName + ' (' + suffix + ')' : baseName,
					format: app.format,
					created: now + i,
					caption: r.caption,
					seed: r.seed || 0,
					duration: r.duration || 0,
					request: r,
					audio: audios[i],
					latents: latents[i]
				} as Song;
				song.id = await putSong(song);
				app.songs.unshift(song);
			}
			app.pendingRequests = [];
			app.pendingIndex = 0;
		} catch (e: unknown) {
			toast(e instanceof Error ? e.message : String(e));
		} finally {
			busySynth = false;
		}
	}

	function clearMetadata() {
		clearSection(app.request, 'metadata');
	}

	function clearFlowMatching() {
		clearSection(app.request, 'flow');
		app.srcRangeStart = null;
		app.srcRangeEnd = null;
	}

	let panelModels = $state(true);
	let panelAdvLM = $state(false);
	let panelTask = $state(true);
	let panelFlow = $state(true);

	const slideM3 = (node: Element) => slide(node, { duration: 250, easing: easeEmphasizedDecel });

	let lmModelOptions = $derived(lmModels.map((n: string) => ({ text: n, value: n })));
	let ditModelOptions = $derived(ditModels.map((n: string) => ({ text: n, value: n })));
	let vaeModelOptions = $derived(vaeModels.map((n: string) => ({ text: n, value: n })));
	let adapterOptions = $derived([
		{ text: 'Disabled', value: '' },
		...(adapterStale
			? [{ text: String(app.request.adapter), value: String(app.request.adapter), disabled: true }]
			: []),
		...adapterList.map((n: string) => ({ text: n, value: n }))
	]);
	let taskOptions = [
		{ text: 'Text2Music: from prompt and LM codes', value: TASK_TEXT2MUSIC },
		{ text: 'Cover: reinterpret in a new style', value: TASK_COVER },
		{ text: 'Cover (no FSQ): closer to the original', value: TASK_COVER_NOFSQ },
		{ text: 'Repaint: regenerate a region', value: TASK_REPAINT },
		{ text: 'Lego: add a stem over backing audio', value: TASK_LEGO },
		{ text: 'Extract: isolate one stem from a mix', value: TASK_EXTRACT },
		{ text: 'Complete: auto-arrange around a partial track', value: TASK_COMPLETE }
	];
	let methodOptions = [
		{ text: 'ODE → Euler (legacy)', value: INFER_ODE },
		{ text: 'Euler', value: 'euler' },
		{ text: 'SDE stochastic', value: INFER_SDE },
		{ text: 'Heun (2× forward/step)', value: 'heun' },
		{ text: 'RK4 (4× forward/step)', value: 'rk4' },
		{ text: 'RK5 (6× forward/step)', value: 'rk5' },
		{ text: 'DPM++ 2M', value: 'dpm2m' },
		{ text: 'DPM++ 3M', value: 'dpm3m' },
		{ text: 'DPM++ 2M adaptive', value: 'dpm2m_ada' },
		{ text: 'JKASS fast', value: 'jkass_fast' },
		{ text: 'JKASS quality', value: 'jkass_quality' },
		{ text: 'STORK 2', value: 'stork2' },
		{ text: 'STORK 4', value: 'stork4' },
		{ text: 'DOPRI5', value: 'dopri5' },
		{ text: 'DOP853', value: 'dop853' },
		{ text: 'Gauss–Legendre 2s', value: 'gl2s' }
	];

	const schedulePresetOptions = [
		{ text: 'Custom / parameterized…', value: SCHEDULE_CUSTOM_SENTINEL },
		{ text: 'Linear', value: 'linear' },
		{ text: 'Cosine', value: 'cosine' },
		{ text: 'DDIM uniform', value: 'ddim_uniform' },
		{ text: 'SGM uniform (Karras)', value: 'sgm_uniform' },
		{ text: 'Karras alias', value: 'karras' },
		{ text: 'Tangent (bong)', value: 'bong_tangent' },
		{ text: 'Linear–quadratic', value: 'linear_quadratic' },
		{ text: 'Power (default exp)', value: 'power' },
		{ text: 'Beta 57', value: 'beta57' }
	];

	let schedulePresetSelectValue = $derived.by(() => {
		const raw = (app.request.schedule_method ?? '').trim() || SCHEDULE_LINEAR;
		const known = schedulePresetOptions.some((o) => o.value === raw && o.value !== SCHEDULE_CUSTOM_SENTINEL);
		return known ? raw : SCHEDULE_CUSTOM_SENTINEL;
	});
	let dcwOptions = [
		{ text: 'Low', value: DCW_MODE_LOW },
		{ text: 'High', value: DCW_MODE_HIGH },
		{ text: 'Double', value: DCW_MODE_DOUBLE },
		{ text: 'Pix', value: DCW_MODE_PIX }
	];

	// Repaint start/end are bound to srcRange* (which syncs to request.repainting_*).
	// Presented as a plain number input so the user can type a value directly.
	let repaintStartText = $derived(
		app.srcRangeStart != null ? String(Math.round(app.srcRangeStart * 100) / 100) : ''
	);
	let repaintEndText = $derived(
		app.srcRangeEnd != null ? String(Math.round(app.srcRangeEnd * 100) / 100) : ''
	);
	function onRepaintStart(e: Event) {
		const s = (e.target as HTMLInputElement).value.trim();
		app.srcRangeStart = s === '' ? null : isNaN(Number(s)) ? app.srcRangeStart : Number(s);
	}
	function onRepaintEnd(e: Event) {
		const s = (e.target as HTMLInputElement).value.trim();
		app.srcRangeEnd = s === '' ? null : isNaN(Number(s)) ? app.srcRangeEnd : Number(s);
	}
</script>

<form class="form ace-neutral-fields" onsubmit={(e) => e.preventDefault()}>
	<input
		type="file"
		accept=".json,.mp3,.wav,.vae"
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
					options={lmModelOptions.length > 0 ? lmModelOptions : [{ text: 'Loading...', value: '' }]}
					value={app.request.lm_model || ''}
					onchange={(e) => {
						app.request.lm_model = (e.target as HTMLSelectElement).value;
					}}
				/>
				<SelectOutlined
					label="DiT model"
					options={ditModelOptions.length > 0
						? ditModelOptions
						: [{ text: 'Loading...', value: '' }]}
					value={app.request.synth_model || ''}
					onchange={(e) => {
						app.request.synth_model = (e.target as HTMLSelectElement).value;
					}}
				/>
				<SelectOutlined
					label="VAE"
					options={vaeModelOptions.length > 0
						? vaeModelOptions
						: [{ text: 'Loading...', value: '' }]}
					value={app.request.vae || ''}
					onchange={(e) => {
						app.request.vae = (e.target as HTMLSelectElement).value;
					}}
				/>
				<div class="adapter-row">
					<div class="adapter-select">
						<SelectOutlined
							label="LoRA"
							options={adapterOptions}
							value={app.request.adapter || ''}
							onchange={(e) => {
								app.request.adapter = (e.target as HTMLSelectElement).value;
							}}
						/>
					</div>
					<div class="adapter-scale">
						<NumericTextFieldOutlined label="Scale" bind:value={app.request.adapter_scale} />
					</div>
				</div>
				<div class="adapter-module-grid">
					<NumericTextFieldOutlined label="Self attn" bind:value={app.request.adapter_scale_self} />
					<NumericTextFieldOutlined
						label="Cross attn"
						bind:value={app.request.adapter_scale_cross}
					/>
					<NumericTextFieldOutlined label="MLP" bind:value={app.request.adapter_scale_mlp} />
				</div>
			</div>
		{/if}
	</div>

	<TextFieldOutlined label="Name" bind:value={app.name} />
	<div class="lyrics-block">
		<AceTextFieldOutlinedMultiline label="Caption" bind:value={app.request.caption} />
		<div class="lyrics-toggle">
			<Chip
				variant="input"
				selected={!app.request.use_cot_caption}
				onclick={() => (app.request.use_cot_caption = !app.request.use_cot_caption)}
				title="Lock caption: LM keeps your text intact (skips CoT caption refinement)"
			>
				Lock caption
			</Chip>
		</div>
	</div>

	<div class="lyrics-block">
		<AceTextFieldOutlinedMultiline label="Lyrics" bind:value={app.request.lyrics} />
		<div class="lyrics-toggle">
			<Chip variant="input" selected={instrumental} onclick={toggleInstrumental}>Instrumental</Chip>
		</div>
	</div>

	<div class="section-with-clear">
		<span class="section-title">Metadata</span>
		<Button variant="text" iconType="full" onclick={clearMetadata}>
			<Icon icon={iconClose} />
		</Button>
	</div>
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
				<TextFieldOutlinedMultiline
					label="Negative prompt"
					bind:value={app.request.lm_negative_prompt}
				/>
				<TextFieldOutlinedMultiline label="Audio codes" bind:value={app.request.audio_codes} />
			</div>
		{/if}
	</div>

	<div class="inline-row">
		<div class="batch-field">
			<NumericTextFieldOutlined label="Batch" bind:value={app.request.lm_batch_size} />
		</div>
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

	<div class="ace-primary-strip action-row">
		<Button variant="filled" disabled={busy} onclick={compose}>Compose</Button>
		<Button variant="outlined" disabled={!busyLm} onclick={cancelPipeline}>
			<Icon icon={iconStop} /> Cancel
		</Button>
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
					onchange={(e) => {
						app.request.task_type = (e.target as HTMLSelectElement).value;
					}}
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
		<div class="panel-header-row">
			<button class="panel-header" type="button" onclick={() => (panelFlow = !panelFlow)}>
				<span class="chevron" class:open={panelFlow}><Icon icon={iconExpandMore} size={18} /></span>
				Flow matching
			</button>
			<div class="panel-clear">
				<Button variant="text" iconType="full" onclick={clearFlowMatching}>
					<Icon icon={iconClose} />
				</Button>
			</div>
		</div>
		{#if panelFlow}
			<div class="panel-body" transition:slideM3>
				<div class="grid-2col">
					<NumericTextFieldOutlined label="Steps" bind:value={app.request.inference_steps} />
					<NumericTextFieldOutlined
						label="Cover str."
						bind:value={app.request.audio_cover_strength}
					/>
					<NumericTextFieldOutlined
						label="Cover noise"
						bind:value={app.request.cover_noise_strength}
					/>
					<SelectOutlined
						label="DCW mode"
						options={dcwOptions}
						value={app.request.dcw_mode || ''}
						onchange={(e) => {
							app.request.dcw_mode = (e.target as HTMLSelectElement).value;
						}}
					/>
					<NumericTextFieldOutlined label="DCW scaler" bind:value={app.request.dcw_scaler} />
					<NumericTextFieldOutlined
						label="DCW high scaler"
						bind:value={app.request.dcw_high_scaler}
					/>
					<TextFieldOutlined
						label="Repaint start"
						value={repaintStartText}
						oninput={onRepaintStart}
					/>
					<TextFieldOutlined label="Repaint end" value={repaintEndText} oninput={onRepaintEnd} />
					<NumericTextFieldOutlined label="CFG scale" bind:value={app.request.guidance_scale} />
					<NumericTextFieldOutlined label="Shift" bind:value={app.request.shift} />
					<NumericTextFieldOutlined label="Seed" bind:value={app.request.seed} />
				</div>
				<SelectOutlined
					label="Schedule preset"
					options={schedulePresetOptions}
					value={schedulePresetSelectValue}
					onchange={(e) => {
						const v = (e.target as HTMLSelectElement).value;
						if (v === SCHEDULE_CUSTOM_SENTINEL) return;
						app.request.schedule_method = v;
					}}
				/>
				<TextFieldOutlined
					label="Schedule method"
					bind:value={app.request.schedule_method}
					placeholder="linear · cosine · power:2 · composite:linear+cosine:0.5:0.5 …"
				/>
				<SelectOutlined
					label="Solver (infer_method)"
					options={methodOptions}
					value={app.request.infer_method || ''}
					onchange={(e) => {
						app.request.infer_method = (e.target as HTMLSelectElement).value;
					}}
				/>
			</div>
		{/if}
	</div>

	<div class="inline-row">
		<div class="batch-field">
			<NumericTextFieldOutlined label="Batch" bind:value={app.request.synth_batch_size} />
		</div>
		<div class="batch-field peak-clip-field">
			<NumericTextFieldOutlined label="Peak clip" bind:value={app.request.peak_clip} />
		</div>
		<div class="spacer"></div>
		<span class="inline-label">Format</span>
		<Chip variant="input" selected={app.format === 'mp3'} onclick={() => (app.format = 'mp3')}
			>MP3</Chip
		>
		<Chip variant="input" selected={app.format === 'wav16'} onclick={() => (app.format = 'wav16')}
			>WAV16</Chip
		>
		<Chip variant="input" selected={app.format === 'wav24'} onclick={() => (app.format = 'wav24')}
			>WAV24</Chip
		>
		<Chip variant="input" selected={app.format === 'wav32'} onclick={() => (app.format = 'wav32')}
			>WAV32</Chip
		>
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

	<div class="ace-primary-strip action-row">
		<Button variant="filled" disabled={busy} onclick={synthesize}>Synthesize</Button>
		<Button variant="outlined" disabled={!busySynth} onclick={cancelPipeline}>
			<Icon icon={iconStop} /> Cancel
		</Button>
	</div>
</form>

<style>
	.form {
		display: flex;
		flex-direction: column;
		gap: 1.25rem;
		padding-top: 0.25rem;
		/* Outlined field label "chip" matches surface (fixes harsh white cut-out in light mode) */
		--m3v-background: var(--m3c-surface);
	}

	/* Choice chips (Track, Format, Instrumental, ...): brand accent */
	.form.ace-neutral-fields :global(button.m3-container.input) {
		--m3c-secondary: var(--ace-brand-secondary);
		--m3c-secondary-container: var(--ace-brand-secondary-container);
		--m3c-on-secondary-container: var(--ace-brand-on-secondary-container);
	}

	/* Outlined text fields only -- neutral border + focus (pickers keep theme outline below) */
	.form.ace-neutral-fields :global(.m3-container:has(> input)) {
		--m3c-primary: var(--m3c-on-surface-variant);
		--m3c-primary-container: var(--m3c-surface-container-high);
		--m3c-on-primary-container: var(--m3c-on-surface);
		--m3c-outline: color-mix(in srgb, var(--m3c-on-surface) 30%, var(--m3c-surface) 70%);
	}
	.form.ace-neutral-fields :global(.m3-container:has(textarea)) {
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
	.form.ace-neutral-fields .ace-panel :global(.m3-container:has(textarea)) {
		--m3v-background: var(--m3c-surface);
	}
	.form.ace-neutral-fields .ace-panel :global(.m3-container:has(> input) input),
	.form.ace-neutral-fields .ace-panel :global(.m3-container:has(textarea) textarea) {
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
	.form .ace-primary-strip.action-row {
		display: flex;
		gap: 0.5rem;
	}
	.form .ace-primary-strip.action-row > :global(*:first-child) {
		flex: 1;
	}

	/* Toolbar buttons spread evenly */
	.toolbar {
		display: flex;
		gap: 0.5rem;
	}
	.toolbar > :global(*) {
		flex: 1;
	}

	/* Expansion panels */
	.ace-panel {
		--m3v-background: var(--m3c-surface);
		border: 1px solid var(--m3c-outline-variant);
		border-radius: var(--m3-shape-large);
		background: var(--m3c-surface);
		/* Must stay visible so native <select> popup lists aren’t clipped (overflow:hidden kills popovers). */
		overflow: visible;
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

	.section-title {
		font-size: 0.85rem;
		color: var(--m3c-on-surface);
		font-weight: 600;
		padding: 0.4rem 0 0;
	}
	.lyrics-header,
	.caption-header,
	.metadata-header {
		display: flex;
		align-items: center;
		justify-content: space-between;
	}
	.header-toggle {
		display: flex;
		flex-direction: row;
		align-items: center;
		gap: 0.25rem;
		font-size: 0.8rem;
		font-weight: 400;
		color: var(--m3c-on-surface-variant);
		cursor: pointer;
	}
	.header-toggle input[type='checkbox'] {
		cursor: pointer;
	}
	.has-clear {
		position: relative;
	}
	.details-clear {
		position: absolute;
		top: 0.4rem;
		right: 0;
	}
	.clear-btn {
		display: inline-flex;
		align-items: center;
		justify-content: center;
		padding: 0;
	}
	.panel-header:hover {
		background: oklch(from var(--m3c-on-surface) l c h / 0.08);
	}
	.panel-header-row {
		display: flex;
		align-items: center;
	}
	.panel-header-row > .panel-header {
		flex: 1;
	}
	.panel-clear {
		padding-right: 0.5rem;
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

	/* Adapter (LoRA) row: select + scale side by side */
	.adapter-row {
		display: flex;
		gap: 0.75rem;
	}
	.adapter-select {
		flex: 1;
		min-width: 0;
	}
	.adapter-scale {
		width: 5.5rem;
		flex-shrink: 0;
	}
	.adapter-module-grid {
		display: grid;
		grid-template-columns: repeat(3, minmax(0, 1fr));
		gap: 0.75rem;
	}

	/* Lyrics block: multiline + instrumental chip stacked */
	.lyrics-block {
		display: flex;
		flex-direction: column;
		gap: 0.5rem;
	}
	.lyrics-toggle {
		display: flex;
		justify-content: flex-end;
	}

	/* Section title with inline clear button */
	.section-with-clear {
		display: flex;
		align-items: center;
		gap: 0.5rem;
		margin-bottom: -0.75rem;
	}
	.section-with-clear > .section-title {
		flex: 1;
	}
	.section-title {
		@apply --m3-title-small;
		color: var(--m3c-on-surface);
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
	.peak-clip-field {
		width: 6.25rem;
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
