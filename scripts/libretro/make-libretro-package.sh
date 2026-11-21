#!/usr/bin/env bash
# Usage: make-libretro-package.sh <build directory> <output directory> <name suffix>
# Creates riverhorse_libretro-<suffix>.zip: the core and its .info file, for the frontend's cores directory.

set -e

if [ "$#" -ne 3 ]; then
	echo "Syntax: $0 <build directory> <output directory> <name suffix>"
	exit 1
fi

ROOTDIR=$(realpath "$(dirname "${BASH_SOURCE[0]}")/../..")
BUILDDIR=$(realpath "$1")
OUTDIR=$(realpath -m "$2")
SUFFIX="$3"
mkdir -p "$OUTDIR"

STAGING=$(mktemp -d)
trap 'rm -rf "$STAGING"' EXIT
mkdir -p "$STAGING/core"

cp "$BUILDDIR/src/riverhorse-libretro/riverhorse_libretro.so" "$STAGING/core/"
strip --strip-unneeded "$STAGING/core/riverhorse_libretro.so"
patchelf --remove-rpath "$STAGING/core/riverhorse_libretro.so"
cp "$ROOTDIR/src/riverhorse-libretro/riverhorse_libretro.info" "$STAGING/core/"

rm -f "$OUTDIR/riverhorse_libretro-$SUFFIX.zip"
(cd "$STAGING/core" && zip -qr "$OUTDIR/riverhorse_libretro-$SUFFIX.zip" .)
echo "Created $OUTDIR/riverhorse_libretro-$SUFFIX.zip"
