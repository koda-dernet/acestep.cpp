<script lang="ts">
	import { Button, Card, Chip, Icon } from 'm3-svelte';
	import iconPlayArrow from '@ktibow/iconset-material-symbols/play-arrow';
	import iconStop from '@ktibow/iconset-material-symbols/stop';
	import iconEdit from '@ktibow/iconset-material-symbols/edit';
	import iconDownload from '@ktibow/iconset-material-symbols/download';
	import iconDelete from '@ktibow/iconset-material-symbols/delete';
	import { app, setRequest } from '../lib/state.svelte.js';
	import { deleteSong } from '../lib/db.js';
	import type { Song } from '../lib/types.js';
	import Waveform from './Waveform.svelte';

	let { song }: { song: Song } = $props();

	let playing = $state(false);
	let time = $state(0);
	let dur = $state(0);
	let rangeStart = $state(-1);
	let rangeEnd = $state(-1);

	let isRef = $derived(app.refSongId === song.id);
	let isSrc = $derived(app.srcSongId === song.id);

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
			app.srcRangeStart = -1;
			app.srcRangeEnd = -1;
			rangeStart = -1;
			rangeEnd = -1;
		} else {
			app.srcSongId = song.id ?? null;
		}
	}

	// sync local range to global when this song is the src
	$effect(() => {
		if (isSrc) {
			app.srcRangeStart = rangeStart;
			app.srcRangeEnd = rangeEnd;
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

	function downloadAudio() {
		const url = URL.createObjectURL(song.audio);
		const a = document.createElement('a');
		a.href = url;
		const safe = song.name.replace(/[^a-zA-Z0-9 _-]/g, '') || 'song';
		const ext = song.format === 'wav' ? '.wav' : '.mp3';
		a.download = `${safe}_${song.seed}${ext}`;
		a.click();
		URL.revokeObjectURL(url);
	}

	async function remove() {
		if (song.id == null) return;
		if (app.refSongId === song.id) app.refSongId = null;
		if (app.srcSongId === song.id) app.srcSongId = null;
		await deleteSong(song.id);
		const idx = app.songs.findIndex((s) => s.id === song.id);
		if (idx >= 0) app.songs.splice(idx, 1);
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
				<Button
					variant={playing ? 'tonal' : 'text'}
					iconType="full"
					onclick={toggle}
				>
					<Icon icon={playing ? iconStop : iconPlayArrow} />
				</Button>
			</div>
			<span class="song-name">{song.name}</span>
			<div class="actions">
				<Button variant="text" iconType="full" onclick={downloadAudio}>
					<Icon icon={iconDownload} />
				</Button>
				<Button variant="text" iconType="full" onclick={remove}>
					<Icon icon={iconDelete} />
				</Button>
			</div>
		</div>

		<div class="waveform-block">
			<Waveform
				audio={song.audio}
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
			<span class="time-display">
				{fmtPos(time)} / {fmtDur(dur)}
			</span>
			<div class="bottom-actions">
				<Button variant="text" iconType="full" onclick={load}>
					<Icon icon={iconEdit} />
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
