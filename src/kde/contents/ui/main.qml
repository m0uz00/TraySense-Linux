import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami
import org.kde.plasma.plasma5support as Plasma5Support

PlasmoidItem {
    id: root

    property string displayText: "[cpu] -- --   [ram] --   [gpu] -- -- VRAM --   [ssd] --"

    property real cpuValue: -1
    property real cpuTempValue: -1
    property real ramValue: -1
    property real gpuValue: -1
    property real gpuTempValue: -1
    property real vramValue: -1
    property real ssdTempValue: -1

    readonly property bool darkTheme: {
        const background = Kirigami.Theme.backgroundColor;
        return (background.r * 0.299 + background.g * 0.587 + background.b * 0.114) < 0.5;
    }

    readonly property color foreground: Kirigami.Theme.textColor
    readonly property string command: "sh -c 'exec \"${TRAYSENSE_BIN:-TraySense}\" --once'"

    preferredRepresentation: fullRepresentation
    toolTipMainText: displayText

    fullRepresentation: Row {
        spacing: 10
        Layout.preferredWidth: implicitWidth
        Layout.preferredHeight: implicitHeight

        Row {
            spacing: 3

            Badge {
                shape: "cpu"
                asset: root.cpuValue >= 0 ? "cpu-blue" : "cpu-gray"
                available: root.cpuValue >= 0
                value: Math.max(root.cpuValue, 0)
                accent: "#50BEFF"
            }

            Text {
                text: root.pct(root.cpuValue)
                color: root.foreground
                font.pixelSize: 12
            }

            Text {
                text: root.deg(root.cpuTempValue)
                color: root.tempColor(root.cpuTempValue, false)
                font.pixelSize: 12
            }
        }

        Row {
            spacing: 3

            Badge {
                shape: "ram"
                asset: root.ramValue >= 0 ? "ram-purple" : "ram-gray"
                available: root.ramValue >= 0
                value: Math.max(root.ramValue, 0)
                accent: "#B991FF"
            }

            Text {
                text: root.pct(root.ramValue)
                color: root.foreground
                font.pixelSize: 12
            }
        }

        Row {
            spacing: 3

            Badge {
                shape: "gpu"
                asset: root.gpuValue >= 0 ? "gpu-green" : "gpu-gray"
                available: root.gpuValue >= 0
                value: Math.max(root.gpuValue, 0)
                accent: "#76D75A"
            }

            Text {
                text: root.pct(root.gpuValue)
                color: root.foreground
                font.pixelSize: 12
            }

            Text {
                text: root.deg(root.gpuTempValue)
                color: root.tempColor(root.gpuTempValue, false)
                font.pixelSize: 12
            }

            Text {
                text: "VRAM"
                color: root.foreground
                opacity: 0.6
                font.pixelSize: 12
            }

            Text {
                text: root.pct(root.vramValue)
                color: root.vramValue >= 0 ? "#76D75A" : root.foreground
                font.pixelSize: 12
            }
        }

        Row {
            spacing: 3

            Badge {
                shape: "disk"
                asset: root.ssdTempValue >= 0 ? "disk-hot-" + root.heatIndex(root.ssdTempValue) : "disk-gray"
                available: root.ssdTempValue >= 0
                fill: false
                value: 0
                accent: "#8C8C8C"
            }

            Text {
                text: root.deg(root.ssdTempValue)
                color: root.tempColor(root.ssdTempValue, true)
                font.pixelSize: 12
            }
        }
    }

    Plasma5Support.DataSource {
        id: executable
        engine: "executable"
        connectedSources: []

        onNewData: (source, data) => {
            const output = data["stdout"] || "";
            executable.disconnectSource(source);
            if (output.length > 0)
                root.applyOutput(output);
        }
    }

    Timer {
        id: pollTimer
        interval: 1000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: executable.connectSource(root.command)
    }

    function findValue(output, key) {
        const prefix = key + ": ";
        const lines = output.split("\n");

        for (let i = 0; i < lines.length; i++) {
            if (lines[i].indexOf(prefix) !== 0)
                continue;

            const value = parseFloat(lines[i].substring(prefix.length));
            return isNaN(value) ? -1 : value;
        }

        return -1;
    }

    function pct(value) {
        return value < 0 ? "--" : Math.round(value) + "%";
    }

    function deg(value) {
        return value < 0 ? "--" : Math.round(value) + "°";
    }

    function tempColor(value, ssd) {
        if (value < 0)
            return foreground;

        const hot = ssd ? 65 : 80;
        const critical = ssd ? 75 : 90;

        if (value >= critical)
            return darkTheme ? "#ff4b41" : "#c81919";
        if (value >= hot)
            return darkTheme ? "#ffaa37" : "#c35f00";

        return foreground;
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

    function formatLabel() {
        return "[cpu] " + pct(cpuValue) + " " + deg(cpuTempValue) +
               "   [ram] " + pct(ramValue) +
               "   [gpu] " + pct(gpuValue) + " " + deg(gpuTempValue) + " VRAM " + pct(vramValue) +
               "   [ssd] " + deg(ssdTempValue);
    }

    function applyOutput(output) {
        cpuValue = findValue(output, "CPU");
        cpuTempValue = findValue(output, "CPU temp");
        ramValue = findValue(output, "RAM");
        gpuValue = findValue(output, "GPU usage");
        gpuTempValue = findValue(output, "GPU temp");
        vramValue = findValue(output, "VRAM");
        ssdTempValue = findValue(output, "SSD temp");

        displayText = formatLabel();
    }
}
