<div align="center">
  <img src="scripts/io.github.thenotoriousfox.Riverhorse.png" width="200" alt="Riverhorse" /><br>
  <h1>Riverhorse</h1>
  <p><strong>A PlayStation emulator that starts straight into the game.</strong></p>
</div>

<div align="center">
  <a href="https://riverhorse.app"><img src="https://img.shields.io/badge/website-riverhorse.app-8b5cf6?style=for-the-badge" alt="Website" /></a>
  <a href="https://github.com/thenotoriousfox/riverhorse/releases"><img src="https://img.shields.io/github/v/release/thenotoriousfox/riverhorse?style=for-the-badge&logo=github&label=release" alt="Release" /></a>
  <a href="https://github.com/thenotoriousfox/riverhorse/stargazers"><img src="https://img.shields.io/github/stars/thenotoriousfox/riverhorse?style=for-the-badge&logo=github" alt="Stars" /></a>
  <a href="https://github.com/thenotoriousfox/riverhorse/commits/main"><img src="https://img.shields.io/github/last-commit/thenotoriousfox/riverhorse?style=for-the-badge&logo=github" alt="Last commit" /></a>
  <a href="https://github.com/thenotoriousfox/riverhorse/issues"><img src="https://img.shields.io/github/issues/thenotoriousfox/riverhorse?style=for-the-badge&logo=github" alt="Issues" /></a>
  <a href="https://hippos-linux.org/discord"><img src="https://img.shields.io/badge/discord-join-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Discord" /></a>
</div>

---

## ℹ️ About

Riverhorse is a standalone PS1 emulator for Linux, Windows and macOS. It aims to start straight into a game from a frontend, run well on low-end ARM devices, and never interrupt play with dialogs.

It is based on [DuckStation](https://github.com/stenzek/duckstation) `v0.1-7371` by Connor McLaughlin (Stenzek) and contributors, the last DuckStation release published under the GNU GPL v3. The full upstream history up to that release is kept in this repository.

> [!IMPORTANT]
> Riverhorse is not affiliated with or endorsed by the DuckStation project. Please do not report Riverhorse problems to DuckStation.

---

## 🚧 Status

The current release is 1.3.0; downloads are on [riverhorse.app](https://riverhorse.app) and the [Releases](https://github.com/thenotoriousfox/riverhorse/releases) page. Releases are tagged `vMAJOR.MINOR.PATCH`; the upstream tag `v0.1-7371` marks the base. Settings files use the same format and keys as the base. See [CHANGELOG.md](CHANGELOG.md) for everything changed since.

| Platform | Build |
|---|---|
| Linux x64 / ARM64 | AppImage, Flatpak |
| Windows x64 / ARM64 | Zip |
| macOS | Zip |
| libretro: Linux x64 / ARM64 / ARMv7, Windows x64, Android ARM64 / ARMv7 / x64, macOS | Single-file core |

---

## 🎮 Features

### Emulation

- CD-ROM drive timing modelled on hardware measurements: seeks, track jump-back, Pause, speed changes, motor spin-up and braking
- GPU, timer, SPU, DMA, MDEC and GTE timing reworked for accuracy
- Extended PGXP precision through shifts, masks, unaligned loads and stores, and more GTE commands
- Threaded hardware renderer and a fast software rasterizer for ARM handhelds
- CloneCD images, WAVE files in cue sheets, MODE1 and cooked-sector images

### Play

- Rollback netplay for up to four players and four spectators, direct or through a relay server, with a public session list
- Save sync: memory cards, save states and game settings kept the same on every device through a synced folder or a WebDAV server, so a game closed on one device resumes on another
- Patch database with widescreen, 60 FPS and fix patches per game, plus your own patches per game
- About 66,000 cheats for 1,900+ games, including libretro-database
- Cheats that replace instructions without writing RAM, for games that check their own code
- Maximum CD-ROM read speedup and fast forward while loading
- Frameskip for devices that can't keep up, and frame pacing on 120, 180 and 240 Hz displays, with optional black frame insertion at 120 Hz
- Controller lights (DualShock 4, DualSense) that dim while the DualShock is in digital mode

### Interface

- Fullscreen interface with dark, light and high-contrast themes
- Home screen with "Continue Playing" and library cover shelves
- In-game side panel with quick graphics toggles
- Border overlays (bezels) around the game
- Post-processing with GLSL, ReShade FX and libretro slang shader presets
- MMPX (pixel art) and Cubic texture filters
- Interface Size option with a Handheld preset, and touch swiping
- Native Wayland, with X11 as a fallback

### Frontends

- libretro core for RetroArch and other frontends: software, OpenGL, OpenGL ES and Vulkan rendering, grouped core options, multi-disc control, rumble, light guns with crosshairs, NeGcon and mouse, cheat database codes as options, frameskip, notifications, save states, netplay, rewind, RetroAchievements
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

## 🛠️ Building on Windows

Needs Visual Studio 2022 (or its Build Tools) with the C++ and "C++ Clang
tools for Windows" components.

1. Build the dependencies into `dep\msvc\deps-x64`:
   `scripts\deps\build-dependencies-windows-x64.bat`
2. From an "x64 Native Tools Command Prompt", configure:
   ```
   cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release ^
     -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON ^
     -DCMAKE_C_COMPILER="%VCINSTALLDIR%Tools\Llvm\x64\bin\clang-cl.exe" ^
     -DCMAKE_CXX_COMPILER="%VCINSTALLDIR%Tools\Llvm\x64\bin\clang-cl.exe" ^
     -DCMAKE_PREFIX_PATH="%CD%\dep\msvc\deps-x64"
   ```
3. Build: `ninja -C build-release`
4. Run: `build-release\bin\riverhorse.exe`

---

## 🧩 libretro core

Build the dependencies with `scripts/deps/build-dependencies-linux.sh
--libretro deps-libretro`, then configure a separate build directory as above
with `-DCMAKE_PREFIX_PATH="$PWD/deps-libretro" -DBUILD_QT_FRONTEND=OFF
-DBUILD_LIBRETRO_CORE=ON -DENABLE_DESKTOP_INTEGRATION=OFF` and build the
`riverhorse_libretro` target. Without desktop integration the core needs no
SDL, curl, D-Bus, udev, libbacktrace, Discord, X11 or Wayland libraries.

To build the core without a dependency directory, fetch the bundled sources
with `git submodule update --init --recursive` and configure with
`-DBUILD_QT_FRONTEND=OFF -DBUILD_LIBRETRO_CORE=ON
-DENABLE_DESKTOP_INTEGRATION=OFF -DUSE_BUNDLED_DEPENDENCIES=ON` (no
`CMAKE_PREFIX_PATH`). The result needs only the C and C++ runtime.
`scripts/libretro/make-libretro-package.sh <core file> <output directory>
<name suffix>` packs the result. For Windows, cross-compile with
`-DCMAKE_TOOLCHAIN_FILE=scripts/libretro/mingw-w64-x86_64.cmake`; for 32-bit
ARM Linux, with `-DCMAKE_TOOLCHAIN_FILE=scripts/libretro/linux-armhf.cmake`
(clang and Debian's `g++-arm-linux-gnueabihf`).

Each release has the core for Linux, Windows, Android and macOS
(`riverhorse_libretro-<platform>.zip`). To install: put the core
(`riverhorse_libretro.so`, `.dll`, `.dylib`, or `riverhorse_libretro_android.so`)
and `riverhorse_libretro.info` in the frontend's cores and info directories,
and BIOS images in its system directory. On Android, use RetroArch's Load Core >
Install or Restore a Core. The game database, cheats, patches and shaders are built into the
core.

---

## 🌐 Netplay

Up to four players can play over the network with rollback: the host starts
a game and picks System > Host Netplay Session (or Host Netplay Session in
the in-game panel); the others pick System > Join Netplay Session (or Start
Game > Join Netplay) and choose the session from the list of open sessions,
or enter the session code the host is shown. The host can list the session
or keep it to people with the code, and can ask to accept each player.
Everyone needs the same game in their game list, a BIOS with the same hash
and the same Riverhorse version. For three or four players, the host turns
on a multitap first (Settings > Controllers > Global Settings > Multitap Mode, port 1 for most
games); players then take slots 1A-1D in the order they joined. From a
frontend, start Riverhorse with `-netplayhost <port> -- <game>` or
`-netplayjoin <code|host:port> -- <game>` (the game is used when it matches
the host's, otherwise the game list is searched), plus `-netplaynick <name>`
and, to watch, `-netplayspectate`.

With `-netplayevents`, Riverhorse prints netplay events on stdout for a
frontend to read, one line each, whether the session was started from the
command line or the menus: `netplay <event> key=value ...`, with values
percent-encoded (anything other than letters, digits and `-._~:` becomes
`%XX`) and empty when unknown. Events:

- `hosting port=<udp port> code=<session code> serial=<game serial>
  title=<game title> nick=<host nickname> players=<max players>
  password=yes|no` when a hosted session starts (`code` is empty without
  the relay)
- `player-joined nick=<nickname> player=<n>` and `player-left
  nick=<nickname> player=<n>`, with players numbered from 1 (the host)
- `joined player=<n>` when this instance joins someone else's session
- `stopped` when the session ends; a frontend should also treat the
  process exiting as the end of the session

Hosts can list their session publicly ("List in the session browser" in the
host dialog, List Hosted Sessions in Big Picture, or `-netplaypublic` with
`-netplayhost`; off by default). The relay serves the list as JSON over HTTP
on the relay's port number (TCP): `GET /v1/sessions`, optionally
`?version=<Riverhorse version tag>` to keep only sessions of that version
(players can only join the same version). Each session has `id` and `code`
(the session code, join with `-netplayjoin <code>`), `nickname`, `serial`,
`title`, `version`, `protocol`, `state_version`, `players`, `max_players`,
`password` and `joinable` (not full). Sessions leave the list when the host
stops or stops answering the relay (30 seconds). Host addresses are never
listed; joining goes through the relay.

Session codes come from a relay server, which introduces the players; they
then connect directly, and only players whose routers block that keep going
through the relay. Riverhorse uses a default relay; the host and join
dialogs (or Settings > Essentials > Netplay in Big Picture) can turn the relay off or set your
own as Custom Relay Server (`host` or `host:port`, the same for everyone).
Without a relay, or when it can't be reached, players join with the host's
address and UDP port (31200 by default), which must be reachable, so the
host forwards it on their router when playing over the internet. To run a
relay, build with `-DBUILD_RELAY_SERVER=ON` and start `riverhorse-relay
[port]` (UDP 31210 by default) on a machine with that port open.

---

## 🔄 Save sync

Settings > Save Sync (or the Memory Cards page in Big Picture) keeps memory
cards, save states and game settings the same on all your devices. Pick
Folder for a folder another program keeps in sync, such as Syncthing,
Dropbox or Nextcloud, or WebDAV Server with its address, username and
password (Nextcloud, many NAS boxes, `rclone serve webdav`). Riverhorse
creates `memcards`, `savestates` and `gamesettings` folders inside it.

Riverhorse syncs when it starts, before a game boots and after it closes,
and Sync Now syncs straight away. The save state made when a game closes
travels too, so you can resume on another device. A save changed on two
devices is kept twice: the older copy gets `(conflict <device> <date>)` in
its name. Files a sync replaces or removes are kept for 30 days in
`savesync/backup` in the user directory. The WebDAV password is stored in
`settings.ini`.

---

## 🖥️ Wayland and X11

On Linux, Riverhorse runs natively on Wayland when the session provides it,
and on X11 otherwise. To use X11 on a Wayland session:

- AppImage: start it with `RIVERHORSE_X11=1`
- Flatpak: `flatpak run --socket=x11 --env=QT_QPA_PLATFORM=xcb
  io.github.thenotoriousfox.Riverhorse`
- Built from source: start it with `QT_QPA_PLATFORM=xcb`

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
