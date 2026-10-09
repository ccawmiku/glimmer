#!/usr/bin/env python3
"""Compare the actual C++ port against the unchanged upstream JavaScript renderer."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
def run(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True)
with tempfile.TemporaryDirectory() as tmp:
    runner = str(Path(tmp) / 'gallery')
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Isrc/gallery', 'src/gallery/gallery.cpp', 'tests/gallery_runner.cpp', '-o', runner], cwd=ROOT, check=True)
    seeds = [1, 42, 20261008, 4294967295] + [i * 2654435761 & 0xffffffff for i in range(1, 61)]
    worst = 0
    pictures = set()
    for seed in seeds:
        actual = json.loads(run(runner, str(seed)))
        expected = json.loads(run('node', 'tests/gallery_reference.mjs', str(seed)))
        diff = max(abs(a-b) for a,b in zip(actual['raw'],expected['raw']))
        worst = max(worst, diff)
        assert diff < 1e-5, (seed, diff)
        assert actual['pixel_hash'] == expected['pixel_hash'], (seed, 'native scanline pixels differ from upstream canvas draws')
        assert actual['edge'] == expected['edge'], seed
        assert actual['background'] == expected['background'], seed
        expected['stripes'] = [s for s in expected['stripes'] if s[0] < 240]
        assert actual['stripes'] == expected['stripes'], (seed, 'stripe geometry/color/layer order differ')
        pixels = []
        for y in range(240):
            row = [actual['background']] * 240
            for pos,size,color,vertical in actual['stripes']:
                if vertical: row[pos:min(240,pos+size+actual['edge'])] = [color]*min(240-pos,size+actual['edge'])
                elif pos <= y < pos+size+actual['edge']: row = [color]*240
            pixels.extend(row)
        digest = hashlib.sha256(bytes(v for c in pixels for v in (c>>8,c&255))).hexdigest()
        pictures.add(digest)
    assert len(pictures) == len(seeds), 'independent seeds must produce independent pictures'
    print(json.dumps({'seeds':len(seeds),'unique_frames':len(pictures),'max_decoder_error':worst,'frame_bytes':actual['frame_bytes'],'oracle':'upstream JS modules; all stripe descriptors and RGB565 pixels match'},ensure_ascii=False,indent=2))
