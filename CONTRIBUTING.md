# Contributing to Riverhorse

Thanks for helping. This page covers what is accepted, the licence rules
every change must follow, and how to build and test.

## Licence rules

Riverhorse is GPL-3.0-only. By opening a pull request you agree that your
contribution is licensed under the GPL v3, and you confirm you have the
right to license it that way. Sign off each commit (`git commit -s`) to say
so; the sign-off follows the [Developer Certificate of Origin](https://developercertificate.org/).

DuckStation releases after `v0.1-7371` (from 2024-09-01) are under
CC BY-NC-ND 4.0, which does not allow modified versions to be shared:

- Do not copy code, game database entries, patches, shaders, translations
  or artwork from those releases, and do not rewrite their code line by line.
- A fix seen there may be reimplemented from a description of the hardware
  behaviour. Say in the pull request where you learned about the problem
  and describe the behaviour in your own words.
- Code from GPL-compatible projects (SwanStation, Beetle PSX, PCSX-Redux,
  pcsx_rearmed) may be copied with its notices kept. Add a row to the table
  in [NOTICE.md](NOTICE.md). GPL-2.0-only files cannot be combined with
  GPL-3.0 and are not accepted.

Patches (widescreen, 60 FPS, fixes) in `data/resources/patches.txt` follow the
same rules: write your own or take them from a GPL-compatible source, and
give the disc serial and the version you tested on.

## What is accepted

- Accuracy fixes with a test case: a game that changes, or a test ROM.
- Performance work that keeps the regression runner's output identical.
- Linux, handheld and frontend integration improvements.
- Translations of the Qt and fullscreen interfaces.
- New patches for the patch database.

Not accepted: features that are not PS1 hardware (expanded VRAM, more than
8 MB RAM), telemetry, online services that need an account, and code whose
origin is unclear.

## Building

See [Building on Linux](README.md#building-on-linux). Run
`scripts/deps/build-dependencies-linux.sh <prefix>` once, then configure
with `-DCMAKE_PREFIX_PATH=<prefix>`. Add `-DBUILD_REGTEST=ON` for the
regression runner and `-DBUILD_LIBRETRO_CORE=ON` (in a separate build
directory, with dependencies from `--libretro`) for the libretro core.

## Testing

The regression runner plays a game without a window and writes frames:

```
riverhorse-regtest -renderer Software -frames 3600 -dumpinterval 600 -dumpdir out -- game.chd
```

Compare the frames before and after your change. Useful options:

- `-statetest`: saves and reloads a state after every dumped frame; the
  output must not change.
- `-renderer OpenGL`, `-upscale 2`, `-pgxp`: hardware renderer paths.
- `-hwthread`: the threaded hardware renderer.
- `-runahead N`, `-cheats`, `-cpu Interpreter`.

`scripts/run_regression_tests.py` runs a folder of games and
`scripts/check_regression_tests.py` compares two runs.

Changes to the libretro core should be tried in RetroArch with both the
OpenGL (`glcore`) and Vulkan video drivers: load a game, save and load a
state, toggle fullscreen, quit.

## Style

- Follow the formatting in `.clang-format`.
- Commit messages: a short imperative subject that says what changes for
  the player or the code ("Time CD-ROM Pause from the one-track jump back"),
  and a body when the reason is not obvious.
- Comments say what to do with a setting or an interface, not what the
  next line does.

## Translations

Qt interface strings live in `src/riverhorse-qt/translations/*.ts` and are
edited with Qt Linguist. Run
`src/riverhorse-qt/translations/update-and-edit-language.sh <code>` to
refresh a language file from the source before editing it. Fullscreen UI
strings are collected by `scripts/generate_fullscreen_ui_translation_strings.py`.

## Releases

Builds run only when a tag is pushed (the daily Contributors workflow only
regenerates the README images). Push a tag such as `v1.0.1` to build every
platform and publish a release under that tag; the built-in updater offers
the newest release. Pull requests and branch pushes are not built; build and
test them locally.
