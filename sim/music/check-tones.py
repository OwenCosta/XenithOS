#!/usr/bin/env python3
"""Checks what the simulator heard of the Tones album (make-tones.sh, music_tones.txt).

Each quarter second of audio.wav: its loudest of the four tones (Goertzel), and how much of the
sound that is. Passes when the four come in order (500, 750, 1000, 1250 Hz), each for about three
seconds, with no quieter quarter between them (a gap), and no jump between two samples bigger than
the steepest of the tones can make (a click). Prints what it found either way.

    check-tones.py audio.wav        exit 0 when it all holds
"""
import math
import struct
import sys
import wave

TONES = [500, 750, 1000, 1250]


def goertzel(xs, f, rate):
    k = 2 * math.cos(2 * math.pi * f / rate)
    s1 = s2 = 0.0
    for x in xs:
        s1, s2 = x + k * s1 - s2, s1
    return math.sqrt(max(0.0, s1 * s1 + s2 * s2 - k * s1 * s2)) / len(xs)


def main():
    w = wave.open(sys.argv[1])
    rate, n = w.getframerate(), w.getnframes()
    left = struct.unpack("<%dh" % (n * 2), w.readframes(n))[0::2]
    q = rate // 4
    quarters = []
    for i in range(n // q):
        xs = left[i * q:(i + 1) * q]
        rms = math.sqrt(sum(x * x for x in xs) / len(xs))
        power = [goertzel(xs, f, rate) for f in TONES]
        best = max(range(4), key=lambda k: power[k])
        quarters.append((i / 4, rms, TONES[best], power[best] / (sum(power) or 1)))
    # runs of quarters where one tone is clearly what's playing
    runs = []
    for t, rms, f, share in quarters:
        if rms < 100 or share < 0.95:
            continue
        if runs and runs[-1][0] == f and abs(runs[-1][2] + 0.25 - t) < 0.01:
            runs[-1][2] = t
        else:
            runs.append([f, t, t])
    # the tones' runs, in the order heard (anything shorter than a second is a sound effect)
    tones = [(f, a, b + 0.25) for f, a, b in runs if b + 0.25 - a >= 1.0]
    print("heard:", ", ".join("%d Hz from %.2f to %.2f s" % t for t in tones))
    ok = True
    if [f for f, _, _ in tones] != TONES:
        print("FAIL: the tones aren't the four, in order")
        ok = False
    for f, a, b in tones:
        if not 2.5 <= b - a <= 3.5:
            print("FAIL: %d Hz lasted %.2f s, not about 3" % (f, b - a))
            ok = False
    if tones:
        start, end = tones[0][1], tones[-1][2]
        quiet = [t for t, rms, _, _ in quarters if start <= t < end - 0.25 and rms < 300]
        if quiet:
            print("FAIL: quiet between the songs at", ", ".join("%.2f s" % t for t in quiet))
            ok = False
        # a click: a step between samples bigger than the highest tone at full volume could make
        first, last = int(start * rate) + rate // 4, int(end * rate) - rate // 4
        jump = max(abs(left[j] - left[j - 1]) for j in range(first + 1, last))
        limit = int(32768 * 2 * math.pi * TONES[-1] / rate * 0.25)     # the files are at -12 dB or so
        print("largest step between samples: %d (a click would be over %d)" % (jump, limit))
        if jump > limit:
            print("FAIL: a click")
            ok = False
    print("PASS" if ok else "FAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
