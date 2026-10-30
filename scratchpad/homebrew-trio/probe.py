#!/usr/bin/env python3
"""Run the existing desktop probe, releasing button pulses independently of frames.

Some legacy homebrew waits for release without presenting another frame. This
external driver sends a neutral button state after 120 ms to model a real tap.
All game input and frame acknowledgements use the unchanged desktop protocol.
"""
from pathlib import Path
import struct
import json
import time
import sys
import threading
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import session_probe

timed_inputs = []
if "--timed-input" in sys.argv:
    index = sys.argv.index("--timed-input")
    timed_inputs = json.loads(Path(sys.argv[index + 1]).read_text())
    del sys.argv[index:index + 2]
started = False

original_send = session_probe.send
send_lock = threading.Lock()

def release(connection):
    try:
        with send_lock:
            original_send(connection, struct.pack('<III', 4, 4, 0))
    except OSError:
        pass

def timed_driver(connection):
    start = time.monotonic()
    for event in sorted(timed_inputs, key=lambda item: item["seconds"]):
        remaining = start + event["seconds"] - time.monotonic()
        if remaining > 0:
            time.sleep(remaining)
        try:
            with send_lock:
                original_send(connection, struct.pack("<III", 4, 4, event.get("buttons", 0)))
                original_send(connection, struct.pack("<I4dBHH6d", 7, *event.get("left_stick", (0, 0)), *event.get("right_stick", (0, 0)), event.get("touched", False), event.get("touch_x", 0), event.get("touch_y", 0), 0, -1, 0, 0, 0, 0))
        except OSError:
            break

def send(connection, body):
    global started
    if timed_inputs and not started:
        started = True
        threading.Thread(target=timed_driver, args=(connection,), daemon=True).start()
    with send_lock:
        original_send(connection, body)
    if len(body) == 12 and struct.unpack_from('<II', body) == (4, 4) and struct.unpack_from('<I', body, 8)[0]:
        timer = threading.Timer(0.12, release, (connection,))
        timer.daemon = True
        timer.start()

session_probe.send = send
session_probe.main()
