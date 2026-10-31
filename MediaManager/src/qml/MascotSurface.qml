import QtQuick

// Root of the mascot panel: N clipped slots, one delegate each.
//
// MascotSurface (C++) drives everything through property names -- the slot count and every
// per-slot value, the mirror included: setMirror() applies it to each slot rather than to a
// property here, so there is no root flag that could drift out of step with what is drawn.
// This file decides no content: the Repeater owns the delegate items, and a slot count change
// is the only thing that creates or destroys one.
Item {
	id: surface

	// Set from C++ (MascotSurface::setSlotCount).
	property int slotCount: 0

	// M4 Task 6: the app's own colour behind the layer. A QQuickWidget blits its offscreen buffer
	// opaquely, so wherever no video covers a pixel the window would otherwise read as black; the
	// rectangle below paints the palette colour there instead. Both defaults are the historical
	// look -- a transparent, hidden rectangle -- so a surface nobody sets these on (the pair and
	// the columns) renders exactly as it did. Written by MascotSurface::setBackdrop().
	property color backdropColor: "transparent"
	property bool backdropVisible: false

	// Written by MascotSurface::setSurfaceOpacity(): the layer's own opacity, applied to the content
	// rather than to the root item so the backdrop below it stays solid (see the note there).
	property real contentOpacity: 1.0

	// M4 Task 6: first child, so the Repeater's slots below are drawn above it.
	Rectangle {
		id: backdrop

		anchors.fill: parent
		color: surface.backdropColor
		visible: surface.backdropVisible
	}

	// M5 fix: the layer's opacity fades the content only. It used to be written on the root item, so
	// the backdrop faded with the video and a low `mascots_background_opacity` left the layer's empty
	// pixels black instead of the app's colour. `MascotSurface::setSurfaceOpacity()` writes this
	// property now.
	Item {
		id: content

		anchors.fill: parent
		opacity: surface.contentOpacity

		Repeater {
			model: surface.slotCount
			delegate: MascotSlot {}
		}
	}
}
