#!/usr/bin/env python3
"""Cleans up data/resources/chtdb.txt and patches.txt, and optionally imports libretro-database cheats.

Usage: update_cheat_databases.py [--libretro-database <checkout>]

Without arguments, each game's codes are gathered under its exact disc serial in a single block, so the
emulator finds all of them. With --libretro-database, the PlayStation cheat files from a libretro-database
checkout (https://github.com/libretro/libretro-database, CC BY-SA 4.0) are added: widescreen and frame rate
codes to patches.txt, the rest to chtdb.txt. Codes already present are skipped. Update the commit recorded
in NOTICE.md after an import.
"""

import argparse
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESOURCES = os.path.join(ROOT, "data", "resources")
CHEATS_FILE = os.path.join(RESOURCES, "chtdb.txt")
PATCHES_FILE = os.path.join(RESOURCES, "patches.txt")
GAMEDB_FILE = os.path.join(RESOURCES, "gamedb.yaml")
LIBRETRO_DIR = os.path.join("cht", "Sony - PlayStation")

SERIAL = re.compile(r"[A-Z]{4}-\d{5}")
SUFFIXED_SERIAL = re.compile(r"([A-Z]{4}-\d{5})[A-Z0-9]+")
SHORT_JAPANESE_SERIAL = re.compile(r"SLP-(\d{5})")
PATCH_NAME = re.compile(r"wide ?screen|16[:x-]9|aspect ratio|\b(30|50|60) ?fps\b|frame ?rate", re.IGNORECASE)
NOT_PATCH_NAME = re.compile(r"display|show|debug", re.IGNORECASE)
LIBRETRO_DEVICE = re.compile(r" \((GameShark|Game Buster|Xploder|Action Replay|DuckStation)\)$")
NAME_TAGS = re.compile(r" \((?:[A-Z][a-z](?:,[A-Z][a-z])+|EDC)\)")
LIBRETRO_CODE = re.compile(r"[0-9A-Fa-f]{8}[ +][0-9A-Fa-f]{4}(\+[0-9A-Fa-f]{8}[ +][0-9A-Fa-f]{4})*")
LIBRETRO_EXCLUDED = {
    "Gran Turismo 2 (USA) (Rev 2) (DuckStation).cht",
    "Need for Speed - High Stakes (USA) (DuckStation).cht",
    "R4 - Ridge Racer Type 4 (USA) (DuckStation).cht",
}


class Database:
    def __init__(self, path):
        self.path = path
        self.header = []
        self.games = {}
        self.comments = {}
        game, pending = None, []
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f.read().split("\n"):
                stripped = line.strip()
                if game is None and not stripped.startswith(":"):
                    self.header.append(line)
                elif stripped.startswith(":"):
                    serial = stripped[1:]
                    game = self.games.setdefault(serial, [])
                    self.comments.setdefault(serial, pending)
                    pending = []
                elif stripped.startswith(";"):
                    pending.append(stripped)
                elif stripped.startswith("#"):
                    if game:
                        game[-1][1].extend(pending)
                    game.append([stripped[1:], []])
                    pending = []
                elif stripped and game:
                    game[-1][1].extend(pending + [stripped])
                    pending = []
        if game:
            game[-1][1].extend(pending)
        while self.header and not self.header[-1].strip():
            self.header.pop()
        if self.header and self.header[-1].lstrip().startswith(";") and self.games:
            first = next(iter(self.games))
            if not self.comments.get(first):
                self.comments[first] = []

    def add(self, serial, name, lines):
        codes = self.games.setdefault(serial, [])
        body = code_lines(lines)
        if not body or any(code_lines(existing) == body for _, existing in codes):
            return False
        names = {existing for existing, _ in codes}
        unique, number = name, 2
        while unique in names:
            unique, number = f"{name} ({number})", number + 1
        codes.append([unique, lines])
        return True

    def has_code(self, serial, lines):
        body = code_lines(lines)
        return any(code_lines(existing) == body for _, existing in self.games.get(serial, []))

    def save(self):
        out = list(self.header)
        out.append("")
        for serial, codes in self.games.items():
            if not codes:
                continue
            out.extend(self.comments.get(serial, []))
            out.append(":" + serial)
            for name, lines in codes:
                out.append("#" + name)
                out.extend(lines)
            out.append("")
        with open(self.path, "w", encoding="utf-8") as f:
            f.write("\n".join(out))


def code_lines(lines):
    return [line.upper() for line in lines if not line.startswith(";")]


def read_gamedb():
    names, serials = {}, set()
    entry = None

    def finish():
        if entry:
            names.setdefault(entry["name"], set()).add(entry["serial"])
            if entry["set_name"]:
                names.setdefault(entry["set_name"], set()).update(entry["set_serials"] or [entry["serial"]])

    with open(GAMEDB_FILE, encoding="utf-8") as f:
        for line in f:
            match = re.match(r"^([A-Z]{4}-\d{5}):", line)
            if match:
                finish()
                entry = {"serial": match.group(1), "name": None, "set_name": None, "set_serials": []}
                serials.add(entry["serial"])
            elif entry is None:
                continue
            elif match := re.match(r'^  name: "(.*)"', line):
                entry["name"] = match.group(1)
            elif match := re.match(r'^    name: "(.*)"', line):
                entry["set_name"] = match.group(1)
            elif match := re.match(r"^      - ([A-Z]{4}-\d{5})$", line):
                entry["set_serials"].append(match.group(1))
    finish()
    names.pop(None, None)
    return names, serials


def canonical_serial(serial, known_serials):
    if SERIAL.fullmatch(serial):
        return serial
    match = SUFFIXED_SERIAL.fullmatch(serial)
    if match:
        return match.group(1)
    match = SHORT_JAPANESE_SERIAL.fullmatch(serial)
    if match:
        candidates = [s for s in (f"SLPM-{match.group(1)}", f"SLPS-{match.group(1)}") if s in known_serials]
        if len(candidates) == 1:
            return candidates[0]
    return serial


def normalize(database, known_serials):
    games = database.games
    comments = database.comments
    database.games, database.comments = {}, {}
    for serial, codes in games.items():
        target = canonical_serial(serial, known_serials)
        database.comments.setdefault(target, comments.get(serial, []))
        for name, lines in codes:
            database.add(target, name, lines)


def libretro_cheats(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        fields = dict(re.findall(r'^(cheat\d+_(?:desc|code)) = "(.*)"', f.read(), re.MULTILINE))
    index = 0
    while f"cheat{index}_desc" in fields:
        name = fields[f"cheat{index}_desc"].strip().replace("\\", "/")
        code = fields.get(f"cheat{index}_code", "").strip()
        index += 1
        if not name or not LIBRETRO_CODE.fullmatch(code):
            continue
        values = re.split(r"[ +]", code)
        yield name, [f"{values[i].upper()} {values[i + 1].upper()}" for i in range(0, len(values), 2)]


def import_libretro(checkout, cheats, patches, names):
    directory = os.path.join(checkout, LIBRETRO_DIR)
    loose_names = {}
    for name, serials in names.items():
        loose_names.setdefault(NAME_TAGS.sub("", name), set()).update(serials)
    added = {"cheats": 0, "patches": 0}
    unmatched = []
    for filename in sorted(os.listdir(directory)):
        if not filename.endswith(".cht") or filename in LIBRETRO_EXCLUDED:
            continue
        title = LIBRETRO_DEVICE.sub("", filename[:-4])
        serials = sorted(names.get(title) or loose_names.get(NAME_TAGS.sub("", title), ()))
        if not serials:
            unmatched.append(filename)
            continue
        for name, lines in libretro_cheats(os.path.join(directory, filename)):
            is_patch = bool(PATCH_NAME.search(name)) and not NOT_PATCH_NAME.search(name)
            target, other = (patches, cheats) if is_patch else (cheats, patches)
            for serial in serials:
                if other.has_code(serial, lines):
                    continue
                if target.add(serial, name, lines):
                    added["patches" if is_patch else "cheats"] += 1
    return added, unmatched


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--libretro-database", help="path to a libretro-database checkout")
    args = parser.parse_args()

    names, known_serials = read_gamedb()
    cheats, patches = Database(CHEATS_FILE), Database(PATCHES_FILE)
    normalize(cheats, known_serials)
    normalize(patches, known_serials)

    if args.libretro_database:
        added, unmatched = import_libretro(args.libretro_database, cheats, patches, names)
        print(f"added {added['cheats']} cheats and {added['patches']} patches")
        print(f"{len(unmatched)} files did not match a game in gamedb.yaml:")
        for filename in unmatched:
            print("  " + filename)

    cheats.save()
    patches.save()
    for database in (cheats, patches):
        odd = sorted(s for s in database.games if not SERIAL.fullmatch(s))
        print(f"{os.path.basename(database.path)}: {len(database.games)} games; not a disc serial: {' '.join(odd)}")


if __name__ == "__main__":
    main()
