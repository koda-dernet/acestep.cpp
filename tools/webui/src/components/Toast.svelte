<script lang="ts">
	import { Icon } from 'm3-svelte';
	import iconCheckCircle from '@ktibow/iconset-material-symbols/check-circle';
	import iconError from '@ktibow/iconset-material-symbols/error';
	import { app } from '../lib/state.svelte.js';
</script>

{#if app.toast}
	<div class="toast" class:ok={app.toastOk} class:err={!app.toastOk}>
		<Icon icon={app.toastOk ? iconCheckCircle : iconError} size={18} />
		<span>{app.toast}</span>
	</div>
{/if}

<style>
	.toast {
		pointer-events: none;
		position: fixed;
		bottom: 1.5rem;
		left: 50%;
		z-index: 9999;
		display: flex;
		max-width: min(100vw - 2rem, 28rem);
		align-items: center;
		gap: 0.625rem;
		border-radius: var(--m3-shape-small);
		padding: 0.75rem 1.25rem;
		@apply --m3-body-medium;
		box-shadow: var(--m3-elevation-3);
		animation: ace-toast-in 0.2s var(--m3-timing-function-fast-spatial) forwards;
	}
	.toast.ok {
		background-color: var(--m3c-primary-container);
		color: var(--m3c-on-primary-container);
	}
	.toast.err {
		background-color: var(--m3c-inverse-surface);
		color: var(--m3c-inverse-on-surface);
	}
</style>
