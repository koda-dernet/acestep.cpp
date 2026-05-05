<script lang="ts">
	import { Button, Card, Chip, Icon } from 'm3-svelte';
	import iconPlayArrow from '@ktibow/iconset-material-symbols/play-arrow';
	import iconStop from '@ktibow/iconset-material-symbols/stop';
	import iconEdit from '@ktibow/iconset-material-symbols/edit';
	import iconDownload from '@ktibow/iconset-material-symbols/download';
	import iconDelete from '@ktibow/iconset-material-symbols/delete';
	import iconHearing from '@ktibow/iconset-material-symbols/hearing';
	import iconMemory from '@ktibow/iconset-material-symbols/memory';
	import iconVerticalAlignTop from '@ktibow/iconset-material-symbols/vertical-align-top';
	import iconVerticalAlignBottom from '@ktibow/iconset-material-symbols/vertical-align-bottom';
	import { app, setRequest, toast } from '../lib/state.svelte.js';
	import { deleteSong, saveJob, clearJob, putSong } from '../lib/db.js';
	import {
		understandSubmit,
		vaeEncode,
		pollJob,
		jobResultUnderstand,
		jobResultLatents
	} from '../lib/api.js';
	import type { Song } from '../lib/types.js';
	import { displaySongName } from '../lib/songName.js';
	import Waveform from './Waveform.svelte';

	let { song }: { song: Song } = $props();

	let playing = $state(false);
	let time = $state(0);
	let dur = $state(0);
	let rangeStart = $state(0);
	let rangeEnd = $state(0);

	let isRef = $derived(app.refSongId === song.id);
	let isSrc = $derived(app.srcSongId === song.id);
	let displayName = $derived(displaySongName(song));

	function toggleRef() {
		if (isRef) {
			app.refSongId = null;
		} else {
			app.refSongId = song.id ?? null;
		}
	}

	function toggleSrc() {
		if (isSrc) {
			app.srcSongId = null;
			app.srcRangeStart = null;
			app.srcRangeEnd = null;
			rangeStart = 0;
			rangeEnd = 0;
		} else {
			app.srcSongId = song.id ?? null;
		}
	}

	// waveform drag to global state
	$effect(() => {
		if (isSrc && rangeEnd > rangeStart) {
			app.srcRangeStart = rangeStart;
			app.srcRangeEnd = rangeEnd;
		}
	});

	// global state to waveform visual (field input)
	$effect(() => {
		if (isSrc) {
			const rs = app.srcRangeStart;
			const re = app.srcRangeEnd;
			if (rs != null && re != null && re > rs) {
				rangeStart = rs;
				rangeEnd = re;
			} else {
				rangeStart = 0;
				rangeEnd = 0;
			}
		}
	});

	function toggle() {
		playing = !playing;
	}

	function load() {
		app.name = song.name;
		setRequest({ ...song.request });
		app.pendingRequests = [];
		app.pendingIndex = 0;
	}

	let scanning = $state(false);

	// analyze audio: send to /understand, fill form with detected metadata.
	// persists the job under 'lm' key so page reload resumes polling.
	// Uploads cached latents when present so the server skips VAE encode where possible.
	// Understand enriches the analyzed card in place; cover/synth keep creating new cards.
	async function scan() {
		scanning = true;
		try {
			const jobId = await understandSubmit(
				song.latents ? null : song.audio,
				song.latents ?? null,
				app.request.lm_model as string,
				app.request.synth_model as string
			);
			saveJob('lm', jobId);
			await pollJob(jobId);
			const { requests, latents } = await jobResultUnderstand(jobId);
			clearJob('lm');
			if (song.id != null) {
				const newLatents = latents ?? song.latents ?? undefined;
				const newRequest =
					requests.length > 0 && !song.request.caption ? requests[0] : { ...song.request };
				const dirty = newLatents !== song.latents || newRequest !== song.request;
				if (dirty) {
					const enriched: Song = {
						id: song.id,
						name: song.name,
						format: song.format,
						created: song.created,
						caption: newRequest.caption ?? song.caption,
						seed: newRequest.seed ?? song.seed,
						duration: newRequest.duration ?? song.duration,
						request: newRequest,
						audio: song.audio,
						...(newLatents ? { latents: newLatents } : {})
					};
					await putSong(enriched);
					if (latents) song.latents = latents;
					if (newRequest !== song.request) song.request = newRequest;
				}
			}
			app.name = song.name;
			if (requests.length > 0) {
				setRequest(requests[0]);
			}
			app.pendingRequests = requests;
			app.pendingIndex = 0;
		} catch (e: unknown) {
			toast(e instanceof Error ? e.message : String(e));
		} finally {
			scanning = false;
		}
	}

	function downloadAudio() {
		const url = URL.createObjectURL(song.audio);
		const a = document.createElement('a');
		a.href = url;
		const safe = displayName.replace(/[\\/:*?"<>|\x00-\x1f]/g, '') || 'song';
		const ext = song.format.startsWith('wav') ? '.wav' : '.mp3';
		a.download = `${safe}${ext}`;
		a.click();
		URL.revokeObjectURL(url);
	}

	function downloadLatents() {
		if (!song.latents) return;
		const url = URL.createObjectURL(song.latents);
		const a = document.createElement('a');
		a.href = url;
		const safe = displayName.replace(/[\\/:*?"<>|\x00-\x1f]/g, '') || 'song';
		a.download = `${safe}.vae`;
		a.click();
		URL.revokeObjectURL(url);
	}

	async function encodeOnly() {
		if (song.latents || song.id == null) return;
		scanning = true;
		try {
			const jobId = await vaeEncode(song.audio, app.request);
			saveJob('lm', jobId);
			await pollJob(jobId);
			const latents = await jobResultLatents(jobId);
			clearJob('lm');
			const enriched: Song = {
				id: song.id,
				name: song.name,
				format: song.format,
				created: song.created,
				caption: song.caption,
				seed: song.seed,
				duration: song.duration,
				request: { ...song.request },
				audio: song.audio,
				latents
			};
			await putSong(enriched);
			song.latents = latents;
		} catch (e: unknown) {
			toast(e instanceof Error ? e.message : String(e));
		} finally {
			scanning = false;
		}
	}

	async function remove() {
		if (song.id == null) return;
		if (app.refSongId === song.id) app.refSongId = null;
		if (app.srcSongId === song.id) app.srcSongId = null;
		await deleteSong(song.id);
		const idx = app.songs.findIndex((s) => s.id === song.id);
		if (idx >= 0) app.songs.splice(idx, 1);
	}

	// Delete every track above this card (newer entries).
	async function removeAbove() {
		if (song.id == null) return;
		const idx = app.songs.findIndex((s) => s.id === song.id);
		if (idx <= 0) return;
		const victims = app.songs.slice(0, idx);
		for (const s of victims) {
			if (s.id == null) continue;
			if (app.refSongId === s.id) app.refSongId = null;
			if (app.srcSongId === s.id) app.srcSongId = null;
			await deleteSong(s.id);
		}
		app.songs.splice(0, idx);
	}

	// Delete every track below this card (older entries).
	async function removeBelow() {
		if (song.id == null) return;
		const idx = app.songs.findIndex((s) => s.id === song.id);
		if (idx < 0 || idx === app.songs.length - 1) return;
		const victims = app.songs.slice(idx + 1);
		for (const s of victims) {
			if (s.id == null) continue;
			if (app.refSongId === s.id) app.refSongId = null;
			if (app.srcSongId === s.id) app.srcSongId = null;
			await deleteSong(s.id);
		}
		app.songs.splice(idx + 1);
	}

	// MM:SS:XX (hundredths) for current position
	function fmtPos(s: number): string {
		const m = Math.floor(s / 60);
		const sec = Math.floor(s % 60);
		const cs = Math.floor((s * 100) % 100);
		return (
			String(m).padStart(2, '0') +
			':' +
			String(sec).padStart(2, '0') +
			':' +
			String(cs).padStart(2, '0')
		);
	}

	// MM:SS for total duration
	function fmtDur(s: number): string {
		const m = Math.floor(s / 60);
		const sec = Math.floor(s % 60);
		return String(m).padStart(2, '0') + ':' + String(sec).padStart(2, '0');
	}
</script>

<div class="song-card-scope">
	<Card variant="outlined">
		<div class="card-inner">
			<div class="top-row">
				<div class="play-btn" class:playing>
					<Button variant={playing ? 'tonal' : 'text'} iconType="full" onclick={toggle}>
						<Icon icon={playing ? iconStop : iconPlayArrow} />
					</Button>
				</div>
				<span class="song-name">{displayName}</span>
				<div class="actions">
					<Button variant="text" iconType="full" onclick={downloadAudio}>
						<Icon icon={iconDownload} />
					</Button>
					<Button
						variant="text"
						iconType="full"
						disabled={scanning || !!song.latents}
						onclick={encodeOnly}
					>
						<Icon icon={iconMemory} />
					</Button>
					<Button variant="text" iconType="full" disabled={!song.latents} onclick={downloadLatents}>
						<Icon icon={iconDownload} />
					</Button>
					<Button
						variant="text"
						iconType="full"
						title="Delete newer tracks (above this card)"
						onclick={removeAbove}
					>
						<Icon icon={iconVerticalAlignTop} />
					</Button>
					<Button
						variant="text"
						iconType="full"
						title="Delete older tracks (below this card)"
						onclick={removeBelow}
					>
						<Icon icon={iconVerticalAlignBottom} />
					</Button>
					<Button variant="text" iconType="full" onclick={remove}>
						<Icon icon={iconDelete} />
					</Button>
				</div>
			</div>

			<div class="waveform-block">
				<Waveform
					{song}
					bind:playing
					bind:time
					bind:dur
					selectable={isSrc}
					bind:rangeStart
					bind:rangeEnd
				/>
			</div>

			<div class="bottom-row">
				<span class="format-badge">{song.format.toUpperCase()}</span>
				{#if song.latents}
					<span class="format-badge">VAE</span>
				{/if}
				<span class="time-display">
					{fmtPos(time)} / {fmtDur(dur)}
				</span>
				<div class="bottom-actions">
					<Button variant="text" iconType="full" onclick={load}>
						<Icon icon={iconEdit} />
					</Button>
					<Button variant="text" iconType="full" disabled={scanning} onclick={scan}>
						<Icon icon={iconHearing} />
					</Button>
					<Chip variant="input" selected={isSrc} onclick={toggleSrc}>Src</Chip>
					<Chip variant="input" selected={isRef} onclick={toggleRef}>Ref</Chip>
				</div>
			</div>
		</div>
	</Card>
</div>

<style>
	/* Neutral chrome; waveform uses green in Waveform.svelte; chips use brand */
	.song-card-scope {
		--m3c-primary: var(--m3c-on-surface-variant);
		--m3c-primary-container: var(--m3c-surface-container-high);
		--m3c-on-primary-container: var(--m3c-on-surface);
	}
	.song-card-scope :global(button.m3-container.input) {
		--m3c-secondary: var(--ace-brand-secondary);
		--m3c-secondary-container: var(--ace-brand-secondary-container);
		--m3c-on-secondary-container: var(--ace-brand-on-secondary-container);
	}
	.waveform-block {
		display: flex;
		flex-direction: column;
		gap: 0.25rem;
		min-width: 0;
	}
	.card-inner {
		display: flex;
		flex-direction: column;
		gap: 0.25rem;
		padding: 0.5rem;
	}
	.top-row {
		display: flex;
		align-items: center;
		gap: 0.5rem;
		min-width: 0;
	}
	.play-btn {
		flex-shrink: 0;
	}
	.song-name {
		@apply --m3-body-medium;
		font-weight: 500;
		color: var(--m3c-on-surface);
		flex: 1;
		min-width: 0;
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
	}
	.actions {
		display: flex;
		flex-shrink: 0;
		align-items: center;
		gap: 0.125rem;
	}
	.bottom-row {
		display: flex;
		align-items: center;
		gap: 0.5rem;
		min-width: 0;
	}
	.format-badge {
		@apply --m3-label-small;
		background-color: var(--m3c-surface-container-high);
		color: var(--m3c-on-surface-variant);
		padding: 0.125rem 0.5rem;
		border-radius: var(--m3-shape-small);
		font-family: var(--m3-font-mono);
		flex-shrink: 0;
	}
	.time-display {
		@apply --m3-label-small;
		font-family: var(--m3-font-mono);
		color: var(--m3c-on-surface-variant);
		flex: 1;
		min-width: 0;
		white-space: nowrap;
	}
	.bottom-actions {
		display: flex;
		align-items: center;
		gap: 0.25rem;
		flex-shrink: 0;
	}
</style>
