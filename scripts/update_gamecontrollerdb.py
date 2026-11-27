#!/usr/bin/env python3
"""Updates data/resources/gamecontrollerdb.txt to the latest SDL_GameControllerDB.

Usage: update_gamecontrollerdb.py [--commit <sha>]

Downloads gamecontrollerdb.txt and its LICENSE from https://github.com/mdqinc/SDL_GameControllerDB at the
latest master commit (or the given one), writes them to data/resources, records the commit in NOTICE.md and
prints the number of mappings per platform. Review the diff before committing.
"""

import argparse
import json
import os
import re
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESOURCES = os.path.join(ROOT, "data", "resources")
DATABASE_FILE = os.path.join(RESOURCES, "gamecontrollerdb.txt")
LICENSE_FILE = os.path.join(RESOURCES, "gamecontrollerdb-LICENSE.txt")
NOTICE_FILE = os.path.join(ROOT, "NOTICE.md")

REPOSITORY = "mdqinc/SDL_GameControllerDB"
NOTICE_COMMIT = re.compile(r"(https://github\.com/mdqinc/SDL_GameControllerDB, commit )[0-9a-f]+")
MAPPING = re.compile(r"^[0-9a-fx]{32},|^xinput,")
PLATFORM = re.compile(r"platform:([^,]+),")


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "riverhorse-update-script"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def latest_commit():
    data = json.loads(fetch(f"https://api.github.com/repos/{REPOSITORY}/commits/master"))
    return data["sha"]


def write_text(path, data):
    text = data.decode("utf-8")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--commit", help="SDL_GameControllerDB commit to use instead of the latest master")
    args = parser.parse_args()

    commit = args.commit or latest_commit()
    raw = f"https://raw.githubusercontent.com/{REPOSITORY}/{commit}"
    database = write_text(DATABASE_FILE, fetch(f"{raw}/gamecontrollerdb.txt"))
    write_text(LICENSE_FILE, fetch(f"{raw}/LICENSE"))

    with open(NOTICE_FILE, encoding="utf-8") as f:
        notice = f.read()
    notice, replaced = NOTICE_COMMIT.subn(rf"\g<1>{commit[:8]}", notice)
    if replaced != 1:
        raise SystemExit("NOTICE.md has no SDL_GameControllerDB entry to update")
    with open(NOTICE_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write(notice)

    counts = {}
    for line in database.splitlines():
        if MAPPING.match(line):
            platform = PLATFORM.search(line)
            name = platform.group(1) if platform else "Any"
            counts[name] = counts.get(name, 0) + 1

    print(f"SDL_GameControllerDB {commit[:8]}: {sum(counts.values())} mappings")
    for name, count in sorted(counts.items()):
        print(f"  {name}: {count}")


if __name__ == "__main__":
    main()
