#!/usr/bin/env python3
"""Disassemble DOAX's default.xbe at a guest VA, naming kernel imports.

    python3 tools/xbedis.py ADDR [COUNT] [--xbe PATH]
    python3 tools/xbedis.py --refs ADDR      # dwords in the image equal to ADDR
"""
import argparse, json, os, struct, sys
import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_XBE = os.path.join(HERE, "..", "work", "xbe", "default.xbe")


def load(xbe_path):
    x = open(xbe_path, "rb").read()
    base = struct.unpack_from("<I", x, 0x104)[0]
    nsec = struct.unpack_from("<I", x, 0x11C)[0]
    shdr = struct.unpack_from("<I", x, 0x120)[0] - base
    secs = []
    for i in range(nsec):
        o = shdr + i * 56
        _, va, vsz, raw, rsz, name_va = struct.unpack_from("<IIIIII", x, o)
        n = name_va - base
        name = x[n:x.index(b"\0", n)].decode()
        secs.append((name, va, vsz, raw, rsz))
    names = {}
    ana = os.path.splitext(xbe_path)[0] + "_analysis.json"
    if os.path.exists(ana):
        d = json.load(open(ana))
        for imp in d.get("kernel_imports", []):
            names[imp.get("ordinal")] = imp.get("name")
    thunk = struct.unpack_from("<I", x, 0x158)[0] ^ 0x5B6D40B6   # retail key
    return x, secs, names, thunk


def off(secs, va):
    for name, sva, vsz, raw, rsz in secs:
        if sva <= va < sva + min(vsz, rsz):
            return raw + va - sva
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addr")
    ap.add_argument("count", nargs="?", type=int, default=40)
    ap.add_argument("--xbe", default=DEFAULT_XBE)
    ap.add_argument("--refs", action="store_true")
    a = ap.parse_args()
    x, secs, names, thunk = load(a.xbe)
    va = int(a.addr, 16)
    if a.refs:
        needle = struct.pack("<I", va)
        for name, sva, vsz, raw, rsz in secs:
            data = x[raw:raw + rsz]
            i = data.find(needle)
            while i >= 0:
                print(f"0x{sva + i:08X} in {name}")
                i = data.find(needle, i + 1)
        return
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    o = off(secs, va)
    if o is None:
        sys.exit("address not in a loaded section")
    n = 0
    for ins in md.disasm(x[o:o + a.count * 16], va):
        note = ""
        for tok in ins.op_str.replace("[", " ").replace("]", " ").split():
            if tok.startswith("0x"):
                t = int(tok, 16)
                if thunk <= t < thunk + 0x400:
                    to = off(secs, t)
                    if to is not None:
                        ordn = struct.unpack_from("<I", x, to)[0] & 0x7FFFFFFF
                        note = f"  ; {names.get(ordn, '#%d' % ordn)}"
        print(f"0x{ins.address:08X}  {ins.mnemonic:6} {ins.op_str}{note}")
        n += 1
        if n >= a.count:
            break


if __name__ == "__main__":
    main()
