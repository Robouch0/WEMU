#!/usr/bin/env python3
"""rpxtool.py -- inspect a Wii U RPX (big-endian ELF32, SHF_DEFLATED sections).

Subcommands (addresses are real guest addresses, hex, 0x optional):
  secs                          list sections (addr, size, flags)
  disasm ADDR [COUNT]           disassemble COUNT instructions (default 40)
  word ADDR [COUNT]             dump raw big-endian words
  callers ADDR                  find bl/b to ADDR across .text
  store ADDR                    find instructions whose disasm mentions ADDR (const refs)
  xref  ADDR                    find lis/addi or lis/ori pairs materialising ADDR

NOTE: .text is SHF_DEFLATED; the address span is the DECOMPRESSED length. Whole-.text
scans decode per-word (capstone list-disasm desyncs on embedded data words).
"""
import argparse
import sys, zlib, struct

SHF_DEFLATED = 0x08000000


class Rpx:
    def __init__(self, path):
        self.blob = open(path, "rb").read()
        b = self.blob
        e_shoff, = struct.unpack_from(">I", b, 0x20)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(">HHH", b, 0x2E)
        self.secs = []
        for i in range(e_shnum):
            o = e_shoff + i * e_shentsize
            name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from(">10I", b, o)
            self.secs.append(dict(i=i, name_off=name, type=typ, flags=flags, addr=addr,
                                  off=off, size=size, link=link))
        strt = self.secs[e_shstrndx]
        sd = self._raw(strt)
        for s in self.secs:
            end = sd.find(b"\0", s["name_off"])
            s["name"] = sd[s["name_off"]:end].decode("ascii", "replace")
            s["data"] = None

    def _raw(self, s):
        d = self.blob[s["off"]:s["off"] + s["size"]]
        if s["flags"] & SHF_DEFLATED:
            d = zlib.decompress(d[4:])
        return d

    def data(self, s):
        if s["data"] is None:
            s["data"] = self._raw(s) if s["type"] != 8 else b""
        return s["data"]

    def sec_for(self, addr):
        for s in self.secs:
            if not s["addr"]:
                continue
            span = len(self.data(s)) if s["flags"] & SHF_DEFLATED else s["size"]
            if s["addr"] <= addr < s["addr"] + span:
                return s
        return None

    def read(self, addr, n):
        s = self.sec_for(addr)
        if not s:
            raise SystemExit("no section for 0x%08X" % addr)
        o = addr - s["addr"]
        return self.data(s)[o:o + n]

    def text(self):
        s = next(x for x in self.secs if x["name"] == ".text")
        return s["addr"], self.data(s)


def md():
    import capstone
    m = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    m.detail = False
    return m


def cmd_secs(r, args):
    for s in r.secs:
        span = len(r.data(s)) if (s["flags"] & SHF_DEFLATED and s["type"] != 8) else s["size"]
        print("%-20s type=%-2d flags=%08X addr=%08X size=%08X span=%08X%s"
              % (s["name"], s["type"], s["flags"], s["addr"], s["size"], span,
                 "  [DEFLATED]" if s["flags"] & SHF_DEFLATED else ""))


def cmd_disasm(r, args):
    addr = int(args[0], 16)
    n = int(args[1]) if len(args) > 1 else 40
    code = r.read(addr, n * 4)
    for ins in md().disasm(code, addr):
        print("%08X  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))


def cmd_word(r, args):
    addr = int(args[0], 16)
    n = int(args[1]) if len(args) > 1 else 8
    d = r.read(addr, n * 4)
    for i in range(0, len(d), 16):
        row = d[i:i + 16]
        print("%08X: %s" % (addr + i, " ".join("%08X" % w for (w,) in struct.iter_unpack(">I", row))))


def cmd_callers(r, args):
    target = int(args[0], 16)
    base, code = r.text()
    hits = 0
    for off in range(0, len(code) - 3, 4):
        w, = struct.unpack_from(">I", code, off)
        if (w >> 26) != 18:                      # b / bl / ba / bla
            continue
        li = w & 0x03FFFFFC
        if li & 0x02000000:
            li -= 0x04000000
        pc = base + off
        dst = li if (w & 2) else pc + li
        if dst == target:
            print("%08X  %s -> %08X" % (pc, "bl" if (w & 1) else "b", dst))
            hits += 1
    print("# %d caller(s)" % hits)


def cmd_xref(r, args):
    """find lis rX,hi ; (addi|ori) rY,rX,lo materialising ADDR"""
    target = int(args[0], 16)
    hi, lo = (target >> 16) & 0xFFFF, target & 0xFFFF
    hi_adj = (hi - 1) & 0xFFFF                   # for sign-extended addi
    base, code = r.text()
    hits = 0
    lis = {}
    for off in range(0, len(code) - 3, 4):
        w, = struct.unpack_from(">I", code, off)
        pc = base + off
        op = w >> 26
        if op == 15:                             # addis/lis
            rt, ra, imm = (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
            if ra == 0:
                lis[rt] = (pc, imm)
            continue
        if op in (14, 24):                       # addi, ori
            rt = (w >> 21) & 31 if op == 14 else (w >> 16) & 31
            ra = (w >> 16) & 31 if op == 14 else (w >> 21) & 31
            imm = w & 0xFFFF
            src = lis.get(ra)
            if not src:
                continue
            want = hi_adj if (op == 14 and imm & 0x8000) else hi
            if src[1] == want and imm == lo:
                print("%08X (lis @%08X) -> %08X" % (pc, src[0], target))
                hits += 1
    print("# %d xref(s)" % hits)


def cmd_store(r, args):
    """scan .text for any instruction whose immediate equals ADDR's low half (cheap grep)."""
    target = int(args[0], 16)
    base, code = r.text()
    m = md()
    n = 0
    for off in range(0, len(code) - 3, 4):
        w, = struct.unpack_from(">I", code, off)
        if (w & 0xFFFF) != (target & 0xFFFF):
            continue
        for ins in m.disasm(code[off:off + 4], base + off):
            print("%08X  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
            n += 1
    print("# %d hit(s)" % n)


CMDS = dict(secs=cmd_secs, disasm=cmd_disasm, word=cmd_word, callers=cmd_callers,
            store=cmd_store, xref=cmd_xref)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rpx", required=True, help="Path to the RPX to inspect")
    parser.add_argument("command", choices=CMDS)
    parser.add_argument("args", nargs="*")
    options = parser.parse_args()
    CMDS[options.command](Rpx(options.rpx), options.args)
