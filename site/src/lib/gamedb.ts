import {parse} from 'yaml';
import {readRepoFile} from './repo-markdown';

interface DatabaseEntry {
	name: string;
	compatibility?: {rating?: string};
	traits?: string[];
	libcrypt?: boolean;
}

export interface CompatibilityRow {
	title: string;
	serial: string;
	problem?: string;
	needsSbi: boolean;
	adjustments: string[];
}

const PROBLEMS: Record<string, string> = {
	CrashesInGame: 'Crashes during play',
	GraphicalAudioIssues: 'Graphics or sound problems',
	CrashesInIntro: 'Crashes at the intro',
	DoesntBoot: "Doesn't start",
};

const ADJUSTMENTS: Record<string, string> = {
	DisableWidescreen: 'Widescreen hack off',
	ForcePGXPCPUMode: 'PGXP CPU mode',
	ForceRecompilerICache: 'Instruction cache emulated',
	DisableTextureFiltering: 'Texture filtering off',
	ForceRecompilerLUTFastmem: 'Slower memory access mode',
	ForceInterlacing: 'Interlacing kept',
	ForceInterpreter: 'CPU interpreter',
	DisablePGXPDepthBuffer: 'PGXP depth buffer off',
	ForceRoundTextureCoordinates: 'Rounded texture coordinates',
	DisableUpscaling: 'Upscaling off',
	DisablePGXP: 'PGXP off',
	ForcePGXPVertexCache: 'PGXP vertex cache',
	ForceSoftwareRenderer: 'Software renderer',
	DisablePGXPPreserveProjFP: 'PGXP projection precision off',
	DisablePGXPCulling: 'PGXP culling off',
	DisableTrueColor: 'True colour off',
	ForceSoftwareRendererForReadbacks: 'Software renderer for readbacks',
	DisablePGXPColorCorrection: 'PGXP colour correction off',
	DisablePGXPTextureCorrection: 'PGXP texture correction off',
	DisablePGXPOn2DPolygons: 'PGXP off for 2D',
	ForceAccurateBlending: 'Accurate blending',
	DisableScaledDithering: 'Scaled dithering off',
	DisableForceNTSCTimings: 'Original PAL timing kept',
	NormalizePGXPDepth: 'PGXP depth normalised',
	ForceRecompilerMemoryExceptions: 'Memory exceptions emulated',
	ForceCDROMSubQSkew: 'CD subchannel timing',
	DisableCDROMSpeedupOnMDEC: 'No CD speed-up during videos',
	DisableCDROMReadSpeedup: 'No CD read speed-up',
};

export async function compatibilityRows(): Promise<CompatibilityRow[]> {
	const database = parse(await readRepoFile('data/resources/gamedb.yaml')) as Record<string, DatabaseEntry>;
	const rows: CompatibilityRow[] = [];
	for (const [serial, entry] of Object.entries(database)) {
		const rating = entry.compatibility?.rating;
		const problem = rating && rating !== 'NoIssues' && rating !== 'Unknown' ? PROBLEMS[rating] ?? rating : undefined;
		const adjustments = (entry.traits ?? []).map(trait => {
			const label = ADJUSTMENTS[trait];
			if (!label) throw new Error(`gamedb.yaml trait "${trait}" needs a label in site/src/lib/gamedb.ts`);
			return label;
		});
		const needsSbi = entry.libcrypt === true;
		if (problem || needsSbi || adjustments.length) {
			rows.push({title: entry.name, serial, problem, needsSbi, adjustments});
		}
	}
	return rows.sort((a, b) => a.title.localeCompare(b.title, 'en'));
}
