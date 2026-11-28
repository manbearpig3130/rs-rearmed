#!/usr/bin/env python3
"""loop_test.py's reading of a recording, on ones made up: plucks that ring on, an amplifier's slower and louder
version of them a known time later, noise under both, among channels that carry nothing or something else."""
import math
import os
import random
import struct
import sys
import tempfile
import unittest
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import loop_test  # noqa: E402

RATE = 48000


def pluck(n, since):
    """A string plucked SINCE samples ago: a sharp start, ringing on for a third of a second."""
    return 9000 * math.sin(2 * math.pi * 196 * since / RATE + 0.9) * math.exp(-since / (0.12 * RATE)) if 0 <= since < n else 0.0


def recording(path, delay, channels=4, bleed=0.0, seconds=9, pedal=False):
    rnd = random.Random(7)
    n = seconds * RATE
    starts = [int((1.0 + 0.9 * k) * RATE) + rnd.randint(0, 500) for k in range(8)]
    dry, wet, smooth = [], [], 0.0
    for i in range(n):
        d = sum(pluck(RATE // 3, i - s) for s in starts if 0 <= i - s < RATE // 3)
        late = sum(pluck(RATE // 3, i - delay - s) for s in starts if 0 <= i - delay - s < RATE // 3)
        smooth += (2.5 * late - smooth) * 0.25      # an amplifier's and a cabinet's: louder, its start rounder
        dry.append(d + rnd.uniform(-20, 20))
        wet.append(max(-32000, min(32000, smooth + bleed * d)) + rnd.uniform(-40, 40))
    hum = [300 * math.sin(2 * math.pi * 50 * i / RATE) for i in range(n)]
    order = [hum, dry, [rnd.uniform(-10, 10) for _ in range(n)], wet][:channels]
    if pedal:  # the guitar once more, louder and on both sides of a stereo input, each pluck some other time early
        early = [rnd.randint(300, 1700) for _ in starts]
        both = [sum(2.5 * pluck(RATE // 3, i + e - s) for s, e in zip(starts, early) if 0 <= i + e - s < RATE // 3) for i in range(n)]
        order += [both, both]
    with wave.open(path, "wb") as w:
        w.setnchannels(len(order)), w.setsampwidth(2), w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<%dh" % len(order), *(int(c[i]) for c in order)) for i in range(n)))


class Reading(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "r.wav")

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_delay_and_which_channels(self):
        for delay in (480, 1104, 2400):  # 10, 23 and 50 ms
            recording(self.path, delay)
            rate, channels = loop_test.read_wav(self.path)
            self.assertEqual(loop_test.choose(channels, rate), (1, 3))  # the guitar on the second, the game on the fourth
            d, plucks, sounds = loop_test.delays(channels[1], channels[3], rate)
            self.assertEqual((plucks, sounds, len(d)), (8, 8, 8))
            for x in d:
                self.assertLess(abs(x - delay), 48, (delay, d))  # within a millisecond of it, each
            self.assertEqual(loop_test.report(self.path), 0)
            self.assertEqual(loop_test.report(self.path, 2, 4), 0)

    def test_the_guitar_twice_on_the_interface(self):
        """A pedal's stereo output beside the guitar's own channel, louder and starting some other time before it:
        the pair is the channel the game's sound keeps a fixed time behind, not the loudest two."""
        recording(self.path, 1800, pedal=True)
        rate, channels = loop_test.read_wav(self.path)
        self.assertEqual(loop_test.choose(channels, rate), (1, 3))
        self.assertEqual(loop_test.report(self.path), 0)

    def test_a_hum_and_a_silence_are_no_plucks(self):
        recording(self.path, 1000)
        rate, channels = loop_test.read_wav(self.path)
        self.assertEqual((loop_test.onsets(channels[0], rate), loop_test.onsets(channels[2], rate)), ([], []))

    def test_nothing_is_made_up_without_the_cable(self):
        recording(self.path, 1000, channels=3)  # the guitar alone: no second channel has its plucks
        self.assertEqual(loop_test.report(self.path), 1)


if __name__ == "__main__":
    unittest.main()
