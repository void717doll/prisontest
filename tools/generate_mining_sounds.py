#!/usr/bin/env python3
"""Create the original stone-break clips; no recordings or dependencies needed."""

import math
from pathlib import Path
import random
import struct
import wave


RATE = 44100
DESTINATION = Path(__file__).resolve().parents[1] / "sounds"


def lowpass(samples, cutoff):
    weight = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
    previous = 0.0
    result = []
    for sample in samples:
        previous += weight * (sample - previous)
        result.append(previous)
    return result


def make_break(variant):
    rng = random.Random(961900 + variant)
    duration = (0.180, 0.172, 0.188)[variant]
    count = round(RATE * duration)
    noise = [rng.uniform(-1.0, 1.0) for _ in range(count)]
    bass = lowpass(noise, 280)
    body = lowpass(noise, 2900 + variant * 240)
    grit = lowpass(noise, 6500)
    chips = [(rng.uniform(0.014, 0.095), rng.uniform(0.05, 0.14)) for _ in range(7)]
    signal = []
    for index in range(count):
        time = index / RATE
        attack = min(1.0, time / 0.0012)
        fracture = (body[index] - bass[index]) * math.exp(-time / 0.032)
        contact = (grit[index] - body[index]) * math.exp(-time / 0.007) * 0.45
        thud = sum(math.sin(2 * math.pi * frequency * time) for frequency in
                   (197 + variant * 11, 331 - variant * 9, 509 + variant * 17))
        thud *= 0.065 * math.exp(-time / 0.014)
        debris = 0.0
        for onset, strength in chips:
            age = time - onset
            if age >= 0:
                debris += strength * min(1.0, age / 0.0008) * math.exp(-age / 0.006)
        sample = attack * (fracture + contact + thud + (grit[index] - bass[index]) * debris)
        sample *= min(1.0, (duration - time) / 0.015)
        signal.append(sample)
    # Remove low-frequency/DC energy and leave headroom when clips overlap.
    rumble = lowpass(signal, 70)
    signal = [math.tanh(1.4 * (sample - low)) for sample, low in zip(signal, rumble)]
    peak = max(abs(sample) for sample in signal)
    signal = [sample * 0.65 / peak for sample in signal]
    signal[0] = signal[-1] = 0.0
    return signal


def main():
    DESTINATION.mkdir(exist_ok=True)
    for variant in range(3):
        signal = make_break(variant)
        path = DESTINATION / ("stone_break_%d.wav" % (variant + 1))
        pcm = struct.pack("<%dh" % len(signal), *(round(sample * 32767) for sample in signal))
        with wave.open(str(path), "wb") as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(RATE)
            output.writeframes(pcm)
        print("%s: %.3f seconds, 44.1 kHz mono PCM" % (path.name, len(signal) / RATE))


if __name__ == "__main__":
    main()
