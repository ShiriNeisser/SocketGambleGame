#!/usr/bin/env python3
"""Generate static SVG comparison charts for the README (no external deps).
Data reproduced verbatim from the validated benchmark artifact for this
conversation — see docs/architecture-comparison/README note in the PR."""
import os
import math

OUT_DIR = "docs/architecture-comparison"
os.makedirs(OUT_DIR, exist_ok=True)

ARCH = {
    "thread_per_client": {"label": "Thread-per-client", "color": "#2a78d6"},
    "epoll_only":         {"label": "Event-driven (epoll only)", "color": "#eb6834"},
    "epoll_thread_pool":  {"label": "Epoll + thread pool", "color": "#1baf7a"},
}
ORDER = ["thread_per_client", "epoll_only", "epoll_thread_pool"]

scaleData = [
    {"n":10,  "arch":"thread_per_client", "wall":65.02,  "rss":2180},
    {"n":10,  "arch":"epoll_only",        "wall":82.04,  "rss":2432},
    {"n":10,  "arch":"epoll_thread_pool", "wall":46.01,  "rss":2200},
    {"n":50,  "arch":"thread_per_client", "wall":145.02, "rss":2804},
    {"n":50,  "arch":"epoll_only",        "wall":162.04, "rss":2356},
    {"n":50,  "arch":"epoll_thread_pool", "wall":46.02,  "rss":2240},
    {"n":100, "arch":"thread_per_client", "wall":245.16, "rss":3480},
    {"n":100, "arch":"epoll_only",        "wall":260.18, "rss":2528},
    {"n":100, "arch":"epoll_thread_pool", "wall":45.02,  "rss":2348},
    {"n":256, "arch":"thread_per_client", "wall":260.21, "rss":5408},
    {"n":256, "arch":"epoll_only",        "wall":260.20, "rss":2328},
    {"n":256, "arch":"epoll_thread_pool", "wall":239.96, "rss":2520},
    {"n":512, "arch":"thread_per_client", "wall":260.24, "rss":8980},
    {"n":512, "arch":"epoll_only",        "wall":260.24, "rss":2604},
    {"n":512, "arch":"epoll_thread_pool", "wall":45.07,  "rss":2868},
]

burstData = [
    {"arch":"thread_per_client", "n":4,   "p95":0.170},
    {"arch":"thread_per_client", "n":8,   "p95":0.234},
    {"arch":"thread_per_client", "n":20,  "p95":0.217},
    {"arch":"thread_per_client", "n":50,  "p95":0.456},
    {"arch":"thread_per_client", "n":100, "p95":0.395},
    {"arch":"thread_per_client", "n":200, "p95":0.431},
    {"arch":"epoll_only", "n":4,   "p95":0.117},
    {"arch":"epoll_only", "n":8,   "p95":0.731},
    {"arch":"epoll_only", "n":20,  "p95":0.232},
    {"arch":"epoll_only", "n":50,  "p95":1.098},
    {"arch":"epoll_only", "n":100, "p95":0.819},
    {"arch":"epoll_only", "n":200, "p95":0.450},
    {"arch":"epoll_thread_pool", "n":4,   "p95":0.452},
    {"arch":"epoll_thread_pool", "n":8,   "p95":0.237},
    {"arch":"epoll_thread_pool", "n":20,  "p95":0.375},
    {"arch":"epoll_thread_pool", "n":50,  "p95":11.378},
    {"arch":"epoll_thread_pool", "n":100, "p95":3.248},
    {"arch":"epoll_thread_pool", "n":200, "p95":6.436},
]

W, H = 720, 380
PAD_L, PAD_R, PAD_T, PAD_B = 60, 24, 24, 50
PLOT_W = W - PAD_L - PAD_R
PLOT_H = H - PAD_T - PAD_B

SURFACE = "#fcfcfb"
GRID = "#e1e0d9"
BASELINE = "#c3c2b7"
TEXT_PRIMARY = "#0b0b0b"
TEXT_MUTED = "#898781"


def nice_ticks(max_val, count=5):
    if max_val <= 0:
        return [0, 1]
    step = max_val / (count - 1)
    mag = 10 ** math.floor(math.log10(step))
    norm = step / mag
    nice = 1 if norm < 1.5 else 2 if norm < 3 else 5 if norm < 7 else 10
    step = nice * mag
    ticks = []
    v = 0
    while v <= max_val * 1.12:
        ticks.append(round(v, 3))
        v += step
    return ticks


def x_pos(v, x0, x1, log=True):
    vv = math.log10(v) if log else v
    v0 = math.log10(x0) if log else x0
    v1 = math.log10(x1) if log else x1
    return PAD_L + (vv - v0) / (v1 - v0) * PLOT_W


def y_pos(v, y_top):
    return PAD_T + PLOT_H - (v / y_top) * PLOT_H


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def make_chart(filename, title, y_label, x_label, x_ticks, data, y_key, y_fmt):
    y_max = max(d[y_key] for d in data)
    y_ticks = nice_ticks(y_max)
    y_top = y_ticks[-1] if y_ticks[-1] > 0 else 1

    parts = []
    parts.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" '
                  f'font-family="Segoe UI, Arial, sans-serif">')
    parts.append(f'<rect x="0" y="0" width="{W}" height="{H}" fill="{SURFACE}"/>')
    parts.append(f'<text x="{PAD_L}" y="18" font-size="15" font-weight="600" fill="{TEXT_PRIMARY}">{esc(title)}</text>')

    # gridlines + y ticks
    for t in y_ticks:
        y = y_pos(t, y_top)
        stroke = BASELINE if t == 0 else GRID
        parts.append(f'<line x1="{PAD_L}" x2="{W-PAD_R}" y1="{y:.1f}" y2="{y:.1f}" stroke="{stroke}" stroke-width="1"/>')
        parts.append(f'<text x="{PAD_L-8}" y="{y+4:.1f}" font-size="11" fill="{TEXT_MUTED}" text-anchor="end">{y_fmt(t)}</text>')

    # x ticks
    for t in x_ticks:
        x = x_pos(t, x_ticks[0], x_ticks[-1])
        parts.append(f'<text x="{x:.1f}" y="{H-PAD_B+18}" font-size="11" fill="{TEXT_MUTED}" text-anchor="middle">{t}</text>')

    parts.append(f'<text x="{PAD_L+PLOT_W/2:.1f}" y="{H-8}" font-size="11" fill="{TEXT_MUTED}" text-anchor="middle">{esc(x_label)}</text>')
    parts.append(f'<text x="10" y="{PAD_T+8}" font-size="11" fill="{TEXT_MUTED}">{esc(y_label)}</text>')

    # series lines + dots
    for arch in ORDER:
        pts = sorted([d for d in data if d["arch"] == arch], key=lambda d: d["n"])
        if not pts:
            continue
        color = ARCH[arch]["color"]
        path = []
        for i, p in enumerate(pts):
            x = x_pos(p["n"], x_ticks[0], x_ticks[-1])
            y = y_pos(p[y_key], y_top)
            path.append(f'{"M" if i==0 else "L"}{x:.1f},{y:.1f}')
        parts.append(f'<path d="{" ".join(path)}" fill="none" stroke="{color}" stroke-width="2.5" '
                      f'stroke-linejoin="round" stroke-linecap="round"/>')
        for p in pts:
            x = x_pos(p["n"], x_ticks[0], x_ticks[-1])
            y = y_pos(p[y_key], y_top)
            parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.5" fill="{color}" stroke="{SURFACE}" stroke-width="2"/>')

    # legend
    lx = PAD_L
    ly = H - 12
    for arch in ORDER:
        color = ARCH[arch]["color"]
        parts.append(f'<rect x="{lx}" y="{ly-16}" width="14" height="3" rx="1.5" fill="{color}"/>')
        parts.append(f'<text x="{lx+20}" y="{ly-13}" font-size="11" fill="{TEXT_PRIMARY}">{esc(ARCH[arch]["label"])}</text>')
        lx += 20 + len(ARCH[arch]["label"]) * 6.2 + 24

    parts.append('</svg>')
    path_out = os.path.join(OUT_DIR, filename)
    with open(path_out, "w") as f:
        f.write("\n".join(parts))
    print("wrote", path_out)


make_chart(
    "wall_time_vs_concurrency.svg",
    "Time to finish a full match",
    "seconds", "concurrent clients",
    [10, 50, 100, 256, 512], scaleData, "wall",
    lambda v: f"{v:g}s"
)

make_chart(
    "peak_memory_vs_concurrency.svg",
    "Peak memory (RSS)",
    "KB", "concurrent clients",
    [10, 50, 100, 256, 512], scaleData, "rss",
    lambda v: f"{v:g}"
)

make_chart(
    "burst_auth_p95_latency.svg",
    "AUTH round-trip p95 latency under a connection burst",
    "ms (p95)", "concurrent clients (burst)",
    [4, 8, 20, 50, 100, 200], burstData, "p95",
    lambda v: f"{v:g}"
)
