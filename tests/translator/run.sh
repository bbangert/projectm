#!/bin/sh
# Translate every warp/comp shader of a preset pack and validate the GLSL with glslang.
#
#   run.sh <projectM-translate-presets> <preset_dir> <work_dir> [es100|es300|glsl330 ...]
#
# Per version V this writes <work_dir>/V/*.frag, V.tsv (translation) and V.glslang.tsv, then
# prints a summary (summarize.py). GLSLANG overrides the glslang binary.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
TRANSLATE=$1
PACK=$2
WORK=$3
shift 3
[ $# -gt 0 ] || set -- es100

mkdir -p "$WORK"
find "$PACK" -name '*.milk' | LC_ALL=C sort > "$WORK/presets.txt"
for v in "$@"; do
    rm -rf "${WORK:?}/$v"
    mkdir -p "$WORK/$v"
    "$TRANSLATE" "$v" "$WORK/$v" < "$WORK/presets.txt" > "$WORK/$v.tsv"
    find "$WORK/$v" -name '*.frag' -print0 | LC_ALL=C sort -z | xargs -0 -r -P "$(nproc)" -n 40 "$HERE/check_glslang.sh" \
        | LC_ALL=C sort > "$WORK/$v.glslang.tsv"
    python3 "$HERE/summarize.py" "$WORK/$v.tsv" "$WORK/$v.glslang.tsv"
done
