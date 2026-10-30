#!/usr/bin/env python3
"""Measure/capture the isolated core's desktop protocol without a Qt window.

Acknowledges every TV frame immediately. This measures core delivery, excluding
desktop texture uploads and vsync. Uses only the Python standard library.
"""

import argparse
import csv
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile
import time


def read_exact(connection, count):
    result = bytearray(count)
    remaining = memoryview(result)
    while remaining:
        received = connection.recv_into(remaining)
        if not received:
            raise EOFError("core disconnected")
        remaining = remaining[received:]
    return result


def send(connection, body):
    connection.sendall(struct.pack("<I", len(body)) + body)


def save_frame(path, width, height, rgba):
    rgb = bytearray(width * height * 3)
    for channel in range(3):
        rgb[channel::3] = rgba[channel::4]
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + rgb)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rpx", type=Path)
    parser.add_argument("--core", type=Path, default=root / "build/core/wemu")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cpu", choices=("interpreter", "llvm", "x64"), default="interpreter")
    parser.add_argument("--native-cache", type=Path)
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--seconds", type=float, default=120)
    parser.add_argument("--measure-from", type=int, default=150)
    parser.add_argument("--capture-every", type=int, default=0)
    parser.add_argument("--capture-from", type=int, default=1)
    parser.add_argument("--language", type=int, default=1)
    parser.add_argument("--profile", action="store_true")
    parser.add_argument("--clock-profile", action="store_true",
                        help="Record guest ticks versus host time and summarize the emulation speed ratio")
    parser.add_argument("--clock-profile-every", type=int, default=120)
    parser.add_argument("--draw-profile-from", type=int)
    parser.add_argument("--h264-trace", action="store_true")
    parser.add_argument("--thread-dump-ms", type=int)
    parser.add_argument("--ips", action="store_true")
    parser.add_argument("--jit-profile", action="store_true")
    parser.add_argument("--unthrottled", action="store_true")
    parser.add_argument("--thread-registers", action="store_true")
    parser.add_argument("--trace-hle-name")
    parser.add_argument("--trace-hle-every", type=int, default=100000)
    parser.add_argument("--press-at", type=int, action="append", default=[], metavar="FRAME")
    parser.add_argument("--press-buttons", type=lambda value: int(value, 0), default=0x8000, metavar="MASK")
    parser.add_argument("--hold-frames", type=int, default=6)
    parser.add_argument("--control-file", type=Path,
                        help="Live JSON input state: buttons, left_stick/right_stick, touched, touch_x/touch_y")
    parser.add_argument("--input-script", type=Path, help="JSON list of input states with a frame number; each replaces the previous state")
    parser.add_argument("--env", action="append", default=[], metavar="WEMU_NAME=VALUE")
    args = parser.parse_args()
    if args.frames < 1 or args.seconds <= 0 or args.measure_from < 1 or args.capture_every < 0 or args.capture_from < 1:
        parser.error("frame/time limits and measurement start must be positive; capture interval must be nonnegative")
    if args.clock_profile_every < 1:
        parser.error("clock sample interval must be positive")
    if args.hold_frames < 1 or any(frame < 1 for frame in args.press_at) or not 0 <= args.press_buttons <= 0xFFFFFFFF:
        parser.error("button frame numbers/duration must be positive and the mask must fit 32 bits")
    rpx, core, output = args.rpx.resolve(), args.core.resolve(), args.output.resolve()
    if not rpx.is_file() or not core.is_file():
        parser.error("RPX and core executable must exist")
    output.mkdir(parents=True, exist_ok=True)
    inputs = sorted(json.loads(args.input_script.read_text()), key=lambda event: event["frame"]) if args.input_script else []
    input_index, scripted_controls = 0, {}
    environment = {key: value for key, value in os.environ.items() if not key.startswith("WEMU_")}
    environment.update({key: "1" for key in (
        "WEMU_NATIVE_RASTER", "WEMU_NATIVE_DEFER_READBACK", "WEMU_NATIVE_LAZY_READBACK",
        "WEMU_NATIVE_RESIDENT_TEXTURES", "WEMU_NATIVE_RESIDENT_TARGETS", "WEMU_FRAME_CLOCK", "WEMU_NO_FRAMESKIP",
    )})
    environment.update(WEMU_SYSTEM_LANGUAGE=str(args.language), WEMU_SYSTEM_REGION="4", WEMU_TITLE_REGION="4")
    if args.cpu == "llvm":
        environment["WEMU_NATIVE_CPU"] = "1"
        if args.native_cache:
            environment["WEMU_NATIVE_CACHE_DIR"] = str(args.native_cache.resolve())
    elif args.cpu == "x64":
        environment["WEMU_X64_JIT"] = "1"
    if args.profile:
        environment["WEMU_FRAME_PROFILE"] = "1"
    if args.clock_profile:
        environment["WEMU_CLOCK_PROFILE"] = "1"
        environment["WEMU_CLOCK_PROFILE_EVERY"] = str(args.clock_profile_every)
    if args.h264_trace:
        environment["WEMU_H264_TRACE"] = "1"
    if args.thread_dump_ms is not None:
        environment["WEMU_THREAD_DUMP_MS"] = str(args.thread_dump_ms)
    if args.ips:
        environment["WEMU_IPS"] = "1"
    if args.jit_profile:
        environment["WEMU_JIT_PROFILE"] = "1"
    if args.unthrottled:
        environment["WEMU_UNTHROTTLED"] = "1"
    if args.thread_registers:
        environment.update(WEMU_THREAD_REGS="1", WEMU_THREAD_STACK="1")
    if args.trace_hle_name is not None:
        environment["WEMU_TRACE_HLE_NAME"] = args.trace_hle_name
        environment["WEMU_TRACE_HLE_EVERY"] = str(args.trace_hle_every)
    if args.draw_profile_from is not None:
        environment.update(WEMU_RASTER_PROFILE="1", WEMU_NATIVE_SHADER_AUDIT="1",
                           WEMU_RASTER_PROFILE_FROM=str(args.draw_profile_from))
    for setting in args.env:
        key, separator, value = setting.partition("=")
        if not separator or not key.startswith("WEMU_"):
            parser.error("--env requires WEMU_NAME=VALUE")
        environment[key] = value

    rows = []
    error = None
    limited = False
    last_buttons = 0
    last_gamepad = None
    with tempfile.TemporaryDirectory(prefix="wemu-probe-") as temporary, \
            socket.socket(socket.AF_UNIX) as server, (output / "core.log").open("w") as log:
        channel = str(Path(temporary) / "window")
        server.bind(channel)
        server.listen(1)
        server.settimeout(min(30, args.seconds))
        started = time.monotonic()
        process = subprocess.Popen([str(core), "--gui-session", str(rpx), "--window-channel", channel],
                                   cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT)
        connection = None
        try:
            connection, _ = server.accept()
            while len(rows) < args.frames:
                remaining = args.seconds - (time.monotonic() - started)
                if remaining <= 0:
                    limited = True
                    break
                connection.settimeout(remaining)
                size, = struct.unpack("<I", read_exact(connection, 4))
                if not 4 <= size <= 4096 * 4096 * 4 + 128:
                    raise ValueError(f"invalid packet size: {size}")
                body = read_exact(connection, size)
                kind, = struct.unpack_from("<I", body)
                if kind == 1:
                    if body != struct.pack("<II", 1, 4):
                        raise ValueError("unsupported desktop protocol version")
                elif kind == 2:
                    _, sequence, width, height, fps, movie_fps, presents, movies = struct.unpack_from("<IQIIddQQ", body)
                    if not 0 < width <= 4096 or not 0 < height <= 4096 or size != 52 + width * height * 4:
                        raise ValueError("invalid framebuffer dimensions")
                    elapsed = time.monotonic() - started
                    buttons = args.press_buttons if any(first <= sequence < first + args.hold_frames for first in args.press_at) else 0
                    controls = {}
                    while input_index < len(inputs) and inputs[input_index]["frame"] <= sequence:
                        scripted_controls = {key: value for key, value in inputs[input_index].items() if key != "frame"}
                        print(f"input={scripted_controls} frame={sequence}", flush=True)
                        input_index += 1
                    controls.update(scripted_controls)
                    if args.control_file and args.control_file.exists():
                        controls.update(json.loads(args.control_file.read_text()))
                    buttons = controls.get("buttons", buttons)
                    gamepad = struct.pack("<I4dBHH6d", 7, *controls.get("left_stick", (0, 0)),
                                          *controls.get("right_stick", (0, 0)), controls.get("touched", False),
                                          controls.get("touch_x", 0), controls.get("touch_y", 0),
                                          *controls.get("accel", (0, -1, 0)), *controls.get("gyro", (0, 0, 0)))
                    if gamepad != last_gamepad:
                        send(connection, gamepad)
                        print(f"gamepad={controls} frame={sequence}", flush=True)
                        last_gamepad = gamepad
                    if buttons != last_buttons:
                        send(connection, struct.pack("<III", 4, 4, buttons))
                        print(f"buttons={buttons:#x} frame={sequence}", flush=True)
                        last_buttons = buttons
                    rows.append((sequence, elapsed, presents, movies, fps, movie_fps))
                    send(connection, struct.pack("<IQ", 5, sequence))
                    if controls.get("stop", False):
                        break
                    if args.capture_every and sequence >= args.capture_from and sequence % args.capture_every == 0:
                        save_frame(output / f"frame_{sequence:05}.ppm", width, height, body[52:])
                    if sequence % 50 == 0:
                        print(f"frame={sequence} elapsed={elapsed:.2f}s presents={presents} movies={movies}", flush=True)
        except TimeoutError:
            limited = True
        except (EOFError, OSError, ValueError, struct.error) as exception:
            error = str(exception)
        finally:
            if connection:
                try:
                    send(connection, struct.pack("<III", 4, 3, 0))
                except OSError:
                    pass
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            if connection:
                connection.close()

    with (output / "frames.csv").open("w", newline="") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(("sequence", "elapsed_seconds", "presents", "movies", "reported_fps", "reported_movie_fps"))
        writer.writerows(rows)
    measured = [row for row in rows if row[0] >= args.measure_from]
    summary = dict(rpx=str(rpx), cpu=args.cpu, frames=len(rows), exit_code=process.returncode,
                   time_limit_reached=limited, error=error, measure_from=args.measure_from,
                   core_delivery_only=True, unthrottled=environment.get("WEMU_UNTHROTTLED", "").startswith("1"),
                   frame_clock=environment.get("WEMU_FRAME_CLOCK") == "1")
    if len(measured) > 1:
        first, last = measured[0], measured[-1]
        seconds = last[1] - first[1]
        summary.update(measured_seconds=seconds, present_fps=(last[2] - first[2]) / seconds,
                       movie_fps=(last[3] - first[3]) / seconds, movie_frames=last[3] - first[3])
    clock_rows = [(int(frame), int(ticks), float(wall), int(interval) if interval else None)
                  for frame, ticks, wall, interval in re.findall(
                      r"\[CLOCK\] presents=(\d+) ticks=(\d+) wall=([\d.]+)(?: interval=(\d+))?",
                      (output / "core.log").read_text(errors="replace"))]
    if clock_rows:
        with (output / "clock.csv").open("w", newline="") as csv_file:
            writer = csv.writer(csv_file)
            writer.writerow(("presents", "guest_ticks", "wall_seconds", "swap_interval"))
            writer.writerows(clock_rows)
        clock_window = [row for row in clock_rows if args.measure_from <= row[0] <= len(rows)]
        if len(clock_window) > 1:
            first, last = clock_window[0], clock_window[-1]
            wall = last[2] - first[2]
            guest = (last[1] - first[1]) / 62156250
            if wall > 0:
                summary["guest_clock"] = dict(first_present=first[0], last_present=last[0],
                                              wall_seconds=wall, guest_seconds=guest, rate=guest / wall,
                                              swap_intervals=sorted({row[3] for row in clock_window if row[3] is not None}))
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 1 if error or not rows or process.returncode else 0


if __name__ == "__main__":
    raise SystemExit(main())
