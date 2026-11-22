# Changelog

Changes since the DuckStation `v0.1-7371` base. Releases are tagged
`vMAJOR.MINOR.PATCH`.

## 1.1.0 (2026-11-22)

### New
- Riverhorse Dark and Riverhorse Light themes for the desktop window
  (Settings > Theme), using the same colours as the fullscreen interface.
  Riverhorse Dark is the default for new installs.
- Automatic cover art (Settings > Game List > Fetch Missing Cover Art, also
  asked in the setup wizard; off by default): box art for games without a
  cover is downloaded from the libretro-thumbnails collection when they are
  shown in the game list, the grid or Big Picture, and saved to the covers
  folder.
- Cover column with box-art thumbnails in the game list, shown by default
  when cover fetching is on.
- Xplorer/Xploder cheat cartridge on the parallel port (Settings > BIOS >
  Parallel Port, also in Big Picture): loads a flash dump, emulates its
  flash chip, SRAM, bank switching and I/O ports, and writes changes back
  to the dump when "Allow Image Writes" is on. Tested with Unirom flashed
  and booted from the emulated cartridge.

### Changes
- The updater offers the newest release and compares version tags, instead
  of following a fixed `latest` release; the update channel setting is
  gone. Copies of 1.0.0 still find this release through `latest`.
- The recompiler's lookup tables take about 17 MB instead of 200 MB, which
  matters on 1 GB handhelds.
- Code in the parallel port area is now fetched (open bus with no device)
  instead of raising a bus error, and parallel port accesses take the time
  set in the memory control registers.
- The toolbar is shown by default, and new windows open large enough for
  all of it.
- A multi-disc game is listed once: discs named in an `.m3u` playlist are
  hidden, and a playlist whose discs already form a merged entry is hidden.
- Games start on double-click or Enter only.
- Purple cover placeholder, smaller settings sidebar icons, and track
  columns in Game Properties that fit their text.
- Third-party notices rewritten: contents list, wrapped text, a Riverhorse
  copyright line, and the components that were missing (ENet, GGPO, psx_gpu,
  glad, minizip, libretro API, Vulkan Headers, libbacktrace, libwebp,
  libcurl, Qt, RAInterface, WinPixEventRuntime, Inter, Roboto Mono,
  Font Awesome, Remix Icon).

### Fixes
- COP0 data breakpoints never triggered, so cheat cartridge firmware that
  boots through one (such as Unirom on an Xplorer) fell through to the BIOS.
- On KDE, a single click or a right-click on a game started it.
- The window and taskbar icon was missing on Wayland.
- Help menu icons and About box links were hard to read in dark themes.

## 1.0.0 (2026-11-21)

### New
- libretro core (`riverhorse_libretro.so`) for RetroArch and other
  frontends: software renderer, OpenGL and Vulkan hardware rendering in
  the frontend's own context, core options, DualShock and digital pads,
  disc control for multi-disc games, cheats, save states, RetroAchievements
  memory access. Built as a single shared library with the game database,
  cheats, patches and shaders inside, so nothing goes in the system
  directory except BIOS images.
- Threaded hardware renderer (Settings > Graphics > Threaded Hardware
  Rendering) with asynchronous presentation.
- Fast software rasterizer option (psx_gpu), aimed at ARM handhelds.
- Cheat codes that replace instructions without writing RAM (cheat register
  sub-types E1-E3), for games that check their own code.
- Relaxed VSync option.
- New fullscreen interface look: Riverhorse themes (dark, light, high
  contrast) loaded from `resources/themes/*.ini`, the Inter font, a home
  screen with "Continue Playing" and library cover shelves, an in-game side
  panel with quick graphics toggles, an Essentials settings page, an
  Interface Size option with a Handheld preset, and touch swiping.
- Rollback netplay for two players and up to four spectators over UDP
  (ENet and GGPO), ported from an unmerged 2023 DuckStation pull request:
  host from System > Host Netplay Session or the in-game panel, join from
  System > Join Netplay Session or Start Game > Join Netplay, or start with
  `-netplayhost <port>` / `-netplayjoin <host:port>`. The host's emulation
  settings, BIOS and patches are applied to every player; analog sticks
  are synced; a desync makes the host resend its state.
- Regression runner `-rollbacktest <frames>`: replays frames after loading
  memory states and reports the first byte that differs.
- Patch database (`patches.txt`): widescreen, 60 FPS and fix patches chosen
  per game from Game Properties > Patches; they start once the BIOS kernel
  has initialised.
- Cheats and patches from libretro-database: 65,900 cheats and 23 patches
  for about 1,930 games, 346 of which had none before.
- Maximum CD-ROM read speedup, fast forward while loading, and normal-speed
  reads while the MDEC decodes video.
- Right stick as shoulder and trigger buttons; Densha de Go! digital
  controller variant.
- CloneCD images, WAVE files in cue sheets, MODE1 and cooked-sector images,
  LibCrypt subchannel files in a `subchannels` folder.
- Regression runner options: save state round trips, threaded renderer,
  runahead, cheats.

### Emulation accuracy
- CD-ROM drive timing modelled on hardware measurements: seeks, track
  jump-back, Pause, SeekL, speed changes, motor spin-up and braking,
  command replacement, invalid commands, CD audio reports and auto pause.
- GPU timing and status as the v2 GPU: GP1(00h) restart, VBlank at Y1/Y2,
  scanline lengths, interlaced fields, GPUSTAT.13/31, measured drawing
  costs, 16-word FIFO, DMA request holds, odd VRAM reads, rectangle flips.
- Timers: wrap and target compares, exact IRQ scheduling, toggle mode,
  sync modes, gated reads.
- SPU: transfer timing interleaved with samples, key on timing, interpolation
  precision, ADPCM filters, noise clock, volume sweeps, unused registers.
- DMA, MDEC, memory cards, controller and serial ports, GTE command
  aliases and timing, CPU exceptions, coprocessor branches, KUSEG mirrors.
- PGXP precision through shifts, masks, halfword carries, unaligned loads
  and stores, GTE GPF/GPL and light/colour MVMVA; depth normalisation.

### Fixes
- Cheats and patches stored under a second block for the same game, or
  under a serial with extra letters (`SCUS-94244CE`) or a shortened one
  (`SLP-86219`), are offered again.
- OpenGL and Vulkan start on hybrid NVIDIA laptops (EGL config matching the
  X11 window visual; the discrete GPU is preferred).
- Save state loading checks lengths and value ranges and resets on failure.
- Display texture dumps are no longer upside down on OpenGL.
- Unhandled crashes are passed to the previous handler instead of being
  hidden by the fault handler.

### Changes
- Rewind saves every 0.5 seconds by default.
- Renamed to Riverhorse, with its own application ID, Discord application and
  UI fonts for Japanese, Korean and Chinese.
