// @ts-check
import {defineConfig} from 'astro/config';
import sitemap from '@astrojs/sitemap';

export default defineConfig({
	site: 'https://riverhorse.app',
	integrations: [sitemap()],
	build: {format: 'file'},
});
