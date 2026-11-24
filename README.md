<div align="center">
  <img src="scripts/io.github.thenotoriousfox.Riverhorse.png" width="200" alt="Riverhorse" /><br>
  <h1>Riverhorse</h1>
  <p><strong>A PlayStation emulator that starts straight into the game.</strong></p>
</div>

<div align="center">
  <a href="https://github.com/thenotoriousfox/riverhorse/releases"><img src="https://img.shields.io/github/v/release/thenotoriousfox/riverhorse?style=for-the-badge&logo=github&label=release" alt="Release" /></a>
  <a href="https://github.com/thenotoriousfox/riverhorse/stargazers"><img src="https://img.shields.io/github/stars/thenotoriousfox/riverhorse?style=for-the-badge&logo=github" alt="Stars" /></a>
  <a href="https://github.com/thenotoriousfox/riverhorse/commits/main"><img src="https://img.shields.io/github/last-commit/thenotoriousfox/riverhorse?style=for-the-badge&logo=github" alt="Last commit" /></a>
  <a href="https://github.com/thenotoriousfox/riverhorse/issues"><img src="https://img.shields.io/github/issues/thenotoriousfox/riverhorse?style=for-the-badge&logo=github" alt="Issues" /></a>
  <a href="https://hippos-linux.org/discord"><img src="https://img.shields.io/badge/discord-join-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Discord" /></a>
</div>

---

## ℹ️ About

Riverhorse is a standalone PS1 emulator for Linux. It aims to start straight into a game from a frontend, run well on low-end ARM devices, and never interrupt play with dialogs.

It is based on [DuckStation](https://github.com/stenzek/duckstation) `v0.1-7371` by Connor McLaughlin (Stenzek) and contributors, the last DuckStation release published under the GNU GPL v3. The full upstream history up to that release is kept in this repository.

> [!IMPORTANT]
> Riverhorse is not affiliated with or endorsed by the DuckStation project. Please do not report Riverhorse problems to DuckStation.

---

## 🚧 Status

The current release is 1.2.0; downloads are on the [Releases](https://github.com/thenotoriousfox/riverhorse/releases) page. Releases are tagged `vMAJOR.MINOR.PATCH`; the upstream tag `v0.1-7371` marks the base. Settings files use the same format and keys as the base. See [CHANGELOG.md](CHANGELOG.md) for everything changed since.

| Platform | Build |
|---|---|
| Linux x64 / ARM64 | AppImage, Flatpak |
| Windows x64 / ARM64 | Zip |
| macOS | Zip |
| libretro (Linux x64 / ARM64) | Single-file core |

---

## 🎮 Features

### Emulation

- CD-ROM drive timing modelled on hardware measurements: seeks, track jump-back, Pause, speed changes, motor spin-up and braking
- GPU, timer, SPU, DMA, MDEC and GTE timing reworked for accuracy
- Extended PGXP precision through shifts, masks, unaligned loads and stores, and more GTE commands
- Threaded hardware renderer and a fast software rasterizer for ARM handhelds
- CloneCD images, WAVE files in cue sheets, MODE1 and cooked-sector images

### Play

- Rollback netplay for up to four players and four spectators, direct or through a relay server
- Patch database with widescreen, 60 FPS and fix patches per game
- About 66,000 cheats for 1,900+ games, including libretro-database
- Cheats that replace instructions without writing RAM, for games that check their own code
- Maximum CD-ROM read speedup and fast forward while loading

### Interface

- Fullscreen interface with dark, light and high-contrast themes
- Home screen with "Continue Playing" and library cover shelves
- In-game side panel with quick graphics toggles
- Border overlays (bezels) around the game
- Interface Size option with a Handheld preset, and touch swiping
- Native Wayland, with X11 as a fallback

### Frontends

- libretro core for RetroArch and other frontends: software, OpenGL and Vulkan rendering, core options, multi-disc control, cheats, save states, RetroAchievements
- Command-line options to start a game, host or join netplay straight from a launcher

---

## 💾 BIOS

A BIOS image is needed to play games. Dump it from your own console; none is
provided. Images from any hardware version or region work, but a BIOS region
that does not match the game region can cause compatibility problems.

PAL games with LibCrypt protection need their SBI file next to the disc image,
with the same name (`Game.cue` and `Game.sbi`). CHD images with subchannel
data work without one.

---

## 🛠️ Building on Linux

Debian/Ubuntu build dependencies:

```
build-essential clang cmake curl extra-cmake-modules git libasound2-dev libcurl4-openssl-dev libdbus-1-dev libdecor-0-dev libegl-dev libevdev-dev libfontconfig-dev libfreetype-dev libgtk-3-dev libgudev-1.0-dev libharfbuzz-dev libinput-dev libopengl-dev libpipewire-0.3-dev libpulse-dev libssl-dev libudev-dev libwayland-dev libx11-dev libx11-xcb-dev libxcb1-dev libxcb-composite0-dev libxcb-cursor-dev libxcb-damage0-dev libxcb-glx0-dev libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-present-dev libxcb-randr0-dev libxcb-render0-dev libxcb-render-util0-dev libxcb-shape0-dev libxcb-shm0-dev libxcb-sync-dev libxcb-util-dev libxcb-xfixes0-dev libxcb-xinput-dev libxcb-xkb-dev libxext-dev libxkbcommon-x11-dev libxrandr-dev lld llvm ninja-build pkg-config zlib1g-dev
```

1. Clone: `git clone https://github.com/thenotoriousfox/riverhorse.git && cd riverhorse`
2. Build the bundled dependencies (slow; the directory can live outside the
   tree): `scripts/deps/build-dependencies-linux.sh deps`
3. Configure:
   ```
   cmake -B build-release -G Ninja \
     -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
     -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
     -DCMAKE_EXE_LINKER_FLAGS_INIT=-fuse-ld=lld \
     -DCMAKE_MODULE_LINKER_FLAGS_INIT=-fuse-ld=lld \
     -DCMAKE_SHARED_LINKER_FLAGS_INIT=-fuse-ld=lld \
     -DCMAKE_PREFIX_PATH="$PWD/deps"
   ```
4. Build: `ninja -C build-release`
5. Run: `./build-release/bin/riverhorse`

---

## 🧩 libretro core

Build the dependencies with `scripts/deps/build-dependencies-linux.sh
--libretro deps-libretro`, then configure a separate build directory as above
with `-DCMAKE_PREFIX_PATH="$PWD/deps-libretro" -DBUILD_QT_FRONTEND=OFF
-DBUILD_LIBRETRO_CORE=ON` and build the `riverhorse_libretro` target.
`scripts/libretro/make-libretro-package.sh` packs the result.

To install: put `riverhorse_libretro.so` and `riverhorse_libretro.info` in
the frontend's cores and info directories, and BIOS images in its system
directory. The game database, cheats, patches and shaders are built into the
core.

---

## 🌐 Netplay

Up to four players can play over the network with rollback: the host starts
a game and picks System > Host Netplay Session (or Host Netplay Session in
the in-game panel) and is shown a session code; the others pick System >
Join Netplay Session (or Start Game > Join Netplay) and enter that code.
Everyone needs the same game in their game list, a BIOS with the same hash
and the same Riverhorse version. For three or four players, the host turns
on a multitap first (Settings > Controllers > Multitap, port 1 for most
games); players then take slots 1A-1D in the order they joined. From a
frontend, start Riverhorse with `-netplayhost <port> -- <game>` or
`-netplayjoin <code|host:port> -- <game>` (the game is used when it matches
the host's, otherwise the game list is searched), plus `-netplaynick <name>`
and, to watch, `-netplayspectate`.

Session codes come from a relay server, which introduces the players; they
then connect directly, and only players whose routers block that keep going
through the relay. Riverhorse uses a default relay; Settings > Essentials >
Netplay (or the host and join dialogs) can turn the relay off or set your
own as Custom Relay Server (`host` or `host:port`, the same for everyone).
Without a relay, or when it can't be reached, players join with the host's
address and UDP port (31200 by default), which must be reachable, so the
host forwards it on their router when playing over the internet. To run a
relay, build with `-DBUILD_RELAY_SERVER=ON` and start `riverhorse-relay
[port]` (UDP 31210 by default) on a machine with that port open.

---

## 🖥️ Wayland and X11

Riverhorse runs natively on Wayland when the session provides it, and on X11
otherwise. To run the AppImage through X11 on a Wayland session, start it
with `RIVERHORSE_X11=1`.

---

## 📁 User directory

Settings, BIOS images, memory cards and save states live in
`$XDG_CONFIG_HOME/riverhorse` if that directory exists, otherwise in
`~/.local/share/riverhorse`. Put a file named `portable.txt` next to the
executable to keep them in the executable's directory instead.

---

## 🤝 Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the licence rules, what is accepted, and how to test.

> [!WARNING]
> Nothing may be copied from DuckStation releases after `v0.1-7371`; they are under CC BY-NC-ND 4.0.

---

## ❓ Support

- **Bug reports and feature requests**: [GitHub Issues](https://github.com/thenotoriousfox/riverhorse/issues)
- **Community**: [Discord](https://hippos-linux.org/discord)

---

## 🙏 Credits

- [DuckStation](https://github.com/stenzek/duckstation) by Connor McLaughlin (Stenzek) and contributors — the emulator Riverhorse is built on
- [libretro-database](https://github.com/libretro/libretro-database) — cheat codes (CC BY-SA 4.0)
- Everyone listed in [CONTRIBUTORS.md](CONTRIBUTORS.md) and [NOTICE.md](NOTICE.md)

---

## 💖 Supporters

Riverhorse is free and open source. If you find it useful, consider supporting development.

<p align="center">
  <a href="https://github.com/sponsors/thenotoriousfox"><img src="https://img.shields.io/badge/sponsor-thenotoriousfox-ea4aaa?style=for-the-badge&logo=github-sponsors" alt="Sponsor on GitHub" /></a>
</p>

<p align="center">
  <img src="https://cdn.jsdelivr.net/gh/thenotoriousfox/riverhorse@contributors/sponsors.svg" alt="Sponsors" />
</p>

---

## 👥 Contributors

Thank you to everyone who has helped make Riverhorse better!

<p align="center">
  <img src="https://cdn.jsdelivr.net/gh/thenotoriousfox/riverhorse@contributors/github.svg" alt="GitHub contributors" />
</p>

Riverhorse is built on DuckStation; its contributors are credited in [CONTRIBUTORS.md](CONTRIBUTORS.md).

---

## 📄 License

Riverhorse is free software under the **GNU General Public License v3.0**. See [LICENSE](LICENSE). Third-party code under `dep/` keeps its own licence, given in each directory.

"PlayStation" and "PSX" are registered trademarks of Sony Interactive Entertainment. Riverhorse is not affiliated with Sony Interactive Entertainment.

---

## ⭐ Star History

<a href="https://www.star-history.com/?repos=thenotoriousfox%2Friverhorse&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=thenotoriousfox/riverhorse&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=thenotoriousfox/riverhorse&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=thenotoriousfox/riverhorse&type=date&legend=top-left" />
 </picture>
</a>
