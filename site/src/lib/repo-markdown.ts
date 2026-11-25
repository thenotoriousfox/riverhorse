import {readFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createMarkdownProcessor} from '@astrojs/markdown-remark';

const REPO = resolve(process.cwd(), '..');
const REPO_BLOB = 'https://github.com/thenotoriousfox/riverhorse/blob/main/';

const processor = await createMarkdownProcessor({gfm: true});

export async function readRepoFile(name: string): Promise<string> {
	return readFile(resolve(REPO, name), 'utf-8');
}

export async function renderMarkdown(markdown: string): Promise<string> {
	const linked = markdown.replace(/\]\((?!https?:|#)([^)]+)\)/g, `](${REPO_BLOB}$1)`);
	return (await processor.render(linked)).code;
}

export async function readmeSections(names: readonly string[]): Promise<string> {
	const readme = await readRepoFile('README.md');
	const sections = new Map<string, string>();
	for (const part of readme.split(/\n(?=## )/)) {
		const heading = part.match(/^## (?:[\p{Extended_Pictographic}ℹ]️?\s)?(.+)/u);
		if (heading) {
			const body = part.slice(part.indexOf('\n')).replace(/\n---\s*$/, '');
			sections.set(heading[1].trim(), `## ${heading[1].trim()}${body}`);
		}
	}
	return names.map(name => {
		const section = sections.get(name);
		if (!section) throw new Error(`README.md has no "${name}" section`);
		return section;
	}).join('\n\n');
}
