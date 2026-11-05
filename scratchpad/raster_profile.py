#!/usr/bin/env python3
"""Summarize WEMU_RASTER_PROFILE draw timings without external profilers."""

import argparse
from collections import defaultdict
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--native", action="store_true", help="summarize WEMU_NATIVE_SHADER_AUDIT lowering coverage by draw time")
    args = parser.parse_args()
    pattern = re.compile(
        r"\[RASTER\] frame=(\d+) draw=(\d+) us=(\d+) pixels=(\d+) "
        r"ps=([0-9A-F]+) words=(\d+)"
    )
    programs = defaultdict(lambda: [0, 0, 0])
    frames = set()
    lowering = {}
    stages = defaultdict(int)
    staged_draws = 0
    staged_native = 0
    stage_pattern = re.compile(
        r"\[RASTER_STAGES\] frame=\d+ draw=\d+ native=(true|false) "
        r"prepare_us=(\d+) execute_us=(\d+) verify_us=(\d+) fallback_us=(\d+) other_us=(-?\d+)"
    )
    native_pattern = re.compile(r"\[NATIVEPS\] ps=([0-9A-F]+) lowered=(true|false).* reason=(.*)")
    with args.log.open() as log:
        for line in log:
            stage = stage_pattern.search(line)
            if stage:
                native, *values = stage.groups()
                staged_draws += 1
                staged_native += native == "true"
                for name, value in zip(("native preparation", "backend execution", "reference verification",
                                        "collected software fallback", "other replay work"), values):
                    stages[name] += int(value)
            native = native_pattern.search(line)
            if native:
                shader, supported, reason = native.groups()
                lowering[shader] = "lowered" if supported == "true" else reason
            match = pattern.search(line)
            if not match:
                continue
            frame, _, micros, pixels, shader, words = match.groups()
            frames.add(int(frame))
            row = programs[(shader, int(words))]
            row[0] += int(micros)
            row[1] += int(pixels)
            row[2] += 1
    if not programs:
        parser.error("no completed draw timings found")
    total = sum(row[0] for row in programs.values())
    print(f"{len(frames)} observed frames; {total / 1e6:.3f} seconds in completed draws")
    print("Pixel Program       Words   Draws   Seconds   Share   ns/pixel")
    for (shader, words), (micros, pixels, draws) in sorted(
        programs.items(), key=lambda entry: entry[1][0], reverse=True
    ):
        share = micros * 100 / total if total else 0
        cost = f"{micros * 1000 / pixels:.1f}" if pixels else "n/a"
        print(f"{shader}  {words:5d}  {draws:6d}  {micros / 1e6:8.3f}  {share:5.1f}%  {cost:>9}")
    print("Draw time includes setup and vertex work; this is not whole-session FPS.")
    if staged_draws:
        print(f"\nReplay stages: {staged_draws} draws, {staged_native} committed natively")
        for name, micros in stages.items():
            print(f"{micros / 1e6:8.3f}s  {name}")
        print("Other includes geometry, direct software draws, result commit and unclassified work.")
        print("Deferred execution excludes later resolution; its cost appears at the consumer.")
    if args.native:
        coverage = defaultdict(int)
        for (shader, _), (micros, _, _) in programs.items():
            coverage[lowering.get(shader, "unknown (no lowering record)")] += micros
        print("\nFragment-lowering coverage (not draw eligibility or native execution):")
        for reason, micros in sorted(coverage.items(), key=lambda entry: entry[1], reverse=True):
            share = micros * 100 / total if total else 0
            print(f"{micros / 1e6:8.3f}s {share:6.2f}%  {reason}")


if __name__ == "__main__":
    main()
