#!/usr/bin/env bash
# Usage: make-libretro-package.sh <core file> <output directory> <name suffix> [packaged core name]
# Creates riverhorse_libretro-<suffix>.zip: the core and its .info file, for the frontend's cores directory.
# Set STRIP to the platform's strip tool when cross-compiling.

set -e

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
	echo "Syntax: $0 <core file> <output directory> <name suffix> [packaged core name]"
	exit 1
fi

ROOTDIR=$(realpath "$(dirname "${BASH_SOURCE[0]}")/../..")
CORE=$(realpath "$1")
mkdir -p "$2"
OUTDIR=$(realpath "$2")
SUFFIX="$3"
NAME="${4:-$(basename "$CORE")}"

STAGING=$(mktemp -d)
trap 'rm -rf "$STAGING"' EXIT

cp "$CORE" "$STAGING/$NAME"
case "$NAME" in
	*.dylib) "${STRIP:-strip}" -x "$STAGING/$NAME" ;;
	*) "${STRIP:-strip}" --strip-unneeded "$STAGING/$NAME" ;;
esac
case "$NAME" in
	*.so) patchelf --remove-rpath "$STAGING/$NAME" ;;
esac
cp "$ROOTDIR/src/riverhorse-libretro/riverhorse_libretro.info" "$STAGING/"

rm -f "$OUTDIR/riverhorse_libretro-$SUFFIX.zip"
(cd "$STAGING" && zip -qr "$OUTDIR/riverhorse_libretro-$SUFFIX.zip" .)
echo "Created $OUTDIR/riverhorse_libretro-$SUFFIX.zip"
