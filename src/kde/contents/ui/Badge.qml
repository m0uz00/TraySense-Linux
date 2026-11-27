import QtQuick

Item {
    id: badge

    property string shape: "cpu"
    property string asset: "cpu-blue"
    property bool available: true
    property bool fill: true
    property real value: 0
    property color accent: "#50BEFF"

    readonly property real unit: width / 16
    readonly property real innerX: (shape === "cpu" ? 2.3 : shape === "gpu" ? 2.85 : shape === "disk" ? 2.2 : 1.1) * unit
    readonly property real innerY: (shape === "cpu" ? 2.3 : shape === "disk" ? 4.5 : 1.6) * unit
    readonly property real innerWidth: (shape === "cpu" ? 11.4 : shape === "gpu" ? 12.1 : shape === "disk" ? 11.6 : 13.8) * unit
    readonly property real innerHeight: (shape === "cpu" ? 11.4 : shape === "disk" ? 7.0 : 10.8) * unit

    width: 24
    height: 24

    Rectangle {
        visible: badge.fill && badge.available && badge.value > 0

        x: badge.innerX
        y: badge.innerY + badge.innerHeight * (1 - Math.min(badge.value, 100) / 100)
        width: badge.innerWidth
        height: badge.innerHeight * Math.min(badge.value, 100) / 100
        radius: Math.max(1, badge.unit * 0.4)
        color: badge.accent
        opacity: 0.42
    }

    Image {
        anchors.fill: parent
        source: Qt.resolvedUrl("../icons/" + badge.asset + ".svg")
        sourceSize.width: 24
        sourceSize.height: 24
        fillMode: Image.PreserveAspectFit
    }
}
