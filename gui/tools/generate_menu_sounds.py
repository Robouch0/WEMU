#!/usr/bin/env python3
"""Synthesizes WEMU's menu sounds, so they are original and can be regenerated.

Usage: generate_menu_sounds.py OUTPUT_DIRECTORY
"""
import sys
import wave
from pathlib import Path

import numpy as np

SFX_RATE = 44100
MUSIC_RATE = 24000
RANDOM = np.random.default_rng(1983)


def db(value):
    return 10 ** (value / 20)


def note(name):
    names = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
    return 440.0 * 2 ** ((names[name[0]] + 12 * (int(name[-1]) - 4) - 9) / 12)


def mallet(frequency, duration, rate, brightness=0.35):
    """A soft struck tone: quiet upper partials that die away before the fundamental."""
    time = np.arange(int(duration * rate)) / rate
    tone = np.zeros_like(time)
    for ratio, level, decay in ((1, 1, 1), (2, brightness, 2.2), (3.01, brightness * 0.4, 4), (4.2, brightness * 0.15, 7)):
        if frequency * ratio < rate / 2:
            tone += level * np.sin(2 * np.pi * frequency * ratio * time) * np.exp(-time * decay * 7 / duration)
    attack = np.minimum(time / 0.004, 1)
    return tone * attack * attack


def room(signal, rate, length=0.35, mix=0.22):
    """A small warm room, made from decaying filtered noise."""
    size = int(length * rate)
    tail = RANDOM.normal(size=size) * np.exp(-np.arange(size) / rate * 9 / length)
    tail = np.convolve(tail, np.ones(8) / 8, mode="same")
    tail /= np.sqrt(np.sum(tail**2))
    return signal[: len(signal)] + mix * np.convolve(signal, tail)[: len(signal)]


def place(track, sound, start):
    begin = int(start)
    track[begin : begin + len(sound)] += sound[: len(track) - begin]


def fade_out(signal, rate, seconds):
    count = int(seconds * rate)
    signal[-count:] *= np.cos(np.linspace(0, np.pi / 2, count)) ** 2
    return signal


def normalized(signal, peak_db):
    return signal / np.max(np.abs(signal)) * db(peak_db)


def write_wav(path, signal, rate):
    frames = signal if signal.ndim == 2 else signal[:, None]
    with wave.open(str(path), "wb") as output:
        output.setnchannels(frames.shape[1])
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes((np.clip(frames, -1, 1) * 32767).astype("<i2").tobytes())


def sequence(notes, gap, length, brightness=0.35, note_length=0.6):
    track = np.zeros(int(length * SFX_RATE) + 1)
    for index, name in enumerate(notes):
        place(track, mallet(note(name), note_length, SFX_RATE, brightness), index * gap * SFX_RATE)
    return track


def effect(track, peak_db):
    extended = np.concatenate([track, np.zeros(int(0.12 * SFX_RATE))])
    return normalized(fade_out(room(extended, SFX_RATE), SFX_RATE, 0.06), peak_db)


def effects():
    return {
        "move": effect(mallet(note("E5"), 0.09, SFX_RATE, 0.2), -20),
        "confirm": effect(sequence(["C5", "G5"], 0.065, 0.3, 0.3, 0.24), -13),
        "back": effect(sequence(["G4", "C4"], 0.065, 0.3, 0.25, 0.24), -14),
        "launch": effect(sequence(["C4", "E4", "G4", "C5", "E5"], 0.085, 1.0, 0.35, 0.65), -10),
    }


def looping_pad(chords, seconds_each):
    """Slow chords. Every partial completes whole cycles, so the end meets the start exactly."""
    period = seconds_each * len(chords)
    count = int(period * MUSIC_RATE)
    time = np.arange(count) / MUSIC_RATE
    left, right = np.zeros(count), np.zeros(count)
    for chord_index, chord in enumerate(chords):
        center = (chord_index + 0.5) / len(chords)
        offset = (time / period - center + 0.5) % 1 - 0.5
        envelope = np.cos(np.clip(offset * len(chords) / 1.5, -0.5, 0.5) * np.pi) ** 2
        for voice, name in enumerate(chord):
            pan = 0.5 + 0.3 * np.sin(voice * 2.4)
            for detune, level in ((-0.12, 0.5), (0.12, 0.5)):
                frequency = round((note(name) + detune) * period) / period
                tone = np.sin(2 * np.pi * frequency * time + voice) + 0.14 * np.sin(4 * np.pi * frequency * time)
                left += envelope * tone * level * (1 - pan) / len(chord)
                right += envelope * tone * level * pan / len(chord)
    return left, right


def looping_chimes(period, count):
    """Sparse quiet notes, added around the loop point so a note ringing past it wraps."""
    left, right = np.zeros(count), np.zeros(count)
    scale = ["C5", "D5", "E5", "G5", "A5", "C6"]
    beat = period / 16
    for step in (1, 3, 6, 8, 11, 12, 14):
        sound = room(mallet(note(RANDOM.choice(scale)), 2.6, MUSIC_RATE, 0.12), MUSIC_RATE, 1.2, 0.4)
        start = int(step * beat * MUSIC_RATE)
        pan = RANDOM.uniform(0.25, 0.75)
        indices = (start + np.arange(len(sound))) % count
        np.add.at(left, indices, sound * (1 - pan))
        np.add.at(right, indices, sound * pan)
    return left, right


def circular_warmth(channel):
    """A soft low-pass and a long tail, wrapped around the loop so its seam stays silent."""
    size = len(channel)
    spectrum = np.fft.rfft(channel)
    frequencies = np.fft.rfftfreq(size, 1 / MUSIC_RATE)
    spectrum *= 1 / (1 + (frequencies / 2200) ** 2)
    tail = np.zeros(size)
    tail[: int(3 * MUSIC_RATE)] = RANDOM.normal(size=int(3 * MUSIC_RATE)) * np.exp(-np.arange(int(3 * MUSIC_RATE)) / MUSIC_RATE * 2.2)
    tail /= np.sqrt(np.sum(tail**2))
    wet = spectrum * np.fft.rfft(tail)
    return np.fft.irfft(spectrum + 0.5 * wet, size)


def music():
    chords = [
        ["C3", "G3", "B3", "D4", "E4"],
        ["A2", "G3", "B3", "C4", "E4"],
        ["F2", "A3", "C4", "E4", "G4"],
        ["G2", "D3", "A3", "B3", "E4"],
    ]
    pad_left, pad_right = looping_pad(chords, 8.0)
    chime_left, chime_right = looping_chimes(32.0, len(pad_left))
    left = circular_warmth(pad_left + 0.1 * chime_left)
    right = circular_warmth(pad_right + 0.1 * chime_right)
    return normalized(np.column_stack([left, right]), -9)


def test_tone(names, gap, seed_brightness):
    length = 5.0
    track = np.zeros(int(length * MUSIC_RATE))
    position = 0.05
    index = 0
    while position < length - 1.4:
        sound = room(mallet(note(names[index % len(names)]), 1.8, MUSIC_RATE, seed_brightness), MUSIC_RATE, 0.9, 0.3)
        place(track, sound, position * MUSIC_RATE)
        position += gap
        index += 1
    return normalized(fade_out(track, MUSIC_RATE, 0.5), -8)


def main():
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    for name, sound in effects().items():
        write_wav(directory / f"{name}.wav", sound, SFX_RATE)
    write_wav(directory / "music.wav", music(), MUSIC_RATE)
    write_wav(directory / "test_low.wav", test_tone(["A2", "E3", "A3", "C4"], 0.55, 0.45), MUSIC_RATE)
    write_wav(directory / "test_high.wav", test_tone(["A5", "C6", "E6", "A6"], 0.45, 0.3), MUSIC_RATE)


if __name__ == "__main__":
    main()
