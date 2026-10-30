import QtQuick
import QtMultimedia

// One mascot slot: a clipped box that draws a single image or video. MascotSurface (C++)
// owns every property here and decides nothing from model data; the file only draws.
//
// Object names carry the slot index (amendment O), so C++ finds a slot item and its children
// by name and child order can never silently mismatch a sink to a rectangle.
Item {
	id: slot

	// The Repeater's index, injected into the delegate. Declaring it as required is what makes
	// the dependency explicit instead of relying on an implicit context property.
	required property int index

	objectName: "mascotSlot" + index
	clip: true

	// Target of the FillMode -> Image.FillMode mapping that lives in MascotSurface (C++,
	// amendment Q): the QML side maps nothing itself.
	property int fillMode: Image.PreserveAspectFit
	property bool mirrored: false
	// Pixel shift that puts the media's visual content centre on the slot's centre.
	// MascotSurface computes it from the source-pixel centre, the slot rectangle, the fill mode
	// and the mirror, so no arithmetic on the centre is repeated here.
	property real contentOffsetX: 0.0
	property bool mediaIsImage: false

	Image {
		id: img

		objectName: "img" + slot.index
		anchors.fill: parent
		fillMode: slot.fillMode
		mirror: slot.mirrored
		visible: slot.mediaIsImage
		// anchors.fill pins the position, so the content shift has to be a transform.
		transform: Translate { x: slot.contentOffsetX }
	}

	// Video outputs for the video path. Named per index (amendment O) but not driven yet: this
	// task's interface has nothing to hand them a QVideoSink.
	VideoOutput { objectName: "videoA" + slot.index }
	VideoOutput { objectName: "videoB" + slot.index }
}