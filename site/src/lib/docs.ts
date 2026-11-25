export interface DocPage {
	slug: string;
	title: string;
	summary: string;
	sections: string[];
}

export const DOCS: DocPage[] = [
	{slug: 'getting-started', title: 'Getting started', summary: 'The BIOS, where your settings and saves live, and Wayland or X11.',
		sections: ['BIOS', 'User directory', 'Wayland and X11']},
	{slug: 'libretro-core', title: 'libretro core', summary: 'Installing and building the core for RetroArch and other frontends.',
		sections: ['libretro core']},
	{slug: 'netplay', title: 'Netplay', summary: 'Playing with up to four people online, session codes, the relay and frontend events.',
		sections: ['Netplay']},
	{slug: 'save-sync', title: 'Save sync', summary: 'Keeping memory cards, save states and game settings the same on every device.',
		sections: ['Save sync']},
	{slug: 'building', title: 'Building from source', summary: 'Building Riverhorse on Linux and Windows.',
		sections: ['Building on Linux', 'Building on Windows']},
];
