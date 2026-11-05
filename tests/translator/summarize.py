#!/usr/bin/env python3
"""Summarize one translator harness run.

usage: summarize.py <version.tsv> <version.glslang.tsv>

Prints stage and preset pass rates for translation and glslang validation, and the failing
presets grouped by the first failure class.
"""
import csv
import os
import sys
from collections import defaultdict


def classify_translate(cls, msg):
    if 'AddressU' in msg or 'sampler_state' in msg:
        return 'translate: sampler_state block left by preprocessing'
    if 'derivative' in msg:
        return 'translate: ddx/ddy/fwidth (no derivatives on target)'
    if 'saturate unhandled type' in msg:
        return 'translate: saturate() on unsupported type'
    return f'translate ({cls}): {msg[:80]}'


def classify_glslang(msg):
    if 'array index out of range' in msg or 'index out of range' in msg:
        return 'glslang: constant array index out of range'
    if 'wrong operand types' in msg or 'cannot convert' in msg:
        return 'glslang: int/float mixing'
    if 'no matching overloaded' in msg:
        return 'glslang: no matching overload'
    return f'glslang: {msg[:80]}'


def pct(n, d):
    return f'{100.0 * n / d:.2f}%' if d else '-'


def main():
    tsv, gtsv = sys.argv[1], sys.argv[2]
    glslang = {}
    with open(gtsv) as f:
        for line in f:
            parts = line.rstrip('\n').split('\t')
            if len(parts) >= 2:
                glslang[parts[0][:-len('.frag')]] = (parts[1] == 'OK', parts[2] if len(parts) > 2 else '')

    presets = defaultdict(dict)
    paths = {}
    with open(tsv) as f:
        for row in csv.DictReader(f, delimiter='\t', quoting=csv.QUOTE_NONE):
            paths[row['idx']] = row['path']
            if row['status'] in ('none', 'default'):
                continue
            key = f"{row['idx']}_{row['stage']}"
            translated = row['status'] in ('ok', 'ok_default')
            valid, gmsg = glslang.get(key, (False, 'no glslang result'))
            failure = ''
            if not translated:
                failure = classify_translate(row['err_class'], row['err_msg'])
            elif not valid:
                failure = classify_glslang(gmsg)
            presets[row['idx']][row['stage']] = (translated, translated and valid, failure)

    stages = [s for p in presets.values() for s in p.values()]
    n_stages = len(stages)
    n_translated = sum(1 for s in stages if s[0])
    n_valid = sum(1 for s in stages if s[1])
    n_presets = len(presets)
    p_translated = sum(1 for p in presets.values() if all(s[0] for s in p.values()))
    p_valid = sum(1 for p in presets.values() if all(s[1] for s in p.values()))

    name = os.path.basename(tsv)
    print(f'## {name}')
    print(f'stages:  {n_stages}  translate {n_translated} ({pct(n_translated, n_stages)})'
          f'  glslang {n_valid} ({pct(n_valid, n_stages)})')
    print(f'presets with shaders: {n_presets}  translate {p_translated} ({pct(p_translated, n_presets)})'
          f'  glslang {p_valid} ({pct(p_valid, n_presets)})')

    by_class = defaultdict(list)
    for idx, p in presets.items():
        failures = [s[2] for _, s in sorted(p.items()) if s[2]]
        if failures:
            by_class[failures[0]].append(os.path.basename(paths[idx]))
    if by_class:
        print('failing presets by first failure class:')
        for cls, names in sorted(by_class.items(), key=lambda kv: (-len(kv[1]), kv[0])):
            print(f'  {len(names):4d}  {cls}')
            for n in sorted(names):
                print(f'          {n}')


if __name__ == '__main__':
    main()
