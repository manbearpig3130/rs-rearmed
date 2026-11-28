#!/usr/bin/env python3
"""How long the game takes from a sound at its guitar input to that sound at its output, measured.

    python3 linux/latency_test.py [--program PATH] [--home DIR] [--keys FRAME:KEY,...] [--wait SECONDS] [--keep]

It runs a second, windowless copy of the game on a copy of RS Re-ARMed's folder, with two made-up PipeWire devices in
the place of the audio interface: an input the game hears and an output it plays to. Clicks are played into the
input; one recording takes what the game hears and what it plays, side by side on the same clock, and the delay
between the two is the game's own: PipeWire's hand-over of a buffer in, the game's input queue, its sound engine
and tones, its output ring, and PipeWire's buffer out.

A real interface adds its own on both sides, which no software can see from inside: PipeWire's figure for the
device (one buffer and a margin each way) is added and said, and the converters and the cable of USB are not.

With --interface and a cable it measures the whole chain with a real interface instead, by itself: the cable from
a stereo output of the interface to a stereo input of it (--loop names that input's two channels). The copy of the
game hears the first of the two and is made to play on the right side alone; a click is played on the left. The
left channel of the loop then records the click as it reaches the interface, the right one the game's sound as it
leaves it, through one converter: the delay a player hears, with no one to pluck a string.

It needs the game not to be running (the made-up devices would appear in it), PipeWire's tools (pw-loopback,
pw-record, pw-play, pw-link, pactl) and a game set up with a profile: the keys it sends take the first profile into
the Tone Designer, where the guitar is heard through a tone. Nothing of the player's own folder is changed.
"""
import argparse
import json
import math
import os
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
RATE = 48000
FEED, INPUT, OUTPUT, RECORDER = "rs-latency-feed", "rs-latency-in", "rs-latency-out", "rs-latency-rec"
CLICKS, GAP = 12, 0.75  # how many clicks, and seconds from one to the next
# Enter on the title, Enter on the first profile, seven down the main menu to TONE DESIGNER, Enter, Enter on EDIT TONE
TONE_DESIGNER = "1300:return,1700:return,2050:down,2080:down,2110:down,2140:down,2170:down,2200:down,2230:down,2330:return,2600:return"


def default_home():
    data = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return os.environ.get("RS_NATIVE_HOME") or os.path.join(data, "rs-rearmed")


def write_clicks(path, left_only=False):
    """A WAV of CLICKS short bursts, GAP apart: a kilohertz for 15 ms, loud, with a sharp start. LEFT_ONLY: two
    channels, the right one silent."""
    frames = bytearray()
    for n in range(int((CLICKS + 1) * GAP * RATE)):
        t = n % int(GAP * RATE)
        in_click = n >= int(GAP * RATE) and t < int(0.015 * RATE)
        v = 0.8 * math.sin(2 * math.pi * 1000 * t / RATE) * math.exp(-t / (0.004 * RATE)) if in_click else 0.0
        if in_click and t == 0:
            v = 0.8  # the first sample is the click's start, whatever the sine's phase
        frames += struct.pack("<hh", int(v * 32767), 0) if left_only else struct.pack("<h", int(v * 32767))
    with wave.open(path, "wb") as w:
        w.setnchannels(2 if left_only else 1), w.setsampwidth(2), w.setframerate(RATE)
        w.writeframes(bytes(frames))


def onsets(samples, least, apart):
    """Where a channel rises past LEAST after APART samples below it: each click's first sample."""
    out, quiet = [], apart
    for i, v in enumerate(samples):
        if abs(v) >= least:
            if quiet >= apart:
                out.append(i)
            quiet = 0
        else:
            quiet += 1
    return out


def measure(path, heard_channel=0, played_channel=1):
    """([delay in samples of each click], a line about the recording) from a recording: one channel is what the
    game heard, another what it played (the left and the right of a recording of two, unless named)."""
    with wave.open(path, "rb") as w:
        n, ch, raw = w.getnframes(), w.getnchannels(), w.readframes(w.getnframes())
    every = struct.unpack("<%dh" % (ch * n), raw[:2 * ch * n])
    heard, played = every[heard_channel::ch], every[played_channel::ch]
    peak_in, peak_out = max(map(abs, heard), default=0), max(map(abs, played), default=0)
    if peak_in < 2000:
        return [], "the clicks did not reach the made-up input (its loudest sample: %d of 32767)" % peak_in
    starts = onsets(heard, peak_in // 4, int(0.3 * RATE))
    floor = sorted(abs(v) for v in played)[int(0.5 * n)]  # what the output is at rest: half of all samples are below it
    least = max(8 * floor, peak_out // 10, 60)
    delays = []
    for s in starts:
        window = played[s:s + int(0.5 * RATE)]
        hit = next((i for i, v in enumerate(window) if abs(v) >= least), None)
        if hit is not None and all(abs(v) < least for v in played[max(0, s - int(0.05 * RATE)):s]):
            delays.append(hit)
    return delays, "%d clicks heard, %d answered; the output's loudest sample %d, at rest %d" % (len(starts), len(delays), peak_out, floor)


def tool(*argv, **kw):
    return subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **kw)


def ports(node):
    """{port name: full name} of a node's ports."""
    out = {}
    for flag in ("-o", "-i"):
        for line in tool("pw-link", flag).stdout.splitlines():
            if line.startswith(node + ":"):
                out[line.split(":", 1)[1]] = line
    return out


def wait_for(what, check, seconds):
    end = time.time() + seconds
    while time.time() < end:
        got = check()
        if got:
            return got
        time.sleep(0.2)
    raise SystemExit("latency_test: %s did not appear within %d seconds" % (what, seconds))


def device_figures():
    """[(name, "in" or "out", frames PipeWire says the device adds besides the buffer)] of the desktop's default
    output and input: the interface the game plays through when nothing says otherwise."""
    defaults = {tool("pactl", "get-default-sink").stdout.strip(): "out", tool("pactl", "get-default-source").stdout.strip(): "in"}
    try:
        nodes = json.loads(tool("pw-dump").stdout)
    except ValueError:
        return []
    out = []
    for o in nodes:
        props = o.get("info", {}).get("props", {})
        way = defaults.get(props.get("node.name"))
        if not way or props.get("media.class") != ("Audio/Sink" if way == "out" else "Audio/Source"):
            continue
        margin = max([int(l.get("minRate", 0)) for l in o["info"].get("params", {}).get("Latency", []) if l.get("minQuantum")] or [0])
        out.append((props.get("node.description") or props.get("node.name"), way, margin))
    return sorted(out, key=lambda d: d[1])


def interface_nodes(part):
    """(source node, its channels, sink node) of the interface whose nodes' names have PART."""
    def find(kind):
        for line in tool("pactl", "list", "short", kind).stdout.splitlines():
            f = line.split("\t")
            if len(f) >= 4 and part.lower() in f[1].lower() and not f[1].endswith(".monitor"):
                return f[1], int(f[3].split("ch")[0].split()[-1])
        raise SystemExit("latency_test: no %s named like %s (pactl list short %s)" % (kind[:-1], part, kind))
    source, channels = find("sources")
    return source, channels, find("sinks")[0]


def game_stream():
    """The id of the game's output stream, as pactl has it; None while there is none."""
    try:
        streams = json.loads(tool("pactl", "-f", "json", "list", "sink-inputs").stdout)
    except ValueError:
        return None
    return next((str(x["index"]) for x in streams if x.get("properties", {}).get("node.name") == "rs-rearmed"), None)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--program", default=os.path.join(HERE, "..", "build", "linux", "rs-rearmed"))
    p.add_argument("--home", default=default_home(), help="RS Re-ARMed's folder, which is copied (default: %(default)s)")
    p.add_argument("--keys", default=TONE_DESIGNER, help="the keys that take the game to where the guitar is heard "
                   "(default: past the title and the profiles, down the main menu to Tone Designer, into Edit Tone)")
    p.add_argument("--wait", type=float, default=52, help="seconds from the game's start to the first click")
    p.add_argument("--keep", action="store_true", help="keep the copy and the recording")
    p.add_argument("--interface", help="a part of a real interface's name: measure through it and a cable, not made-up devices")
    p.add_argument("--loop", default="3,4", help="with --interface: the two channels the cable comes back on (default %(default)s)")
    p.add_argument("--fps", type=int, default=60, help="the frame rate the copy runs at (RS_FPS; the keys' frames are for 60 and follow)")
    p.add_argument("--plucks", action="store_true", help="with --interface: no clicks; the copy hears the interface as a played game does, "
                   "and a player plucks a string for twenty seconds (the cable as tools/latency/loop_test.py has it)")
    p.add_argument("--both-sides", action="store_true", help="with --interface: leave the game playing on both sides (its sound then comes back to it, quieter)")
    p.add_argument("--launcher", action="store_true", help="start the copy as the applications menu starts the game: through "
                   "install/rs_install.py, with the folder's own settings")
    p.add_argument("--window", action="store_true", help="the copy of the game in a window on the desktop, as a played game is")
    p.add_argument("--mods", action="store_true", help="with the mods of the folder loaded, as a played game has them")
    p.add_argument("--pictures", action="store_true", help="save the game's picture every five seconds (with --keep), to see where the keys take it")
    a = p.parse_args(argv)
    a.program, a.home = os.path.abspath(a.program), os.path.abspath(a.home)
    if tool("pgrep", "-x", "rs-rearmed").returncode == 0:
        raise SystemExit("latency_test: the game is running: quit it first (the test's made-up devices would appear in it)")
    image = os.path.join(a.home, "image", "Rocksmith2014_x64_decrypted")
    if not os.path.isfile(a.program) or not os.path.isfile(image):
        raise SystemExit("latency_test: no program at %s or no game set up at %s" % (a.program, a.home))
    work = tempfile.mkdtemp(prefix="rs-latency-", dir=os.path.join(os.path.expanduser("~"), ".cache"))
    home = os.path.join(work, "home")
    started, module = [], None
    try:
        print("copying %s (clones where the file system has them)" % a.home)
        subprocess.run(["cp", "-a", "--reflink=auto", a.home, home], check=True)
        left, right = (int(c) for c in a.loop.split(","))
        if a.interface:
            source, source_channels, sink = interface_nodes(a.interface)
            hears, plays_to, feed, record_from, record_channels = source, sink, sink, source, source_channels
        else:
            hears, plays_to, feed, record_from, record_channels = INPUT, OUTPUT, FEED, None, 2
        # the made-up devices: a sink whose sound comes out of a source (the game's input), and a sink it plays to
        if not a.interface:
          started.append(subprocess.Popen(["pw-loopback", "--capture-props", "media.class=Audio/Sink node.name=%s node.description=RS-latency-feed audio.position=[MONO]" % FEED,
                                         "--playback-props", "media.class=Audio/Source node.name=%s node.description=RS-latency-input audio.position=[MONO]" % INPUT],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
          r = tool("pactl", "load-module", "module-null-sink", "sink_name=%s" % OUTPUT, "channel_map=stereo", "rate=%d" % RATE)
          module = r.stdout.strip() if r.returncode == 0 else None
          wait_for("the made-up devices", lambda: ports(INPUT) and ports(OUTPUT) and ports(FEED), 10)
        env = dict(os.environ, RS_NATIVE_HOME=home, RS_STEAM_DATA_DIR=os.path.join(home, "steam"), SDL_VIDEODRIVER="offscreen",
                   RS_AUDIO_OUTPUT_NODE=plays_to, RS_AUDIO_ONLY_INPUT=hears, RS_AUDIO_LOG="1", RS_LOAD_MODS="0")
        env["RS_FPS"] = str(a.fps)
        keys = ",".join("%d:%s" % (int(k.split(":")[0]) * a.fps // 60, k.split(":")[1]) for k in a.keys.split(","))
        if a.window:
            env.pop("SDL_VIDEODRIVER")
        if a.mods:
            env.pop("RS_LOAD_MODS")
        if a.interface and not a.plucks:
            env["RS_AUDIO_INPUT_CHANNEL"] = str(left)  # the game's one input: the loop's left channel
        env.pop("PIPEWIRE_NODE", None)
        log = open(os.path.join(work, "game.log"), "w")
        frames = int((a.wait + (CLICKS + 3) * GAP + 6) * a.fps)
        more = ["--picture", os.path.join(work, "frame"), "--picture-every", "300"] if a.pictures else []
        script = ["--frames", str(frames), "--keys", keys] + more
        if a.launcher:
            env["RS_REARMED_BIN"] = a.program
            command = [sys.executable, os.path.join(HERE, "install", "rs_install.py"), "run", "--"] + script
        else:
            command = [a.program, "--image", image, "--run", os.path.join(home, "game")] + script
        game = subprocess.Popen(command,
                                stdout=log, stderr=subprocess.STDOUT, env=env, cwd=os.path.dirname(os.path.abspath(a.program)))
        started.append(game)
        print("the game is starting; %d seconds to the first click" % a.wait)
        time.sleep(a.wait)
        if game.poll() is not None:
            raise SystemExit("latency_test: the game ended early (%s)" % os.path.join(work, "game.log"))
        clicks, recording = os.path.join(work, "clicks.wav"), os.path.join(work, "recording.wav")
        # The game's stream at its full level on both sides, whatever the desktop remembers of it: the desktop keeps
        # a stream's level for the next time, and this test changes it below.
        stream = wait_for("the game's output stream", game_stream, 10)
        tool("pactl", "set-sink-input-volume", stream, "100%", "100%")
        if a.plucks:
            sys.path.insert(0, os.path.join(HERE, "..", "tools", "latency"))
            import loop_test
            print("PLUCK NOW: a muted string, short and sharp, once a second, for 20 seconds", flush=True)
            loop_test.record(a.interface, 20, recording)
            print("done plucking", flush=True)
            a.keep = True
            return loop_test.report(recording)
        write_clicks(clicks, left_only=bool(a.interface))
        if a.interface:
            # the game plays on the right alone, so that what it plays never comes back to what it hears; put back below
            if not a.both_sides:
                tool("pactl", "set-sink-input-volume", stream, "0%", "100%")
            rec = subprocess.Popen(["pw-record", "--target", record_from, "--rate", str(RATE), "--channels", str(record_channels), "--format", "s16",
                                    recording], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            started.append(rec)
        else:
            rec = subprocess.Popen(["pw-record", "--target", "0", "--rate", str(RATE), "--channels", "2", "--format", "s16",
                                    "-P", "node.name=%s node.latency=128/%d" % (RECORDER, RATE), recording], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            started.append(rec)
            mine = wait_for("the recorder", lambda: len(ports(RECORDER)) >= 2 and ports(RECORDER), 10)
            first, second = sorted(mine.values())[0], sorted(mine.values())[1]
            heard = next(v for k, v in ports(INPUT).items() if k.startswith("capture"))
            played = next(v for k, v in sorted(ports(OUTPUT).items()) if k.startswith("monitor"))
            for src, dst in ((heard, first), (played, second)):
                r = tool("pw-link", src, dst)
                if r.returncode:
                    raise SystemExit("latency_test: could not link %s to %s: %s" % (src, dst, r.stderr.strip()))
        time.sleep(1.0)
        top = tool("pw-top", "-b", "-n", "3").stdout
        tool("pw-play", "--target", feed, clicks)
        time.sleep(1.0)
        rec.send_signal(signal.SIGINT)
        rec.wait(timeout=10)
        if a.interface:
            tool("pactl", "set-sink-input-volume", stream, "100%", "100%")
        delays, said = measure(recording, left - 1, right - 1) if a.interface else measure(recording)
        print(said)
        lines = [l for l in top.split("\n") if l.rstrip().endswith(" rs-rearmed") or l.rstrip().endswith(" rs-rearmed-input")]
        sizes = sorted({int(l.split()[2]) for l in lines if l.split()[2].isdigit() and int(l.split()[2])})
        quantum = sizes[0] if sizes else 0
        with open(os.path.join(work, "game.log"), errors="replace") as f:
            setup = [l.strip() for l in f if "[rs-audio] output:" in l or "Wwise thread" in l or "LatencyBuffer 1:" in l]
        for l in setup:
            print("  " + l)
        print("  PipeWire's buffers for the game's streams during the test: %s frames" % "/".join(str(x) for x in sizes))
        if len(delays) < CLICKS // 2:
            print("NOT MEASURED: the game answered too few clicks. Is the guitar heard where --keys leave the game? --pictures\n"
                  "saves what it shows. The recording: %s" % recording)
            a.keep = True
            return 1
        ms = sorted(d * 1000.0 / RATE for d in delays)
        mid = ms[len(ms) // 2]
        if a.interface:
            print("\nfrom the interface's input to its output, through the game: %.1f ms (the middle of %d clicks; %.1f to %.1f)"
                  % (mid, len(ms), ms[0], ms[-1]))
            return 0
        print("\nthrough the game, input to output: %.1f ms (the middle of %d clicks; %.1f to %.1f)" % (mid, len(ms), ms[0], ms[-1]))
        added = 0.0
        devices = device_figures()
        for name, way, margin in devices:
            each = (quantum + margin) * 1000.0 / RATE
            added += each
            print("  + %-44s %.1f ms (PipeWire's figure: a buffer of %d frames and %d more)" % ("%s, %s" % (name[:40], way), each, quantum, margin))
        if len(devices) == 2:
            print("with the desktop's default input and output, by PipeWire's figures: %.1f ms, and their converters and cable besides" % (mid + added))
        return 0
    finally:
        late = game_stream()  # (also a run that stopped half way: the stream's two sides as a played game has them)
        if late:
            tool("pactl", "set-sink-input-volume", late, "100%", "100%")
        for proc in reversed(started):
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
        if module:
            tool("pactl", "unload-module", module)
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s" % work)


if __name__ == "__main__":
    sys.exit(main())
