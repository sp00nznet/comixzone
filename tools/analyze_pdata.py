#!/usr/bin/env python3
"""
analyze_pdata.py - Determine manual function boundaries for Comix Zone XBLA.

XenonRecomp auto-detects function boundaries, but 33 functions have switch
tables whose labels fall outside the auto-detected range. This script
computes the correct function entries (address + size) to put in the TOML
config so each function covers its switch base AND all switch labels.
"""

import struct
import sys
import re
import bisect
from pathlib import Path

PE_IMAGE_PATH = Path("D:/recomp/360/comixzone/game_files/pe_image.bin")
SWITCH_TABLES_PATH = Path("D:/recomp/360/comixzone/config/switch_tables.toml")
FUNC_MAPPING_PATH = Path("D:/recomp/360/comixzone/ppc/ppc_func_mapping.cpp")

PDATA_FILE_OFFSET = 0x89200
TEXT_FILE_OFFSET = 0x91E00
TEXT_VA = 0x820A0000
TEXT_SIZE = 0x001D30BC

PROBLEM_ADDRS = sorted([
    0x820CCB54, 0x820D418C, 0x820DDE34, 0x82254420, 0x82255064,
    0x82258448, 0x822585FC, 0x8225B85C, 0x8225CBD0, 0x8225DE18,
    0x82260944, 0x82260AA0, 0x82260B50, 0x82260D98, 0x82260E3C,
    0x82260E90, 0x82260F0C, 0x82260F88, 0x82260FF4, 0x82261274,
    0x82261510, 0x82262634, 0x822627D8, 0x82262960, 0x82262C8C,
    0x82262D58, 0x82262E5C, 0x82263008, 0x822630DC, 0x82263274,
    0x82263784, 0x82263CBC, 0x822641E4,
])

BLR = 0x4E800020


def va_to_file(va):
    return TEXT_FILE_OFFSET + (va - TEXT_VA)


def read_insn(pe, va):
    off = va_to_file(va)
    if 0 <= off <= len(pe) - 4:
        return struct.unpack_from(">I", pe, off)[0]
    return 0


def parse_pdata(pe):
    entries = []
    offset = PDATA_FILE_OFFSET
    while offset + 8 <= TEXT_FILE_OFFSET:
        begin, data = struct.unpack_from(">II", pe, offset)
        if begin == 0 and data == 0:
            break
        flen = (data >> 8) & 0x3FFFFF
        entries.append((begin, flen * 4))
        offset += 8
    return sorted(entries)


def parse_switch_tables(path):
    content = path.read_text()
    blocks = re.split(r'\[\[switch\]\]', content)
    result = {}
    for block in blocks[1:]:
        bm = re.search(r'base\s*=\s*(0x[0-9A-Fa-f]+)', block)
        lm = re.search(r'labels\s*=\s*\[(.*?)\]', block, re.DOTALL)
        if bm and lm:
            base = int(bm.group(1), 16)
            labels = [int(a, 16) for a in re.findall(r'0x[0-9A-Fa-f]+', lm.group(1))]
            result[base] = labels
    return result


def parse_func_mapping(path):
    content = path.read_text()
    addrs = [int(m, 16) for m in re.findall(r'\{ (0x[0-9A-Fa-f]+), sub_', content)]
    return sorted(addrs)


def find_auto_func(func_addrs, target):
    """Find auto-detected function containing target: (start, next_start)."""
    idx = bisect.bisect_right(func_addrs, target) - 1
    if idx < 0:
        return (target, target + 4)
    start = func_addrs[idx]
    nxt = func_addrs[idx + 1] if idx + 1 < len(func_addrs) else start + 0x10000
    return (start, nxt)


def find_end_of_code(pe, start_va, upper_bound):
    """Scan forward from start_va to find end of executable code before upper_bound.
    Returns address AFTER the last real instruction (blr or last code before padding)."""
    scan = start_va
    last_code = start_va
    while scan < upper_bound:
        insn = read_insn(pe, scan)
        if insn == 0:
            # Hit padding -- end of code
            return last_code + 4
        last_code = scan
        if insn == BLR:
            # Check if next is padding or new function start
            if scan + 4 < upper_bound:
                nxt = read_insn(pe, scan + 4)
                if nxt == 0:
                    return scan + 4
        scan += 4
    return last_code + 4


def main():
    pe = PE_IMAGE_PATH.read_bytes()
    pdata = parse_pdata(pe)
    switches = parse_switch_tables(SWITCH_TABLES_PATH)
    func_addrs = parse_func_mapping(FUNC_MAPPING_PATH)
    text_end = TEXT_VA + TEXT_SIZE

    print(f"PE: {len(pe):,} bytes | .pdata: {len(pdata)} | switches: {len(switches)} | funcs: {len(func_addrs)}")

    # For each problem switch, determine:
    # 1. Function start: the auto-detected function start that contains the earliest address
    #    (either the switch base or the earliest label)
    # 2. Function end: past the latest label's code

    func_requirements = {}  # func_start -> required_end

    for base in PROBLEM_ADDRS:
        labels = switches.get(base, [])
        if not labels:
            print(f"WARNING: no switch table for 0x{base:08X}")
            continue

        all_addrs = [base] + labels
        min_addr = min(all_addrs)
        max_addr = max(all_addrs)

        # Function start: the auto-detected function containing min_addr
        start, _ = find_auto_func(func_addrs, min_addr)

        # Function must extend past max_addr. Find the auto-detected function
        # containing max_addr and use its end as our end.
        _, next_after_max = find_auto_func(func_addrs, max_addr)

        # Now find actual end of code in the region around max_addr
        # Use the next auto-detected function boundary as upper limit
        code_end = find_end_of_code(pe, max_addr, next_after_max)

        # Ensure we cover at least to code_end
        if start in func_requirements:
            func_requirements[start] = max(func_requirements[start], code_end)
        else:
            func_requirements[start] = code_end

    # Convert to list and merge overlapping
    entries = sorted(func_requirements.items())
    merged = []
    for addr, end in entries:
        size = end - addr
        if merged and addr < merged[-1][0] + merged[-1][1]:
            prev_addr, prev_size = merged[-1]
            new_end = max(prev_addr + prev_size, end)
            merged[-1] = (prev_addr, new_end - prev_addr)
        else:
            merged.append((addr, size))

    # Validate: check each merged entry covers its switches
    print(f"\n{'='*80}")
    print(f"Results: {len(merged)} function entries")
    print(f"{'='*80}\n")

    all_covered = set()
    for addr, size in merged:
        end = addr + size
        covered = []
        for pa in PROBLEM_ADDRS:
            if pa not in switches:
                continue
            labels = switches[pa]
            # Switch base must be in [addr, end) AND all labels must be in [addr, end)
            if addr <= pa < end and all(addr <= l < end for l in labels):
                covered.append(pa)
                all_covered.add(pa)

        first = read_insn(pe, addr)
        last = read_insn(pe, end - 4)
        print(f"  0x{addr:08X}  size=0x{size:04X}  end=0x{end:08X}  covers={len(covered)}")
        if covered:
            for c in covered:
                print(f"    - 0x{c:08X} ({len(switches[c])} labels)")

    uncovered = set(PROBLEM_ADDRS) - all_covered
    if uncovered:
        print(f"\n  UNCOVERED ({len(uncovered)}):")
        for a in sorted(uncovered):
            labels = switches.get(a, [])
            if labels:
                min_l, max_l = min(labels), max(labels)
                auto_s, auto_n = find_auto_func(func_addrs, a)
                print(f"    0x{a:08X}: auto_func=0x{auto_s:08X}, labels 0x{min_l:08X}-0x{max_l:08X}")

    # TOML output
    print(f"\n{'='*80}")
    print("TOML output:")
    print(f"{'='*80}\n")

    print("functions = [")
    for addr, size in merged:
        end = addr + size
        covered = []
        for pa in PROBLEM_ADDRS:
            if pa in switches:
                labels = switches[pa]
                if addr <= pa < end and all(addr <= l < end for l in labels):
                    covered.append(pa)
        comment = ", ".join(f"0x{c:08X}" for c in covered) if covered else "no direct coverage"
        print(f"    {{ address = 0x{addr:08X}, size = 0x{size:04X} }},  # {comment}")
    print("]")

    print(f"\nCoverage: {len(all_covered)}/{len(PROBLEM_ADDRS)}")


if __name__ == "__main__":
    main()
