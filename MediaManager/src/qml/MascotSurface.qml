import QtQuick

// The layer's slots, the veil and both columns' slots are one scene ordered by z alone: separate
// QQuickWidgets cannot be ordered (`WA_AlwaysStackOnTop` draws one above every sibling).
Item {
	id: surface

	property int slotCount: 0

	// An opaque blit, so uncovered pixels take the window colour -- and a layer-off run is byte-identical.
	property color backdropColor: "transparent"
	property bool backdropVisible: false

	// `veilZ` is `mascots_columns_z`: 1 above the columns, 3 below.
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
