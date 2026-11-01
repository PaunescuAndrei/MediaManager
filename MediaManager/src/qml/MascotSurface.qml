import QtQuick

// Root of one mascot scene: N clipped slots, one delegate each, plus the veil rectangle.
//
// MascotSurface (C++) drives everything through property names -- the slot count and every
// per-slot value, the mirror included: setMirror() applies it to each slot rather than to a
// property here, so there is no root flag that could drift out of step with what is drawn.
// This file decides no content: the Repeater owns the delegate items, and a slot count change
// is the only thing that creates or destroys one.
//
// (M6: this file is the whole of the three-layer compositing. The sheet draws the background
// layer's slots, the veil rectangle and the two columns' slots in one scene, and z is what puts
// them in order -- the layer at 0, the veil at 1 (above the columns, the default) or 3 (below
// them), the columns at 2. That is the fix the M5 milestone could not make: three widgets cannot
// be ordered, because a QQuickWidget carrying Qt::WA_AlwaysStackOnTop is drawn above every
// sibling whatever the stack says, but three items in one scene are ordered by z alone. The
// slots' groups and the veil's z are written by MascotSurface::setSlotGroup()/::setVeil(), and
// the numbers below are the only place the stacking order is spelled out.)
Item {
	id: surface

	// Set from C++ (MascotSurface::setSlotCount).
	property int slotCount: 0

	// M4 Task 6: the app's own colour behind the layer. A QQuickWidget blits its offscreen buffer
	// opaquely, so wherever no video covers a pixel the window would otherwise read as black; the
	// rectangle below paints the palette colour there instead. Both defaults are the historical
	// look -- a transparent, hidden rectangle -- so a surface nobody sets these on (the pair and
	// the columns) renders exactly as it did. Written by MascotSurface::setBackdrop().
	// (M6: the sheet is always opaque -- it is the window-sized surface under every pane -- so this
	// backdrop is now the window's own colour on every path, not only on the layer's holes. That is
	// what keeps a run with the layer off byte-identical: the sheet paints exactly what the central
	// widget painted behind it.)
	property color backdropColor: "transparent"
	property bool backdropVisible: false

	// M6: the veil -- `mascots_background_opacity` as one rectangle in this scene. Written by
	// MascotSurface::setVeil(); `veilZ` is `mascots_columns_z` (1 = the veil above the columns,
	// 3 = below them) and is the only difference between the two settings.
	property color veilColor: "transparent"
	property real veilOpacity: 0.0
	property bool veilVisible: false
	property int veilZ: 1

	// M4 Task 6: first child, so the Repeater's slots below are drawn above it.
	// (M6: z -1 rather than declaration order, because the slots carry z of their own now.
	// Negative z keeps it under the layer group at 0 and under everything else.)
	Rectangle {
		id: backdrop

		anchors.fill: parent
		color: surface.backdropColor
		visible: surface.backdropVisible
		z: -1
	}

	// M5 fix: the layer's opacity fades the content only. It used to be written on the root item, so
	// the backdrop faded with the video and a low `mascots_background_opacity` left the layer's empty
	// pixels black instead of the app's colour. `MascotSurface::setSurfaceOpacity()` writes this
	// property now.
	// (M6: there is no content wrapper any more, and this property is gone with it. A wrapper would
	// have to hold every slot, and its one z would then keep the layer's slots and the columns' on the
	// same plane -- the veil could not be put between them. The fade is per slot now:
	// MascotSurface::applySlot() writes each slot's own opacity, and the layer group's slots carry the
	// surface's content fade. The backdrop still does not fade, which is the point M5 fixed.)
	Repeater {
		model: surface.slotCount
		delegate: MascotSlot {}
	}

	// M6: the veil rectangle. It fills the sheet -- the central widget's own rectangle, which is the
	// one the window-wide veil widget used to fill -- and it sits at veilZ, so the columns at z 2 are
	// either above it (1, the default: mascots are not veiled) or below it (3: they are, once, at the
	// same alpha the layer gets). Nothing is masked, nothing is painted per side, and a future change
	// to the UI cannot move it: it is the same scene as the layers it veils.
	Rectangle {
		id: veil

		anchors.fill: parent
		color: surface.veilColor
		opacity: surface.veilOpacity
		visible: surface.veilVisible
		z: surface.veilZ
	}
}
