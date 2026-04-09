<script lang="ts">
	// TextFieldOutlined binds a string; AceRequest fields are number | undefined.
	import { TextFieldOutlined } from 'm3-svelte';

	let { label, value = $bindable<number | undefined>() } = $props<{
		label: string;
		value?: number | undefined;
	}>();

	let str = $state('');
	let focused = $state(false);

	$effect(() => {
		if (!focused) str = value == null ? '' : String(value);
	});

	function syncToModel() {
		const t = str.trim();
		if (t === '') {
			value = undefined;
			return;
		}
		const n = Number(str);
		if (!Number.isNaN(n)) value = n;
	}
</script>

<TextFieldOutlined
	{label}
	bind:value={str}
	onfocus={() => (focused = true)}
	onblur={() => {
		focused = false;
		syncToModel();
		str = value == null ? '' : String(value);
	}}
	oninput={syncToModel}
/>
