<script lang="ts">
	import { Button, Icon, Slider } from 'm3-svelte';
	import iconLightMode from '@ktibow/iconset-material-symbols/light-mode';
	import iconDarkMode from '@ktibow/iconset-material-symbols/dark-mode';
	import iconVolumeUp from '@ktibow/iconset-material-symbols/volume-up';
	import iconVolumeDown from '@ktibow/iconset-material-symbols/volume-down';
	import iconVolumeOff from '@ktibow/iconset-material-symbols/volume-off';
	import { app } from './lib/state.svelte.js';
	import { props } from './lib/api.js';
	import { getAllSongs } from './lib/db.js';
	import { PROPS_POLL_MS } from './lib/config.js';
	import RequestForm from './components/RequestForm.svelte';
	import SongList from './components/SongList.svelte';
	import Toast from './components/Toast.svelte';

	// boot: load songs from IndexedDB
	$effect(() => {
		getAllSongs()
			.then((songs) => (app.songs = songs.reverse()))
			.catch(() => {});
	});

	// poll /props every PROPS_POLL_MS, null on failure (grey badges)
	function pollProps() {
		props()
			.then((h) => (app.props = h))
			.catch(() => (app.props = null));
	}

	$effect(() => {
		pollProps();
		const id = setInterval(pollProps, PROPS_POLL_MS);
		return () => clearInterval(id);
	});

	function toggleDark() {
		app.dark = !app.dark;
	}

	let volumeIcon = $derived(
		app.volume === 0 ? iconVolumeOff : app.volume < 0.5 ? iconVolumeDown : iconVolumeUp
	);

	// sync dark/light on documentElement (colorScheme for M3 light-dark tokens)
	$effect(() => {
		document.documentElement.style.colorScheme = app.dark ? 'dark' : 'light';
	});
</script>

<div class="root">
	<header class="header">
		<div class="header-brand">
			<span class="app-name">acestep.cpp</span>
			<span class="app-version">{__ACE_VERSION__}</span>
		</div>

		<div class="spacer"></div>

		<div class="volume-control">
			<Icon icon={volumeIcon} size={18} />
			<div class="volume-slider">
				<Slider bind:value={app.volume} min={0} max={1} step="any" size="xs" showValue={false} endStops={false} />
			</div>
		</div>

		<Button variant="text" iconType="full" onclick={toggleDark}>
			<Icon icon={app.dark ? iconLightMode : iconDarkMode} />
		</Button>
	</header>

	<main class="main">
		<section class="panel panel-form">
			<RequestForm />
		</section>
		<section class="panel panel-songs">
			<SongList />
		</section>
	</main>
</div>

<Toast />

<style>
	.root {
		display: flex;
		flex-direction: column;
		min-height: 100dvh;
		background: var(--m3c-surface);
	}
	.header {
		display: flex;
		align-items: center;
		gap: 0.5rem;
		padding: 0.25rem 1rem;
		background: var(--m3c-surface-container-low);
		flex-shrink: 0;
	}
	.header-brand {
		display: flex;
		align-items: baseline;
		gap: 0.35rem;
		flex-wrap: wrap;
		min-width: 0;
	}
	.app-name {
		@apply --m3-title-medium;
		font-weight: 700;
		color: var(--m3c-on-surface);
		line-height: 1.2;
	}
	.app-version {
		font-size: 0.6875rem;
		line-height: 1.15;
		font-weight: 500;
		color: var(--m3c-on-surface-variant);
		letter-spacing: 0.02em;
	}
	.spacer {
		flex: 1;
	}
	.volume-control {
		display: flex;
		align-items: center;
		gap: 0.25rem;
		color: var(--m3c-on-surface-variant);
	}
	.volume-slider {
		width: 10rem;
		transform: scaleY(0.65);
		--m3c-primary: var(--m3c-on-surface-variant);
	}
	.volume-slider :global(.m3-container) {
		min-inline-size: 0 !important;
	}

	.main {
		display: flex;
		flex: 1;
		min-height: 0;
		gap: 1rem;
		padding: 0 1rem 1rem;
		overflow: hidden;
		background: var(--m3c-surface);
	}
	.panel {
		min-height: 0;
		overflow-y: auto;
		overflow-x: hidden;
		/* Lets M3 outlined labels (slightly above the field box) paint inside the panel without clipping */
		padding-top: 0.25rem;
		box-sizing: border-box;
	}
	.panel-form {
		width: 400px;
		flex-shrink: 0;
	}
	.panel-songs {
		flex: 1;
		min-width: 0;
	}
	@media (max-width: 800px) {
		.main {
			flex-direction: column;
		}
		.panel-form {
			max-width: none;
			width: 100%;
			flex: none;
		}
	}
</style>
