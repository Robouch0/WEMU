#!/usr/bin/env python3
"""Runs the WEMU RPX test suite headless and reports per-scene performance.

The core runs in --gui-session mode; this script plays the desktop window
side of the protocol (acknowledging every TV frame immediately, so presentation
to a real window and vsync are excluded). Per-scene timings come from the
core's [FRAME_PROFILE] records between the suite's "[WCS] scene-begin/end"
markers; probe pixels are checked on the TV frames received here.

  run.py                    # all scenes, x64 JIT, best of 3 runs; compares with this machine's baseline
  run.py --save-baseline    # record this machine's baseline (baselines/<host>-<cpu>.json)
  run.py --scenes shadows,postfx --frames 300 --repeat 1
  run.py --analyze-last     # report on the newest GUI session (the viewer's - sequence)

The report leads with a verdict. Checks listed in expectations.json are known
WEMU bugs and do not fail a run; any other failed check, or a scene slower than
the baseline by more than --tolerance, does (exit code 1). Performance is not
judged on battery power or a busy host. Each run also writes summary.html,
summary.md, result.json and junit.xml, and shows the trend over earlier runs.

Uses only the Python standard library.
"""

import argparse
import datetime
import html
import json
import math
import os
import platform
import re
import select
import socket
import statistics
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

import report_html

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PROTOCOL_VERSION = 5
EXPECTATIONS = HERE / 'expectations.json'
SESSION_LOGS = Path.home() / '.local/share/WEMU/WEMU/logs'
STAGES = ('guest', 'hle', 'replay', 'native', 'readback', 'wait', 'video', 'present', 'pace', 'compile')
WCS_RE = re.compile(r'\[WCS\] (\S+)(.*)')
FIELD_RE = re.compile(r'(\w+)=(\S+)')


# ---------------------------------------------------------------------------
# Running the core

def read_exact(connection, count):
    data = bytearray(count)
    view = memoryview(data)
    while view:
        received = connection.recv_into(view)
        if not received:
            raise EOFError('core disconnected')
        view = view[received:]
    return data


def send(connection, body):
    connection.sendall(struct.pack('<I', len(body)) + body)


def probe_pixel(rgba, width, x, y):
    """Mean of the 2x2 block at (x-1..x, y-1..y): the centre of an even-sized frame."""
    total = [0, 0, 0]
    for dy in (-1, 0):
        for dx in (-1, 0):
            offset = ((y + dy) * width + (x + dx)) * 4
            for c in range(3):
                total[c] += rgba[offset + c]
    return tuple(round(t / 4) for t in total)


def write_png(path, width, height, rgba):
    rows = b''.join(b'\x00' + bytes(rgba[y * width * 4:(y + 1) * width * 4]) for y in range(height))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))


def resident_mib(pid):
    try:
        for line in Path(f'/proc/{pid}/status').read_text().splitlines():
            if line.startswith('VmRSS:'):
                return int(line.split()[1]) / 1024
    except OSError:
        pass
    return None


def performance_cores():
    """The fast cores of a hybrid CPU (Intel P-cores), or None when every core is the same.

    Only used by --performance-cores. Pinning does not steady the frame rate: the swings
    between runs are GPU wait time, the guest and HLE CPU time stay the same.
    """
    try:
        cores = set()
        for part in Path('/sys/devices/cpu_core/cpus').read_text().strip().split(','):
            first, _, last = part.partition('-')
            cores.update(range(int(first), int(last or first) + 1))
        return cores if cores and Path('/sys/devices/cpu_atom/cpus').exists() else None
    except (OSError, ValueError):
        return None


def run_core(args, output):
    environment = {k: v for k, v in os.environ.items() if not k.startswith('WEMU_')}
    # The desktop launch configuration (see scratchpad/session_probe.py / the GUI launcher).
    for key in ('WEMU_NATIVE_RASTER', 'WEMU_NATIVE_DEFER_READBACK', 'WEMU_NATIVE_LAZY_READBACK',
                'WEMU_NATIVE_RESIDENT_TEXTURES', 'WEMU_NATIVE_RESIDENT_TARGETS', 'WEMU_FRAME_CLOCK',
                'WEMU_NO_FRAMESKIP', 'WEMU_FRAME_PROFILE'):
        environment[key] = '1'
    if not args.throttled:
        environment['WEMU_UNTHROTTLED'] = '1'
    if args.cpu == 'x64':
        environment['WEMU_X64_JIT'] = '1'
    elif args.cpu == 'llvm':
        environment['WEMU_NATIVE_CPU'] = '1'
    for setting in args.env:
        key, _, value = setting.partition('=')
        environment[key] = value

    log_path = output / 'core.log'
    pixels = {}  # sequence -> (r, g, b) at the probe position
    frames = []  # (sequence, host seconds since start)
    memory = []  # (last sequence, resident MiB) sampled about twice a second
    last_sample = [0.0]
    error, timed_out = None, False
    with tempfile.TemporaryDirectory(prefix='wcs-') as temporary, \
            socket.socket(socket.AF_UNIX) as server, log_path.open('w') as log:
        channel = str(Path(temporary) / 'window')
        server.bind(channel)
        server.listen(1)
        server.settimeout(30)
        started = time.monotonic()
        cores = performance_cores() if args.performance_cores else None
        process = subprocess.Popen([str(args.core), '--gui-session', str(args.rpx), '--window-channel', channel],
                                   cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                   preexec_fn=(lambda: os.sched_setaffinity(0, cores)) if cores else None)
        connection = None

        def finished():
            log.flush()
            return '[WCS] suite-end' in log_path.read_text(errors='replace')

        def sample_memory():
            now = time.monotonic()
            if now - last_sample[0] < 0.5:
                return
            last_sample[0] = now
            mib = resident_mib(process.pid)
            if mib is not None:
                memory.append((frames[-1][0] if frames else 0, mib))

        try:
            connection, _ = server.accept()
            while True:
                remaining = args.timeout - (time.monotonic() - started)
                if remaining <= 0:
                    timed_out = True
                    break
                # Poll so a guest that stops presenting (e.g. after suite-end) is noticed.
                if not select.select([connection], [], [], 1.0)[0]:
                    if finished() or process.poll() is not None:
                        break
                    continue
                connection.settimeout(remaining)
                size, = struct.unpack('<I', read_exact(connection, 4))
                if not 4 <= size <= 4096 * 4096 * 4 + 128:
                    raise ValueError(f'invalid packet size {size}')
                body = read_exact(connection, size)
                kind, = struct.unpack_from('<I', body)
                if kind == 1:
                    if body != struct.pack('<II', 1, PROTOCOL_VERSION):
                        raise ValueError('unsupported desktop protocol version')
                    continue
                if kind != 2:
                    continue
                _, sequence, width, height = struct.unpack_from('<IQII', body)
                if size != 56 + width * height * 4:
                    raise ValueError('invalid framebuffer size')
                frames.append((sequence, time.monotonic() - started))
                sample_memory()
                pixels[sequence] = probe_pixel(memoryview(body)[56:], width, width // 2, height // 2)
                if args.capture_every and sequence % args.capture_every == 0:
                    write_png(output / f'frame_{sequence:05}.png', width, height, memoryview(body)[56:])
                send(connection, struct.pack('<IQ', 5, sequence))
                if sequence % 60 == 0:
                    if finished():
                        break
                    if not args.quiet:
                        print(f'  frame {sequence} ({time.monotonic() - started:.0f}s)', flush=True)
        except TimeoutError:
            timed_out = True
        except (EOFError, OSError, ValueError, struct.error) as exception:
            error = str(exception)
        finally:
            if connection:
                try:
                    send(connection, struct.pack('<III', 4, 3, 0))  # request shutdown
                except OSError:
                    pass
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            if connection:
                connection.close()
    return dict(pixels=pixels, frames=frames, memory=memory, error=error, timed_out=timed_out, exit_code=process.returncode,
                host_seconds=time.monotonic() - started)


# ---------------------------------------------------------------------------
# Parsing

def fields(text):
    return dict(FIELD_RE.findall(text))


def percentile(values, fraction):
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, round(fraction * (len(ordered) - 1))))
    return ordered[index]


def parse_log(text, warmup):
    scenes, order, current, last_scene = {}, [], None, None
    suite = {}
    for line in text.splitlines():
        if '[FRAME_PROFILE]' in line:
            if current is not None:
                record = fields(line)
                current['profile'].append({'wall': int(record.get('wall_us', 0)),
                                           **{stage: int(record.get(f'{stage}_us', 0)) for stage in STAGES}})
            continue
        if '[NATIVE_VULKAN] device=' in line:
            suite['gpu'] = line.split('device=', 1)[1].strip()
        match = WCS_RE.search(line)
        if not match:
            continue
        event, info = match.group(1), fields(match.group(2))
        if event == 'scene-begin':
            current = scenes[info['name']] = dict(name=info['name'], profile=[], probes=[], details=[], errors=[],
                                                  first_present=int(info.get('present', 0)))
            order.append(info['name'])
        elif event == 'scene-end':
            if current is not None:
                current.update(frames=int(info['frames']), guest_ms=int(info['guest_ms']),
                               last_present=int(info.get('present', 0)))
            last_scene, current = current, None
        elif event == 'probe' and last_scene is not None:
            last_scene['probes'].append(info)
        elif event == 'detail' and last_scene is not None:
            last_scene['details'].append(match.group(2).strip())
        elif event == 'check' and info.get('name') in scenes:
            scenes[info['name']]['guest_check'] = info['result']
        elif event == 'error':
            (current or last_scene or suite).setdefault('errors', []).append(match.group(2).strip())
        elif event in ('suite-begin', 'suite-end'):
            suite[event] = info

    for scene in scenes.values():
        scene['workload'] = fields(' '.join(scene['details']))
        samples = scene.pop('profile')[warmup:]
        scene['measured_frames'] = len(samples)
        if not samples:
            continue
        walls = [s['wall'] for s in samples]
        mean = statistics.fmean(walls)
        # Median frame time: robust to the odd hitch (compiles, host scheduling).
        median = statistics.median(walls)
        scene['fps'] = 1e6 / median if median else 0.0
        scene['frame_ms'] = {'mean': mean / 1000, 'p50': percentile(walls, 0.5) / 1000,
                             'p95': percentile(walls, 0.95) / 1000, 'p99': percentile(walls, 0.99) / 1000,
                             'max': max(walls) / 1000}
        scene['stage_ms'] = {stage: statistics.fmean(s[stage] for s in samples) / 1000 for stage in STAGES}
        scene['stability'] = stability(walls)
        scene['frame_series_ms'] = [round(w / 1000, 2) for w in walls]
    return suite, [scenes[name] for name in order]


MISSED_60HZ_MS = 1.25 * 1000 / 60  # paced frames run ~16.8 ms; a dropped refresh is ~33 ms
MISSED_30HZ_MS = 1.25 * 1000 / 30


def stability(walls_us):
    """Frame pacing figures for one scene (frame times in microseconds)."""
    ms = [w / 1000 for w in walls_us]
    median = statistics.median(ms)
    mean = statistics.fmean(ms)
    quarter = max(1, len(ms) // 4)
    first, last = statistics.median(ms[:quarter]), statistics.median(ms[-quarter:])
    return {
        'fps_mean': 1000 / mean if mean else 0.0,
        'low_1pct_fps': 1000 / percentile(ms, 0.99),        # fps of the slowest 1% of frames
        'low_01pct_fps': 1000 / percentile(ms, 0.999),
        'jitter_ms': statistics.pstdev(ms),                 # frame-time standard deviation
        'cv_pct': 100 * statistics.pstdev(ms) / mean if mean else 0.0,
        # A stutter is a visible hitch: twice the usual frame time and a missed 60 Hz refresh.
        'stutters': sum(1 for v in ms if v > 2 * median and v > MISSED_60HZ_MS),
        'worst_ms': max(ms),
        'over_16ms_pct': 100 * sum(1 for v in ms if v > MISSED_60HZ_MS) / len(ms),  # missed a 60 Hz refresh
        'over_33ms_pct': 100 * sum(1 for v in ms if v > MISSED_30HZ_MS) / len(ms),
        # Within 25% of the median, but never stricter than 2 ms (imperceptible at high rates).
        'stable_pct': 100 * sum(1 for v in ms if abs(v - median) <= max(0.25 * median, 2.0)) / len(ms),
        'drift_pct': 100 * (first / last - 1) if last else 0.0,  # fps change, last quarter vs first
    }


def attach_memory(scenes, memory):
    """Resident memory of the core across each scene (samples tagged by frame sequence)."""
    for scene in scenes:
        if 'last_present' not in scene:
            continue
        inside = [mib for sequence, mib in memory if scene['first_present'] < sequence <= scene['last_present'] + 1]
        if inside:
            scene['memory_mib'] = {'start': inside[0], 'end': inside[-1], 'peak': max(inside),
                                   'growth': inside[-1] - inside[0]}


# Thresholds for warnings (reported, never failing a run on their own).
UNSTABLE_PCT, STUTTER_PCT, DRIFT_PCT, GROWTH_MIB, DRIFT_MIN_FRAMES = 90.0, 1.0, -10.0, 64.0, 1800


def warnings_for(scene, paced):
    figures, notes = scene.get('stability'), []
    if figures:
        # Even pacing only matters when frames are paced (GUI sessions, --throttled).
        if paced and figures['stable_pct'] < UNSTABLE_PCT:
            notes.append(f"unstable pacing: {figures['stable_pct']:.0f}% of frames near the median frame time")
        if 100 * figures['stutters'] / max(1, scene['measured_frames']) > STUTTER_PCT:
            notes.append(f"{figures['stutters']} stutters (frames over 2x median that miss a 60 Hz refresh), worst {figures['worst_ms']:.1f} ms")
        # Short scenes drift with CPU boost windows; only long ones show real decay.
        if scene['measured_frames'] >= DRIFT_MIN_FRAMES and figures['drift_pct'] < DRIFT_PCT:
            notes.append(f"fps drifted {figures['drift_pct']:+.0f}% from first to last quarter")
    growth = (scene.get('memory_mib') or {}).get('growth', 0)
    if growth > GROWTH_MIB:
        notes.append(f'memory grew {growth:.0f} MiB during the scene')
    return notes


# ---------------------------------------------------------------------------
# Checks: every probe is pass, known (expected failure), fail (new) or fixed.

RANK = {'pass': 0, 'unverified': 1, 'fixed': 2, 'known': 3, 'fail': 4}


def load_expectations():
    try:
        return json.loads(EXPECTATIONS.read_text())
    except (OSError, ValueError) as exception:
        print(f'warning: {EXPECTATIONS.name} not usable ({exception}); every failure counts as new', file=sys.stderr)
        return {}


def scene_status(scene):
    if scene.get('guest_check') != 'PASS' or any(p['status'] == 'fail' for p in scene['probes']):
        return 'fail'
    return 'known' if any(p['status'] == 'known' for p in scene['probes']) else 'pass'


def check_probes(scenes, pixels, expectations, verify=True):
    """Host-side probe verification against the TV frames the core delivered.

    Without frames (an analysed GUI session log) probes are reported as unverified.
    """
    expected = expectations.get('probes', {})
    for scene in scenes:
        for probe in scene['probes']:
            known = expected.get(f"{scene['name']}/{probe['name']}")
            if known:
                probe['known'] = known
            if not verify:
                probe.update(sequence=None, got='n/a', result='UNVERIFIED', guest_readback='UNVERIFIED', status='unverified')
                continue
            want = tuple(map(int, probe['want'].split(',')))
            tolerance = int(probe['tol'])
            first, count = int(probe['present']), int(probe['count'])
            # Sequence numbers start at 1 for present index 0; check the middle repeat.
            candidates = [first + 1 + i for i in range(count)]
            sequence = candidates[len(candidates) // 2]
            got = pixels.get(sequence)
            probe['sequence'] = sequence
            probe['got'] = ','.join(map(str, got)) if got else 'missing'
            probe['result'] = 'PASS' if got and all(abs(g - w) <= tolerance for g, w in zip(got, want)) else 'FAIL'
            guest = tuple(map(int, probe['guest'].split(',')))
            probe['guest_readback'] = 'PASS' if all(abs(g - w) <= tolerance for g, w in zip(guest, want)) else 'FAIL'
            if probe['result'] == 'PASS':
                probe['status'] = 'fixed' if known and not known.get('order_dependent') else 'pass'
            else:
                probe['status'] = 'known' if known else 'fail'
        guest_ok = scene.get('guest_check') == 'PASS'
        probes_ok = all(p['result'] in ('PASS', 'UNVERIFIED') for p in scene['probes'])
        scene['check'] = 'PASS' if guest_ok and probes_ok else 'FAIL'
        scene['status'] = scene_status(scene)


# ---------------------------------------------------------------------------
# Machine state: performance is only judged when the host can be trusted.

LOW_POWER_PROFILES = ('low-power', 'quiet', 'cool', 'power-saver')


def read_text(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return None


def environment_info():
    supplies = Path('/sys/class/power_supply')
    mains = [read_text(s / 'online') == '1' for s in supplies.glob('*') if read_text(s / 'type') == 'Mains']
    battery = any(read_text(s / 'type') == 'Battery' for s in supplies.glob('*'))
    if mains:
        power = 'ac' if any(mains) else 'battery'
    else:
        power = 'unknown' if battery or not supplies.is_dir() else 'ac'  # no battery at all: a desktop
    model = next((line.split(':', 1)[1].strip() for line in (read_text('/proc/cpuinfo') or '').splitlines()
                  if line.startswith('model name')), platform.processor() or 'unknown')
    cores = os.cpu_count() or 1
    try:
        load = os.getloadavg()[0]
    except OSError:
        load = 0.0
    profile = read_text('/sys/firmware/acpi/platform_profile')
    reasons = []
    if power == 'battery':
        reasons.append('on battery power')
    if profile in LOW_POWER_PROFILES:
        reasons.append(f'power profile "{profile}"')
    if load > max(4.0, cores / 4):
        reasons.append(f'host busy (load {load:.1f} on {cores} threads)')
    return dict(cpu=model, threads=cores, power=power, profile=profile,
                governor=read_text('/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor'), load=round(load, 2),
                unreliable=reasons)


# ---------------------------------------------------------------------------
# Reporting

def git_revision():
    try:
        rev = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT, capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=ROOT, capture_output=True,
                               text=True).stdout.strip()
        return rev + ('-dirty' if dirty else '')
    except OSError:
        return 'unknown'


def merge_runs(runs):
    """One scene list from repeated runs: best fps, spread, and the worst result of every check.

    Host throttling (power profiles, thermals, background load) only ever makes
    a run slower, so the fastest run is the best estimate of what the core can do.
    A check that fails in any run counts as failing.
    """
    merged = []
    for scene in runs[0]:
        same = [next((s for s in run if s['name'] == scene['name']), None) for run in runs]
        timed = [s for s in same if s and s.get('fps')]
        result = dict(scene)
        if timed:
            representative = max(timed, key=lambda s: s['fps'])
            fps_runs = [s['fps'] for s in timed]
            result.update(fps=max(fps_runs), fps_runs=fps_runs, spread=(max(fps_runs) - min(fps_runs)) / max(fps_runs),
                          frame_ms=representative['frame_ms'], stage_ms=representative['stage_ms'],
                          stability=representative['stability'], frame_series_ms=representative['frame_series_ms'],
                          memory_mib=representative.get('memory_mib'))
        for probe in result['probes']:
            twins = [p for s in same if s for p in s['probes'] if p['name'] == probe['name']]
            worst = max(twins, key=lambda p: RANK[p['status']])
            probe.update(status=worst['status'], result=worst['result'], got=worst['got'])
        result['check'] = 'PASS' if all(s and s.get('check') == 'PASS' for s in same) else 'FAIL'
        result['status'] = max((s['status'] if s else 'fail' for s in same), key=RANK.get)
        merged.append(result)
    return merged


STEADY_SPREAD = 0.15  # runs of a scene within 15% of each other: steady enough for the speed index

# What a scene's workload says about raw throughput, most telling first.
THROUGHPUT = (('triangles_per_frame', 1e6, 'M triangles/s'), ('fullscreen_pixels_per_frame', 1e9, 'G pixels/s'),
              ('instances', 1e3, 'k instances/s'), ('particles', 1e3, 'k particles/s'), ('draws_per_frame', 1e3, 'k draws/s'))


def throughput(scene):
    for key, scale, unit in THROUGHPUT:
        amount = scene.get('workload', {}).get(key, '')
        if amount.isdigit() and scene.get('fps'):
            return dict(value=int(amount) * scene['fps'] / scale, unit=unit, per_frame=int(amount), what=key.replace('_', ' '))
    return None


def compare(scenes, baseline, tolerance):
    """Performance against the baseline: rows per scene and a summary."""
    reference = {scene['name']: scene for scene in baseline.get('scenes', [])}
    rows, slower, faster, ratios, notes, noisy, steady = [], [], [], [], [], [], []
    for scene in scenes:
        base = reference.get(scene['name'])
        row = dict(name=scene['name'], fps=scene.get('fps'), base_fps=base.get('fps') if base else None, verdict='new')
        if base and scene.get('fps') and base.get('fps'):
            if base.get('frames') != scene.get('frames'):
                # A scene's speed depends on how long it runs (shader warm-up, CPU boost windows).
                row.update(verdict='incomparable', base_fps=None)
                notes.append(f"{scene['name']}: baseline ran {base.get('frames')} frames, this run {scene.get('frames')}; not compared")
            else:
                change = scene['fps'] / base['fps'] - 1.0
                row['change'] = change
                ratios.append(1.0 + change)
                # Runs of one scene can differ a lot (GPU-light scenes on a laptop swing by half).
                # A change counts only when it is beyond the tolerance AND the two sets of runs do
                # not overlap: slower than every baseline run, or every run faster than the baseline.
                floor = min(base.get('fps_runs') or [base['fps']])
                worst = min(scene.get('fps_runs') or [scene['fps']])
                if change < -tolerance:
                    row['verdict'] = 'slower' if scene['fps'] < floor else 'noise'
                elif change > tolerance:
                    row['verdict'] = 'faster' if worst > base['fps'] else 'noise'
                else:
                    row['verdict'] = 'same'
                text = f"{scene['name']}: {base['fps']:.1f} -> {scene['fps']:.1f} fps ({change:+.1%})"
                if row['verdict'] == 'slower':
                    slower.append(text)
                elif row['verdict'] == 'faster':
                    faster.append(text)
                elif row['verdict'] == 'noise':
                    noisy.append(f"{text}: runs ranged {worst:.0f}-{scene['fps']:.0f} fps now, "
                                 f"{floor:.0f}-{base['fps']:.0f} in the baseline")
                # The speed index only counts scenes that repeat well in both.
                if max(scene.get('spread', 0.0), base.get('spread', 0.0)) <= STEADY_SPREAD:
                    steady.append(1.0 + change)
                low, base_low = (s.get('stability', {}).get('low_1pct_fps') for s in (scene, base))
                if low and base_low:
                    row['low_change'] = low / base_low - 1.0
                    if row['verdict'] == 'same' and row['low_change'] < -2 * tolerance:
                        notes.append(f"{scene['name']}: 1% low fell {row['low_change']:+.0%} "
                                     f"({base_low:.0f} -> {low:.0f} fps) while the median held")
        rows.append(row)
    counted = steady or ratios
    index = math.exp(statistics.fmean(math.log(r) for r in counted)) if counted else None
    return rows, dict(slower=slower, faster=faster, index=index, compared=len(ratios), steady=len(steady), noisy=noisy,
                      notes=notes, tolerance=tolerance)


def history(output, meta, names, limit=20):
    """Earlier comparable runs on this machine, oldest first (read from results/*/result.json).

    Comparable means the same scene list and repeat count: a scene runs faster early in a
    run (CPU boost) and the best of three runs beats a single one.
    """
    entries = []
    for path in (HERE / 'results').glob('*/result.json'):
        if path.parent == output:
            continue
        try:
            old = json.loads(path.read_text())
            old_meta = old['meta']
            if (old_meta['host'], old_meta['cpu'], bool(old_meta['throttled'])) != (meta['host'], meta['cpu'], bool(meta['throttled'])):
                continue
            if old_meta.get('reliable') is False or old_meta.get('repeat') != meta['repeat']:
                continue
            if [s['name'] for s in old['scenes']] != names:
                continue
            entries.append(dict(date=old_meta['date'], revision=old_meta.get('revision', '?'),
                                scenes={s['name']: (s['fps'], s.get('frames')) for s in old['scenes'] if s.get('fps')}))
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return sorted(entries, key=lambda e: e['date'])[-limit:]


def attach_trend(scenes, entries):
    for scene in scenes:
        if not scene.get('fps'):
            continue
        points = [dict(date=e['date'], revision=e['revision'], fps=e['scenes'][scene['name']][0]) for e in entries
                  if scene['name'] in e['scenes'] and e['scenes'][scene['name']][1] == scene.get('frames')]
        scene['trend'] = points
        if points:
            scene['previous_change'] = scene['fps'] / points[-1]['fps'] - 1.0


def summarize(result, perf, statuses, expectations, baseline_note):
    """The verdict and the figures the reports lead with."""
    meta, scenes = result['meta'], result['scenes']
    probes = [(s['name'], p) for s in scenes for p in s['probes']]
    count = {status: sum(1 for _, p in probes if p['status'] == status) for status in RANK}
    failures = []
    for scene in scenes:
        if scene.get('guest_check') != 'PASS':
            errors = '; '.join(scene.get('errors', [])) or 'see the core log'
            failures.append(f"{scene['name']}: self-check {scene.get('guest_check', 'did not run')} ({errors})")
        failures += [f"{scene['name']}/{p['name']}: expected {p['want']}, got {p['got']}" for p in scene['probes']
                     if p['status'] == 'fail']
    for index, status in enumerate(statuses):
        if not status['complete']:
            failures.append(f'run {index + 1}: suite did not finish' + (' (timeout)' if status['timed_out'] else '') +
                            (f": {status['error']}" if status['error'] else ''))
    fixed = [f"{name}/{p['name']} ({p['known']['ref']})" for name, p in probes if p['status'] == 'fixed']
    slower = perf['slower'] if meta['reliable'] else []

    caveats = []
    if not meta['reliable']:
        caveats.append('performance not judged: ' + ', '.join(meta['environment']['unreliable']))
    if baseline_note:
        caveats.append(baseline_note)
    if meta.get('profiled') is False:
        caveats.append('no [FRAME_PROFILE] records: start the GUI with WEMU_FRAME_PROFILE=1 for timing')
    if count['unverified']:
        caveats.append(f"{count['unverified']} probes not verified (a session log has no frames): run.py without "
                       '--analyze-* checks them')

    readback_ok = sum(1 for _, p in probes if p.get('guest_readback') == 'PASS')
    known_readback = expectations.get('guest_readback')
    verified = len(probes) - count['unverified']
    if known_readback and verified and readback_ok:
        fixed.append(f"guest readback now matches on {readback_ok} of {verified} probes ({known_readback['ref']})")

    if failures:
        verdict, headline = 'FAIL', f"{len(failures)} new failure{'s' if len(failures) != 1 else ''}"
    elif slower:
        verdict, headline = 'SLOWER', f"{len(slower)} scene{'s' if len(slower) != 1 else ''} slower than the baseline"
    else:
        verdict, headline = 'PASS', 'no regressions'
    if count['known']:
        headline += f", {count['known']} known failure{'s' if count['known'] != 1 else ''}"

    timed = [s for s in scenes if s.get('stability')]
    lowest = min(timed, key=lambda s: s['stability']['low_1pct_fps'], default=None)
    grown = max((s for s in scenes if s.get('memory_mib')), key=lambda s: s['memory_mib']['growth'], default=None)
    return dict(verdict=verdict, headline=headline, probes=count, probe_total=len(probes), failures=failures, fixed=fixed,
                slower=slower, faster=perf['faster'], slower_unjudged=[] if meta['reliable'] else perf['slower'],
                index=perf['index'], compared=perf['compared'], steady=perf['steady'], noisy=perf['noisy'],
                tolerance=perf['tolerance'], perf_notes=perf['notes'],
                caveats=caveats, scenes=dict(total=len(scenes), **{k: sum(1 for s in scenes if s['status'] == k)
                                                                  for k in ('pass', 'known', 'fail')}),
                stutters=sum(s['stability']['stutters'] for s in timed),
                missed_60hz_pct=max((s['stability']['over_16ms_pct'] for s in timed), default=0.0),
                lowest_1pct=dict(scene=lowest['name'], fps=lowest['stability']['low_1pct_fps']) if lowest else None,
                peak_memory_mib=max((s['memory_mib']['peak'] for s in scenes if s.get('memory_mib')), default=None),
                largest_growth=dict(scene=grown['name'], mib=grown['memory_mib']['growth']) if grown else None,
                guest_readback=dict(passing=readback_ok, verified=verified, known=known_readback))


STATUS_LABEL = {'pass': 'pass', 'known': 'known', 'fail': 'FAIL', 'fixed': 'fixed', 'unverified': 'unverified'}


def change_text(row):
    if 'change' not in row:
        return '—'
    return f"{row['change']:+.1%}" + {'slower': ' SLOWER', 'faster': ' faster', 'noise': ' (noise)'}.get(row['verdict'], '')


def headline_lines(result):
    """The verdict block shared by the terminal and Markdown reports."""
    meta, summary = result['meta'], result['summary']
    count, env = summary['probes'], meta['environment']
    lines = [f"VERDICT: {summary['verdict']} — {summary['headline']}"]
    if count['unverified'] == summary['probe_total'] and summary['probe_total']:
        ok = summary['scenes']['total'] - summary['scenes']['fail']
        checked = (f"{ok} of {summary['scenes']['total']} scenes pass their self-checks, "
                   f"{count['unverified']} probes not verified")
    else:
        checked = f"{summary['scenes']['total']} scenes, {summary['probe_total']} probes: {count['pass']} pass"
        for status in ('known', 'fail', 'fixed', 'unverified'):
            if count[status] or status == 'fail':
                checked += f", {count[status]} {'new failures' if status == 'fail' else status}"
    lines.append(f'  correctness  {checked}')
    if summary['index'] is not None:
        over = f"{summary['steady']} steady scenes" if summary['steady'] else f"{summary['compared']} scenes"
        performance = (f"{summary['index']:.1%} of the baseline (geometric mean of {over}), "
                       f"{len(summary['slower']) + len(summary['slower_unjudged'])} slower, {len(summary['faster'])} faster"
                       + (f", {len(summary['noisy'])} too noisy to judge" if summary['noisy'] else '')
                       + f" (tolerance {summary['tolerance']:.0%})")
    else:
        performance = 'not compared'
    lines.append(f'  performance  {performance}')
    if summary['lowest_1pct']:
        smooth = (f"{summary['stutters']} stutters, lowest 1% low {summary['lowest_1pct']['fps']:.0f} fps "
                  f"({summary['lowest_1pct']['scene']})")
        if meta['throttled']:
            smooth += f", up to {summary['missed_60hz_pct']:.1f}% of frames missed a 60 Hz refresh"
        lines.append(f'  smoothness   {smooth}')
    if summary['peak_memory_mib']:
        lines.append(f"  memory       peak {summary['peak_memory_mib']:.0f} MiB, largest growth "
                     f"{summary['largest_growth']['mib']:+.0f} MiB ({summary['largest_growth']['scene']})")
    power = {'ac': 'AC power', 'battery': 'BATTERY', 'unknown': 'power source unknown'}[env['power']]
    lines.append(f"  machine      {env['cpu']}, {meta.get('gpu') or 'GPU unknown'}, {power}"
                 f"{', profile ' + env['profile'] if env['profile'] else ''}, load {env['load']}")
    lines += [f'  note         {caveat}' for caveat in summary['caveats']]
    return lines


def issue_lines(summary, bullet):
    lines = []
    for title, items in (('New failures', summary['failures']), ('Slower than the baseline', summary['slower']),
                         ('Slower, but not judged (unreliable machine state)', summary['slower_unjudged']),
                         ('Fixed: remove from expectations.json', summary['fixed']),
                         ('Faster than the baseline', summary['faster']),
                         ('Too noisy to judge (runs of the scene overlap the baseline)', summary['noisy']),
                         ('Performance notes', summary['perf_notes'])):
        if items:
            lines += ['', f'{title}:'] + [f'{bullet}{item}' for item in items]
    return lines


def scene_cells(scene, row, paced, repeated):
    f, mem, rate = scene.get('stability'), scene.get('memory_mib'), scene.get('throughput')
    ms = scene.get('frame_ms')
    cells = [scene['name'], STATUS_LABEL[scene['status']], f"{scene['fps']:.1f}" if scene.get('fps') else '—',
             change_text(row)]
    if repeated:
        cells.append(f"{scene['spread']:.0%}" if 'spread' in scene else '—')
    cells += [f"{f['low_1pct_fps']:.1f}" if f else '—', f"{ms['p99']:.2f}" if ms else '—', str(f['stutters']) if f else '—']
    if paced:
        cells.append(f"{f['over_16ms_pct']:.1f}%" if f else '—')
    cells.append(f"{mem['growth']:+.0f} MiB" if mem else '—')
    if not paced:
        cells.append(f"{rate['value']:.1f} {rate['unit']}" if rate else '—')
    cells.append(f"{scene['previous_change']:+.1%}" if 'previous_change' in scene else '—')
    return cells


def scene_header(paced, repeated):
    return (['scene', 'status', 'fps', 'vs baseline'] + (['run spread'] if repeated else []) +
            ['1% low', 'p99 ms', 'stutters'] + (['missed 60 Hz'] if paced else []) +
            ['memory'] + ([] if paced else ['throughput']) + ['vs previous run'])


def console(result, rows):
    """A short, aligned report for the terminal: verdict first, one line per scene."""
    meta = result['meta']
    by_name = {row['name']: row for row in rows}
    shape = (meta['throttled'], meta['repeat'] > 1)
    table = [scene_header(*shape)] + [scene_cells(s, by_name.get(s['name'], {}), *shape) for s in result['scenes']]
    widths = [max(len(line[i]) for line in table) for i in range(len(table[0]))]
    lines = [f"WEMU RPX suite — core {meta['revision']}, cpu {meta['cpu']}, {meta['host']}, best of {meta['repeat']} run(s)", '']
    lines += headline_lines(result) + ['']
    for index, line in enumerate(table):
        lines.append('  '.join(cell.ljust(widths[i]) if i < 2 else cell.rjust(widths[i]) for i, cell in enumerate(line)).rstrip())
        if index == 0:
            lines.append('  '.join('-' * w for w in widths))
    return '\n'.join(lines + issue_lines(result['summary'], '  - '))


def markdown(result, rows):
    meta, summary = result['meta'], result['summary']
    lines = [f"# WEMU RPX suite — {meta['date']}", '',
             f"core `{meta['revision']}` · cpu `{meta['cpu']}` · host `{meta['host']}`"
             f" · warmup {meta['warmup']} frames · best of {meta['repeat']} run(s)", '', '```']
    lines += headline_lines(result) + ['```']
    lines += issue_lines(summary, '- ')
    by_name = {row['name']: row for row in rows}
    shape = (meta['throttled'], meta['repeat'] > 1)
    header = scene_header(*shape)
    lines += ['', '## Scenes', '', '| ' + ' | '.join(header) + ' |', '|---|---|' + '---:|' * (len(header) - 2)]
    lines += ['| ' + ' | '.join(scene_cells(s, by_name.get(s['name'], {}), *shape)) + ' |' for s in result['scenes']]
    known = [(scene['name'], p) for scene in result['scenes'] for p in scene['probes'] if p['status'] == 'known']
    if known:
        lines += ['', '## Known failures', '', '| probe | expected | got | bug | reason |', '|---|---|---|---|---|']
        lines += [f"| {s}/{p['name']} | {p['want']} | {p['got']} | {p['known']['ref']} | {p['known']['reason']} |" for s, p in known]
    readback = summary['guest_readback']
    if readback['known'] and readback['verified'] and not readback['passing']:
        lines += ['', f"Guest readback (the RPX reading its own frame through `GX2CopySurface`) fails on all "
                      f"{readback['verified']} probes: {readback['known']['reason']} ({readback['known']['ref']})."]
    lines += ['', '## Frame times and where the time goes (ms per frame)', '',
              '| scene | p50 | p95 | p99 | max | ' + ' | '.join(STAGES) + ' |', '|---|' + '---:|' * (4 + len(STAGES))]
    for scene in result['scenes']:
        ms, stages = scene.get('frame_ms'), scene.get('stage_ms', {})
        if ms:
            lines.append(f"| {scene['name']} | {ms['p50']:.2f} | {ms['p95']:.2f} | {ms['p99']:.2f} | {ms['max']:.2f} | " +
                         ' | '.join(f"{stages.get(s, 0):.2f}" for s in STAGES) + ' |')
    warned = [(scene['name'], w) for scene in result['scenes'] for w in scene.get('warnings', [])]
    if warned:
        lines += ['', '## Warnings', ''] + [f'- `{s}` {w}' for s, w in warned]
    probes = [(scene['name'], p) for scene in result['scenes'] for p in scene['probes']]
    if probes:
        lines += ['', '## All probes', '', '| scene | probe | expected | got | status |', '|---|---|---|---|---|']
        lines += [f"| {s} | {p['name']} | {p['want']} | {p['got']} | {STATUS_LABEL[p['status']]} |" for s, p in probes]
    details = [(scene['name'], d) for scene in result['scenes'] for d in scene['details'] + scene['errors']]
    if details:
        lines += ['', '## Workload details', ''] + [f'- `{s}` {d}' for s, d in details]
    return '\n'.join(lines) + '\n'


def junit(result, rows):
    """JUnit XML for CI: known failures are skipped tests, new failures and slowdowns fail."""
    def attribute(text):
        return html.escape(str(text), quote=True)

    by_name = {row['name']: row for row in rows}
    judged = result['meta']['reliable']
    suites, total, failed, skipped = [], 0, 0, 0
    for scene in result['scenes']:
        cases = []
        if scene.get('guest_check') == 'PASS':
            cases.append('<testcase name="self-check"/>')
        else:
            cases.append(f'<testcase name="self-check"><failure message="{attribute("; ".join(scene.get("errors", [])) or "failed")}"/></testcase>')
        for p in scene['probes']:
            name = attribute(p['name'])
            if p['status'] == 'fail':
                cases.append(f'<testcase name="{name}"><failure message="expected {attribute(p["want"])}, got {attribute(p["got"])}"/></testcase>')
            elif p['status'] in ('known', 'unverified'):
                why = f"known: {p['known']['ref']} {p['known']['reason']}" if p['status'] == 'known' else 'not verified'
                cases.append(f'<testcase name="{name}"><skipped message="{attribute(why)}"/></testcase>')
            else:
                cases.append(f'<testcase name="{name}"/>')
        row = by_name.get(scene['name'], {})
        if 'change' in row:
            if row['verdict'] == 'slower' and judged:
                cases.append(f'<testcase name="performance"><failure message="{attribute(change_text(row))} against the baseline '
                             f'({row["base_fps"]:.1f} to {row["fps"]:.1f} fps)"/></testcase>')
            elif not judged:
                cases.append('<testcase name="performance"><skipped message="unreliable machine state"/></testcase>')
            else:
                cases.append('<testcase name="performance"/>')
        fails, skips = sum('<failure' in c for c in cases), sum('<skipped' in c for c in cases)
        total, failed, skipped = total + len(cases), failed + fails, skipped + skips
        suites.append(f'<testsuite name="rpx-suite.{attribute(scene["name"])}" tests="{len(cases)}" failures="{fails}" '
                      f'skipped="{skips}">' + ''.join(cases) + '</testsuite>')
    return (f'<?xml version="1.0" encoding="UTF-8"?>\n<testsuites name="WEMU RPX suite" tests="{total}" failures="{failed}" '
            f'skipped="{skipped}">\n' + '\n'.join(suites) + '\n</testsuites>\n')


def newest_session_log():
    logs = sorted(SESSION_LOGS.glob('session-*.log'), key=lambda p: p.stat().st_mtime, reverse=True)
    for path in logs[:40]:
        try:
            if '[WCS] suite-begin' in path.read_text(errors='replace'):
                return path
        except OSError:
            continue
    sys.exit(f'no session log with a test sequence in {SESSION_LOGS} (open the suite in the GUI and press -)')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--core', type=Path, default=ROOT / 'build/core/wemu')
    parser.add_argument('--rpx', type=Path, default=HERE / 'build/game/code/wcs.rpx')
    parser.add_argument('--cpu', choices=('x64', 'interpreter', 'llvm'), default='x64')
    parser.add_argument('--scenes', help='comma-separated scene names (default: all)')
    parser.add_argument('--frames', type=int, help='frames per scene (default: the scene table, 600)')
    parser.add_argument('--warmup', type=int, default=60, help='frames excluded from timing at each scene start')
    parser.add_argument('--throttled', action='store_true', help='keep 60 Hz pacing (default: unthrottled)')
    parser.add_argument('--timeout', type=float, default=1800)
    parser.add_argument('--output', type=Path, help='result directory (default: results/<timestamp>)')
    parser.add_argument('--baseline', default='auto', metavar='PATH|none',
                        help='baseline to compare against (default: baselines/<host>-<cpu>.json when it exists)')
    parser.add_argument('--save-baseline', nargs='?', const='auto', metavar='PATH',
                        help='write this run as the baseline (default path: baselines/<host>-<cpu>.json)')
    parser.add_argument('--force', action='store_true', help='save a baseline even when the machine state is unreliable')
    parser.add_argument('--repeat', type=int, default=3, help='suite runs; the fastest per scene counts (default 3)')
    parser.add_argument('--tolerance', type=float, default=0.10, help='fps change treated as noise (default 0.10)')
    parser.add_argument('--capture-every', type=int, default=0, metavar='N', help='save every Nth TV frame as PNG')
    parser.add_argument('--env', action='append', default=[], metavar='WEMU_NAME=VALUE')
    parser.add_argument('--analyze-log', type=Path, metavar='SESSION_LOG',
                        help='report on a GUI session log (viewer "-" sequence) instead of running the core')
    parser.add_argument('--analyze-last', action='store_true', help='--analyze-log on the newest GUI session with a sequence')
    parser.add_argument('--performance-cores', action='store_true',
                        help='pin the core to the performance cores of a hybrid CPU')
    parser.add_argument('--quiet', action='store_true')
    args = parser.parse_args()
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    if args.analyze_last:
        args.analyze_log = newest_session_log()
        print(f'Analysing {args.analyze_log}')
    if args.analyze_log:
        output = (args.output or HERE / 'results' / f'session-{stamp}').resolve()
        output.mkdir(parents=True, exist_ok=True)
        return run_suite(args, output, stamp)
    args.core, args.rpx = args.core.resolve(), args.rpx.resolve()
    if not args.core.is_file():
        parser.error(f'core not found: {args.core}')
    if not args.rpx.is_file():
        parser.error(f'RPX not found: {args.rpx} (run build.sh)')
    if args.save_baseline == 'auto' and (args.scenes or args.frames):
        parser.error('--save-baseline with --scenes/--frames needs an explicit PATH (the default baseline is the full suite)')

    output = (args.output or HERE / 'results' / stamp).resolve()
    output.mkdir(parents=True, exist_ok=True)

    # The RPX reads /vol/content/suite.cfg; <game>/content is next to <game>/code.
    # mode=bench selects the timed suite; without it (GUI launch) the RPX opens
    # its interactive viewer, so the previous file is restored afterwards.
    config = ['mode=bench']
    if args.scenes:
        config.append(f'scenes={args.scenes}')
    if args.frames:
        config.append(f'frames={args.frames}')
    content = args.rpx.parent.parent / 'content'
    content.mkdir(exist_ok=True)
    config_path = content / 'suite.cfg'
    previous = config_path.read_text() if config_path.exists() else None
    config_path.write_text(''.join(line + '\n' for line in config))
    try:
        return run_suite(args, output, stamp)
    finally:
        if previous is None:
            config_path.unlink(missing_ok=True)
        else:
            config_path.write_text(previous)


def analyze_session(args, expectations):
    """Scenes from a GUI session log (the viewer's - sequence), timed by WEMU_FRAME_PROFILE."""
    text = args.analyze_log.read_text(errors='replace')
    start = text.rfind('[WCS] suite-begin')  # the most recent sequence in the session
    if start < 0:
        sys.exit(f'{args.analyze_log}: no "[WCS] suite-begin" marker (run the - sequence in the viewer)')
    suite, scenes = parse_log(text[text.rfind('\n', 0, start) + 1:], args.warmup)
    suite.setdefault('gpu', next((line.split('device=', 1)[1].strip() for line in text.splitlines()
                                  if '[NATIVE_VULKAN] device=' in line), None))
    check_probes(scenes, {}, expectations, verify=False)
    status = dict(source=str(args.analyze_log), complete='suite-end' in suite, timed_out=False, error=None,
                  profiled=any(s.get('fps') for s in scenes))
    return [scenes], [status], suite


def run_suite(args, output, stamp):
    expectations = load_expectations()
    environment = environment_info()
    session = bool(args.analyze_log)
    runs, statuses = [], []
    suite = {}
    if session:
        runs, statuses, suite = analyze_session(args, expectations)
    for index in range(0 if session else args.repeat):
        directory = output / f'run{index + 1}' if args.repeat > 1 else output
        directory.mkdir(parents=True, exist_ok=True)
        print(f'Run {index + 1}/{args.repeat}: {args.rpx.name} on {args.core} ({args.cpu}) -> {directory}', flush=True)
        run = run_core(args, directory)
        suite, scenes = parse_log((directory / 'core.log').read_text(errors='replace'), args.warmup)
        check_probes(scenes, run['pixels'], expectations)
        attach_memory(scenes, run['memory'])
        runs.append(scenes)
        statuses.append(dict(frames_delivered=len(run['frames']), host_seconds=round(run['host_seconds'], 1),
                             exit_code=run['exit_code'], error=run['error'], timed_out=run['timed_out'],
                             complete='suite-end' in suite and not run['timed_out']))
    if not session:
        # The machine can change state during a long run (unplugged, a build starting).
        environment['unreliable'] = sorted(set(environment['unreliable']) | set(environment_info()['unreliable']))
    scenes = merge_runs(runs)
    paced = args.throttled or session

    meta = dict(date=stamp, revision=git_revision(), cpu=args.cpu if not session else 'gui session', host=platform.node(),
                platform=platform.platform(), warmup=args.warmup, throttled=paced, repeat=1 if session else args.repeat,
                runs=statuses, suite=suite, gpu=suite.get('gpu'), environment=environment,
                # A session log says nothing about the machine state when it was recorded.
                reliable=session or not environment['unreliable'], config=dict(scenes=args.scenes, frames=args.frames))
    if session:
        meta['profiled'] = statuses[0]['profiled']
    result = dict(meta=meta, scenes=scenes)
    for scene in scenes:
        scene['warnings'] = warnings_for(scene, paced)
        scene['throughput'] = None if paced else throughput(scene)

    default_baseline = HERE / 'baselines' / f'{platform.node()}-{args.cpu}.json'
    baseline, baseline_note = {}, None
    if not session and args.baseline != 'none':
        path = default_baseline if args.baseline == 'auto' else Path(args.baseline)
        if path.is_file():
            baseline = json.loads(path.read_text())
            meta['baseline'] = dict(path=str(path), date=baseline.get('meta', {}).get('date'),
                                    revision=baseline.get('meta', {}).get('revision'))
        elif args.baseline == 'auto':
            baseline_note = 'no baseline for this machine yet: record one with  run.py --save-baseline'
        else:
            sys.exit(f'baseline not found: {path}')
    rows, perf = compare(scenes, baseline, args.tolerance)
    attach_trend(scenes, history(output, meta, [s['name'] for s in scenes]))
    result['summary'] = summarize(result, perf, statuses, expectations, baseline_note)

    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    (output / 'summary.md').write_text(markdown(result, rows))
    (output / 'summary.html').write_text(report_html.render(result, rows))
    (output / 'junit.xml').write_text(junit(result, rows))
    print('\n' + console(result, rows))
    print(f"\nReport: {output / 'summary.html'}\n        {output / 'summary.md'}, result.json, junit.xml")
    if args.save_baseline:
        target = default_baseline if args.save_baseline == 'auto' else Path(args.save_baseline)
        if result['summary']['failures']:
            print(f'Baseline NOT saved: the run has new failures.')
        elif not meta['reliable'] and not args.force:
            print(f"Baseline NOT saved: {', '.join(environment['unreliable'])} (plug in, close heavy programs, or use --force).")
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(json.dumps(result, indent=2) + '\n')
            print(f'Baseline saved: {target}')
    return 0 if result['summary']['verdict'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
