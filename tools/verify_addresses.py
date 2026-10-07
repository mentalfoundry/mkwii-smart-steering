#!/usr/bin/env python3
"""Check that every PAL address the mod uses maps to the same thing in another
region after Kamek's versions.txt remapping.

Kamek leaves an address outside every versions.txt range unchanged, without
warning, so a missing or wrong range silently patches the wrong code. This
script catches that by comparing the real game binaries.

Usage:
  verify_addresses.py <pal_dir> <other_dir> <E|J|K>
Each dir must contain main.dol and StaticR.rel extracted from the disc.

Checks:
  * functions: instruction words at the remapped address match PAL up to the
    first blr; direct branches must land on remapped targets; words the REL
    leaves for relocation must carry the same relocation type.
  * vtable slots: the remapped slot's relocation points at the remapped
    function.
  * .bss singletons: every PAL code reference to the singleton, remapped,
    references the remapped singleton, and all singletons agree on one .bss
    base for the other region.
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Functions come from externals.txt (everything below the .bss range);
# vtable slots must be kept in sync with the kmWritePointer hooks in src/.
def read_externals(path):
    funcs = {}
    for line in open(path):
        m = re.match(r"^\s*([A-Za-z0-9_]+)\s*=\s*0x([0-9a-fA-F]+)", line)
        if m and int(m.group(2), 16) < 0x809BD6E0:
            funcs[m.group(1)] = int(m.group(2), 16)
    return funcs


FUNCS = read_externals(os.path.join(ROOT, "externals.txt"))
VTABLE_SLOTS = {  # slot -> function it must hold
    0x808CA774: 0x80732C70,  # AI::Player       UpdateRealPlayerDriving
    0x808CA7F8: 0x80732C70,  # AI::PlayerBike
    0x808CA850: 0x80732C70,  # AI::PlayerKart
    0x808B2D9C: 0x80521768,  # System::KPadPlayer calc
}
BSS_SINGLETONS = {
    "CourseMap_spInstance": 0x809BD6E8,
    "KPadDirector_spInstance": 0x809BD70C,
    "RaceConfig_spInstance": 0x809BD728,
    "RaceManager_spInstance": 0x809BD730,
}
PAL_TEXT_BASE = 0x805103B4  # see docs/research.md
PAL_BSS_BASE = 0x809BD6E0
MAX_FUNC_WORDS = 64

R_PPC_ADDR32, R_PPC_REL24 = 1, 10
R_RVL_NONE, R_RVL_SECT, R_RVL_STOP = 201, 202, 203


def parse_versions(path):
    regions, cur = {}, None
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"\[(\w+)\]", line)
        if m:
            cur = m.group(1)
            regions[cur] = []
            continue
        m = re.match(r"([0-9a-fA-F]+)-([0-9a-fA-F*]+):\s*([+-])0x([0-9a-fA-F]+)", line)
        if m and cur:
            end = 0xFFFFFFFF if m.group(2) == "*" else int(m.group(2), 16)
            delta = int(m.group(4), 16) * (1 if m.group(3) == "+" else -1)
            regions[cur].append((int(m.group(1), 16), end, delta))
    return regions


def remap(regions, region, addr):
    for start, end, delta in regions[region]:
        if start <= addr <= end:
            return addr + delta, True
    return addr, False


class Dol:
    def __init__(self, path):
        self.data = d = open(path, "rb").read()
        self.secs = []
        for i in range(18):
            off, addr, size = (struct.unpack_from(">I", d, base + i * 4)[0] for base in (0, 0x48, 0x90))
            if size:
                self.secs.append((addr, size, off))

    def word(self, addr):
        for a, s, o in self.secs:
            if a <= addr < a + s:
                return struct.unpack_from(">I", self.data, o + addr - a)[0]
        return None


class Rel:
    def __init__(self, path, text_base):
        self.data = d = open(path, "rb").read()
        nsec, sec_off = struct.unpack_from(">II", d, 0xC)
        self.secs = []
        for i in range(nsec):
            off, size = struct.unpack_from(">II", d, sec_off + i * 8)
            self.secs.append((off & ~1, size, bool(off & 1)))
        self.text = next(i for i, (o, s, x) in enumerate(self.secs) if s and x)
        self.bss = [i for i, (o, s, x) in enumerate(self.secs) if s and o == 0]
        self.load = text_base - self.secs[self.text][0]
        imp_off, imp_size = struct.unpack_from(">II", d, 0x28)
        self.relocs = {}  # (section, offset) -> (type, module, target section, addend)
        for i in range(imp_size // 8):
            mod, p = struct.unpack_from(">II", d, imp_off + i * 8)
            sect = pos = 0
            while True:
                o, t, s, a = struct.unpack_from(">HBBI", d, p)
                p += 8
                if t == R_RVL_STOP:
                    break
                pos += o
                if t == R_RVL_SECT:
                    sect, pos = s, 0
                elif t != R_RVL_NONE:
                    self.relocs[(sect, pos)] = (t, mod, s, a)

    def locate(self, addr):
        for i, (o, s, x) in enumerate(self.secs):
            if o and s and self.load + o <= addr < self.load + o + s:
                return i, addr - self.load - o
        return None

    def addr_of(self, sect, off):
        return self.load + self.secs[sect][0] + off

    def word(self, sect, off):
        return struct.unpack_from(">I", self.data, self.secs[sect][0] + off)[0]


def branch_target(word, addr):
    if word >> 26 != 18 or word & 2:  # b/bl, relative only
        return None
    disp = word & 0x03FFFFFC
    if disp & 0x02000000:
        disp -= 0x04000000
    return (addr + disp) & 0xFFFFFFFF


def main():
    pal_dir, other_dir, region = sys.argv[1:4]
    regions = parse_versions(os.path.join(ROOT, "versions.txt"))
    o_text_base, ok = remap(regions, region, PAL_TEXT_BASE)
    if not ok:
        sys.exit("StaticR .text base is not covered by versions.txt for " + region)
    pdol, odol = Dol(os.path.join(pal_dir, "main.dol")), Dol(os.path.join(other_dir, "main.dol"))
    prel = Rel(os.path.join(pal_dir, "StaticR.rel"), PAL_TEXT_BASE)
    orel = Rel(os.path.join(other_dir, "StaticR.rel"), o_text_base)

    failures = []

    def report(good, msg):
        print(("  ok    " if good else "  FAIL  ") + msg)
        if not good:
            failures.append(msg)

    print("Region %s: StaticR loaded at 0x%08X (PAL 0x%08X)" % (region, orel.load, prel.load))

    print("Functions:")
    for name, paddr in FUNCS.items():
        oaddr, mapped = remap(regions, region, paddr)
        why = "" if mapped else "address not covered by versions.txt"
        ploc, oloc = prel.locate(paddr), orel.locate(oaddr)
        for k in range(MAX_FUNC_WORDS):
            if why:
                break
            pa, oa = paddr + 4 * k, oaddr + 4 * k
            if ploc:
                if not oloc:
                    why = "not inside StaticR in this region"
                    break
                pw, ow = prel.word(ploc[0], ploc[1] + 4 * k), orel.word(oloc[0], oloc[1] + 4 * k)
                pr, orr = prel.relocs.get((ploc[0], ploc[1] + 4 * k)), orel.relocs.get((oloc[0], oloc[1] + 4 * k))
            else:
                pw, ow, pr, orr = pdol.word(pa), odol.word(oa), None, None
                if pw is None or ow is None:
                    why = "outside main.dol sections"
                    break
            if (pr is None) != (orr is None) or (pr and pr[0] != orr[0]):
                why = "relocation differs at +0x%X" % (4 * k)
            elif pr is None and pw != ow:
                pt, ot = branch_target(pw, pa), branch_target(ow, oa)
                if pt is None or ot is None or remap(regions, region, pt)[0] != ot:
                    why = "instruction differs at +0x%X (%08X vs %08X)" % (4 * k, pw, ow)
            if pw == 0x4E800020:
                break
        report(not why, "%-34s 0x%08X -> 0x%08X %s" % (name, paddr, oaddr, why))

    print("Vtable slots:")
    for pslot, pfunc in VTABLE_SLOTS.items():
        oslot, m1 = remap(regions, region, pslot)
        ofunc, _ = remap(regions, region, pfunc)
        ploc, oloc = prel.locate(pslot), orel.locate(oslot)
        pr = prel.relocs.get(ploc) if ploc else None
        orr = orel.relocs.get(oloc) if oloc else None
        good = (m1 and pr and orr and pr[0] == orr[0] == R_PPC_ADDR32
                and (pr[2], pr[3]) == prel.locate(pfunc) and (orr[2], orr[3]) == orel.locate(ofunc))
        why = "" if good else ("address not covered by versions.txt" if not m1 else "slot does not hold the function")
        report(bool(good), "slot 0x%08X -> 0x%08X holds 0x%08X %s" % (pslot, oslot, ofunc, why))

    print("Singletons (.bss):")
    o_bss_bases = set()
    for name, paddr in BSS_SINGLETONS.items():
        oaddr, mapped = remap(regions, region, paddr)
        poff = paddr - PAL_BSS_BASE
        refs = [k for k, v in prel.relocs.items() if v[2] in prel.bss and v[3] == poff and prel.secs[k[0]][2]]
        why = "" if mapped else "address not covered by versions.txt"
        if not refs:
            why = why or "no PAL code reference found"
        for sect, off in refs:
            if why:
                break
            oref, _ = remap(regions, region, prel.addr_of(sect, off))
            oloc = orel.locate(oref)
            orr = orel.relocs.get(oloc) if oloc else None
            if orr is None or orr[2] not in orel.bss:
                why = "reference at 0x%08X not found in this region" % oref
            else:
                o_bss_bases.add(oaddr - orr[3])
        report(not why, "%-26s 0x%08X -> 0x%08X (%d code refs) %s" % (name, paddr, oaddr, len(refs), why))
    report(len(o_bss_bases) == 1, ".bss base agrees across singletons: %s"
           % ", ".join("0x%08X" % b for b in sorted(o_bss_bases)))

    print("\n" + ("ALL CHECKS PASSED" if not failures else "%d CHECK(S) FAILED" % len(failures)))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
