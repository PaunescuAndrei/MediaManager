import QtQuick
import QtMultimedia

// One mascot slot: a clipped box that draws a single image or video. MascotSurface (C++)
// owns every property here and decides nothing from model data; the file only draws.
//
// Object names carry the slot index (amendment O), so C++ finds a slot item and its children
// by name and child order can never silently mismatch a sink to a rectangle.
Item {
	id: slot

	// `index` is the Repeater's delegate context property, which the Repeater always injects.
	// Deliberately not declared as a required property: that form depends on the engine
	// populating required properties of delegates, and a delegate that failed to be populated
	// would leave every slot silently blank.
	objectName: "mascotSlot" + index
	clip: true

	// Target of the FillMode -> Image.FillMode mapping that lives in MascotSurface (C++,
	// amendment Q): the QML side maps nothing itself.
	property int fillMode: Image.PreserveAspectFit
	// True for the height-normalised mode (MascotSurface::FillMode::HeightFit): the media is
	// scaled to this slot's height with its aspect ratio kept, and `paintedWidth` is the width
	// that produces. Both are written by MascotSurface, which is the only place the geometry is
	// computed -- this file draws what it is told.
	property bool heightFill: false
	property real paintedWidth: 0.0
	// Set by MascotSurface::setSlotMirrored and ::setMirror. This is the single authoritative
	// mirror value: the C++ side derives the content shift from the same property it writes
	// here, so the flip and the shift cannot disagree.
	property bool mirrored: false
	// Pixel shift that puts the media's visual content centre on the slot's centre.
	// MascotSurface computes it from the source-pixel centre, the slot rectangle, the fill mode
	// and the mirror, so no arithmetic on the centre is repeated here.
	property real contentOffsetX: 0.0
	property bool mediaIsImage: false

	Image {
		id: img

		objectName: "img" + index
		// Explicit geometry instead of anchors.fill, because in height-normalised mode the
		// painted width is not the slot's width. Centring after that keeps the other three
		// modes on exactly the rectangle they filled before (same size, same origin), which is
		// the rectangle the content shift is derived from. In height-normalised mode the slot's
		// own clip is the whole crop: vertically the image is always the slot's height.
		width: slot.heightFill ? slot.paintedWidth : parent.width
		height: parent.height
		anchors.centerIn: parent
		// The Image is already the painted size in height-normalised mode, so Stretch has
		// nothing to do; the other modes keep their Qt Quick meaning.
		fillMode: slot.heightFill ? Image.Stretch : slot.fillMode
		mirror: slot.mirrored
		visible: slot.mediaIsImage
		// Pinned, not left to Image's default: the pixels are already decoded and sitting in
		// MascotImageCache, and the provider serves them from memory (no disk, no decoder), so an
		// asynchronous load would only defer the texture by a frame and show the previous image
		// in the meantime. Synchronous here means the frame painted after a swap carries that
		// swap, which is also what the latency probe measures.
		asynchronous: false
		// The position is pinned by anchors, so the content shift has to be a transform.
		transform: Translate { x: slot.contentOffsetX }
	}

	// Video outputs for the video path. Named per index (amendment O) but not driven yet: this
	// task's interface has nothing to hand them a QVideoSink.
	VideoOutput { objectName: "videoA" + index }
	VideoOutput { objectName: "videoB" + index }
}
