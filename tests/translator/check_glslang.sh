#!/bin/sh
# usage: check_glslang.sh file.frag... -> "<file>\tOK|FAIL\t<first error>" per file.
# glslang picks the profile from each file's #version line. GLSLANG overrides the binary.
G=${GLSLANG:-glslang}
for f in "$@"; do
    if out=$("$G" "$f" 2>&1); then
        printf '%s\tOK\t\n' "$(basename "$f")"
    else
        e=$(printf '%s\n' "$out" | grep -m1 'ERROR:' | sed -E 's/ERROR: [^:]*:[0-9]+: //' | tr '\t' ' ' | cut -c1-160)
        printf '%s\tFAIL\t%s\n' "$(basename "$f")" "$e"
    fi
done
