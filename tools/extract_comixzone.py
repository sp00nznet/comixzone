"""
Custom STFS extractor for Comix Zone XBLA.
Fixes issue where first entry's start_block=0 causes zero file table reads.
"""
import os
import struct
import sys


def get_cluster(startclust, offset):
    """Get the real starting cluster offset (from wxPirs algorithm)."""
    rst = 0
    while startclust >= 170:
        startclust //= 170
        rst += (startclust + 1) * offset
    return rst


def extract(input_path, output_dir):
    with open(input_path, 'rb') as f:
        magic = f.read(4)
        print(f"Magic: {magic}")
        assert magic in (b'LIVE', b'PIRS', b'CON '), f"Not STFS: {magic}"

        fsize = os.path.getsize(input_path)

        # Determine file table start
        f.seek(0xC032)
        pathind = struct.unpack(">H", f.read(2))[0]
        if pathind == 0xFFFF:
            start = 0xC000
            offset = 0x1000
        else:
            start = 0xD000
            offset = 0x2000

        print(f"File table start: 0x{start:X}, offset: 0x{offset:X}")

        # Read a generous amount of file table data (scan until empty entries)
        # Instead of relying on firstclust, read up to 16 blocks
        max_ft_blocks = 16
        f.seek(start)
        ft_data = f.read(0x1000 * max_ft_blocks)

        paths = {0xFFFF: ""}
        os.makedirs(output_dir, exist_ok=True)
        files_extracted = []

        num_entries = len(ft_data) // 64
        for i in range(num_entries):
            cur = ft_data[i * 64:(i + 1) * 64]
            namelen_flags = cur[40]

            name_len = namelen_flags & 0x3F
            is_dir = bool(namelen_flags & 0x80)
            is_contiguous = bool(namelen_flags & 0x40)

            if name_len == 0:
                break

            outname = cur[0:name_len].decode('ascii', errors='replace')
            clustsize1 = struct.unpack("<H", cur[41:43])[0] + (cur[43] << 16)
            startclust = struct.unpack("<H", cur[47:49])[0] + (cur[49] << 16)
            pathind = struct.unpack(">H", cur[50:52])[0]
            filelen = struct.unpack(">I", cur[52:56])[0]

            type_str = "DIR " if is_dir else "FILE"
            contig = " [contiguous]" if is_contiguous else ""
            print(f"  [{type_str}] {outname:<45s} {filelen:>12d} bytes  block={startclust}  blocks={clustsize1}{contig}  path={pathind}")

            if is_dir:
                paths[i] = paths.get(pathind, "") + outname + "/"
                full_dir = os.path.join(output_dir, paths[i])
                os.makedirs(full_dir, exist_ok=True)
            else:
                parent = paths.get(pathind, "")
                out_path = os.path.join(output_dir, parent, outname)
                os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)

                adstart = startclust * 0x1000 + start
                remaining = filelen
                file_data = bytearray()
                cur_clust = startclust

                while remaining > 0:
                    realstart = adstart + get_cluster(cur_clust, offset)
                    f.seek(realstart)
                    chunk = f.read(min(0x1000, remaining))
                    if not chunk:
                        print(f"    WARNING: Read failed at offset 0x{realstart:X}")
                        break
                    file_data.extend(chunk)
                    cur_clust += 1
                    adstart += 0x1000
                    remaining -= len(chunk)

                with open(out_path, 'wb') as outfile:
                    outfile.write(bytes(file_data[:filelen]))

                file_magic = bytes(file_data[:4]) if len(file_data) >= 4 else b''
                magic_str = ""
                if file_magic == b'XEX2':
                    magic_str = " >> XEX2 EXECUTABLE"
                elif file_magic == b'\x89PNG':
                    magic_str = " >> PNG"

                print(f"    -> {out_path} ({filelen} bytes){magic_str}")
                files_extracted.append((outname, out_path, filelen))

        print(f"\nExtracted {len(files_extracted)} file(s) to {output_dir}")
        return files_extracted


if __name__ == '__main__':
    stfs_file = sys.argv[1] if len(sys.argv) > 1 else \
        r"extracted/584109A1/000D0000/DB1C551FA4736189BA9D9C2A7D118A7F33A7D8D558"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "game_files"
    extract(stfs_file, out_dir)
