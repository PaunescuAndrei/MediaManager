import QtQuick
import QtMultimedia

// GPU-rendered video surface for the preview tooltip/cards.
// Overlay values are set from OverlayGraphicsVideoWidget (C++).
Item {
    id: root

    property string overlayText: ""
    property real overlayScaleMultiplier: 1.0
    property int overlayPadX: 4
    property int overlayPadY: 2
    property int overlayMargin: 6

    Rectangle {
        anchors.fill: parent
        color: "#111111"
    }

    VideoOutput {
        id: video
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
    }

    Item {
        id: overlay
        anchors.fill: parent
        visible: root.overlayText.length > 0

        // Letterboxed video area, or the whole item while no frame is available.
        readonly property rect area: video.contentRect.width > 0 && video.contentRect.height > 0
                                     ? video.contentRect
                                     : Qt.rect(0, 0, root.width, root.height)
        readonly property real autoScale: area.height > 0 ? area.height / 200.0 : 1.0
        readonly property real scaleFactor: Math.max(0.1, root.overlayScaleMultiplier) * autoScale
        readonly property int fontSize: Math.min(48, Math.max(6, Math.round(11.0 * scaleFactor)))
        readonly property int padX: Math.min(96, Math.max(0, Math.round(root.overlayPadX * scaleFactor)))
        readonly property int padY: Math.min(96, Math.max(0, Math.round(root.overlayPadY * scaleFactor)))
        readonly property int margin: Math.min(128, Math.max(0, Math.round(root.overlayMargin * scaleFactor)))

        TextMetrics {
            id: metrics
            font: label.font
            text: root.overlayText
        }

        readonly property real boxWidth: metrics.boundingRect.width + 2 * padX
        readonly property real boxHeight: metrics.boundingRect.height + 2 * padY
        readonly property real boxX: Math.max(area.x, area.x + area.width - boxWidth - margin)
        readonly property real boxY: Math.max(area.y, area.y + area.height - boxHeight - margin)

        Rectangle {
            x: overlay.boxX
            y: overlay.boxY
            width: overlay.boxWidth
            height: overlay.boxHeight
            radius: 4
            color: Qt.rgba(0, 0, 0, 0.45)
        }

        Text {
            id: label
            x: overlay.boxX
            y: overlay.boxY
            width: overlay.boxWidth
            height: overlay.boxHeight
            text: root.overlayText
            color: "white"
            font.pointSize: overlay.fontSize
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
