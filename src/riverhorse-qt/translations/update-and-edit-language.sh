#!/usr/bin/env bash
# Usage: update-and-edit-language.sh <language code> [Qt bin directory]
# Refreshes riverhorse-qt_<code>.ts from the source, then opens it in Qt Linguist.
# Pass the Qt bin directory if lupdate and linguist are not on PATH.

set -e

if [ "$#" -lt 1 ]; then
	echo "Syntax: $0 <language code> [Qt bin directory]"
	exit 1
fi

DIR=$(realpath "$(dirname "${BASH_SOURCE[0]}")")
QTBIN="${2:+$2/}"
cd "$DIR"
"${QTBIN}lupdate" ../ ../../core/ ../../util/ \
	-tr-function-alias QT_TRANSLATE_NOOP+=TRANSLATE,QT_TRANSLATE_NOOP+=TRANSLATE_SV,QT_TRANSLATE_NOOP+=TRANSLATE_STR,QT_TRANSLATE_NOOP+=TRANSLATE_FS,QT_TRANSLATE_N_NOOP3+=TRANSLATE_FMT,QT_TRANSLATE_NOOP+=TRANSLATE_NOOP,translate+=TRANSLATE_PLURAL_STR,translate+=TRANSLATE_PLURAL_FS \
	-noobsolete -ts "riverhorse-qt_$1.ts"
"${QTBIN}linguist" "riverhorse-qt_$1.ts" &
