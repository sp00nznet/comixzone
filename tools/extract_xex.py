#!/usr/bin/env python3
"""
Extract PE image from XEX2 with full LZX (Normal) compression support.
Based on extract_pe.py + lzx_decompress.py from 360tools.
"""
import struct
import sys
import os

# Add 360tools to path for lzx_decompress
sys.path.insert(0, r'D:\recomp\360\ecco\360tools\tools')
from lzx_decompress import LZXDecoder

try:
    from Crypto.Cipher import AES
except ImportError:
    from Cryptodome.Cipher import AES

XEX2_RETAIL_KEY = bytes([
    0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
    0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91
])
AES_BLANK_IV = b'\x00' * 16
SEC_AES_KEY_OFFSET = 0x150


def read_be32(data, off):
    return struct.unpack('>I', data[off:off+4])[0]

def read_be16(data, off):
    return struct.unpack('>H', data[off:off+2])[0]


def extract_pe(xex_path, output_path):
    with open(xex_path, 'rb') as f:
        data = bytearray(f.read())

    assert data[0:4] == b'XEX2', f"Not XEX2: {bytes(data[0:4])}"

    pe_data_offset = read_be32(data, 8)
    sec_info_offset = read_be32(data, 16)
    opt_header_count = read_be32(data, 20)

    print(f"XEX2: {len(data)} bytes")
    print(f"  PE data offset: 0x{pe_data_offset:X}")
    print(f"  Security info offset: 0x{sec_info_offset:X}")

    # Parse optional headers
    ffi_offset = 0
    entry_point = 0
    image_base = 0
    pos = 24
    for i in range(opt_header_count):
        hdr_id = read_be32(data, pos)
        hdr_val = read_be32(data, pos + 4)
        key_id = (hdr_id >> 8) & 0xFFFFFF

        if key_id == 0x000003:
            ffi_offset = hdr_val
        elif key_id == 0x000101:
            entry_point = hdr_val
        elif key_id == 0x000102:
            image_base = hdr_val
        pos += 8

    print(f"  Entry point: 0x{entry_point:08X}")
    print(f"  Image base: 0x{image_base:08X}")

    # File format info
    ffi_size = read_be32(data, ffi_offset)
    enc_type = read_be16(data, ffi_offset + 4)
    comp_type = read_be16(data, ffi_offset + 6)

    comp_names = {0: 'None', 1: 'Basic', 2: 'Normal (LZX)', 3: 'Delta'}
    enc_names = {0: 'None', 1: 'Normal'}
    print(f"  Encryption: {enc_names.get(enc_type, enc_type)}")
    print(f"  Compression: {comp_names.get(comp_type, comp_type)}")

    image_size = read_be32(data, sec_info_offset + 4)
    print(f"  Image size: 0x{image_size:X} ({image_size} bytes)")

    # Decrypt file key
    enc_key = bytes(data[sec_info_offset + SEC_AES_KEY_OFFSET:
                         sec_info_offset + SEC_AES_KEY_OFFSET + 16])

    if enc_type == 1:
        cipher = AES.new(XEX2_RETAIL_KEY, AES.MODE_CBC, AES_BLANK_IV)
        file_key = cipher.decrypt(enc_key)
        print(f"  Decrypted file key: {file_key.hex()}")
    else:
        file_key = None

    # Decrypt PE data
    raw_pe_data = bytes(data[pe_data_offset:])
    if file_key:
        pad_len = (16 - (len(raw_pe_data) % 16)) % 16
        if pad_len:
            raw_pe_data += b'\x00' * pad_len
        cipher = AES.new(file_key, AES.MODE_CBC, AES_BLANK_IV)
        decrypted_data = bytearray(cipher.decrypt(raw_pe_data))
        print(f"  Decrypted {len(decrypted_data)} bytes")
    else:
        decrypted_data = bytearray(raw_pe_data)

    # Decompress
    if comp_type == 2:  # Normal (LZX)
        print("\nDecompressing LZX blocks...")

        # LZX compression header in file format info:
        # offset+8: window_bits (4 bytes, only low byte used)
        # offset+12: first block hash (20 bytes)
        # offset+32: block descriptors {data_size(4), hash(20)}[]

        # Window info
        window_info = read_be32(data, ffi_offset + 8)
        window_bits = window_info & 0x1F  # bits 0-4
        if window_bits < 15:
            window_bits = 15
        if window_bits > 21:
            window_bits = 21
        window_size = 1 << window_bits

        print(f"  Window bits: {window_bits} (window size: {window_size})")

        # Read block descriptors
        # After the 8-byte FFI header and 4-byte window info and 20-byte first block hash
        blocks = []
        block_pos = ffi_offset + 8 + 4 + 20  # Skip window_info + first hash

        while block_pos + 24 <= ffi_offset + ffi_size:
            block_data_size = read_be32(data, block_pos)
            # 20 bytes of hash follow
            if block_data_size == 0:
                break
            blocks.append(block_data_size)
            block_pos += 24  # 4 bytes size + 20 bytes hash

        print(f"  Found {len(blocks)} LZX compression blocks")
        for i, bs in enumerate(blocks[:5]):
            print(f"    Block[{i}]: compressed size = {bs} (0x{bs:X})")
        if len(blocks) > 5:
            print(f"    ... and {len(blocks) - 5} more blocks")

        # Decompress each block
        # Each block decompresses to window_size bytes (except possibly the last)
        pe_image = bytearray()
        src_pos = 0

        decoder = LZXDecoder(window_bits)

        for i, comp_size in enumerate(blocks):
            if src_pos + comp_size > len(decrypted_data):
                print(f"  WARNING: Block {i} overflows decrypted data")
                comp_size = len(decrypted_data) - src_pos
                if comp_size <= 0:
                    break

            block_data = bytes(decrypted_data[src_pos:src_pos + comp_size])

            # Each LZX block decompresses to window_size except possibly last
            remaining = image_size - len(pe_image)
            decomp_size = min(window_size, remaining)

            if decomp_size <= 0:
                break

            try:
                decompressed = decoder.decompress(block_data, decomp_size)
                pe_image.extend(decompressed)
            except Exception as e:
                print(f"  ERROR decompressing block {i}: {e}")
                # Try to continue with remaining blocks
                pe_image.extend(b'\x00' * decomp_size)

            src_pos += comp_size

            if i % 100 == 0 and i > 0:
                print(f"    Decompressed {i}/{len(blocks)} blocks ({len(pe_image)} bytes)...")

        final_image = bytes(pe_image[:image_size])
        print(f"  Decompressed to {len(final_image)} bytes")

    elif comp_type == 1:  # Basic block
        print("\nDecompressing basic blocks...")
        blocks = []
        block_pos = ffi_offset + 8
        while block_pos + 8 <= ffi_offset + ffi_size:
            ds = read_be32(data, block_pos)
            zs = read_be32(data, block_pos + 4)
            if ds == 0 and zs == 0:
                break
            blocks.append((ds, zs))
            block_pos += 8

        pe_image = bytearray(image_size)
        src_pos = 0
        dst_pos = 0
        for ds, zs in blocks:
            pe_image[dst_pos:dst_pos + ds] = decrypted_data[src_pos:src_pos + ds]
            src_pos += ds
            dst_pos += ds + zs

        final_image = bytes(pe_image[:image_size])
    else:
        final_image = bytes(decrypted_data[:image_size])

    # Validate PE
    print(f"\nValidating PE ({len(final_image)} bytes)...")
    print(f"  First 16 bytes: {' '.join(f'{b:02X}' for b in final_image[:16])}")

    has_mz = final_image[0:2] == b'MZ'
    if has_mz:
        e_lfanew = struct.unpack('<I', final_image[0x3C:0x40])[0]
        pe_off = e_lfanew
        print(f"  MZ header found, PE at 0x{pe_off:X}")
    elif final_image[0:4] == b'PE\x00\x00':
        pe_off = 0
        print("  PE signature at offset 0")
    else:
        # Xbox 360 XEX2 PE images typically don't have MZ header
        # The PE starts at offset 0 with COFF header (no PE\0\0 signature)
        # Or it could be at a custom offset
        pe_off = None
        for try_off in [0, 0x80, 0x100, 0x200, 0x400, 0x1000]:
            if try_off + 4 <= len(final_image) and final_image[try_off:try_off+4] == b'PE\x00\x00':
                pe_off = try_off
                print(f"  Found PE signature at 0x{pe_off:X}")
                break

        if pe_off is None:
            # Check for COFF header at offset 0 (machine type 0x01F2 = PPC Xbox 360)
            if len(final_image) >= 2:
                machine = struct.unpack('<H', final_image[0:2])[0]
                if machine == 0x01F2:
                    print(f"  COFF header at offset 0 (machine=0x{machine:04X} = PPC/Xbox360)")
                    pe_off = -4  # Adjust so pe_off+4 = 0 (COFF header at 0)
                else:
                    print(f"  No standard PE header found (first word: 0x{machine:04X})")
                    pe_off = None

    # Parse sections if we found a valid header
    if pe_off is not None:
        coff_off = pe_off + 4 if pe_off >= 0 else 0
        if coff_off + 20 <= len(final_image):
            machine = struct.unpack('<H', final_image[coff_off:coff_off+2])[0]
            num_sections = struct.unpack('<H', final_image[coff_off+2:coff_off+4])[0]
            opt_hdr_size = struct.unpack('<H', final_image[coff_off+16:coff_off+18])[0]
            sec_table = coff_off + 20 + opt_hdr_size

            machine_names = {0x01F2: 'PowerPC (Xbox 360)', 0x14C: 'x86', 0x8664: 'x64'}
            print(f"  Machine: 0x{machine:04X} ({machine_names.get(machine, 'Unknown')})")
            print(f"  Sections: {num_sections}")

            for i in range(num_sections):
                off = sec_table + i * 40
                if off + 40 > len(final_image):
                    break
                name = final_image[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
                vsize = struct.unpack('<I', final_image[off+8:off+12])[0]
                vaddr = struct.unpack('<I', final_image[off+12:off+16])[0]
                rsize = struct.unpack('<I', final_image[off+16:off+20])[0]
                roff = struct.unpack('<I', final_image[off+20:off+24])[0]
                chars = struct.unpack('<I', final_image[off+36:off+40])[0]
                flags = []
                if chars & 0x20: flags.append('CODE')
                if chars & 0x40: flags.append('IDATA')
                if chars & 0x80: flags.append('UDATA')
                if chars & 0x20000000: flags.append('X')
                if chars & 0x40000000: flags.append('R')
                if chars & 0x80000000: flags.append('W')
                print(f"    {name:8s} VA=0x{image_base+vaddr:08X} VSize=0x{vsize:06X} "
                      f"Raw=0x{rsize:06X} @ 0x{roff:06X} [{','.join(flags)}]")

    with open(output_path, 'wb') as f:
        f.write(final_image)
    print(f"\nWrote {len(final_image)} bytes to {output_path}")

    # Also save metadata
    meta = {
        'entry_point': entry_point,
        'image_base': image_base,
        'image_size': image_size,
    }
    print(f"\n=== KEY INFO FOR RECOMPILATION ===")
    print(f"  Entry point:  0x{entry_point:08X}")
    print(f"  Image base:   0x{image_base:08X}")
    print(f"  Image size:   0x{image_size:X}")
    return True


if __name__ == '__main__':
    xex = sys.argv[1] if len(sys.argv) > 1 else 'game_files/default.xex'
    out = sys.argv[2] if len(sys.argv) > 2 else 'game_files/pe_image.bin'
    extract_pe(xex, out)
