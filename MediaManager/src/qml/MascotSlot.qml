import QtQuick
import QtMultimedia

// Object names carry the slot index, so C++ finds a slot and its children by name and child order can never mismatch a sink.
Item {
	id: slot

	// Not a required property: a delegate whose required properties were not populated would leave every slot silently blank.
	objectName: "mascotSlot" + index
	clip: true

	property int fillMode: Image.PreserveAspectFit
	property bool heightFill: false
	property real paintedWidth: 0.0
	property bool widthFill: false
	property real paintedHeight: 0.0
	// The single authoritative mirror: C++ derives the content shift from the same value it writes here.
	property bool mirrored: false
	// Puts the media's content centre on the slot's centre; the scroll offsets below are added on top of it.
	property real contentOffsetX: 0.0
	property real scrollOffsetX: 0.0
	property real scrollOffsetY: 0.0
	property bool mediaIsImage: false
	// True only while the picture is painted smaller than its resolution -- the case mipmapping helps.
	// Video has no filtering knob in Qt, so it is always linear.
	property bool minifying: false

	// Image and the two outputs are exclusive per slot; the outputs keep their geometry, so a swap costs no relayout.
	property bool videoVisible: false
	property real videoOpacityA: 0.0
	property real videoOpacityB: 0.0
	// 0 is a zero-duration animation, not a second code path.
	property int crossfadeMs: 0

	// Interpolated in QML so a swap is a property write, and an interrupted crossfade continues from the value on screen.
	Behavior on videoOpacityA {
		NumberAnimation {
			duration: slot.crossfadeMs
			easing.type: Easing.InOutQuad
		}
	}
	Behavior on videoOpacityB {
		NumberAnimation {
			duration: slot.crossfadeMs
			easing.type: Easing.InOutQuad
		}
	}

	Image {
		id: img

		objectName: "img" + index
		width: slot.heightFill ? slot.paintedWidth : parent.width
		height: slot.widthFill ? slot.paintedHeight : parent.height
		anchors.centerIn: parent
		fillMode: slot.heightFill || slot.widthFill ? Image.Stretch : slot.fillMode
		mirror: slot.mirrored
		visible: slot.mediaIsImage
		smooth: true
		mipmap: slot.minifying
		// The provider answers from the cache inside the same property write, so a synchronous load is the frame after a swap.
		asynchronous: false
		transform: Translate {
			x: slot.contentOffsetX + slot.scrollOffsetX
			y: slot.scrollOffsetY
		}
	}

	VideoOutput {
		id: videoA

		objectName: "videoA" + index
		width: slot.heightFill ? slot.paintedWidth : parent.width
		height: slot.widthFill ? slot.paintedHeight : parent.height
		anchors.centerIn: parent
		fillMode: slot.heightFill || slot.widthFill ? VideoOutput.Stretch : slot.fillMode
		mirrored: slot.mirrored
		visible: slot.videoVisible
		opacity: slot.videoOpacityA
		transform: Translate {
			x: slot.scrollOffsetX
			y: slot.scrollOffsetY
		}
	}

	VideoOutput {
		id: videoB

		objectName: "videoB" + index
		width: slot.heightFill ? slot.paintedWidth : parent.width
		height: slot.widthFill ? slot.paintedHeight : parent.height
		anchors.centerIn: parent
		fillMode: slot.heightFill || slot.widthFill ? VideoOutput.Stretch : slot.fillMode
		mirrored: slot.mirrored
		visible: slot.videoVisible
		opacity: slot.videoOpacityB
		transform: Translate {
			x: slot.scrollOffsetX
			y: slot.scrollOffsetY
		}
	}
}
