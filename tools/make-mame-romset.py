#!/usr/bin/env python3
"""Assemble a MAME `tiki100` ROM set from the ROM image in this repository.

MAME needs two files for the TIKI-100:

    tikirom-2.03w.u10   8 KB    the firmware    -- `tiki.rom` here is a
                                byte-exact match (sha1 9663...f83a)
    53ls140.u4          256 B   the memory-decode PROM

Only the firmware ships with this project, so the PROM is reconstructed from
the decode behaviour the MAME driver itself documents (src/mame/tiki/tiki100.*):

    prom_addr = mdis << 5 | vire << 4 | rome << 3 | (offset >> 13)
    prom      = prom_rom[prom_addr] ^ 0xff
    bits        ROM0 = 0x01, ROM1 = 0x02, VIR = 0x04, RAM0 = 0x08

so a stored byte is simply the one's complement of the active-signal mask.
In `mrq_r` the signals are applied in the order ROM0, ROM1, VIR, RAM0, each
overwriting the last, which means RAM0 must never be asserted together with
ROM or video RAM: whichever bank is readable asserts exactly one signal.

Only address bits 0-5 reach the device, so the 64-entry table is mirrored four
times to fill the 256-byte image.

The result is behaviourally correct but will not match MAME's recorded CRC.
MAME treats a checksum mismatch as a warning and still runs the machine (a
missing file, by contrast, is fatal) -- expect a "WRONG CHECKSUMS" notice.
"""

import hashlib
import pathlib
import sys

ROM0, ROM1, VIR, RAM0 = 0x01, 0x02, 0x04, 0x08

FIRMWARE_SHA1 = "96336633ecaf1b2190c36c43295ac9f785d1f83a"

ROOT = pathlib.Path(__file__).resolve().parent.parent


def decode_signals(vire, rome, bank):
    """Active signals for one 8 KB bank, with main memory enabled (mdis=1).

    `rome` and `vire` are the SYL latch bits written to port $1C: _ROME is
    active low, so rome=0 means the firmware ROM is mapped, and vire=1 maps
    the 32 KB of video RAM. Where the video RAM lands depends on whether the
    ROM is still in the way -- $0000-$7FFF when it is not, $4000-$BFFF when
    it is. Everything not claimed by ROM or video RAM is plain RAM.
    """
    rom_mapped = (rome == 0)

    if rom_mapped and bank == 0:
        return ROM0                       # $0000-$1FFF
    if rom_mapped and bank == 1:
        return ROM1                       # $2000-$3FFF

    if vire:
        if rom_mapped:
            if 2 <= bank <= 5:            # $4000-$BFFF
                return VIR
        elif bank <= 3:                   # $0000-$7FFF
            return VIR

    return RAM0


def build_prom():
    table = bytearray(64)
    for vire in (0, 1):
        for rome in (0, 1):
            for bank in range(8):
                # mdis=0 means an expansion card is driving the bus, so the
                # mainboard asserts nothing. Those entries stay 0xFF.
                table[0x00 | vire << 4 | rome << 3 | bank] = 0xFF
                signals = decode_signals(vire, rome, bank)
                table[0x20 | vire << 4 | rome << 3 | bank] = (~signals) & 0xFF
    return bytes(table) * 4


def main():
    out_dir = ROOT / "roms" / "tiki100"
    firmware_src = ROOT / "tiki.rom"

    if not firmware_src.is_file():
        sys.exit(f"ERROR: {firmware_src} not found")

    firmware = firmware_src.read_bytes()
    digest = hashlib.sha1(firmware).hexdigest()
    if digest != FIRMWARE_SHA1:
        print(f"WARNING: tiki.rom sha1 is {digest},")
        print(f"         expected {FIRMWARE_SHA1} (TIKI ROM v2.03 W).")
        print("         MAME will report a checksum mismatch.")

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "tikirom-2.03w.u10").write_bytes(firmware)
    (out_dir / "53ls140.u4").write_bytes(build_prom())

    print(f"ROM set written to {out_dir}")
    print(f"  tikirom-2.03w.u10  {len(firmware)} bytes  (copied from tiki.rom)")
    print("  53ls140.u4         256 bytes  (reconstructed, CRC will not match)")


if __name__ == "__main__":
    main()
