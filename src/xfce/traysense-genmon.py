#!/usr/bin/env python3
import math
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from xml.sax.saxutils import escape

ICON_DIR = Path(__file__).resolve().parent / "icons"
GEOMETRY = {
    "cpu": (2.3, 2.3, 11.4, 11.4),
    "ram": (1.1, 1.6, 13.8, 10.8),
    "gpu": (2.85, 1.6, 12.1, 10.8),
}
FG = "#ffffff"
VRAM = "#76d75a"

def find_binary():
    candidates = []
    env = os.environ.get("TRAYSENSE_BIN")
    if env:
        candidates.append(Path(env))
    found = shutil.which("TraySense")
    if found:
        candidates.append(Path(found))
    candidates.append(Path(__file__).resolve().parents[2] / "build" / "TraySense")
    candidates.append(Path.home() / ".local" / "bin" / "TraySense")

    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    return None

def read_metrics(binary):
    try:
        result = subprocess.run([str(binary), "--once"], capture_output=True,
                                text=True, timeout=3)
    except Exception:
        return None

    if result.returncode != 0:
        return None

    values = {}
    for line in result.stdout.splitlines():
        if ": " not in line:
            continue
        key, value = line.split(": ", 1)
        match = re.match(r"-?\d+(?:\.\d+)?", value.strip())
        values[key] = float(match.group()) if match else -1.0
    return values

def asset(name):
    path = ICON_DIR / (name + ".svg")
    try:
        text = path.read_text()
    except OSError:
        return ""
    text = re.sub(r"^.*?<svg[^>]*>", "", text, flags=re.S)
    return text.rsplit("</svg>", 1)[0]

def text_width(text):
    return len(text) * 7.2

def heat_index(temperature):
    if temperature < 55:
        return 0
    if temperature < 70:
        return 1
    if temperature < 80:
        return 2
    if temperature < 90:
        return 3
    return 4

def temp_color(value, ssd):
    if value < 0:
        return FG
    hot = 65 if ssd else 80
    critical = 75 if ssd else 90
    if value >= critical:
        return "#ff4b41"
    if value >= hot:
        return "#ffaa37"
    return FG

def percent(value):
    return "--" if value < 0 else str(int(value + 0.5)) + "%"

def degrees(value):
    return "--" if value < 0 else str(int(value + 0.5)) + "°"

def add_fill(parts, shape, value, color, x):
    if value <= 0:
        return
    gx, gy, gw, gh = GEOMETRY[shape]
    fraction = min(value, 100.0) / 100.0
    height = gh * fraction
    y = gy + gh - height
    parts.append(
        '<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="0.6" '
        'fill="%s" fill-opacity="0.42"/>' % (
            x + gx * 1.5, y * 1.5, gw * 1.5, height * 1.5, color))

def add_icon(parts, name, x):
    parts.append('<g transform="translate(%.2f,0) scale(1.5)">' % x)
    parts.append(asset(name))
    parts.append("</g>")

def add_text(parts, text, color, x, opacity=1.0):
    parts.append(
        '<text x="%.2f" y="16.5" font-family="DejaVu Sans Mono, monospace" '
        'font-size="12" fill="%s" fill-opacity="%.2f" stroke="#000000" '
        'stroke-width="2" paint-order="stroke">%s</text>' % (
            x, color, opacity, escape(text)))
    return x + text_width(text)

def build_svg(values):
    parts = []
    tool = []
    x = 0.0

    cpu = values.get("CPU", -1.0)
    cpu_temp = values.get("CPU temp", -1.0)
    ram = values.get("RAM", -1.0)
    gpu = values.get("GPU usage", -1.0)
    gpu_temp = values.get("GPU temp", -1.0)
    vram = values.get("VRAM", -1.0)
    ssd = values.get("SSD temp", -1.0)

    add_fill(parts, "cpu", cpu, "#50beff", x)
    add_icon(parts, "cpu-blue" if cpu >= 0 else "cpu-gray", x)
    x += 24 + 3
    x = add_text(parts, percent(cpu), FG, x)
    x += 3
    x = add_text(parts, degrees(cpu_temp), temp_color(cpu_temp, False), x)
    x += 10

    add_fill(parts, "ram", ram, "#b991ff", x)
    add_icon(parts, "ram-purple" if ram >= 0 else "ram-gray", x)
    x += 24 + 3
    x = add_text(parts, percent(ram), FG, x)
    x += 10

    add_fill(parts, "gpu", gpu, "#76d75a", x)
    add_icon(parts, "gpu-green" if gpu >= 0 else "gpu-gray", x)
    x += 24 + 3
    x = add_text(parts, percent(gpu), FG, x)
    x += 3
    x = add_text(parts, degrees(gpu_temp), temp_color(gpu_temp, False), x)
    x += 3
    x = add_text(parts, "VRAM", FG, x, 0.6)
    x += 3
    x = add_text(parts, percent(vram), VRAM if vram >= 0 else FG, x)
    x += 10

    add_icon(parts, "disk-hot-%d" % heat_index(ssd) if ssd >= 0 else "disk-gray", x)
    x += 24 + 3
    x = add_text(parts, degrees(ssd), temp_color(ssd, True), x)

    tool.append("CPU: %s %s" % (percent(cpu), degrees(cpu_temp)))
    tool.append("RAM: %s" % percent(ram))
    tool.append("GPU: %s %s" % (percent(gpu), degrees(gpu_temp)))
    tool.append("VRAM: %s" % percent(vram))
    tool.append("SSD: %s" % degrees(ssd))

    width = math.ceil(x + 2)
    svg = '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="24" viewBox="0 0 %d 24">%s</svg>' % (
        width, width, "".join(parts))
    return svg, "\n".join(tool)

def runtime_dir():
    base = os.environ.get("XDG_RUNTIME_DIR")
    if base:
        path = Path(base) / ("traysense-xfce-%d" % os.getuid())
    else:
        path = Path("/tmp") / ("traysense-xfce-%d" % os.getuid())
    path.mkdir(parents=True, exist_ok=True)
    return path

def clean_old(directory):
    now = time.time()
    for path in directory.glob("tray-*.svg"):
        try:
            if now - path.stat().st_mtime > 60:
                path.unlink()
        except OSError:
            pass

def main():
    binary = find_binary()
    values = read_metrics(binary) if binary else None

    if values is None:
        print("<txt>TraySense: --</txt>")
        print("<tool>TraySense: binary not found or failed</tool>")
        return 0

    svg, tooltip = build_svg(values)
    directory = runtime_dir()
    clean_old(directory)
    path = directory / ("tray-%d.svg" % os.getpid())
    path.write_text(svg)

    print("<img>%s</img>" % path)
    print("<tool>%s</tool>" % escape(tooltip))
    return 0

if __name__ == "__main__":
    sys.exit(main())
