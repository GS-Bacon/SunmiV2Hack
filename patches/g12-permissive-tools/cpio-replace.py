#!/usr/bin/env python3
"""Replace a file inside a cpio newc archive, allowing size change.

Usage: cpio-replace.py IN.cpio TARGET_NAME NEW_CONTENT.bin OUT.cpio
"""

import sys
from pathlib import Path

NEWC_MAGIC = b"070701"


def align4(n):
    return (n + 3) & ~3


def parse_hex(data, off, n):
    return int(data[off:off + n].decode("ascii"), 16)


def build_header(fields, name):
    keys = ["ino", "mode", "uid", "gid", "nlink", "mtime", "filesize",
            "devmajor", "devminor", "rdevmajor", "rdevminor", "namesize", "check"]
    hdr = NEWC_MAGIC
    for k in keys:
        hdr += f"{fields[k]:08X}".encode("ascii")
    hdr += name.encode("ascii") + b"\x00"
    # pad name to 4-byte boundary from start of header
    pad = (4 - (len(hdr) % 4)) % 4
    hdr += b"\x00" * pad
    return hdr


def iter_entries(data):
    off = 0
    while off + 110 <= len(data):
        if data[off:off + 6] != NEWC_MAGIC:
            raise ValueError(f"bad magic at 0x{off:x}: {data[off:off + 6]!r}")
        fields = {}
        keys = ["ino", "mode", "uid", "gid", "nlink", "mtime", "filesize",
                "devmajor", "devminor", "rdevmajor", "rdevminor", "namesize", "check"]
        for i, k in enumerate(keys):
            fields[k] = parse_hex(data, off + 6 + i * 8, 8)
        header_end = off + 110
        name_end = header_end + fields["namesize"]
        name = data[header_end:name_end].rstrip(b"\x00").decode("ascii", "replace")
        data_start = align4(name_end)
        data_end = data_start + fields["filesize"]
        next_off = align4(data_end)
        yield (off, fields, name, data_start, data_end, next_off)
        if name == "TRAILER!!!":
            break
        off = next_off


def replace_entry(cpio_bytes, target_name, new_content):
    out = bytearray()
    for start, fields, name, ds, de, next_off in iter_entries(cpio_bytes):
        # Rebuild header + data for this entry
        if name == target_name:
            fields["filesize"] = len(new_content)
            content = new_content
        else:
            content = cpio_bytes[ds:de]
        hdr = build_header(fields, name)
        out += hdr
        out += content
        pad = (4 - (len(content) % 4)) % 4
        out += b"\x00" * pad
        if name == "TRAILER!!!":
            break
    return bytes(out)


def main():
    args = sys.argv[1:]
    if len(args) != 4:
        raise SystemExit(__doc__)
    src = Path(args[0]).read_bytes()
    target = args[1]
    new = Path(args[2]).read_bytes()
    out_path = args[3]
    result = replace_entry(src, target, new)
    Path(out_path).write_bytes(result)
    print(f"src: {len(src)}B  out: {len(result)}B  delta: {len(result) - len(src):+d}B")


if __name__ == "__main__":
    main()
