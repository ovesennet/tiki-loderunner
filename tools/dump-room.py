"""Dev check: decode packed rooms from the generated asm and print ASCII maps."""
import sys

CH = ".#=H-h&*"   # empty bricks solid ladder rope hidbrick hidladder box


def load(symbol, path="src/asm/leveldata.asm"):
    buf = bytearray()
    grab = False
    for line in open(path, encoding="ascii"):
        s = line.strip()
        if s.startswith("_") and s.endswith(":"):
            grab = (s == symbol + ":")
            continue
        if grab and s.startswith("defb"):
            buf.extend(int(v.strip()[1:], 16) for v in s[4:].split(","))
    return bytes(buf)


def room(data, n):
    d = data[(n - 1) * 264:n * 264]
    bits = int.from_bytes(d, "big")
    out = []
    for r in range(22):
        out.append("".join(CH[(bits >> (2112 - 3 * (r * 32 + c + 1))) & 7]
                           for c in range(32)))
    return out


def main():
    data = load("_level_data")
    men = load("_level_men")
    print("level_data %d bytes, level_men %d bytes" % (len(data), len(men)))
    for n in [int(a) for a in sys.argv[1:]] or [1]:
        rec = men[(n - 1) * 12:n * 12]
        print("--- room %d  player row=%d col=%d enemies=%d ---"
              % (n, rec[0] & 0x1F, rec[1] >> 3, rec[1] & 7))
        print("\n".join(room(data, n)))


if __name__ == "__main__":
    main()
