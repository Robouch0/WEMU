#!/usr/bin/env python3
"""Compare matched draw timings; neither draw counts nor sums are game FPS."""

import argparse
from collections import defaultdict
from pathlib import Path
import re
from statistics import median


DRAW = re.compile(
    r"\[RASTER\] frame=(\d+) draw=(\d+) us=(\d+) pixels=(\d+) "
    r"ps=([0-9A-F]+) words=(\d+) target=(\d+)x(\d+)"
)


def read_draws(lines, first, last):
    result = {}
    for line in lines:
        match = DRAW.search(line)
        if not match:
            continue
        frame, draw, micros, pixels, shader, words, width, height = match.groups()
        if first <= int(frame) <= last:
            key = (int(frame), int(draw), shader, int(words), int(pixels), int(width), int(height))
            if key in result:
                raise ValueError(f"Duplicate draw identity: {key}")
            result[key] = int(micros)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--first-frame", type=int, default=0)
    parser.add_argument("--last-frame", type=int, default=2**63 - 1)
    args = parser.parse_args()
    if args.first_frame < 0 or args.last_frame < args.first_frame:
        parser.error("invalid frame interval")
    try:
        with args.before.open() as source:
            before = read_draws(source, args.first_frame, args.last_frame)
        with args.after.open() as source:
            after = read_draws(source, args.first_frame, args.last_frame)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    common = before.keys() & after.keys()
    if not common:
        parser.error("no matching draw identities")
    groups = defaultdict(list)
    for key in common:
        groups[key[2]].append((before[key], after[key]))
    print(f"Matched {len(common)} draws; unmatched before={len(before) - len(common)}, after={len(after) - len(common)}")
    print("Pixel Program       Draws   Before s    After s   Change   Median us before/after")
    for shader, pairs in sorted(groups.items(), key=lambda item: sum(a for a, _ in item[1]), reverse=True):
        old, new = zip(*pairs)
        total_old, total_new = sum(old), sum(new)
        change = f"{100 * (total_new / total_old - 1):+.1f}%" if total_old else "n/a"
        print(f"{shader} {len(pairs):6d} {total_old / 1e6:10.3f} {total_new / 1e6:10.3f} {change:>8}"
              f" {median(old):9.1f}/{median(new):.1f}")
    old = sum(before[key] for key in common)
    new = sum(after[key] for key in common)
    print(f"Matched draw time: {old / 1e6:.3f} -> {new / 1e6:.3f} seconds")
    print("Matches require frame, draw, shader, word/pixel counts and target size; inputs may still differ.")
    print("This is not GPU-only time or game FPS. Compare identical diagnostics and repeated runs.")


if __name__ == "__main__":
    main()
