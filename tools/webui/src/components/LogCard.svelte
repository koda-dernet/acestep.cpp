<script lang="ts">
	import { Icon } from 'm3-svelte';
	import iconExpandMore from '@ktibow/iconset-material-symbols/expand-more';
	import { app } from '../lib/state.svelte.js';
	import { SSE_RECONNECT_MS, LOG_MAX_LINES } from '../lib/config.js';

	let lines = $state<string[]>([]);
	let logEl: HTMLPreElement | undefined = $state();

	$effect(() => {
		let es: EventSource | null = null;
		let timer = 0;

		function connect() {
			es = new EventSource('logs');
			es.onmessage = (e: MessageEvent) => {
				lines.push(e.data);
				if (lines.length > LOG_MAX_LINES) lines.splice(0, lines.length - LOG_MAX_LINES);
				if (logEl) logEl.scrollTop = logEl.scrollHeight;
			};
			es.onerror = () => {
				es?.close();
				es = null;
				lines.push('[Client] Server unavailable');
				if (lines.length > LOG_MAX_LINES) lines.splice(0, lines.length - LOG_MAX_LINES);
				timer = setTimeout(connect, SSE_RECONNECT_MS) as unknown as number;
			};
		}

		connect();
		return () => {
			clearTimeout(timer);
			es?.close();
		};
	});
</script>

<div class="log-card">
	<button
		class="log-header"
		onclick={() => (app.logsOpen = !app.logsOpen)}
		type="button"
	>
		<span class="chevron" class:open={app.logsOpen}>
			<Icon icon={iconExpandMore} size={16} />
		</span>
		Server logs
	</button>
	<div class="log-expand" class:open={app.logsOpen}>
		<div class="log-inner">
			<pre class="log-body" bind:this={logEl}>{lines.join('\n')}</pre>
		</div>
	</div>
</div>

<style>
	.log-card {
		display: flex;
		flex-direction: column;
		border-radius: var(--m3-shape-medium);
		background: var(--m3c-surface-container);
		overflow: hidden;
	}
	.log-header {
		display: flex;
		align-items: center;
		gap: 0.5rem;
		padding: 0.625rem 1rem;
		border: none;
		cursor: pointer;
		user-select: none;
		@apply --m3-title-small;
		color: var(--m3c-on-surface);
		background: var(--m3c-surface-container-high);
		text-align: left;
	}
	.log-header:hover {
		filter: brightness(1.1);
	}
	.chevron {
		display: inline-flex;
		transition:
			color var(--m3-easing-fast),
			transform var(--m3-easing-fast);
	}
	.chevron.open {
		transform: rotate(180deg);
	}

	/* Match SelectOutlined ::picker(select) open timing (500ms height, emphasized decel) */
	.log-expand {
		display: grid;
		grid-template-rows: 0fr;
		overflow: hidden;
		transition:
			grid-template-rows 500ms var(--m3-timing-function-emphasized-decel),
			opacity 150ms var(--m3-easing-fast);
	}
	.log-expand.open {
		grid-template-rows: 1fr;
	}
	.log-inner {
		min-height: 0;
		overflow: hidden;
	}
	.log-body {
		margin: 0;
		padding: 0.5rem 0.75rem;
		overflow-x: auto;
		max-height: 20rem;
		overflow-y: auto;
		font-family: 'JetBrains Mono', var(--m3-font-mono);
		@apply --m3-body-small;
		line-height: 1.45;
		white-space: pre;
		color: var(--m3c-on-surface-variant);
	}
</style>
