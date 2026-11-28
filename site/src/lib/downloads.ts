export interface ReleaseAsset {
	name: string;
	download_count: number;
}

export interface Release {
	tag_name: string;
	published_at: string;
	draft: boolean;
	prerelease: boolean;
	assets: ReleaseAsset[];
}

export const RELEASES_API = 'https://api.github.com/repos/thenotoriousfox/riverhorse/releases?per_page=100';

const PLATFORMS: {name: string; matches: (file: string) => boolean}[] = [
	{name: 'Linux (AppImage)', matches: file => file.endsWith('.AppImage')},
	{name: 'Linux (Flatpak)', matches: file => file.endsWith('.flatpak')},
	{name: 'Windows', matches: file => file.startsWith('riverhorse-windows-') && file.endsWith('-release.zip')},
	{name: 'macOS', matches: file => file.startsWith('riverhorse-mac-')},
	{name: 'RetroArch core', matches: file => file.startsWith('riverhorse_libretro-')},
];

export const PLATFORM_NAMES = PLATFORMS.map(platform => platform.name);

export interface ReleaseDownloads {
	tag: string;
	date: string;
	total: number;
	byPlatform: number[];
}

export interface DownloadSummary {
	total: number;
	byPlatform: number[];
	releases: ReleaseDownloads[];
}

export function summariseDownloads(releases: Release[]): DownloadSummary {
	const rows = releases
		.filter(release => !release.draft && !release.prerelease && /^v\d/.test(release.tag_name))
		.sort((a, b) => b.published_at.localeCompare(a.published_at))
		.map(release => {
			const byPlatform = PLATFORMS.map(platform =>
				release.assets.filter(asset => platform.matches(asset.name)).reduce((sum, asset) => sum + asset.download_count, 0));
			return {
				tag: release.tag_name,
				date: release.published_at.slice(0, 10),
				total: byPlatform.reduce((sum, count) => sum + count, 0),
				byPlatform,
			};
		});

	return {
		total: rows.reduce((sum, row) => sum + row.total, 0),
		byPlatform: PLATFORMS.map((_, index) => rows.reduce((sum, row) => sum + row.byPlatform[index], 0)),
		releases: rows,
	};
}

const number = (value: number) => value.toLocaleString('en');

export function renderDownloads(summary: DownloadSummary): string {
	const platformRows = PLATFORM_NAMES.map((name, index) => {
		const count = summary.byPlatform[index];
		const share = summary.total ? Math.round((count / summary.total) * 100) : 0;
		return `<tr><td>${name}</td><td class="num">${number(count)}</td><td class="num">${share}%</td></tr>`;
	}).join('');

	const releaseRows = summary.releases.map(release =>
		`<tr><td><a href="https://github.com/thenotoriousfox/riverhorse/releases/tag/${release.tag}">${release.tag}</a></td>`
		+ `<td>${release.date}</td><td class="num"><strong>${number(release.total)}</strong></td>`
		+ release.byPlatform.map(count => `<td class="num">${number(count)}</td>`).join('') + '</tr>').join('');

	return `<p class="stat-total"><strong>${number(summary.total)}</strong> downloads across ${summary.releases.length} releases</p>`
		+ `<h2>By platform</h2><table><thead><tr><th>Platform</th><th class="num">Downloads</th><th class="num">Share</th></tr></thead><tbody>${platformRows}</tbody></table>`
		+ `<h2>By release</h2><table><thead><tr><th>Release</th><th>Date</th><th class="num">Total</th>`
		+ PLATFORM_NAMES.map(name => `<th class="num">${name}</th>`).join('')
		+ `</tr></thead><tbody>${releaseRows}</tbody></table>`;
}

export async function fetchDownloadSummary(): Promise<DownloadSummary | null> {
	try {
		const response = await fetch(RELEASES_API, {headers: {Accept: 'application/vnd.github+json'}});
		return response.ok ? summariseDownloads(await response.json()) : null;
	} catch {
		return null;
	}
}
