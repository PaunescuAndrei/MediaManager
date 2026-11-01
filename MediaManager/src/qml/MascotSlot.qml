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
	// The same pair for the other axis (M3 Task 3, MascotSurface::FillMode::WidthFit): the media
	// is scaled to this slot's width with its aspect ratio kept, `paintedHeight` is the height
	// that produces, and the slot's clip is what crops the overflow. Written by MascotSurface
	// like everything else here; `heightFill` keeps its exact M1 meaning, so the two flags are
	// never both set and a slot that carries neither keeps its rectangle.
	property bool widthFill: false
	property real paintedHeight: 0.0
	// Set by MascotSurface::setSlotMirrored and ::setMirror. This is the single authoritative
	// mirror value: the C++ side derives the content shift from the same property it writes
	// here, so the flip and the shift cannot disagree.
	property bool mirrored: false
	// Pixel shift that puts the media's visual content centre on the slot's centre.
	// MascotSurface computes it from the source-pixel centre, the slot rectangle, the fill mode
	// and the mirror, so no arithmetic on the centre is repeated here.
	property real contentOffsetX: 0.0
	// The scroll driver's translation, in pixels, one per axis (M3 Task 3). It is added to the
	// centring above rather than replacing it: contentOffsetX still puts the content centre on
	// the slot's centre, these move that centred picture by how far the pan has travelled. The
	// surface owns the centre, the driver owns these, and a band that does not pan leaves both
	// at 0 -- which is what a `Static` panel always writes.
	property real scrollOffsetX: 0.0
	property real scrollOffsetY: 0.0
	property bool mediaIsImage: false
	// M6: quality hint written by MascotSurface. True while the media is being painted *smaller* than its
	// own resolution -- the case where plain linear sampling underwater a texture and shimmers as the pan
	// moves -- and it turns the Image's mipmapping on for exactly those slots. An image that is scaled up
	// (the common case: a 1080-tall panel showing a smaller picture, or a 640x360 clip in a 1932x1090
	// window) reads level 0, where mipmaps would only cost 33% more texture memory for nothing.
	property bool minifying: false

	// Video path state, all of it written by MascotSurface. The image above and the two video
	// outputs below are exclusive per slot: videoVisible is what makes the swap, and the outputs
	// keep their geometry the whole time so showing one costs no relayout.
	property bool videoVisible: false
	// One opacity per output, because a crossfade is one player fading in while the other fades
	// out; MascotSurface::setSlotVideoOpacity writes them one half at a time and never both.
	property real videoOpacityA: 0.0
	property real videoOpacityB: 0.0
	// Duration of the animations below, written together with the opacity it applies to. The
	// caller that wants a cut passes 0, which is a zero-duration animation and not a second
	// code path: C++ has no "instant" branch and QML no special case.
	property int crossfadeMs: 0

	// The animation lives here, not in C++. C++ writes one target opacity per half of a
	// crossfade and QML interpolates, so the swap costs a property write instead of a per-frame
	// timer on the GUI thread, and an interrupted crossfade continues from the value currently
	// on screen (that is what Behavior means by animating from the current value). A duration of
	// 0 completes on the next animation tick, which runs before that frame is painted, so the
	// picture only ever shows the old or the new opacity.
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
		// Explicit geometry instead of anchors.fill, because in height-normalised mode the
		// painted width is not the slot's width. Centring after that keeps the other three
		// modes on exactly the rectangle they filled before (same size, same origin), which is
		// the rectangle the content shift is derived from. In height-normalised mode the slot's
		// own clip is the whole crop: vertically the image is always the slot's height.
		// (M3 Task 3: width-normalised mode is the mirror image -- there the painted *height* is
		// not the slot's height -- so the height below is the general form of the same rule. For
		// HeightFit both expressions evaluate exactly as they did before: the width from
		// paintedWidth, the height from parent.height, and the clip still crops horizontally.)
		width: slot.heightFill ? slot.paintedWidth : parent.width
		height: slot.widthFill ? slot.paintedHeight : parent.height
		anchors.centerIn: parent
		// The Image is already the painted size in height-normalised mode, so Stretch has
		// nothing to do; the other modes keep their Qt Quick meaning.
		// (M3 Task 3: width-normalised mode is the same thing on the other axis -- the item is
		// already the painted size there too, so it takes Stretch as well. Crop, Fit and Stretch
		// still keep the slot's rectangle and their Qt Quick meaning.)
		fillMode: slot.heightFill || slot.widthFill ? Image.Stretch : slot.fillMode
		mirror: slot.mirrored
		visible: slot.mediaIsImage
		// (M6: the sampling quality, stated rather than inherited. `smooth` is Qt Quick's default and is
		// what makes a scaled image use linear sampling instead of the nearest texel -- it is written out
		// so the intent is on the record for both directions. `mipmap` is the one real choice: it is
		// trilinear minification, which is what stops a downscaled picture from aliasing, and it is the
		// best filtering Qt Quick offers an Image. The video outputs below have no equivalent knob --
		// VideoOutput samples linearly and Qt exposes no filtering property for it -- so a video is
		// always linear, up or down.)
		smooth: true
		mipmap: slot.minifying
		// Pinned, not left to Image's default: the pixels are already decoded and sitting in
		// MascotImageCache, and the provider serves them from memory (no disk, no decoder), so an
		// asynchronous load would only defer the texture by a frame and show the previous image
		// in the meantime. Synchronous here means the frame painted after a swap carries that
		// swap: the provider answers from the cache inside the same property write.
		asynchronous: false
		// The position is pinned by anchors, so the content shift has to be a transform.
		// (M3 Task 3: the same transform carries the scroll offset now, on both axes. With the
		// offsets at 0 -- every band that does not pan -- `contentOffsetX + 0` is the value this
		// line always wrote, and the y term is a no-op.)
		transform: Translate {
			x: slot.contentOffsetX + slot.scrollOffsetX
			y: slot.scrollOffsetY
		}
	}

	// Video outputs for the video path. Named per index (amendment O) but not driven yet: this
	// task's interface has nothing to hand them a QVideoSink.
	// M2 drives them: MascotSurface::slotSink() hands each output's sink to a player, and
	// setSlotVideoVisible()/setSlotVideoOpacity() write the properties used below. The two lines
	// above are M1's statement of this file, kept unedited as the record of what it asserted.
	//
	// The two video surfaces a crossfade needs. Named per index (amendment O), so C++ finds each
	// one by name and hands its sink to a player (MascotSurface::slotSink); this file creates no
	// player, so nothing here knows what is playing or when it changes -- it draws and it
	// crossfades.
	//
	// Geometry is the Image's, member for member, so a video occupies exactly the pixels of the
	// still it replaces: without that, a swap would also move and resize the picture. The fill
	// mode values agree because MascotSurface (qmlFillModeValue) writes Qt Quick's Image.FillMode
	// numbers, and VideoOutput.FillMode spells the three modes that mapping produces -- Stretch,
	// PreserveAspectFit, PreserveAspectCrop -- with the same values.
	// (M3 Task 3: "member for member" now includes the width-normalised pair and the scroll
	// translation, so a video band pans exactly as the image it replaced would. A video's content
	// offset is always 0 -- there is no alpha silhouette to centre -- so its translate carries
	// only the scroll offset.)
	VideoOutput {
		id: videoA

		objectName: "videoA" + index
		width: slot.heightFill ? slot.paintedWidth : parent.width
		height: slot.widthFill ? slot.paintedHeight : parent.height
		anchors.centerIn: parent
		fillMode: slot.heightFill || slot.widthFill ? VideoOutput.Stretch : slot.fillMode
		// mirrored is a real property since Qt 6.9 (REVISION(6, 9)), so the output flips the
		// frame itself: this is the video path's `mirror:` and needs no Scale transform, which
		// would also have to be undone for the clip and the content shift.
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
