import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import St from 'gi://St';
import Clutter from 'gi://Clutter';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const POLL_INTERVAL_MS = 1000;
const BADGE_SIZE = 22;

const GEOMETRY = {
    cpu: {x: 2.3, y: 2.3, width: 11.4, height: 11.4},
    gpu: {x: 2.85, y: 1.6, width: 12.1, height: 10.8},
    ram: {x: 1.1, y: 1.6, width: 13.8, height: 10.8},
    disk: {x: 2.2, y: 4.5, width: 11.6, height: 7.0},
};

function findValue(output, key) {
    const prefix = key + ': ';
    const lines = output.split('\n');

    for (let i = 0; i < lines.length; i++) {
        if (!lines[i].startsWith(prefix))
            continue;

        const value = parseFloat(lines[i].substring(prefix.length));
        return Number.isFinite(value) ? value : -1;
    }

    return -1;
}

function percent(value) {
    return value < 0 ? '--' : Math.round(value) + '%';
}

function degrees(value) {
    return value < 0 ? '--' : Math.round(value) + '°';
}

function rgba(tint, alpha) {
    const red = parseInt(tint.substring(1, 3), 16);
    const green = parseInt(tint.substring(3, 5), 16);
    const blue = parseInt(tint.substring(5, 7), 16);
    return 'rgba(' + red + ',' + green + ',' + blue + ',' + alpha + ')';
}

function heatIndex(temperature) {
    if (temperature < 55)
        return 0;
    if (temperature < 70)
        return 1;
    if (temperature < 80)
        return 2;
    if (temperature < 90)
        return 3;
    return 4;
}

function tempColor(value, ssd) {
    if (value < 0)
        return null;

    const hot = ssd ? 65 : 80;
    const critical = ssd ? 75 : 90;

    if (value >= critical)
        return '#ff4b41';
    if (value >= hot)
        return '#ffaa37';

    return null;
}

class Badge {
    constructor(shape, iconProvider) {
        this.shape = shape;
        this.iconProvider = iconProvider;
        this.iconKey = '';

        this.actor = new St.Widget({
            width: BADGE_SIZE,
            height: BADGE_SIZE,
        });

        this.fill = new St.Widget({
            style: 'border-radius: 1px;',
        });
        this.actor.add_child(this.fill);

        this.icon = new St.Icon({
            icon_size: BADGE_SIZE,
        });
        this.icon.set_position(0, 0);
        this.icon.set_size(BADGE_SIZE, BADGE_SIZE);
        this.actor.add_child(this.icon);
    }

    update(available, asset, accent, fill, value) {
        const geometry = GEOMETRY[this.shape];
        const scale = BADGE_SIZE / 16;

        if (fill && available && value > 0) {
            const fraction = Math.min(value, 100) / 100;
            const height = geometry.height * scale * fraction;

            this.fill.set_position(
                geometry.x * scale,
                (geometry.y + geometry.height) * scale - height);
            this.fill.set_size(geometry.width * scale, height);
            this.fill.set_style('background-color: ' + rgba(accent, 0.42) +
                '; border-radius: 1px;');
            this.fill.show();
        } else {
            this.fill.hide();
        }

        if (asset !== this.iconKey) {
            this.icon.gicon = this.iconProvider(asset);
            this.iconKey = asset;
        }
    }
}

const TraySenseIndicator = GObject.registerClass(
class TraySenseIndicator extends PanelMenu.Button {
    _init(extensionPath) {
        super._init(0.0, 'TraySense', false);

        this._destroyed = false;
        this._process = null;
        this._svgDir = GLib.build_filenamev([extensionPath, 'icons']);
        this._iconCache = new Map();

        this._box = new St.BoxLayout({
            style: 'spacing: 10px;',
            y_align: Clutter.ActorAlign.CENTER,
        });
        this.add_child(this._box);

        this._cpuBadge = new Badge('cpu', asset => this._iconProvider(asset));
        this._ramBadge = new Badge('ram', asset => this._iconProvider(asset));
        this._gpuBadge = new Badge('gpu', asset => this._iconProvider(asset));
        this._ssdBadge = new Badge('disk', asset => this._iconProvider(asset));

        this._cpuPct = this._makeLabel();
        this._cpuTemp = this._makeLabel();
        this._ramPct = this._makeLabel();
        this._gpuPct = this._makeLabel();
        this._gpuTemp = this._makeLabel();
        this._vramName = this._makeLabel();
        this._vramPct = this._makeLabel();
        this._ssdTemp = this._makeLabel();
        this._vramName.text = 'VRAM';

        this._box.add_child(this._makeGroup([this._cpuBadge.actor, this._cpuPct, this._cpuTemp]));
        this._box.add_child(this._makeGroup([this._ramBadge.actor, this._ramPct]));
        this._box.add_child(this._makeGroup([
            this._gpuBadge.actor,
            this._gpuPct,
            this._gpuTemp,
            this._vramName,
            this._vramPct,
        ]));
        this._box.add_child(this._makeGroup([this._ssdBadge.actor, this._ssdTemp]));

        this._timeoutId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, POLL_INTERVAL_MS, () => {
            this._poll();
            return GLib.SOURCE_CONTINUE;
        });

        this._poll();
    }

    _makeLabel() {
        return new St.Label({
            y_align: Clutter.ActorAlign.CENTER,
            style: 'font-size: 12px;',
        });
    }

    _makeGroup(children) {
        const group = new St.BoxLayout({
            style: 'spacing: 3px;',
            y_align: Clutter.ActorAlign.CENTER,
        });

        for (const child of children)
            group.add_child(child);

        return group;
    }

    _setLabel(label, text, textColor = null, opacity = 1.0) {
        label.text = text;
        label.set_style('font-size: 12px;' +
            (textColor ? ' color: ' + textColor + ';' : ''));
        label.opacity = Math.round(opacity * 255);
    }

    _iconProvider(asset) {
        let icon = this._iconCache.get(asset);

        if (icon)
            return icon;

        try {
            const path = GLib.build_filenamev([this._svgDir, asset + '.svg']);
            const [, contents] = GLib.file_get_contents(path);
            icon = Gio.BytesIcon.new(GLib.Bytes.new(contents));
            this._iconCache.set(asset, icon);
        } catch (error) {
            return null;
        }

        return icon;
    }


    _poll() {
        if (this._process)
            return;

        const binary = GLib.getenv('TRAYSENSE_BIN') || 'TraySense';

        try {
            this._process = Gio.Subprocess.new(
                [binary, '--once'],
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_SILENCE);
        } catch (error) {
            this._process = null;
            return;
        }

        this._process.communicate_utf8_async(null, null, (process, result) => {
            let output = '';

            try {
                const [, stdout] = process.communicate_utf8_finish(result);
                output = stdout || '';
            } catch (error) {
                output = '';
            }

            this._process = null;

            if (this._destroyed || output.length === 0)
                return;

            this._apply(output);
        });
    }

    _apply(output) {
        const cpu = findValue(output, 'CPU');
        const cpuTemp = findValue(output, 'CPU temp');
        const ram = findValue(output, 'RAM');
        const gpu = findValue(output, 'GPU usage');
        const gpuTemp = findValue(output, 'GPU temp');
        const vram = findValue(output, 'VRAM');
        const ssdTemp = findValue(output, 'SSD temp');

        this._cpuBadge.update(cpu >= 0, cpu >= 0 ? 'cpu-blue' : 'cpu-gray',
            '#50BEFF', true, Math.max(cpu, 0));
        this._ramBadge.update(ram >= 0, ram >= 0 ? 'ram-purple' : 'ram-gray',
            '#B991FF', true, Math.max(ram, 0));
        this._gpuBadge.update(gpu >= 0, gpu >= 0 ? 'gpu-green' : 'gpu-gray',
            '#76D75A', true, Math.max(gpu, 0));
        this._ssdBadge.update(ssdTemp >= 0,
            ssdTemp >= 0 ? 'disk-hot-' + heatIndex(ssdTemp) : 'disk-gray',
            '#8C8C8C', false, 0);

        this._setLabel(this._cpuPct, percent(cpu));
        this._setLabel(this._cpuTemp, degrees(cpuTemp), tempColor(cpuTemp, false));
        this._setLabel(this._ramPct, percent(ram));
        this._setLabel(this._gpuPct, percent(gpu));
        this._setLabel(this._gpuTemp, degrees(gpuTemp), tempColor(gpuTemp, false));
        this._setLabel(this._vramName, 'VRAM', null, 0.6);
        this._setLabel(this._vramPct, percent(vram),
            vram >= 0 ? '#76D75A' : null);
        this._setLabel(this._ssdTemp, degrees(ssdTemp), tempColor(ssdTemp, true));
    }

    destroy() {
        this._destroyed = true;

        if (this._timeoutId) {
            GLib.source_remove(this._timeoutId);
            this._timeoutId = 0;
        }

        super.destroy();
    }
});

export default class TraySenseExtension extends Extension {
    enable() {
        this._indicator = new TraySenseIndicator(this.path);
        Main.panel.addToStatusArea(this.uuid, this._indicator);
    }

    disable() {
        this._indicator.destroy();
        this._indicator = null;
    }
}
