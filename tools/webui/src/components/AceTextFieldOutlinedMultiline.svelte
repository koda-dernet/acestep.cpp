<script lang="ts">
	import type { HTMLTextareaAttributes } from 'svelte/elements';

	let {
		label,
		disabled = false,
		required = false,
		error = false,
		value = $bindable(),
		...extra
	}: {
		label: string;
		disabled?: boolean;
		required?: boolean;
		error?: boolean;
		value?: string;
	} & HTMLTextareaAttributes = $props();

	const uid = $props.id();
	const inputId = `${uid}-input`;
</script>

<!--
  Outlined multiline without M3 auto-grow: fixed frame, title inside top of box
  (same idea as Language/BPM labels, not a border “notch”).
-->
<div class="ace-multiline-field">
	<div class="ace-multiline-root">
		<div class="m3-container ace-multiline-framed" class:error>
			<div class="layer"></div>
			<div class="ace-multiline-stack">
				<div class="ace-multiline-head">
					<label class="ace-multiline-head-label" for={inputId}>{label}</label>
				</div>
				<textarea
					id={inputId}
					class="ace-multiline-body"
					placeholder=""
					bind:value
					{disabled}
					{required}
					{...extra}
				></textarea>
			</div>
		</div>
	</div>
</div>

<style>
	@layer tokens {
		:root {
			--m3-field-outlined-shape: var(--m3-shape-extra-small);
		}
	}

	.ace-multiline-field {
		width: 100%;
		max-width: 100%;
		min-width: 0;
	}

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

	.m3-container.ace-multiline-framed {
		display: flex;
		flex-direction: column;
		position: relative;
		width: 100%;
		height: 100%;
		min-height: 0;
		min-width: 0;
		overflow: hidden;
		border-radius: var(--m3-field-outlined-shape);
		z-index: 0;
	}

	.ace-multiline-stack {
		position: relative;
		z-index: 1;
		display: flex;
		flex-direction: column;
		flex: 1 1 auto;
		min-height: 0;
		min-width: 0;
		height: 100%;
	}

	.ace-multiline-head {
		flex: 0 0 auto;
		padding: 0.5rem 0.75rem 0.25rem;
	}

	.ace-multiline-head-label {
		@apply --m3-label-large;
		color: var(--error, var(--m3c-on-surface-variant));
		margin: 0;
		display: block;
		cursor: text;
		/* Same motion as M3 TextFieldOutlined label color (floating label uses extra top/size keys) */
		transition: color 100ms;
	}

	.ace-multiline-body {
		@apply --m3-body-large;
		@apply --m3-focus-none;
		flex: 1 1 auto;
		min-height: 0;
		width: 100%;
		border: none;
		padding: 0.25rem 0.75rem 0.75rem;
		background-color: transparent;
		color: var(--m3c-on-surface);
		resize: none;
		overflow: auto;
		box-sizing: border-box;
		line-height: 1.45;
	}

	.layer {
		position: absolute;
		inset: 0;
		z-index: 0;
		border: 1px solid var(--error, var(--m3c-outline));
		border-radius: var(--m3-field-outlined-shape);
		pointer-events: none;
		transition: all 100ms;
	}

	.m3-container:not(.error):not(:has(.ace-multiline-body:disabled)):hover .layer {
		border-color: var(--error, var(--m3c-on-surface));
	}

	.m3-container:not(.error):focus-within .layer {
		border-color: var(--error, var(--m3c-primary));
		border-width: 0.125rem;
	}

	.m3-container:not(.error):has(.ace-multiline-body:enabled):hover .ace-multiline-head-label {
		color: var(--error, var(--m3c-on-surface));
	}

	.m3-container:not(.error):focus-within .ace-multiline-head-label {
		color: var(--error, var(--m3c-primary));
	}

	.error {
		--error: var(--m3c-error);
	}

	.error:not(:has(.ace-multiline-body:disabled)):hover .layer,
	.error:focus-within .layer {
		--error: var(--m3c-on-error-container);
	}

	.error:not(:has(.ace-multiline-body:disabled)):hover .ace-multiline-head-label {
		--error: var(--m3c-on-error-container);
	}

	.ace-multiline-body:disabled {
		color: --translucent(var(--m3c-on-surface), 0.38);
	}

	.m3-container:has(.ace-multiline-body:disabled) .ace-multiline-head-label {
		color: --translucent(var(--m3c-on-surface), 0.38);
	}

	.m3-container:has(.ace-multiline-body:disabled) .layer {
		border-color: --translucent(var(--m3c-on-surface), 0.38);
	}

	.m3-container.ace-multiline-framed {
		print-color-adjust: exact;
	}
</style>
