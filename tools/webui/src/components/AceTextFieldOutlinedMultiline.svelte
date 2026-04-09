<script lang="ts">
	import type { IconifyIcon } from '@iconify/types';
	import { Icon } from 'm3-svelte';
	import type { HTMLTextareaAttributes } from 'svelte/elements';

	let {
		label,
		leadingIcon,
		disabled = false,
		required = false,
		error = false,
		value = $bindable(),
		...extra
	}: {
		label: string;
		leadingIcon?: IconifyIcon;
		disabled?: boolean;
		required?: boolean;
		error?: boolean;
		value?: string;
	} & HTMLTextareaAttributes = $props();
	const id = $props.id();
</script>

<!--
  Fork of m3-svelte TextFieldOutlinedMultiline without auto-growing height.
  Shell padding keeps the floated label clear of the previous field’s border.
  Inner box defaults to 1:1, vertically resizable; text scrolls inside.
-->
<div class="ace-multiline-shell">
	<div class="ace-multiline-root">
		<div
			class="m3-container"
			class:leading-icon={leadingIcon}
			class:error
		>
			<textarea placeholder=" " bind:value {id} {disabled} {required} {...extra}></textarea>
			<div class="layer"></div>
			<label for={id}>{label}</label>
			{#if leadingIcon}
				<Icon icon={leadingIcon} size={24} />
			{/if}
		</div>
	</div>
</div>

<style>
	@layer tokens {
		:root {
			--m3-field-outlined-shape: var(--m3-shape-extra-small);
		}
	}

	.ace-multiline-shell {
		width: 100%;
		max-width: 100%;
		/* Space above the outline so the label notch isn’t flush against the prior field */
		padding-top: 0.5rem;
		box-sizing: border-box;
	}

	/* overflow must stay visible: floated labels sit on the border and paint above the box;
	   overflow:auto was clipping the top half of the label text. */
	.ace-multiline-root {
		width: 100%;
		max-width: 100%;
		aspect-ratio: 1 / 1;
		min-width: 10rem;
		min-height: 10rem;
		max-height: min(92vh, 48rem);
		resize: vertical;
		overflow: visible;
		display: block;
		box-sizing: border-box;
	}

	.m3-container {
		display: inline-flex;
		position: relative;
		align-items: center;
		width: 100%;
		height: 100%;
		min-height: 0;
		min-width: 0;
		z-index: 0;
	}

	textarea {
		@apply --m3-body-large;
		@apply --m3-focus-none;
		position: absolute;
		inset: 0;
		z-index: 1;
		width: 100%;
		height: 100%;
		border: none;
		padding: 1rem;
		border-radius: var(--m3-field-outlined-shape);
		background-color: transparent;
		color: var(--m3c-on-surface);
		resize: none;
		overflow: auto;
		box-sizing: border-box;
	}

	label {
		@apply --m3-body-large;
		position: absolute;
		z-index: 2;
		inset-inline-start: 0.75rem;
		top: 50%;
		translate: 0 -50%;
		color: var(--error, var(--m3c-on-surface-variant));
		background-color: var(--m3v-background, var(--m3c-surface));
		padding: 0 0.375rem;
		textarea:hover ~ & {
			color: var(--error, var(--m3c-on-surface));
		}
		textarea:focus ~ & {
			color: var(--error, var(--m3c-primary));
		}
		textarea:disabled ~ & {
			color: --translucent(var(--m3c-on-surface), 0.38);
		}
		textarea:focus ~ &,
		textarea:not(:placeholder-shown) ~ & {
			@apply --m3-body-small;
			/* Slight inset so ascenders aren’t flush against the clip edge of ancestors (.panel scroll). */
			top: 0.3125rem;
		}
		pointer-events: none;
		transition:
			color 100ms,
			top 100ms,
			font-size 300ms,
			line-height 300ms,
			letter-spacing 300ms;
	}

	.layer {
		position: absolute;
		inset: 0;
		z-index: 0;
		border: 1px solid var(--error, var(--m3c-outline));
		border-radius: var(--m3-field-outlined-shape);
		pointer-events: none;
		transition: all 100ms;
		textarea:enabled:hover ~ & {
			border-color: var(--error, var(--m3c-on-surface));
		}
		textarea:enabled:focus ~ & {
			border-color: var(--error, var(--m3c-primary));
			border-width: 0.125rem;
		}
	}

	.m3-container > :global(svg) {
		position: relative;
		margin-inline-start: 0.75rem;
		color: var(--m3c-on-surface-variant);
		pointer-events: none;
	}

	.leading-icon > textarea {
		padding-inline-start: 3.25rem;
	}

	.leading-icon > textarea:not(:focus):placeholder-shown ~ label {
		inset-inline-start: 3rem;
	}

	.error {
		--error: var(--m3c-error);
	}

	.error > textarea:hover ~ label,
	.error > textarea:hover ~ .layer {
		--error: var(--m3c-on-error-container);
	}

	textarea:disabled {
		color: --translucent(var(--m3c-on-surface), 0.38);
	}

	textarea:disabled ~ .layer {
		border-color: --translucent(var(--m3c-on-surface), 0.38);
	}

	textarea:disabled ~ :global(svg) {
		color: --translucent(var(--m3c-on-surface), 0.38);
	}

	.m3-container {
		print-color-adjust: exact;
	}
</style>
