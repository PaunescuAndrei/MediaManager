import QtQuick
import QtMultimedia

// Object names carry the slot index, so C++ finds a slot and its children by name and child order can never mismatch a sink.
Item {
	id: slot

	// Not a required property: a delegate whose required properties were not populated would leave every slot silently blank.
	objectName: "mascotSlot" + index
	clip: true

	property real paintedWidth: 0.0
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
	// The other column's stage item while this band is the mirror view of it, null otherwise. Set by the surface,
	// which resolves the slot index the controller names.
	property Item mirrorSource: null


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
		width: slot.paintedWidth
		height: slot.paintedHeight
		anchors.centerIn: parent
		fillMode: Image.Stretch
		mirror: slot.mirrored
		visible: slot.mediaIsImage && !slot.mirrorSource
		smooth: true
		mipmap: slot.minifying
		// The provider answers from the cache inside the same property write, so a synchronous load is the frame after a swap.
		asynchronous: false
		transform: Translate {
			x: slot.contentOffsetX + slot.scrollOffsetX
			y: slot.scrollOffsetY
		}
	}

	// The slot's own picture, one item so a mirrored band can sample it as a texture. Both outputs in one place is
	// also what keeps the crossfade inside the picture the other column draws.
	Item {
		id: stage

		objectName: "stage" + index
		width: slot.width
		height: slot.height
		visible: slot.videoVisible && !slot.mirrorSource

		VideoOutput {
			id: videoA

			objectName: "videoA" + index
			width: slot.paintedWidth
			height: slot.paintedHeight
			anchors.centerIn: parent
			fillMode: VideoOutput.Stretch
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
			width: slot.paintedWidth
			height: slot.paintedHeight
			anchors.centerIn: parent
			fillMode: VideoOutput.Stretch
			mirrored: slot.mirrored
			visible: slot.videoVisible
			opacity: slot.videoOpacityB
			transform: Translate {
				x: slot.scrollOffsetX
				y: slot.scrollOffsetY
			}
		}
	}

	// A mirrored band draws the other column's stage instead of running its own player, so the pair shows one decoded
	// frame rather than two decoders racing. The flip is a negative scale, not a shader: Qt 6's ShaderEffect takes
	// .qsb files only. `hideSource` stays false because the source item is the other column's own picture, and hiding
	// it would blank that column.
	Item {
		id: mirrorView

		// The source's own size, centred, and clipped by the slot: stretching it to this cell's rectangle would
		// distort the picture whenever the two columns' bands are not the same size, which they need not be.
		anchors.centerIn: parent
		width: slot.mirrorSource ? slot.mirrorSource.width : slot.width
		height: slot.mirrorSource ? slot.mirrorSource.height : slot.height
		visible: slot.mirrorSource !== null

		ShaderEffectSource {
			width: parent.width
			height: parent.height
			sourceItem: slot.mirrorSource
			live: true
			hideSource: false
			transform: Scale {
				origin.x: width / 2
				xScale: slot.mirrored ? -1 : 1
			}
		}
	}
}
