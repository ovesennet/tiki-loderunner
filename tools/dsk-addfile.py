#!/usr/bin/env python3
"""dsk-addfile.py - Put a CP/M file into a TIKI-100 disk image, host-side.

`deploy.ps1` only understands the 400K layout. This handles all four
geometries the TIKI-100 uses, so the 90K, 200K and 800K Styx-style images can
be loaded with the game too. It can also rewrite the auto-start command the
CCP executes at boot, and erase files.

The directory-entry construction follows `diskview.c` (AddFileToDisk) in the
TIKI-100 emulator source, but the geometry table does **not**: it was measured
by running KAT on each image and matching the file list, free space and
directory capacity the machine itself reports. Two entries differ from
diskview.c, and one of them silently corrupts data:

  * the 90 KB format interleaves its sectors with a skew of 5, so a file
    written in linear order loads as garbage - the symptom is a disk that
    boots and then dies with "AVBRUTT, adr: ...";
  * the real directory holds 32 entries on 90 KB and 128 on 800 KB, where
    diskview.c assumes 64 and 256. Writing into the slots past the end lands
    on top of file data.

Usage:
  python tools/dsk-addfile.py IMAGE.DSK [HOSTFILE] [--name NAME.EXT]
                              [--replace] [--autostart CMD]
                              [--delete NAME] [--list]
"""
import argparse
import os
import sys

CPM_EMPTY = 0xE5

# size -> (dirOffset, blockSize, maxDir, singleByteAlloc, sectors/track,
#          sectorSize, skew)
#
# maxDir and skew are what the TIKI's own BIOS uses, measured by running KAT
# on each image. Two of them disagree with diskview.c, which matters: writing
# past the real directory would land on top of file data.
#   90 KB  - diskview.c says 64 entries, the machine reports 32.
#   800 KB - diskview.c says 256 entries, the machine reports 128.
#
# The 90 KB format is also the only one that interleaves: its BIOS skew table
# (found at 0x1708 in the boot tracks of a KP/M 1.1 disk) is
#   1 6 11 16 3 8 13 18 5 10 15 2 7 12 17 4 9 14
# which is exactly (logical * 5) mod 18. Addressing it linearly puts the bytes
# on the disk in an order the machine will not read back.
GEOMETRY = {
    40 * 1 * 18 * 128: (3 * 1 * 18 * 128, 1024, 32, True, 18, 128, 5),   # 90 KB
    40 * 1 * 10 * 512: (2 * 1 * 10 * 512, 1024, 64, True, 10, 512, 1),   # 200 KB
    40 * 2 * 10 * 512: (1 * 2 * 10 * 512, 2048, 128, True, 20, 512, 1),  # 400 KB
    80 * 2 * 10 * 512: (1 * 2 * 10 * 512, 2048, 128, False, 20, 512, 1),  # 800 KB
}

# The CCP's input buffer, pre-loaded by INSTALL with a command nobody typed.
# This lives in the reserved boot tracks, which are addressed physically; on
# the 90 KB disk it falls in sector 0, the one sector skew leaves in place.
CCP_MAXLEN, CCP_LEN, CCP_TEXT = 0x86, 0x87, 0x88


class Geometry:
    def __init__(self, size):
        if size not in GEOMETRY:
            raise SystemExit(
                "unsupported image size %d; expected one of %s"
                % (size, ", ".join(str(s) for s in sorted(GEOMETRY))))
        (self.dir_off, self.block, self.max_dir, self.single,
         self.spt, self.sec, self.skew) = GEOMETRY[size]
        self.size = size
        self.allocs = 16 if self.single else 8
        self.total_blocks = (size - self.dir_off) // self.block
        self.dir_blocks = (self.max_dir * 32 + self.block - 1) // self.block
        self.label = "%d KB" % (size // 1024)

    def phys(self, logical):
        """Map a byte offset in the CP/M data area to one in the image."""
        sector, within = divmod(logical, self.sec)
        track, s = divmod(sector, self.spt)
        p = (s * self.skew) % self.spt
        return self.dir_off + (track * self.spt + p) * self.sec + within

    def slot(self, i):
        return i * 32

    def block_off(self, blk):
        return blk * self.block


def lread(img, geo, logical, n):
    """Read n bytes of logical (CP/M-ordered) data out of the image."""
    out = bytearray()
    while n > 0:
        take = min(n, geo.sec - logical % geo.sec)
        p = geo.phys(logical)
        out += img[p:p + take]
        logical += take
        n -= take
    return bytes(out)


def lwrite(img, geo, logical, data):
    i = 0
    while i < len(data):
        take = min(len(data) - i, geo.sec - logical % geo.sec)
        p = geo.phys(logical)
        img[p:p + take] = data[i:i + take]
        logical += take
        i += take


def cpm_name(name):
    base, _, ext = os.path.basename(name).partition(".")
    base, ext = base.upper()[:8], ext.upper()[:3]
    if not base:
        raise SystemExit("cannot derive a CP/M name from %r" % name)
    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def entries(img, geo):
    """Yield (index, logical offset, 32-byte entry) for every directory slot."""
    for i in range(geo.max_dir):
        off = geo.slot(i)
        if geo.phys(off) + 32 > len(img):
            break
        yield i, off, lread(img, geo, off, 32)


def alloc_map(geo, e):
    amap = e[16:32]
    if geo.single:
        return list(amap)
    return [amap[j] | (amap[j + 1] << 8) for j in range(0, 16, 2)]


def live(e):
    return e[0] != CPM_EMPTY and e[0] <= 0x0F


def used_blocks(img, geo):
    used = set(range(min(geo.dir_blocks, geo.total_blocks)))
    for _, _, e in entries(img, geo):
        if not live(e):
            continue
        for b in alloc_map(geo, e):
            if 0 < b < geo.total_blocks:
                used.add(b)
    return used


def catalog(img, geo):
    out = {}
    for i, _, e in entries(img, geo):
        if not live(e):
            continue
        out.setdefault(bytes(c & 0x7F for c in e[1:12]), []).append(i)
    return out


def delete(img, geo, name11, wipe=False):
    """Erase a file. CP/M only marks the directory entry, but when preparing a
    release image it is tidier to blank the freed data blocks as well."""
    n = 0
    for _, off, e in entries(img, geo):
        if not live(e) or bytes(c & 0x7F for c in e[1:12]) != name11:
            continue
        if wipe:
            for b in alloc_map(geo, e):
                if 0 < b < geo.total_blocks:
                    lwrite(img, geo, geo.block_off(b), b"\xE5" * geo.block)
        lwrite(img, geo, off, bytes([CPM_EMPTY]))
        n += 1
    return n


def scrub(img, geo):
    """Blank everything CP/M considers free.

    Erasing a file only sets the first byte of its directory entry to E5, so
    the name and allocation map stay legible in a hex editor, and the data
    blocks keep their contents. Neither is referenced any more, so both can be
    cleared - which is what makes a deleted file actually gone from the image.
    """
    slots = 0
    for _, off, e in entries(img, geo):
        if e[0] != CPM_EMPTY or e[1:] == b"\xE5" * 31:
            continue
        lwrite(img, geo, off, b"\xE5" * 32)
        slots += 1

    used = used_blocks(img, geo)
    blank, blocks = b"\xE5" * geo.block, 0
    for b in range(geo.total_blocks):
        if b in used:
            continue
        if lread(img, geo, geo.block_off(b), geo.block) != blank:
            lwrite(img, geo, geo.block_off(b), blank)
            blocks += 1
    return slots, blocks


def add_file(img, geo, data, name11):
    if name11 in catalog(img, geo):
        raise SystemExit("a file named %r already exists (use --replace)"
                         % name11.decode())

    blocks_needed = (len(data) + geo.block - 1) // geo.block
    entries_needed = max(1, (blocks_needed + geo.allocs - 1) // geo.allocs)

    used = used_blocks(img, geo)
    free = [b for b in range(geo.total_blocks) if b not in used]
    if blocks_needed > len(free):
        raise SystemExit("not enough space: need %d KB, %d KB free"
                         % ((len(data) + 1023) // 1024,
                            len(free) * geo.block // 1024))
    free_slots = [i for i, _, e in entries(img, geo) if e[0] == CPM_EMPTY]
    if entries_needed > len(free_slots):
        raise SystemExit("not enough free directory entries (%d needed, %d free)"
                         % (entries_needed, len(free_slots)))

    alloc = free[:blocks_needed]

    for i, blk in enumerate(alloc):
        chunk = data[i * geo.block:(i + 1) * geo.block]
        if len(chunk) < geo.block:          # CP/M pads the tail with EOF
            chunk += b"\x1a" * (geo.block - len(chunk))
        lwrite(img, geo, geo.block_off(blk), chunk)

    bytes_per_entry = geo.allocs * geo.block
    exm = max(0, bytes_per_entry // 16384 - 1)

    block_idx, remaining, slot_i = 0, len(data), 0
    for e in range(entries_needed):
        while slot_i < geo.max_dir and lread(img, geo, geo.slot(slot_i), 1)[0] != CPM_EMPTY:
            slot_i += 1
        ent = bytearray(32)
        ent[0] = 0                                      # user 0
        ent[1:12] = name11

        log_extent = e * (exm + 1)
        ent[12] = log_extent & 0x1F                     # EX
        ent[14] = (log_extent >> 5) & 0x3F              # S2

        n = min(blocks_needed - block_idx, geo.allocs)
        this = min(n * geo.block, remaining)

        if e < entries_needed - 1:
            rc = 0x80
            if exm > 0:
                ent[12] = (log_extent + exm) & 0x1F
        elif exm > 0 and this > 16384:
            ent[12] = (log_extent + 1) & 0x1F
            rc = (this - 16384 + 127) // 128
        else:
            rc = (this + 127) // 128
        ent[15] = min(rc, 0x80)                         # RC

        for j in range(n):
            blk = alloc[block_idx + j]
            if geo.single:
                ent[16 + j] = blk
            else:
                ent[16 + j * 2] = blk & 0xFF
                ent[16 + j * 2 + 1] = (blk >> 8) & 0xFF

        lwrite(img, geo, geo.slot(slot_i), bytes(ent))
        block_idx += n
        remaining -= this
        slot_i += 1

    return blocks_needed, entries_needed


def read_file(img, geo, name11):
    """Read a file back out of the image, so a write can be verified.

    A directory entry records the highest logical extent it covers plus the
    record count in that extent; every lower extent is full at 128 records.
    So the length is simply (extent * 128 + RC) * 128, rounded to a record.
    """
    exts = []
    for _, _, e in entries(img, geo):
        if not live(e) or bytes(c & 0x7F for c in e[1:12]) != name11:
            continue
        log = (e[12] & 0x1F) | ((e[14] & 0x3F) << 5)
        exts.append((log, e[15], alloc_map(geo, e)))
    if not exts:
        return None
    exts.sort()

    out = bytearray()
    for _, _, blks in exts:
        for b in blks:
            if b:
                out += lread(img, geo, geo.block_off(b), geo.block)

    log_max, rc_last, _ = exts[-1]
    return bytes(out[:(log_max * 128 + rc_last) * 128])


def set_autostart(img, cmd):
    if img[0x80] != 0xC3 or img[0x83] != 0xC3:
        raise SystemExit("no CCP header at 0x80; cannot set the auto-start command")
    maxlen = img[CCP_MAXLEN]
    if len(cmd) > maxlen:
        raise SystemExit("command longer than the CCP input buffer (%d)" % maxlen)
    old = bytes(img[CCP_TEXT:CCP_TEXT + img[CCP_LEN]])
    img[CCP_LEN] = len(cmd)
    img[CCP_TEXT:CCP_TEXT + len(cmd)] = cmd.encode("ascii")
    return old.decode("latin1")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("hostfile", nargs="?")
    ap.add_argument("--name", help="CP/M name to store as (default: the host name)")
    ap.add_argument("--replace", action="store_true", help="delete an existing copy first")
    ap.add_argument("--autostart", help="command the CCP runs at boot")
    ap.add_argument("--delete", action="append", default=[], metavar="NAME",
                    help="erase a file (repeatable)")
    ap.add_argument("--scrub", action="store_true",
                    help="blank erased directory slots and unallocated blocks")
    ap.add_argument("--list", action="store_true", help="just show the catalog")
    args = ap.parse_args()

    img = bytearray(open(args.image, "rb").read())
    size_before = len(img)
    geo = Geometry(size_before)
    print("%s: %s, dir@0x%04X, %d-byte blocks, %d slots, %s alloc"
          % (os.path.basename(args.image), geo.label, geo.dir_off, geo.block,
             geo.max_dir, "8-bit" if geo.single else "16-bit"))

    if args.list:
        for nm, slots in sorted(catalog(img, geo).items()):
            s = nm.decode("latin1")
            print("   %-8s.%-3s  %d extent(s)" % (s[:8].strip(), s[8:].strip(), len(slots)))
        free = geo.total_blocks - len(used_blocks(img, geo))
        print("   free: %d KB, %d directory slots"
              % (free * geo.block // 1024,
                 sum(1 for _, _, e in entries(img, geo) if e[0] == CPM_EMPTY)))
        return

    for spec in args.delete:
        name11 = cpm_name(spec)
        n = delete(img, geo, name11, wipe=True)
        print("   deleted %s: %d extent(s)%s"
              % (name11.decode(), n, "" if n else " (not present)"))

    if args.hostfile:
        name11 = cpm_name(args.name or args.hostfile)
        data = open(args.hostfile, "rb").read()
        if args.replace:
            n = delete(img, geo, name11)
            if n:
                print("   removed %d existing extent(s) of %s" % (n, name11.decode()))
        blocks, ents = add_file(img, geo, data, name11)
        print("   added %s: %d bytes, %d block(s), %d directory entr%s"
              % (name11.decode(), len(data), blocks, ents,
                 "y" if ents == 1 else "ies"))

    if args.scrub:
        slots, blocks = scrub(img, geo)
        print("   scrubbed %d erased directory slot(s) and %d free block(s)"
              % (slots, blocks))

    if args.autostart:
        old = set_autostart(img, args.autostart)
        print("   auto-start: %r -> %r" % (old, args.autostart))

    assert len(img) == size_before, "image size must not change"
    open(args.image, "wb").write(img)
    print("   saved (%d bytes)" % len(img))


if __name__ == "__main__":
    main()
