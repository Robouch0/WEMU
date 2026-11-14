"""Self-contained HTML summary for run.py results (inline SVG, no dependencies).

The page answers one question first: did anything regress? A verdict banner and
four headline figures lead; one table then gives every scene's status, speed
against the baseline and trend over earlier runs. Charts and full tables follow.

Colours and mark specs follow the data-viz reference palette: one blue series,
reserved status colours that always come with an icon and a label, recessive
grid lines, light and dark surfaces.
"""

import html
import json

# kind (colour), icon, label
STATUS = {'PASS': ('good', '✓', 'Pass'), 'FAIL': ('critical', '✕', 'Fail'), 'WARN': ('warning', '!', 'Warning'),
          'UNVERIFIED': ('neutral', '–', 'Not verified'), 'SLOWER': ('critical', '▼', 'Slower'),
          'pass': ('good', '✓', 'Pass'), 'known': ('neutral', '≈', 'Known failure'), 'fail': ('critical', '✕', 'New failure'),
          'fixed': ('good', '★', 'Fixed'), 'unverified': ('neutral', '–', 'Not verified'),
          'slower': ('critical', '▼', 'Slower'), 'faster': ('good', '▲', 'Faster')}

CSS = """
.viz-root { color-scheme: light; --surface-1:#fcfcfb; --surface-2:#f3f2ef; --text-primary:#0b0b0b;
  --text-secondary:#52514e; --text-muted:#7a7974; --grid:#e4e3df; --series-1:#2a78d6; --series-1-soft:#86b6ef;
  --reference:#7a7974; --good:#0ca30c; --warning:#fab219; --critical:#d03b3b; }
@media (prefers-color-scheme: dark) { :root:where(:not([data-theme="light"])) .viz-root { color-scheme: dark;
  --surface-1:#1a1a19; --surface-2:#242422; --text-primary:#f0efec; --text-secondary:#c3c2b7; --text-muted:#8f8e86;
  --grid:#3a3a37; --series-1:#3987e5; --series-1-soft:#1c5cab; --reference:#8f8e86; } }
body { margin:0; background:var(--surface-1); }
.viz-root { font:14px/1.45 system-ui, sans-serif; color:var(--text-primary); background:var(--surface-1);
  max-width:1180px; margin:0 auto; padding:28px 24px 60px; }
h1 { font-size:22px; margin:0 0 4px; } h2 { font-size:16px; margin:36px 0 10px; }
.meta, .caption { color:var(--text-secondary); font-size:13px; }
.banner { display:flex; align-items:center; gap:16px; background:var(--surface-2); border-radius:8px; padding:16px 18px;
  margin-top:18px; border-left:6px solid var(--text-muted); }
.banner.good { border-left-color:var(--good); } .banner.critical { border-left-color:var(--critical); }
.banner .mark { width:40px; height:40px; border-radius:20px; color:#fff; font-size:22px; display:flex; align-items:center;
  justify-content:center; flex:none; }
.banner.good .mark { background:var(--good); } .banner.critical .mark { background:var(--critical); }
.banner .word { font-size:22px; font-weight:700; } .banner .line { color:var(--text-secondary); }
ul.caveats { margin:10px 0 0; padding:0; list-style:none; }
ul.caveats li { margin-top:6px; color:var(--text-secondary); font-size:13px; }
.tiles { display:grid; grid-template-columns:repeat(auto-fit,minmax(220px,1fr)); gap:12px; margin-top:14px; }
.tile { background:var(--surface-2); border-radius:8px; padding:14px 16px; }
.tile .label { color:var(--text-secondary); font-size:12px; text-transform:uppercase; letter-spacing:.04em; }
.tile .value { font-size:28px; font-weight:600; margin-top:2px; }
.tile .unit { font-size:14px; font-weight:400; color:var(--text-secondary); margin-left:4px; }
.tile .sub { color:var(--text-secondary); font-size:12px; margin-top:2px; }
.badge { display:inline-flex; align-items:center; gap:4px; font-size:12px; font-weight:600; padding:1px 8px;
  border-radius:10px; color:var(--text-primary); background:var(--surface-2); white-space:nowrap; }
td .badge { background:var(--surface-1); box-shadow:0 0 0 1px var(--grid); }
.badge .dot { width:14px; height:14px; border-radius:7px; color:#fff; display:inline-flex; align-items:center;
  justify-content:center; font-size:10px; }
.good .dot { background:var(--good); } .critical .dot { background:var(--critical); }
.warning .dot { background:var(--warning); color:#0b0b0b; }
.neutral .dot { background:var(--text-muted); }
table { border-collapse:collapse; width:100%; font-size:13px; }
th { text-align:left; color:var(--text-secondary); font-weight:600; border-bottom:1px solid var(--grid); padding:6px 8px; }
td { border-bottom:1px solid var(--grid); padding:6px 8px; vertical-align:middle; }
td.num, th.num { text-align:right; font-variant-numeric:tabular-nums; white-space:nowrap; }
td.name { font-weight:600; }
.grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(360px,1fr)); gap:16px; }
.card { background:var(--surface-2); border-radius:8px; padding:12px 14px; }
.card h3 { font-size:14px; margin:0; display:flex; justify-content:space-between; gap:8px; }
.card .figures { color:var(--text-secondary); font-size:12px; margin:2px 0 6px; }
svg text { fill:var(--text-secondary); font-size:11px; }
svg.spark { display:block; }
.swatch { display:inline-block; width:14px; height:14px; border-radius:3px; vertical-align:-2px;
  box-shadow:0 0 0 1px var(--grid); margin-right:4px; }
.legend { display:flex; gap:16px; color:var(--text-secondary); font-size:12px; margin:4px 0 8px; flex-wrap:wrap; }
.legend i { display:inline-block; width:12px; height:12px; border-radius:2px; margin-right:5px; vertical-align:-1px; }
#tip { position:fixed; pointer-events:none; background:var(--text-primary); color:var(--surface-1); font-size:12px;
  padding:4px 8px; border-radius:4px; display:none; white-space:nowrap; z-index:5; }
ul.notes { margin:6px 0 0; padding-left:18px; color:var(--text-secondary); }
ul.notes.strong li { color:var(--text-primary); }
details { margin-top:28px; } summary { cursor:pointer; font-size:16px; font-weight:600; }
dl.machine { display:grid; grid-template-columns:max-content 1fr; gap:4px 16px; font-size:13px; margin:8px 0 0; }
dl.machine dt { color:var(--text-secondary); } dl.machine dd { margin:0; }
"""

SCRIPT = """
const tip = document.getElementById('tip');
function show(e, text) { tip.textContent = text; tip.style.display = 'block';
  const x = Math.min(e.clientX + 12, window.innerWidth - tip.offsetWidth - 8);
  tip.style.left = x + 'px'; tip.style.top = (e.clientY - 28) + 'px'; }
document.querySelectorAll('[data-tip]').forEach(el => {
  el.addEventListener('mousemove', e => show(e, el.dataset.tip));
  el.addEventListener('mouseleave', () => tip.style.display = 'none'); });
document.querySelectorAll('svg.frames').forEach(svg => {
  const data = JSON.parse(svg.dataset.series), x0 = +svg.dataset.x0, w = +svg.dataset.w, step = +svg.dataset.step;
  const cross = svg.querySelector('.cross'), dot = svg.querySelector('.hover-dot');
  svg.addEventListener('mousemove', e => {
    const box = svg.getBoundingClientRect(), x = (e.clientX - box.left) * svg.viewBox.baseVal.width / box.width;
    const i = Math.max(0, Math.min(data.length - 1, Math.round((x - x0) / w * (data.length - 1))));
    const px = x0 + i / Math.max(1, data.length - 1) * w, py = +svg.dataset.y0 - data[i] * +svg.dataset.scale;
    cross.setAttribute('x1', px); cross.setAttribute('x2', px); cross.style.display = 'block';
    dot.setAttribute('cx', px); dot.setAttribute('cy', Math.max(+svg.dataset.top, py)); dot.style.display = 'block';
    show(e, 'frames ' + (i * step) + '–' + ((i + 1) * step - 1) + ': ' + data[i].toFixed(2) + ' ms' + (step > 1 ? ' (max)' : ''));
  });
  svg.addEventListener('mouseleave', () => { cross.style.display = dot.style.display = 'none'; tip.style.display = 'none'; });
});
"""


def esc(value):
    return html.escape(str(value))


def badge(status, text=None):
    kind, icon, label = STATUS[status]
    return f'<span class="badge {kind}"><span class="dot">{icon}</span>{esc(text or label)}</span>'


def rgb(text):
    return f'rgb({esc(text)})' if text and text not in ('missing', 'n/a') else 'transparent'


def swatch(text):
    return f'<span class="swatch" style="background:{rgb(text)}"></span>{esc(text)}'


def sparkline(scene, base_fps):
    """fps of earlier comparable runs, then this run (the marked point); dashed line = baseline."""
    trend = scene.get('trend') or []
    if not scene.get('fps'):
        return ''
    if not trend:
        return '<span class="caption">first run</span>'
    values = [p['fps'] for p in trend] + [scene['fps']]
    tips = [f"{p['date']} · {p['revision']}: {p['fps']:.1f} fps" for p in trend] + [f"this run: {scene['fps']:.1f} fps"]
    width, height, pad = 132, 28, 6
    low, high = min(values + ([base_fps] if base_fps else [])), max(values + ([base_fps] if base_fps else []))
    span = (high - low) or 1.0

    def x(i):
        return pad + i / (len(values) - 1) * (width - 2 * pad)

    def y(value):
        return height - pad - (value - low) / span * (height - 2 * pad)

    parts = [f'<svg class="spark" viewBox="0 0 {width} {height}" width="{width}" height="{height}" role="img" '
             f'aria-label="fps of {esc(scene["name"])} over the last {len(values)} runs">']
    if base_fps:
        parts.append(f'<line x1="{pad}" x2="{width - pad}" y1="{y(base_fps):.1f}" y2="{y(base_fps):.1f}" '
                     f'stroke="var(--reference)" stroke-dasharray="3 3"/>')
    parts.append('<polyline points="' + ' '.join(f'{x(i):.1f},{y(v):.1f}' for i, v in enumerate(values)) +
                 '" fill="none" stroke="var(--series-1)" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>')
    parts.append(f'<circle cx="{x(len(values) - 1):.1f}" cy="{y(values[-1]):.1f}" r="4" fill="var(--series-1)" '
                 f'stroke="var(--surface-1)" stroke-width="2"/>')
    parts += [f'<circle cx="{x(i):.1f}" cy="{y(v):.1f}" r="7" fill="transparent" data-tip="{esc(tips[i])}"/>'
              for i, v in enumerate(values)]
    return ''.join(parts) + '</svg>'


def fps_chart(scenes, rows):
    """Horizontal bars: best-run median fps, a dot for the 1% low, a tick for the baseline."""
    base = {row['name']: row.get('base_fps') for row in rows}
    timed = [s for s in scenes if s.get('fps')]
    if not timed:
        return ''
    top = max(max(s['fps'] for s in timed), max((b for b in base.values() if b), default=0)) * 1.12
    label_w, bar_h, gap, width = 120, 16, 10, 1000
    plot_w = width - label_w - 70
    height = len(timed) * (bar_h + gap) + 30
    parts = [f'<svg viewBox="0 0 {width} {height}" width="100%" role="img" aria-label="Frames per second by scene">']
    for tick in range(0, int(top) + 1, max(50, int(top / 6) // 50 * 50 or 50)):
        x = label_w + tick / top * plot_w
        parts.append(f'<line x1="{x:.1f}" x2="{x:.1f}" y1="0" y2="{height - 22}" stroke="var(--grid)"/>'
                     f'<text x="{x:.1f}" y="{height - 6}" text-anchor="middle">{tick}</text>')
    for i, scene in enumerate(timed):
        y = i * (bar_h + gap)
        w = max(4.0, scene['fps'] / top * plot_w)
        low = scene.get('stability', {}).get('low_1pct_fps')
        tip = f"{scene['name']}: {scene['fps']:.1f} fps median" + (f", 1% low {low:.1f}" if low else '')
        if base.get(scene['name']):
            tip += f", baseline {base[scene['name']]:.1f}"
        parts.append(f'<text x="{label_w - 8}" y="{y + bar_h - 3}" text-anchor="end" style="fill:var(--text-primary)">'
                     f'{esc(scene["name"])}</text>')
        parts.append(f'<path d="M{label_w},{y} h{w - 4:.1f} a4,4 0 0 1 4,4 v{bar_h - 8} a4,4 0 0 1 -4,4 h-{w - 4:.1f} z" '
                     f'fill="var(--series-1)" data-tip="{esc(tip)}"/>')
        if low:
            parts.append(f'<circle cx="{label_w + low / top * plot_w:.1f}" cy="{y + bar_h / 2}" r="4.5" fill="var(--surface-1)" '
                         f'stroke="var(--text-primary)" stroke-width="2" data-tip="{esc(tip)}"/>')
        if base.get(scene['name']):
            bx = label_w + base[scene['name']] / top * plot_w
            parts.append(f'<line x1="{bx:.1f}" x2="{bx:.1f}" y1="{y - 3}" y2="{y + bar_h + 3}" stroke="var(--reference)" '
                         f'stroke-width="3" data-tip="{esc(tip)}"/>')
        parts.append(f'<text x="{label_w + w + 8:.1f}" y="{y + bar_h - 3}">{scene["fps"]:.0f}</text>')
    parts.append('</svg>')
    legend = ('<div class="legend"><span><i style="background:var(--series-1)"></i>median fps (best run)</span>'
              '<span><i style="background:var(--surface-1);border:2px solid var(--text-primary);border-radius:7px"></i>'
              '1% low fps</span>' +
              ('<span><i style="background:var(--reference);width:4px"></i>baseline</span>' if any(base.values()) else '') +
              '</div>')
    return legend + ''.join(parts)


def frame_chart(scene, y_max, max_points=360):
    series = scene.get('frame_series_ms') or []
    if not series:
        return '<p class="caption">no timed frames</p>'
    # Keep spikes visible: buckets of `step` frames show their slowest frame.
    step = max(1, -(-len(series) // max_points))
    points = [max(series[i:i + step]) for i in range(0, len(series), step)]
    width, height, x0, top, bottom = 360, 140, 34, 8, 20
    w, y0 = width - x0 - 8, height - bottom
    scale = (y0 - top) / y_max
    coords = ' '.join(f'{x0 + i / max(1, len(points) - 1) * w:.1f},{max(top, y0 - v * scale):.1f}'
                      for i, v in enumerate(points))
    parts = [f'<svg class="frames" viewBox="0 0 {width} {height}" width="100%" role="img" '
             f'aria-label="Frame time for {esc(scene["name"])}" data-series=\'{json.dumps(points)}\' data-x0="{x0}" '
             f'data-w="{w}" data-y0="{y0}" data-top="{top}" data-scale="{scale:.4f}" data-step="{step}">']
    for value in range(0, int(y_max) + 1, 10 if y_max > 30 else 5):
        y = y0 - value * scale
        parts.append(f'<line x1="{x0}" x2="{x0 + w}" y1="{y:.1f}" y2="{y:.1f}" stroke="var(--grid)"/>'
                     f'<text x="{x0 - 4}" y="{y + 4:.1f}" text-anchor="end">{value}</text>')
    for ms, label in ((1000 / 60, '60 fps'), (1000 / 30, '30 fps')):
        if ms < y_max:
            y = y0 - ms * scale
            parts.append(f'<line x1="{x0}" x2="{x0 + w}" y1="{y:.1f}" y2="{y:.1f}" stroke="var(--reference)" '
                         f'stroke-dasharray="4 3"/><text x="{x0 + w}" y="{y - 3:.1f}" text-anchor="end">{label}</text>')
    parts.append(f'<polyline points="{coords}" fill="none" stroke="var(--series-1)" stroke-width="2" '
                 f'stroke-linejoin="round"/>')
    parts.append(f'<line class="cross" y1="{top}" y2="{y0}" stroke="var(--text-muted)" style="display:none"/>'
                 f'<circle class="hover-dot" r="4" fill="var(--series-1)" stroke="var(--surface-2)" stroke-width="2" '
                 f'style="display:none"/>')
    parts.append(f'<text x="{x0}" y="{height - 4}">start</text>'
                 f'<text x="{x0 + w}" y="{height - 4}" text-anchor="end">{len(series)} timed frames · ms</text></svg>')
    return ''.join(parts)


def bullet_list(title, items, strong=False):
    if not items:
        return ''
    return (f'<h2>{esc(title)}</h2><ul class="notes{" strong" if strong else ""}">' +
            ''.join(f'<li>{esc(item)}</li>' for item in items) + '</ul>')


def render(result, rows, max_points=360):
    meta, scenes, summary = result['meta'], result['scenes'], result['summary']
    paced, env, count = meta['throttled'], meta['environment'], summary['probes']
    by_name = {row['name']: row for row in rows}
    probes = [(s['name'], p) for s in scenes for p in s.get('probes', [])]
    p99s = [s['frame_ms']['p99'] for s in scenes if s.get('frame_ms')]
    y_max = max(20.0, min(60.0, (max(p99s) if p99s else 20) * 1.4))

    out = ['<!doctype html><html lang="en"><head><meta charset="utf-8">',
           f'<title>WEMU RPX suite {esc(meta["date"])}: {esc(summary["verdict"])}</title><style>{CSS}</style></head>'
           '<body><div class="viz-root">',
           f'<h1>WEMU RPX test suite</h1><div class="meta">{esc(meta["date"])} · core <code>{esc(meta["revision"])}</code> · '
           f'cpu {esc(meta["cpu"])} · {esc(meta["host"])} · best of {meta["repeat"]} run(s) · '
           f'warmup {meta["warmup"]} frames excluded</div>']

    # Verdict
    kind, icon, _ = STATUS[summary['verdict']]
    out.append(f'<div class="banner {kind}"><div class="mark">{icon}</div><div><div class="word">{esc(summary["verdict"])}</div>'
               f'<div class="line">{esc(summary["headline"][0].upper() + summary["headline"][1:])}.</div></div></div>')
    if summary['caveats']:
        out.append('<ul class="caveats">' + ''.join(f'<li>{badge("WARN", "Note")} {esc(c)}</li>' for c in summary['caveats']) +
                   '</ul>')

    # Headline figures
    def tile(label, value, unit, sub):
        out.append(f'<div class="tile"><div class="label">{esc(label)}</div><div class="value">{esc(value)}'
                   f'<span class="unit">{esc(unit)}</span></div><div class="sub">{esc(sub)}</div></div>')

    out.append('<div class="tiles">')
    if count['unverified'] == summary['probe_total'] and probes:
        tile('Checks', f"{summary['scenes']['pass'] + summary['scenes']['known']} / {summary['scenes']['total']}",
             'scenes pass self-checks', f"{count['unverified']} probes not verified")
    else:
        tile('Checks', f"{count['pass'] + count['fixed']} / {summary['probe_total']}", 'probes pass',
             f"{count['known']} known · {count['fail']} new · {count['fixed']} fixed")
    if summary['index'] is not None:
        over = f"{summary['steady']} steady scenes" if summary['steady'] else f"{summary['compared']} scenes"
        tile('Speed against the baseline', f"{summary['index']:.0%}", '',
             f"geometric mean of {over} · {len(summary['slower']) + len(summary['slower_unjudged'])} slower · "
             f"{len(summary['faster'])} faster" + (f" · {len(summary['noisy'])} too noisy" if summary['noisy'] else ''))
    else:
        tile('Speed against the baseline', '—', '', 'not compared')
    if summary['lowest_1pct']:
        tile('Stutters', summary['stutters'], '',
             f"lowest 1% low: {summary['lowest_1pct']['fps']:.0f} fps ({summary['lowest_1pct']['scene']})" +
             (f" · up to {summary['missed_60hz_pct']:.1f}% missed 60 Hz" if paced else ''))
    else:
        tile('Stutters', '—', '', 'not timed')
    if summary['peak_memory_mib']:
        tile('Peak memory', f"{summary['peak_memory_mib']:.0f}", 'MiB',
             f"largest growth in a scene: {summary['largest_growth']['mib']:+.0f} MiB ({summary['largest_growth']['scene']})")
    out.append('</div>')

    out.append(bullet_list('New failures', summary['failures'], strong=True))
    out.append(bullet_list('Slower than the baseline', summary['slower'], strong=True))
    out.append(bullet_list('Slower, but not judged (unreliable machine state)', summary['slower_unjudged']))
    out.append(bullet_list('Fixed: remove from expectations.json', summary['fixed'], strong=True))

    # The regression table
    baseline = meta.get('baseline')
    caption = 'One row per scene. ' + (
        f'Baseline: {esc(baseline["date"])}, core {esc(baseline["revision"])}; a change counts when it is beyond ±{summary["tolerance"]:.0%} and the two sets of runs do not overlap; otherwise it is noise. '
        if baseline else '')
    caption += ('Trend: fps of earlier comparable runs on this machine, ending at this run (marked); '
                'the dashed line is the baseline.')
    repeated = meta['repeat'] > 1
    head = ['<th>scene</th><th>status</th><th class="num">fps</th><th class="num">vs baseline</th>' +
            ('<th class="num">run spread</th>' if repeated else '') +
            '<th class="num">1% low</th><th class="num">p99 ms</th><th class="num">stutters</th>']
    head.append('<th class="num">missed 60 Hz</th>' if paced else '')
    head.append('<th class="num">memory</th>')
    head.append('' if paced else '<th class="num">throughput</th>')
    head.append('<th>trend</th>')
    out.append(f'<h2>Scenes</h2><p class="caption">{caption}</p><table><tr>{"".join(head)}</tr>')
    for scene in scenes:
        f, mem, row = scene.get('stability') or {}, scene.get('memory_mib'), by_name.get(scene['name'], {})
        ms, rate = scene.get('frame_ms'), scene.get('throughput')
        if row.get('verdict') in ('slower', 'faster'):
            change = badge(row['verdict'], f"{row['change']:+.1%}")
        elif row.get('verdict') == 'noise':
            change = (f'<span data-tip="Inside the run-to-run spread of the baseline: not judged">{row["change"]:+.1%} '
                      f'<span class="caption">noise</span></span>')
        else:
            change = f"{row['change']:+.1%}" if 'change' in row else '—'
        cells = [f'<td class="name">{esc(scene["name"])}</td>', f'<td>{badge(scene["status"])}</td>',
                 f'<td class="num">{scene["fps"]:.1f}</td>' if scene.get('fps') else '<td class="num">—</td>',
                 f'<td class="num">{change}</td>'] + (
                     [f'<td class="num">{scene["spread"]:.0%}</td>' if 'spread' in scene else '<td class="num">—</td>']
                     if repeated else []) + [
                 f'<td class="num">{f["low_1pct_fps"]:.1f}</td>' if f else '<td class="num">—</td>',
                 f'<td class="num">{ms["p99"]:.2f}</td>' if ms else '<td class="num">—</td>',
                 f'<td class="num">{f["stutters"]}</td>' if f else '<td class="num">—</td>']
        if paced:
            cells.append(f'<td class="num">{f["over_16ms_pct"]:.1f}%</td>' if f else '<td class="num">—</td>')
        cells.append(f'<td class="num">{mem["growth"]:+.0f} MiB</td>' if mem else '<td class="num">—</td>')
        if not paced:
            cells.append(f'<td class="num" data-tip="{esc(str(rate["per_frame"]) + " " + rate["what"])}">'
                         f'{rate["value"]:.1f} {esc(rate["unit"])}</td>' if rate else '<td class="num">—</td>')
        cells.append(f'<td>{sparkline(scene, row.get("base_fps"))}</td>')
        out.append('<tr>' + ''.join(cells) + '</tr>')
    out.append('</table><p class="caption">fps = median frame time of the fastest run · ' +
               ('run spread = how far the slowest run was below the fastest · ' if repeated else '') + '1% low = fps of the slowest 1% of '
               'frames · p99 = 99th-percentile frame time · stutter = a frame over twice the median that also misses a 60 Hz '
               'refresh · memory = growth of the core\'s resident size during the scene' +
               ('' if paced else ' · throughput = the scene\'s workload per frame times its fps') + '.</p>')

    # Known failures
    known = [(name, p) for name, p in probes if p['status'] == 'known']
    readback = summary['guest_readback']
    if known or (readback['known'] and readback['verified'] and not readback['passing']):
        out.append('<h2>Known failures</h2><p class="caption">Listed in <code>expectations.json</code>: WEMU bugs the suite '
                   'already tracks. They do not fail the run; a probe that starts passing is reported as fixed.</p>')
    if known:
        out.append('<table><tr><th>probe</th><th>expected</th><th>got</th><th>bug</th><th>reason</th></tr>')
        out += [f'<tr><td>{esc(name)}/{esc(p["name"])}</td><td>{swatch(p["want"])}</td><td>{swatch(p["got"])}</td>'
                f'<td>{esc(p["known"]["ref"])}</td><td>{esc(p["known"]["reason"])}</td></tr>' for name, p in known]
        out.append('</table>')
    if readback['known'] and readback['verified'] and not readback['passing']:
        out.append(f'<p class="caption">Guest readback (the RPX reading its own frame through <code>GX2CopySurface</code>) '
                   f'fails on all {readback["verified"]} probes: {esc(readback["known"]["reason"])} '
                   f'({esc(readback["known"]["ref"])}).</p>')

    # Charts
    out.append('<h2>Frames per second</h2><p class="caption">' +
               ('Paced to the display: 60 fps is the target. ' if paced else 'Unthrottled throughput of each scene. ') +
               'The dot marks the 1% low: the frame rate of the slowest 1% of frames.</p>')
    out.append(fps_chart(scenes, rows))
    out.append('<h2>Frame time over the run</h2><p class="caption">Lower is better; flat is stable. Spikes are stutters. '
               'Long scenes are bucketed, each point showing the slowest frame of its bucket.</p><div class="grid">')
    for scene in scenes:
        f = scene.get('stability') or {}
        figures = (f"{scene.get('fps', 0):.0f} fps · 1% low {f.get('low_1pct_fps', 0):.0f} · jitter "
                   f"{f.get('jitter_ms', 0):.2f} ms · {f.get('stutters', 0)} stutters") if f else 'not timed'
        out.append(f'<div class="card"><h3>{esc(scene["name"])}<span>{badge(scene["status"])}'
                   f'{" " + badge("WARN", str(len(scene["warnings"])) + " warning(s)") if scene.get("warnings") else ""}'
                   f'</span></h3><div class="figures">{figures}</div>{frame_chart(scene, y_max, max_points)}</div>')
    out.append('</div>')

    warnings = [(s['name'], w) for s in scenes for w in s.get('warnings', [])]
    out.append(bullet_list('Warnings', [f'{name}: {w}' for name, w in warnings] + summary['perf_notes']))
    out.append(bullet_list('Faster than the baseline', summary['faster']))
    out.append(bullet_list('Too noisy to judge (runs of the scene overlap the baseline)', summary['noisy']))

    # Detail tables
    stages = [k for k in next((s['stage_ms'] for s in scenes if s.get('stage_ms')), {})]
    if stages:
        out.append('<details><summary>Frame times and where the time goes</summary><p class="caption">Milliseconds per frame. '
                   'guest and hle are the emulated CPU and the OS calls; wait and present are the GPU side.</p><table><tr>'
                   '<th>scene</th><th class="num">p50</th><th class="num">p95</th><th class="num">p99</th><th class="num">max</th>' +
                   ''.join(f'<th class="num">{esc(k)}</th>' for k in stages) + '</tr>')
        for scene in scenes:
            if scene.get('frame_ms'):
                ms = scene['frame_ms']
                out.append(f'<tr><td class="name">{esc(scene["name"])}</td>' +
                           ''.join(f'<td class="num">{ms[k]:.2f}</td>' for k in ('p50', 'p95', 'p99', 'max')) +
                           ''.join(f'<td class="num">{scene["stage_ms"].get(k, 0):.2f}</td>' for k in stages) + '</tr>')
        out.append('</table></details>')
    if probes:
        out.append('<details><summary>All probes</summary><p class="caption">Pixels predicted by the RPX on the CPU and '
                   'checked against the TV frame WEMU delivered.</p><table><tr><th>scene</th><th>probe</th><th>expected</th>'
                   '<th>got</th><th>status</th></tr>')
        out += [f'<tr><td>{esc(name)}</td><td>{esc(p["name"])}</td><td>{swatch(p["want"])}</td><td>{swatch(p["got"])}</td>'
                f'<td>{badge(p["status"])}</td></tr>' for name, p in probes]
        out.append('</table></details>')
    details = [(s['name'], d) for s in scenes for d in s.get('details', []) + s.get('errors', [])]
    if details:
        out.append('<details><summary>Workload details</summary><ul class="notes">' +
                   ''.join(f'<li><b>{esc(s)}</b>: {esc(d)}</li>' for s, d in details) + '</ul></details>')

    power = {'ac': 'AC power', 'battery': 'battery', 'unknown': 'unknown'}[env['power']]
    machine = [('CPU', f"{env['cpu']} ({env['threads']} threads)"), ('GPU', meta.get('gpu') or 'unknown'),
               ('Power', power + (f", profile {env['profile']}" if env['profile'] else '')),
               ('Load at start', env['load']), ('System', meta['platform'])]
    if baseline:
        machine.append(('Baseline', f"{baseline['date']}, core {baseline['revision']} ({baseline['path']})"))
    out.append('<details open><summary>Machine</summary><dl class="machine">' +
               ''.join(f'<dt>{esc(k)}</dt><dd>{esc(v)}</dd>' for k, v in machine) + '</dl>' +
               ('<p class="caption">Session logs do not record the machine state: these values are from the moment of the '
                'analysis.</p>' if meta['cpu'] == 'gui session' else '') + '</details>')
    out.append(f'</div><div id="tip"></div><script>{SCRIPT}</script></body></html>')
    return '\n'.join(part for part in out if part)
