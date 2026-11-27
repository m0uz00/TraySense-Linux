# TraySense

Linux system monitor with tray implementations for KDE Plasma, GNOME Shell, and XFCE.

```
[cpu] 12% 54°   [ram] 61%   [gpu] 4% 44° VRAM 6%   [ssd] 38°
```

KDE, GNOME, and XFCE replace the bracketed labels with SVG metric icons and draw the values next to them.

## Architecture

```
src/bin/           C backend (polls /proc, sysfs, hwmon, NVML)
src/kde/           Plasma 6 applet
src/gnome/         GNOME Shell extension
src/xfce/          XFCE GenMon script + icons
src/icons/         canonical SVG assets
include/           C headers
```

The C backend prints one metric block per sample. Each tray runs the backend as a one-shot (`TraySense --once`) and parses the block.

## Metrics

| Metric | Source |
|---|---|
| CPU usage | `/proc/stat`, delta of user/nice/system/idle/iowait/irq/softirq/steal |
| CPU temperature | hwmon `k10temp` or `coretemp`, `temp1_input` |
| RAM usage | `/proc/meminfo`, `MemTotal` vs `MemAvailable` |
| Disk usage | `statvfs("/")` |
| GPU usage | NVML `nvmlDeviceGetUtilizationRates`; AMD `gpu_busy_percent` |
| GPU temperature | NVML; `card*/device/hwmon/hwmon*/temp*_input` |
| VRAM usage | NVML (allocated memory); `mem_info_vram_*`; Intel `tile0/vram0/*` |
| SSD temperature | hwmon `nvme` or `drivetemp`, `temp1_input` |

NVIDIA support uses `dlopen("libnvidia-ml.so.1")`, so there is no build-time link dependency. If NVML is unavailable the backend falls back to DRM sysfs for AMD and Intel devices.

Intel integrated GPUs have no dedicated VRAM and usually no GPU-utilization counter, so those values display `--`. A temperature is shown if the driver exposes a hwmon sensor.

## Build

Requirements: `gcc`, `make`, Linux, and the usual kernel interfaces listed above. The C backend has no external library dependencies.

```
make            # build/TraySense_debug, -O0 -g
make release    # build/TraySense, -O2
make run        # build and run the release binary
make clean
```

`make` defaults to the unoptimized debug binary. The release binary is `build/TraySense`.

## Backend usage

```
./build/TraySense           # loop forever, one sample every 100 ms
./build/TraySense --once    # wait 100 ms for a CPU delta, print one sample, exit
```

Output:

```
CPU: 12.34%
CPU temp: 54.1°C
RAM: 61.00%
Disk: 46.00%
VRAM: 6.00%
GPU temp: 44.0°C
GPU usage: 4.00%
SSD temp: 38.0°C

```

Samples are separated by a blank line. Trays parse the `key: value` lines, so extra fields can be added without breaking them.

Set `TRAYSENSE_BIN` to an absolute binary path if `TraySense` is not in `PATH`:

```
TRAYSENSE_BIN=/home/user/Coding/TraySense-Linux/build/TraySense
```

For GUI sessions, put that in `~/.config/environment.d/traysense.conf`.

## KDE Plasma 6

```
kpackagetool6 --type Plasma/Applet --install src/kde
```

After editing the applet:

```
kpackagetool6 --type Plasma/Applet --upgrade src/kde
systemctl --user restart plasma-plasmashell.service
```

Add **TraySense** to a panel. The applet shows four groups: CPU (usage + temperature), RAM, GPU (usage + temperature + VRAM), and SSD temperature. It uses the pre-colored SVGs from `src/kde/contents/icons/`; no shader effects or runtime tinting are required.

## GNOME Shell 45–51

```
mkdir -p ~/.local/share/gnome-shell/extensions
ln -s "$PWD/src/gnome" ~/.local/share/gnome-shell/extensions/traysense@m0uz00.github.io
```

Log out and back in on Wayland, then enable it:

```
gnome-extensions enable traysense@m0uz00.github.io
```

If `enable` reports that the extension does not exist, the running shell has not scanned the new directory yet; restart the session and run it again.

The extension spawns `TraySense --once` asynchronously every second, loads the pre-colored SVG files from `src/gnome/icons/` as `Gio.BytesIcon`s, and lays out the same four groups with `St.BoxLayout`.

The extension directory is symlinked, so edits to `src/gnome` apply on the next shell restart. If you install a copy instead, repeat the copy after changes.

## XFCE 4.20

Install the desktop and the Generic Monitor plugin:

```
sudo pacman -S xfce4 xfce4-genmon-plugin
```

Add a **Generic Monitor** item to the panel and configure it:

| Field | Value |
|---|---|
| Command | `python3 /path/to/TraySense-Linux/src/xfce/traysense-genmon.py` |
| Period | `1` second |
| Label | unchecked |

The script runs `TraySense --once`, composes a single 24 px SVG bar with the same four groups, and returns:

```
<img>/run/user/1000/traysense-xfce-1000/tray-PID.svg</img>
<tool>CPU: 12% 54°
RAM: 61%
GPU: 4% 44°
VRAM: 6%
SSD: 38°</tool>
```

GenMon displays the SVG and the multiline tooltip. Icon assets are in `src/xfce/icons/`; the script finds the binary via `TRAYSENSE_BIN`, `TraySense` in `PATH`, the repository `build/TraySense`, or `~/.local/bin/TraySense`.

The panel entry can also be configured from the command line. Add the plugin and find its ID:

```
xfce4-panel --add=genmon
xfconf-query -c xfce4-panel -l | grep -E '/plugins/plugin-[0-9]+$'
```

Then, with the panel stopped, set the properties and start it again:

```
xfce4-panel -q
xfconf-query -c xfce4-panel -p /plugins/plugin-N/command -s "python3 $PWD/src/xfce/traysense-genmon.py"
xfconf-query -c xfce4-panel -p /plugins/plugin-N/use-label -s false
xfconf-query -c xfce4-panel -p /plugins/plugin-N/update-period -s 1000
xfce4-panel --disable-wm-check &
```

Replace `plugin-N` with the actual ID.

## Icons

All icons are SVG with a 16×16 view box:

- `cpu-blue`, `cpu-gray`
- `ram-purple`, `ram-gray`
- `gpu-green`, `gpu-gray`
- `disk-gray`, `disk-hot-0..4`

`src/icons/base/` contains white line and solid templates. Colored variants live in `src/icons/` and are baked into:

- `src/kde/contents/icons/`
- `src/gnome/icons/`
- `src/xfce/icons/`

The KDE/GNOME group icons are tinted by choosing a pre-colored file, not by runtime shaders. Temperature icons use the heat bins 35/55/70/80/90 °C; value text uses orange at 80 °C and red at 90 °C (65/75 °C for SSD).

SSD temperature uses the RAM shape in the KDE and GNOME groups, matching the Windows reference's shape mapping; XFCE uses a disk glyph.

## Performance

- Backend RSS is about 2 MiB without NVML; loading NVML adds the NVIDIA driver's mappings, around 27 MiB total.
- The loop mode polls every 100 ms. Tray implementations call `--once` once per second, so CPU, RAM, disk, and sensor reads are cheap.
- Each `--once` that touches the GPU pays the NVML initialization cost once per process. A long-lived backend stream would remove that; the current implementations trade it for simple, isolated polling.
- No Cairo, PNG generation, or runtime icon tinting.

## Notes

- CPU temperature requires a `k10temp` or `coretemp` hwmon device.
- SSD temperature requires `nvme` or `drivetemp` hwmon support.
- AMD GPU data requires `amdgpu`; older `radeon` devices may not expose `gpu_busy_percent`.
- KDE applet packages are copies: re-run `kpackagetool6 --type Plasma/Applet --upgrade src/kde` after editing `src/kde`.
- The GNOME extension is a development symlink; re-login (or restart the shell) after QML/JS changes.
- Icon layout and palette follow the Windows TraySense design by DMTWEAKS.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
