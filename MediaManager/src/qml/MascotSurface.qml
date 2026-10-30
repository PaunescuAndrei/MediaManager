import QtQuick

// Root of the mascot panel: N clipped slots, one delegate each.
//
// MascotSurface (C++) drives everything through property names -- the slot count, the
// surface-wide mirror flag and every per-slot value. This file decides no content: the Repeater
// owns the delegate items, and a slot count change is the only thing that creates or destroys
// one.
Item {
	id: surface

	// Set from C++ (MascotSurface::setSlotCount).
	property int slotCount: 0
	// Set from C++ (MascotSurface::setMirror). Informational on this side: C++ also writes the
	// effective per-slot flag, because the content shift it derives depends on the mirror.
	property bool mirrored: false

	Repeater {
		model: surface.slotCount
		delegate: MascotSlot {}
	}
}