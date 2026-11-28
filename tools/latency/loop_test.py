#!/usr/bin/env python3
"""The whole delay from a guitar's string to the game's sound, measured with a cable: the same way on a Mac and on
Linux, with the game as it is played.

    python3 tools/latency/loop_test.py [--device PART-OF-ITS-NAME] [--seconds 20] [--dry N --wet N]
    python3 tools/latency/loop_test.py --file RECORDING.wav [--dry N --wet N]

It needs an audio interface with a spare input, and one cable: from an output of the interface that carries the
game's sound (and not the guitar's own, if the interface mixes that in) to the spare input. The interface then
records two things side by side, through the same converter and on the same clock: the guitar as it comes in, and
the game's sound as it goes out. Pluck a muted string once a second; the time from each pluck on the first channel
to its sound on the second is the delay a player hears: the interface in, the system, the game, the system, the
interface out. What the two channels share (the second trip in) is in both and cancels.

It records with the system's own tools (Linux: PipeWire's pw-record; macOS: the sounddevice module where the
Python has it, which takes every channel of an interface, else ffmpeg) while the game runs: start the game, go
where the guitar is heard (a song, the tuner, the Tone Designer), then run this. The first time, macOS asks
whether the terminal may use the microphone.
"""
import argparse
import os
import re
import signal
import struct
import subprocess
import sys
import tempfile
import time
import wave

BLOCK = 12  # samples a block: a quarter of a millisecond at 48 kHz


def read_wav(path):
    """(rate, [one list of samples a channel]) of a 16-bit WAV."""
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit("loop_test: %s is not a 16-bit recording" % path)
        n, ch, rate = w.getnframes(), w.getnchannels(), w.getframerate()
        flat = struct.unpack("<%dh" % (n * ch), w.readframes(n)[:2 * n * ch])
    return rate, [flat[c::ch] for c in range(ch)]


def onsets(x, rate):
    """The samples where a sound begins in X: where the last millisecond is far louder than the fifty before it.
    A pluck's start, and the start of what an amplifier makes of it, by the same rule."""
    blocks = [max(abs(v) for v in x[i:i + BLOCK]) for i in range(0, len(x) - BLOCK + 1, BLOCK)]
    peak = max(blocks, default=0)
    if peak < 400:
        return []
    per_ms = max(1, rate // 1000 // BLOCK)
    before, gap, apart = 50 * per_ms, 2 * per_ms, 400 * per_ms
    sums = [0]
    for b in blocks:
        sums.append(sums[-1] + b)
    out, last = [], -apart
    for k in range(before + gap, len(blocks)):
        if k - last < apart:
            continue
        now = max(blocks[k - per_ms + 1:k + 1])
        rest = (sums[k - gap] - sums[k - gap - before]) / before
        if now > max(6 * rest, 0.08 * peak):
            lo = max(0, (k - per_ms) * BLOCK)
            first = next((i for i in range(lo, (k + 1) * BLOCK) if abs(x[i]) >= now / 2), k * BLOCK)
            out.append(first)
            last = k
    return out


def delays(dry, wet, rate):
    """[the samples from each start in DRY to the next start in WET], for the starts that have one within a fifth of
    a second."""
    starts, answers = onsets(dry, rate), onsets(wet, rate)
    out = []
    for s in starts:
        a = next((a for a in answers if s - rate // 1000 <= a <= s + rate // 5), None)
        if a is not None:
            out.append(a - s)
    return out, len(starts), len(answers)


def choose(channels, rate):
    """(dry, wet), the two channels with the most starts, the earlier one first; None when there are not two."""
    found = sorted(((len(onsets(c, rate)), i) for i, c in enumerate(channels)), reverse=True)
    if len(found) < 2 or found[1][0] < 3:
        return None
    a, b = found[0][1], found[1][1]
    d, _, _ = delays(channels[a], channels[b], rate)
    back, _, _ = delays(channels[b], channels[a], rate)
    lead = lambda v: sum(1 for x in v if x > rate // 2000)  # by more than half a millisecond
    return (a, b) if lead(d) >= lead(back) else (b, a)


def report(path, dry=None, wet=None):
    rate, channels = read_wav(path)
    peaks = [max((abs(v) for v in c), default=0) for c in channels]
    print("the recording: %d channels at %d Hz, %.0f seconds; the loudest sample of each: %s"
          % (len(channels), rate, len(channels[0]) / rate, " ".join("%d" % p for p in peaks)))
    if dry is None or wet is None:
        pair = choose(channels, rate)
        if not pair:
            print("NOT MEASURED: plucks were not found on two channels. Is the cable in, the game's sound on that output, and\n"
                  "the guitar heard in the game? --dry N --wet N name the channels (from 1).")
            return 1
        dry, wet = pair
    else:
        dry, wet = dry - 1, wet - 1
    d, plucks, sounds = delays(channels[dry], channels[wet], rate)
    print("the guitar on channel %d (%d plucks), the game's sound on channel %d (%d starts)" % (dry + 1, plucks, wet + 1, sounds))
    if len(d) < 4:
        print("NOT MEASURED: too few plucks have their sound after them (%d)" % len(d))
        return 1
    ms = sorted(x * 1000.0 / rate for x in d)
    middle = ms[len(ms) // 2]
    close = [m for m in ms if abs(m - middle) <= 5]
    print("\nfrom the string to the game's sound: %.1f ms (the middle of %d plucks; %.1f to %.1f%s)"
          % (middle, len(ms), close[0], close[-1], "" if len(close) == len(ms) else "; %d apart from the rest left out" % (len(ms) - len(close))))
    if middle < 1.5:
        print("(under 2 ms is the interface's own monitoring of the guitar on that output, not the game: turn the guitar's\n"
              " input down in that output's mix, so that it carries the game's sound alone)")
    return 0


def linux_source(part):
    """(node name, channels) of the input to record: the one whose name has PART, or the desktop's default."""
    listing = subprocess.run(["pactl", "list", "short", "sources"], stdout=subprocess.PIPE, text=True).stdout
    default = subprocess.run(["pactl", "get-default-source"], stdout=subprocess.PIPE, text=True).stdout.strip()
    for line in listing.splitlines():
        f = line.split("\t")
        if len(f) >= 4 and ".monitor" not in f[1] and ((part and part.lower() in f[1].lower()) or (not part and f[1] == default)):
            m = re.search(r"(\d+)ch", f[3])
            return f[1], int(m.group(1)) if m else 2
    raise SystemExit("loop_test: no input %s (pactl list short sources has them)" % ("named like " + part if part else "is the default"))


def mac_devices():
    r = subprocess.run(["ffmpeg", "-hide_banner", "-f", "avfoundation", "-list_devices", "true", "-i", ""], stderr=subprocess.PIPE, text=True)
    audio = r.stderr.split("AVFoundation audio devices:")[-1]
    return re.findall(r"\[(\d+)\] (.+)", audio)


def mac_record_every_channel(part, seconds, path):
    """With the sounddevice module (a Python that has it): every channel of the interface, which ffmpeg's capture
    does not promise of one with more than two. False without the module."""
    try:
        import sounddevice as sd
    except ImportError:
        return False
    mine = [(i, d) for i, d in enumerate(sd.query_devices()) if d["max_input_channels"] and part and part.lower() in d["name"].lower()]
    if len(mine) != 1:
        raise SystemExit("loop_test: --device names one of: %s" % ", ".join(d["name"] for d in sd.query_devices() if d["max_input_channels"]))
    index, device = mine[0]
    print("recording %s, %d channels" % (device["name"], device["max_input_channels"]))
    frames = sd.rec(int(seconds * 48000), samplerate=48000, channels=device["max_input_channels"], dtype="int16", device=index)
    sd.wait()
    with wave.open(path, "wb") as w:
        w.setnchannels(device["max_input_channels"]), w.setsampwidth(2), w.setframerate(48000)
        w.writeframes(frames.tobytes())
    return True


def record(part, seconds, path):
    if sys.platform.startswith("linux"):
        node, channels = linux_source(part)
        print("recording %s, %d channels" % (node, channels))
        p = subprocess.Popen(["pw-record", "--target", node, "--channels", str(channels), "--rate", "48000", "--format", "s16", path],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(seconds)
        p.send_signal(signal.SIGINT)
        p.wait(timeout=10)
    elif sys.platform == "darwin" and mac_record_every_channel(part, seconds, path):
        pass
    elif sys.platform == "darwin":
        devices = mac_devices()
        mine = [d for d in devices if part and part.lower() in d[1].lower()]
        if len(mine) != 1:
            raise SystemExit("loop_test: --device names one of: %s" % ", ".join(n for _, n in devices))
        print("recording %s" % mine[0][1])
        r = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "avfoundation", "-i", ":" + mine[0][0], "-t", str(seconds),
                            "-c:a", "pcm_s16le", "-y", path])
        if r.returncode:
            raise SystemExit("loop_test: ffmpeg could not record (may the terminal use the microphone? System Settings, Privacy)")
    else:
        raise SystemExit("loop_test: recording is written for Linux and macOS; --file takes a recording made elsewhere")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--device", default="", help="a part of the interface's name (Linux: the default input when left out)")
    p.add_argument("--seconds", type=float, default=20)
    p.add_argument("--dry", type=int, help="the channel the guitar is on, from 1 (found when left out)")
    p.add_argument("--wet", type=int, help="the channel the cable brings the game's sound back on")
    p.add_argument("--file", help="a recording to read, in the place of making one")
    p.add_argument("--keep", help="where to keep the recording made")
    a = p.parse_args(argv)
    if a.file:
        return report(a.file, a.dry, a.wet)
    path = a.keep or os.path.join(tempfile.mkdtemp(prefix="loop-test-"), "recording.wav")
    print("With the guitar heard in the game: pluck a muted string, short and sharp, about once a second, for %d seconds.\n" % a.seconds)
    for n in (3, 2, 1):
        print("%d..." % n, flush=True)
        time.sleep(1)
    print("now", flush=True)
    record(a.device, a.seconds, path)
    code = report(path, a.dry, a.wet)
    if not a.keep and code == 0:
        os.remove(path)
    elif not a.keep:
        print("the recording is kept: %s" % path)
    return code


if __name__ == "__main__":
    sys.exit(main())
