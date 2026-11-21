# Notice

Riverhorse is a modified version of DuckStation.

DuckStation
Copyright (C) 2019-2024 Connor McLaughlin and contributors
https://github.com/stenzek/duckstation

Riverhorse is based on DuckStation release `v0.1-7371`, commit
`b2577ef8bdaae99840874b6dae8146f0316357b4`, distributed under the GNU General
Public License v3. Source files in that release are marked
`GPL-3.0 OR CC-BY-NC-ND-4.0`; Riverhorse uses them under the GPL-3.0 option
only, and files it changes are marked `GPL-3.0-only`. All files from that release keep their original copyright
and licence notices. Files changed substantially by Riverhorse carry an
additional Riverhorse copyright line.

Modifications
Copyright (C) 2026 Riverhorse contributors

The people credited by DuckStation are listed in [CONTRIBUTORS.md](CONTRIBUTORS.md),
which is kept as published in `v0.1-7371`.

No code, data, shaders, translations or artwork from DuckStation releases
after `v0.1-7371` are included. Later DuckStation releases are under a
different licence and must not be copied into this repository.

## Code imported from other projects

Code taken from other GPL-compatible projects is listed here with its source,
version and licence, and keeps its original notices.

| Files | Source | Version | Licence |
|---|---|---|---|
| `src/core/cdrom.cpp` command table | SwanStation | 922a420c | GPL-3.0 |
| `src/core/dma.cpp` deferred transfers, unhalt | SwanStation | 33744c49, 745de828 | GPL-3.0 |
| `src/core/gpu.cpp`, `gpu.h`, `gpu_commands.cpp` FIFO, display, poly-line and dot timer bounds | SwanStation | 33744c49, 3c5d0177 | GPL-3.0 |
| `src/core/mdec.cpp` FIFO bounds | SwanStation | 33744c49, 3c5d0177 | GPL-3.0 |
| `src/core/spu.cpp` transfer timing | SwanStation | 33744c49 | GPL-3.0 |
| `src/util/state_wrapper.h`, `.cpp` container lengths | SwanStation | 1db8c9b6 | GPL-3.0 |
| `cdrom.cpp`, `cpu_core.cpp`, `gpu.cpp`, `memory_card.cpp`, `multitap.cpp`, `spu.cpp`, `system.cpp`, `timing_event.cpp` save state validation | SwanStation | 1db8c9b6, 02d6afbe, 16945a07, 3c5d0177 | GPL-3.0 |
| `src/core/mdec.cpp` save state validation | SwanStation | 57ace1a3, 7e1163a0 | GPL-3.0 |
| `multitap.cpp`, `analog_controller.cpp`, `negcon_rumble.cpp` transfer state | SwanStation | fe24f640 | GPL-3.0 |
| `src/core/spu.cpp` IRQ9 enable | SwanStation | ee55ba40 | GPL-3.0 |
| `src/core/mdec.cpp` old routines output | SwanStation | 3dc3f3d3, b5fdeb5c | GPL-3.0 |
| `src/core/cpu_recompiler_code_generator.cpp` SRA speculation | SwanStation | 75a2316e | GPL-3.0 |
| `guncon.cpp`, `justifier.cpp`, `playstation_mouse.cpp`, `negcon.cpp` runahead input changes | SwanStation | e0dca36f, 6e31230b | GPL-3.0 |
| `src/core/cheats.cpp` register reset | SwanStation | 9987a7cd | GPL-3.0 |
| `data/resources/gamedb.yaml` Tekken and Soul Blade traits | SwanStation | 0f4a4a3e | GPL-3.0 |
| `dep/psx_gpu/` psx_gpu rasteriser (Exophase), gpulib `prim.c`, `gpu.h`, `gpu_timing.h`, `compiler_features.h`, `arm_features.h` | pcsx_rearmed | 7838055e | GPL-2.0-or-later |
| `src/core/gpu_sw.cpp` RGBA8/BGRA8 copy-out kernels | SwanStation | 93b213d8 | GPL-3.0 |
| `dep/libretro/include/libretro.h`, `libretro_vulkan.h` | libretro-common | 52781c4a | MIT |
| `src/core/netplay.cpp`, `netplay.h`, `netplay_packets.h`, netplay hooks in `system.cpp`, `pad.cpp`, `input_manager.cpp`, `qthost.cpp`, `mainwindow.cpp` | DuckStation pull request #3000 (Stenzek, with Jamie Meyer), submitted 2023-08-30 under GPL-3.0 | 1e585e74 | GPL-3.0 |
| `dep/enet/` ENet | DuckStation pull request #3000 (from lsalzman/enet) | fcd82df9 (enet ea4607a9) | MIT |
| `dep/ggpo-x/` GGPO (ggpo-x fork); `GAMEINPUT_MAX_BYTES` raised to 12 and `BITVECTOR_NIBBLE_SIZE` to 16 for analog input | DuckStation pull request #3000 | 5f99fda9 | MIT |
| `data/resources/chtdb.txt`, `patches.txt` codes from `cht/Sony - PlayStation`, converted to this format by `scripts/update_cheat_databases.py` | [libretro-database](https://github.com/libretro/libretro-database) (libretro and contributors) | 90601bff | CC-BY-SA-4.0, used under GPL-3.0 through its one-way compatibility |

## Fonts

`data/resources/fonts/Inter-Medium.ttf` is Inter 4.1 by the Inter Project
Authors (`extras/ttf/Inter-Medium.ttf` from the release archive at
https://github.com/rsms/inter/releases/tag/v4.1), under the SIL Open Font
License 1.1; the licence is in `data/resources/fonts/OFL-Inter.txt`.

`data/resources/fonts/NotoSansJP-Regular.ttf`, `NotoSansKR-Regular.ttf` and
`NotoSansSC-Regular.ttf` are Noto Sans JP, KR and SC version 2.004 from the
Google Fonts repository (`ofl/notosansjp`, `ofl/notosanskr`, `ofl/notosanssc`),
under the SIL Open Font License 1.1. They are the variable fonts fixed at
weight 400 with `fonttools varLib.instancer wght=400 --update-name-table`.

## Third-party libraries

Libraries bundled under `dep/` keep the licences given in their directories.
