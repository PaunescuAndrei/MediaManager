import QtQuick

// One scene ordered by z alone: separate QQuickWidgets cannot be ordered (`WA_AlwaysStackOnTop` draws one above
// every sibling).
Item {
	id: surface

	property int slotCount: 0

	// The widget blits opaquely: without a backdrop, any pixel no slot covers reads black instead of the window colour.
	property color backdropColor: "transparent"
	property bool backdropVisible: false

	property color veilColor: "transparent"
	property real veilOpacity: 0.0
	property bool veilVisible: false
	property int veilZ: 1

	Rectangle {
		id: backdrop

		anchors.fill: parent
		color: surface.backdropColor
		visible: surface.backdropVisible
		z: -1
	}

	Repeater {
		model: surface.slotCount
		delegate: MascotSlot {}
	}

	Rectangle {
		id: veil

		anchors.fill: parent
		color: surface.veilColor
		opacity: surface.veilOpacity
		visible: surface.veilVisible
		z: surface.veilZ
	}
}
