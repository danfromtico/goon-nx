#!/usr/bin/env python3
"""Sampled addresses (sampler.py output) that no known function covers, with
a proposed function start for each.
    python3 uncovered.py samples.txt [functions.json]"""
import bisect, io, contextlib, json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.join(HERE, '..', '..')
sys.path.insert(0, os.path.join(REPO, 'third_party', 'xboxrecomp'))
from tools.disasm.disasm import Disassembler

samples = sys.argv[1]
fj = sys.argv[2] if len(sys.argv) > 2 else \
    os.path.join(REPO, 'third_party', 'xboxrecomp', 'tools', 'disasm', 'output', 'functions.json')
fl = json.load(open(fj))
bounds = sorted((int(f['start'], 16), int(f['end'], 16)) for f in fl)
starts = [b[0] for b in bounds]

def covered(a):
    i = bisect.bisect_right(starts, a) - 1
    return i >= 0 and a < bounds[i][1]

d = Disassembler(os.path.join(REPO, 'work', 'xbe', 'default.xbe'), stats_only=True)
with contextlib.redirect_stdout(io.StringIO()):
    d.run()
e, img = d.engine, d.image

def start_of(a):
    """Nearest aligned function start at or before a, not past a known one."""
    i = bisect.bisect_right(starts, a) - 1
    floor = bounds[i][1] if i >= 0 else 0
    s = a & ~0xF
    while s >= floor and a - s < 0x10000:
        before = img.read_bytes_at_va(s - 3, 3) or b'\0\0\0'
        if e.probes_as_prologue(s) and (before[2] in (0x90, 0xCC, 0xC3) or before[0] == 0xC2):
            return s
        s -= 0x10
    return None

hits = {}
for line in open(samples):
    p = line.split()
    if len(p) != 3 or p[0] not in ('E', 'S'):
        continue
    a = int(p[1], 16)
    if 0x11000 <= a < 0x239620 and not covered(a):
        hits.setdefault(a, [0, 0])[0 if p[0] == 'E' else 1] += int(p[2])

proposed = {}
for a, (ne, ns) in sorted(hits.items()):
    s = start_of(a)
    proposed.setdefault(s, []).append((a, ne, ns))
for s, items in sorted(proposed.items(), key=lambda kv: (kv[0] is None, kv[0] or 0)):
    tot = sum(x[1] + x[2] for x in items)
    print(('%08X' % s) if s is not None else '????????', 'samples', tot,
          'addrs', ' '.join('%08X' % x[0] for x in items[:6]))
