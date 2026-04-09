import { defineConfig } from 'vite';
import { functionsMixins } from 'vite-plugin-functions-mixins';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';
import { execSync } from 'child_process';
import { readFileSync, writeFileSync, mkdirSync } from 'fs';
import { gzipSync } from 'zlib';
import { resolve } from 'path';

// Dev proxy target: must match the ace-server port (server.cmd / server.sh use 8085; binary default is 8080).
const ACE_SERVER = process.env.ACE_SERVER_URL ?? 'http://127.0.0.1:8085';

// git version baked at build time (same format as C++ ACE_VERSION)
function gitVersion(): string {
	try {
		const hash = execSync('git rev-parse --short HEAD', { cwd: resolve(__dirname, '../..') })
			.toString()
			.trim();
		const date = execSync('git show -s --format=%cs HEAD', { cwd: resolve(__dirname, '../..') })
			.toString()
			.trim();
		return `${hash} (${date})`;
	} catch {
		return 'unknown';
	}
}

// deterministic gzip of the inlined index.html into ../public/index.html.gz.
// the .gz is committed to git so the C++ build works without npm.
// gzip timestamp and OS bytes are zeroed for reproducible output.
function aceGzipPlugin() {
	return {
		name: 'ace:gzip',
		apply: 'build' as const,
		closeBundle() {
			const indexPath = resolve(__dirname, 'dist', 'index.html');
			const publicDir = resolve(__dirname, '..', 'public');
			const gzPath = resolve(publicDir, 'index.html.gz');

			const html = readFileSync(indexPath, 'utf-8');

			const compressed = gzipSync(Buffer.from(html, 'utf-8'), { level: 9 });

			// zero gzip header fields that vary between builds
			compressed[4] = 0; // mtime
			compressed[5] = 0;
			compressed[6] = 0;
			compressed[7] = 0;
			compressed[9] = 0; // OS

			mkdirSync(publicDir, { recursive: true });
			writeFileSync(gzPath, compressed);

			console.log(
				`  index.html: ${html.length} bytes -> index.html.gz: ${compressed.length} bytes`
			);
		}
	};
}

export default defineConfig({
	// functionsMixins: @apply mixins in m3-svelte <style> (must run before svelte())
	plugins: [functionsMixins({ deps: ['m3-svelte'] }), svelte(), viteSingleFile(), aceGzipPlugin()],

	define: {
		__ACE_VERSION__: JSON.stringify(gitVersion())
	},

	// dev server: proxy ace-server endpoints
	server: {
		// allow tunnels (ngrok, etc.); default only allows localhost and *.localhost
		allowedHosts: ['.ngrok-free.dev', '.ngrok-free.app', '.ngrok.io', '.loca.lt', '.trycloudflare.com'],
		proxy: {
			'/lm': ACE_SERVER,
			'/synth': ACE_SERVER,
			'/understand': ACE_SERVER,
			'/health': ACE_SERVER,
			'/props': ACE_SERVER,
			'/logs': ACE_SERVER
		}
	},

	build: {
		assetsInlineLimit: Infinity,
		cssCodeSplit: false
	}
});
