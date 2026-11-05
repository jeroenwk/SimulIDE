#!/bin/sh
# Build SimulIDE's bundled fonts (Inter + JetBrains Mono) from the pinned upstream
# releases: subset to the characters SimulIDE needs, write TTF (used by Qt) and
# WOFF2 (embedded in SVG exports) into resources/fonts/. See specs/inter-fonts.md.
#
# Usage: sh tools/fonts_subset.sh            (run from the SimulIDE root or anywhere)
# Needs: curl, unzip, shasum, uv (runs fonttools + brotli without installing them)
set -eu

log() { echo "[fonts_subset] $*"; }

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/resources/fonts"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/simulide-fonts"
mkdir -p "$CACHE" "$OUT/ttf" "$OUT/woff2"

INTER_VER=4.1
INTER_URL="https://github.com/rsms/inter/releases/download/v$INTER_VER/Inter-$INTER_VER.zip"
INTER_SHA=9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e
JBM_VER=2.304
JBM_URL="https://github.com/JetBrains/JetBrainsMono/releases/download/v$JBM_VER/JetBrainsMono-$JBM_VER.zip"
JBM_SHA=6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf

fetch() {   # url sha file
    if [ ! -f "$CACHE/$3" ]; then
        log "downloading $1"
        curl -sSLf -o "$CACHE/$3.part" "$1"
        mv "$CACHE/$3.part" "$CACHE/$3"
    else
        log "cached $CACHE/$3"
    fi
    echo "$2  $CACHE/$3" | shasum -a 256 -c - >/dev/null || { log "checksum mismatch for $3"; exit 1; }
    log "checksum ok: $3"
}

fetch "$INTER_URL" "$INTER_SHA" "Inter-$INTER_VER.zip"
fetch "$JBM_URL"   "$JBM_SHA"   "JetBrainsMono-$JBM_VER.zip"

SRC="$CACHE/src"
rm -rf "$SRC"; mkdir -p "$SRC"
unzip -q -o -j "$CACHE/Inter-$INTER_VER.zip" "extras/ttf/Inter-Regular.ttf" "extras/ttf/Inter-Medium.ttf" \
    "extras/ttf/Inter-SemiBold.ttf" "extras/ttf/Inter-Bold.ttf" "LICENSE.txt" -d "$SRC"
mv "$SRC/LICENSE.txt" "$OUT/OFL-Inter.txt"
unzip -q -o -j "$CACHE/JetBrainsMono-$JBM_VER.zip" "fonts/ttf/JetBrainsMono-Regular.ttf" "fonts/ttf/JetBrainsMono-Medium.ttf" \
    "fonts/ttf/JetBrainsMono-SemiBold.ttf" "fonts/ttf/JetBrainsMono-Bold.ttf" "fonts/ttf/JetBrainsMono-Italic.ttf" "OFL.txt" -d "$SRC"
mv "$SRC/OFL.txt" "$OUT/OFL-JetBrainsMono.txt"

# Latin, Latin-1, Latin Ext-A/B, IPA-free Greek, Cyrillic, punctuation, super/subscripts,
# currency, letterlike (Ω ohm sign), number forms, arrows, math operators, misc technical,
# geometric shapes, a few dingbats (✓ ✗)
UNI="U+0000-024F,U+02C6-02DD,U+0300-036F,U+0370-03FF,U+0400-04FF,U+2000-206F,U+2070-209F,U+20A0-20BF,U+2100-218F,U+2190-21FF,U+2200-22FF,U+2300-23FF,U+25A0-25FF,U+2713-2717"
UNI_MONO="$UNI,U+2500-259F"
FEAT="kern,ccmp,locl,mark,mkmk"   # no calt/liga: "->", "!=" stay plain characters

subset() {   # src-ttf unicodes
    name=$(basename "$1" .ttf)
    for flavor in ttf woff2; do
        if [ "$flavor" = ttf ]; then f="--hinting"; else f="--flavor=woff2 --no-hinting --desubroutinize"; fi
        uv tool run --quiet --from 'fonttools[woff]' pyftsubset "$1" --unicodes="$2" \
            --layout-features="$FEAT" --name-IDs='*' --name-languages='*' --notdef-outline \
            $f --output-file="$OUT/$flavor/$name.$flavor"
    done
    log "$name: ttf $(wc -c < "$OUT/ttf/$name.ttf") B, woff2 $(wc -c < "$OUT/woff2/$name.woff2") B"
}

for w in Regular Medium SemiBold Bold; do subset "$SRC/Inter-$w.ttf" "$UNI"; done
for w in Regular Medium SemiBold Bold Italic; do subset "$SRC/JetBrainsMono-$w.ttf" "$UNI_MONO"; done
log "done: $OUT"
