#!/usr/bin/env python3
"""Measure the tones in a MAME audio recording.

Splits a WAV into non-silent segments and reports each one's length and
dominant frequency. Used to verify the AY-3-8912 sound driver without a
speaker, and without any host input.

The AY's square wave is unipolar, so each segment is mean-removed before
its zero crossings are counted.

Usage: check-wav.py <file.wav> [silence-threshold]
"""

import struct
import sys
import wave


def read_mono(path):
    """Return the loudest channel's samples and the sample rate.

    MAME writes one channel per emulated speaker - on the tiki100 that is
    the two floppy speakers plus the AY - and the silent ones would other-
    wise divide the tone's amplitude down into the noise when mixed.
    """
    with wave.open(path, "rb") as w:
        rate = w.getframerate()
        channels = w.getnchannels()
        width = w.getsampwidth()
        if width != 2:
            raise SystemExit("expected 16-bit samples, got %d-bit" % (width * 8))
        raw = w.readframes(w.getnframes())

    samples = struct.unpack("<%dh" % (len(raw) // 2), raw)
    if channels == 1:
        return list(samples), rate

    best = None
    best_range = -1
    for c in range(channels):
        d = samples[c::channels]
        span = max(d) - min(d)
        if span > best_range:
            best_range = span
            best = d
    return list(best), rate


def segments(samples, rate, threshold):
    """Yield (start, end) sample indices of runs above the threshold."""
    win = max(1, rate // 1000)          # 1 ms windows
    loud = []
    for i in range(0, len(samples) - win, win):
        block = samples[i:i + win]
        peak = max(block) - min(block)
        loud.append(peak > threshold)

    # Two things are bridged here. A tone can dip for a window or two
    # mid-note, and at the bottom of the death sweep the square wave is
    # slow enough (around 35 Hz) that its flat half-cycles look like
    # silence to a 1 ms window. Bridging 60 ms merges both back into one
    # event while still leaving the victory jingle's 100 ms rest intact.
    out = []
    i = 0
    while i < len(loud):
        if not loud[i]:
            i += 1
            continue
        j = i
        quiet = 0
        while j < len(loud):
            if loud[j]:
                quiet = 0
            else:
                quiet += 1
                if quiet > 60:
                    break
            j += 1
        out.append((i * win, (j - quiet) * win))
        i = j
    return out


def frequency(block, rate):
    if len(block) < 4:
        return 0.0
    mean = sum(block) / float(len(block))
    centred = [s - mean for s in block]
    crossings = 0
    for i in range(1, len(centred)):
        if centred[i - 1] < 0 <= centred[i]:
            crossings += 1
    return crossings * rate / float(len(centred))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    path = sys.argv[1]
    threshold = int(sys.argv[2]) if len(sys.argv) > 2 else 600

    samples, rate = read_mono(path)
    print("%s: %.2f s at %d Hz" % (path, len(samples) / float(rate), rate))

    found = segments(samples, rate, threshold)
    found = [(a, b) for (a, b) in found if b > a]
    print("%d segment(s)" % len(found))
    for n, (a, b) in enumerate(found):
        ms = (b - a) * 1000.0 / rate
        # Skip the attack, which can contain the previous note's tail.
        core = samples[a + (b - a) // 8:b]
        print("  %2d  start %8.1f ms  len %8.1f ms  %9.1f Hz"
              % (n, a * 1000.0 / rate, ms, frequency(core, rate)))


if __name__ == "__main__":
    main()
