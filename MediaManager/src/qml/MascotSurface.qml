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

	Repeater {
		model: surface.slotCount
		delegate: MascotSlot {}
	}
}
