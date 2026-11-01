#pragma once
#include "MascotMedia.h"
#include <QColor>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <QWidget>

class MascotImageCache;
class QQuickItem;
class QQuickWidget;
class QVideoSink;

// The mascot panel: N clipped QML slots rendered by one QQuickWidget, each showing whatever
// MascotMedia it was handed through image://mascots/... .
//
// This class renders what it is told to render. It chooses no media, reads no config and keeps
// no cache pointer (amendment N) -- the cache is used once, to build the image provider, and
// the engine owns that provider from then on. Every setSlot* call is mirrored in Slot so a
// delegate that QML recreated after a slotCount change can be brought back to the state the
// caller already set, without the caller replaying every setter.
class MascotSurface : public QWidget {
	Q_OBJECT
public:
	// How the media is drawn inside its slot rectangle. This enum is the only source of the
	// mapping onto Qt Quick's Image.FillMode (amendment Q).
	//
	// HeightFit is the image path's mode (Static, M1): the media is scaled to the slot's height
	// with its aspect ratio kept -- the old renderer's `pixmap.scaled(w, height,
	// KeepAspectRatio)` -- so the vertical extent always equals the slot height and the slot's
	// width is the only axis that can crop (a wide image) or leave margins (a narrow one).
	// Crop, Fit and Stretch keep Qt Quick's meanings for the milestones that need them, and the
	// painted width that HeightFit produces is computed here, never in QML.
	// (M3 Task 3: WidthFit is HeightFit on the other axis -- the media is scaled to the slot's
	// *width* with its aspect ratio kept, so the height overflows and is clipped, which is the
	// axis a vertically stacked stripe pans along. The two normalised modes are the only ones
	// whose painted size differs from the slot's rectangle; Crop, Fit and Stretch keep that
	// rectangle and let Qt Quick fit or crop the media inside it. ContentSizeFor() below is the
	// one place all five sizes are derived.)
	enum class FillMode { Crop, Fit, Stretch, HeightFit, WidthFit };

	// `cache` is used to construct the image provider and is not stored; it must outlive the
	// shared QML engine, which owns the provider.
	// (M4: `alwaysStackOnTop` is the panel's compositing rule and the background layer's undoing. A
	// panel sits beside the rest of the UI and needs its own alpha composited over the window, which
	// Qt only does reliably for a QQuickWidget that draws last. The background sits *underneath* the
	// UI, so the same attribute makes it paint over the very widgets it is meant to sit behind:
	// measured at runtime, a window-sized layer with it set moved the list's pixels by 50/255 of
	// luminance while every video line said the layer was drawing. The background therefore passes
	// false and is drawn in ordinary stacking order, beneath its siblings.)
	explicit MascotSurface(MascotImageCache* cache, QWidget* parent = nullptr,
		bool alwaysStackOnTop = true);

	// Only a count change creates or destroys delegate items; every other setter writes into
	// the slot that already exists.
	void setSlotCount(int n);
	int slotCount() const;

	void setSlotRect(int i, const QRectF& r);
	// The size the media is drawn at in slot `i`, both axes, for the fill mode that slot
	// carries. This is the single place a pan's travel is derived from -- `max(0, contentWidth -
	// slotWidth)` for a horizontal pan and the same on the height for a vertical one -- so a
	// caller must ask this rather than repeat the fill arithmetic. An invalid slot answers an
	// empty size rather than the surface's.
	QSizeF slotContentSize(int i) const;
	// Horizontal shift in pixels that brings the media's visual content centre onto the slot's
	// centre. Overrides the shift setSlotMedia() derives until the slot's media changes again.
	void setSlotContentOffset(int i, const QPointF& offset);
	// The scroll driver's translation, in pixels, added on top of whatever centring the slot
	// already applies (the offset above, or the value setSlotMedia() derives). One value per
	// axis because a vertical stripe pans along the other axis than a horizontal one, and it is
	// deliberately separate from the centring so the surface keeps owning the centre and the
	// driver only moves it. Mirroring it in Slot is what brings a delegate QML recreated after
	// a slot count change back to the offset the driver last wrote.
	void setSlotScrollOffset(int i, double dx, double dy);
	void setSlotOpacity(int i, double opacity);
	void setSlotFillMode(int i, FillMode mode);
	void setSlotMirrored(int i, bool mirrored);
	// Writes the slot's image source, fill mode and content shift. A media without a usable
	// provider id (or a video) leaves the image hidden rather than pointing it nowhere.
	void setSlotMedia(int i, const MascotMedia& media);
	// The sink one of a slot's two video outputs owns, so a player can be pointed at it without
	// knowing anything about QML: `ab` selects the output (0 = A, 1 = B, the two halves of a
	// crossfade). The sink belongs to the VideoOutput item, not to this class, and dies with the
	// delegate -- so null is the honest answer both while the delegate does not exist and for
	// any pointer a caller kept across a setSlotCount() that shrank the panel. Query it again
	// after such a call; never hold it past one.
	QVideoSink* slotSink(int i, int ab);
	// Whether the slot's video outputs draw at all. The image path and the video path are
	// exclusive per slot, so the caller hides one when it shows the other; the slot's media
	// properties are left alone, because switching back must not cost a reload.
	void setSlotVideoVisible(int i, bool visible);
	// Opacity of one video output (`ab` as in slotSink()) and the duration, in milliseconds,
	// over which QML animates that change. Writing the two outputs with the same duration is a
	// crossfade; a duration of 0 leaves QML animating for no time at all, which is the hard cut,
	// so there is no second code path to keep in step. Both values are mirrored in Slot so a
	// delegate recreated by a slot count change comes back at the opacity last asked for.
	void setSlotVideoOpacity(int i, int ab, double opacity, int crossfadeMs);
	void clearSlot(int i);
	void clearSlots();
	// Surface-wide mirror value (from config, applied by the driver). It is a bulk assignment
	// over every slot and the value a slot created by a later setSlotCount() starts from; the
	// per-slot flag stays the single authoritative value, so a setSlotMirrored() that comes
	// after this one overrides that slot only. There is no separate root property to drift out
	// of step with what is drawn.
	void setMirror(bool mirror);
	// M4 Task 2: the whole layer's opacity, one value for the surface rather than one per slot.
	// The background layer is one number for the picture behind the window, so it is written on
	// the QML root item: that keeps the value inside the scene graph, where the quick widget's
	// own composition already applies it, instead of giving the widget a QGraphicsOpacityEffect,
	// which would add a second offscreen composition per frame for a value Qt Quick can simply
	// draw with. Clamped the way setSlotOpacity() clamps, and there is deliberately no slot
	// equivalent for it: a per-slot opacity is setSlotOpacity() and stays a different thing.
	void setSurfaceOpacity(double opacity);
	// M4 Task 6: the colour a layer with a hole in it shows instead of the quick widget's opaque
	// black. Both values are written on the QML root item, the same path setSurfaceOpacity()
	// takes: `backdropColor` fills a Rectangle that is the root's first child and `backdropVisible`
	// shows or hides it. The slots are declared after that rectangle, so they stay above it. A
	// surface that never calls this keeps the QML defaults (transparent, hidden) and draws exactly
	// as it did, which is what makes the pair's and the columns' surfaces backdrop-free.
	void setBackdrop(const QColor& colour, bool enabled);

signals:
	// Emitted for a release over a slot; `slot` is always a valid index, so a release outside
	// every slot rectangle is not reported.
	void clicked(int slot, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
	// One local image file dropped on a slot (amendment P).
	void imageDropped(int slot, QString path);

protected:
	// Taken for a press over a slot so the matching release is routed here and the panel's host
	// never sees half of a click pair; a press outside every slot is left to the base
	// implementation, which ignores it and lets it propagate.
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dragMoveEvent(QDragMoveEvent* event) override;
	void dropEvent(QDropEvent* event) override;

private:
	// What one slot should look like. The Qt Quick side holds the same values as properties;
	// this copy exists so a recreated delegate can be re-applied and so the content shift can
	// be recomputed when the rectangle, fill mode or mirror changes.
	struct Slot {
		// QPointer, not raw: a delegate is destroyed by the Repeater when the slot count
		// shrinks, and a QPointer nulls itself instead of dangling. resolveSlot() finds a null
		// one again by its object name (amendment O), so the pointer can never survive its item.
		QPointer<QQuickItem> item;
		QPointer<QQuickItem> image;
		// The two VideoOutput items, resolved with the rest of the slot for the same reason and
		// nulled the same way. They are resolved here rather than inside slotSink() because
		// that accessor is const and because a delegate applySlot() has touched must already be
		// reachable for its sinks.
		QPointer<QQuickItem> videoA;
		QPointer<QQuickItem> videoB;

		QRectF rect;
		double opacity = 1.0;
		FillMode fillMode = FillMode::Fit;
		bool mirrored = false;
		// The video path's own state. It is mirrored here for the same reason as everything
		// above: applySlot() writes it back into a delegate QML recreated.
		bool videoVisible = false;
		// Two values, not one: a crossfade is one output fading in while the other fades out,
		// and only the caller knows which player sits on which output.
		double videoOpacityA = 0.0;
		double videoOpacityB = 0.0;
		// Duration of the animation QML runs on either opacity above. 0 is the hard cut.
		int crossfadeMs = 0;

		QString providerId;
		bool mediaIsImage = false;
		QSize nativeSize;
		// Set by MascotMedia::hasContentCenter, never derived from the value below: the centre
		// is only a number when this is true (see MascotMedia.h).
		bool hasContentCenter = false;
		double contentCenterX = 0.0;
		// An explicit setSlotContentOffset() wins over the shift derived from the centre until
		// the slot's media is replaced.
		bool hasExplicitContentOffset = false;
		double explicitContentOffsetX = 0.0;
		// M3 Task 3: the scroll driver's translation, one per axis, applied on top of the
		// centring above. Kept separate from it so the centre stays the surface's answer and the
		// pan stays the driver's; mirrored here for the same reason as every other value, so a
		// delegate QML recreated comes back at the offset the driver last wrote.
		double scrollOffsetX = 0.0;
		double scrollOffsetY = 0.0;
	};

	bool isValidSlot(int i) const;
	// Resolves the slot item and its image child by object name and caches them; false while
	// the delegate does not exist (the mirrored state is applied as soon as it does).
	// Since M2 it resolves the slot's two video outputs the same way and by the same names, so
	// the per-slot QVideoSink slotSink() reports is reachable from the slot index alone.
	bool resolveSlot(int i);
	// Writes every mirrored value into the slot's delegate, unconditionally, and that is the
	// contract: a delegate QML has just created carries none of these values yet, so a change
	// test inside this function would skip exactly the restore it exists to perform (M3 Task 3
	// review: do not "optimise" these writes away). A commit is not a tick -- the writes of an
	// unchanged value are stores with no binding churn and no allocation -- and the per-tick
	// budget is met by setSlotScrollOffset(), which writes only the axis that moved.
	void applySlot(int i);
	void applyAllSlots();
	// Pixel shift for the mirrored content centre, computed from the slot's own scale.
	double contentShiftFor(const Slot& slot, bool mirrored) const;
	// Width the media is painted at: the slot's width for Stretch, the height-normalised width
	// for HeightFit, and the fit/crop scale of the slot for Fit/Crop. The single place the
	// painted width is derived, so the shift and the QML geometry cannot disagree.
	// (M3 Task 3: contentSizeFor() below is that place now -- WidthFit needs the two axes derived
	// together, and a second copy of the arithmetic is exactly what this comment exists to
	// prevent. This function is the width half of it, kept because the content shift only ever
	// needed the width and every answer above is unchanged.)
	double paintedWidthFor(const Slot& slot) const;
	// The size the media is painted at, both axes, for the slot's fill mode: the slot's own size
	// for Stretch, the height-normalised pair for HeightFit, the width-normalised pair for
	// WidthFit, and the fit/crop scale of the slot for Fit/Crop. The single place the painted
	// geometry is derived, so the content shift, the QML item sizes and a caller's pan travel
	// (slotContentSize()) cannot disagree.
	QSizeF contentSizeFor(const Slot& slot) const;
	int slotAt(const QPointF& position) const;

	QQuickWidget* quick = nullptr;
	// QPointer for the same reason the slot items use one: nothing here may keep a pointer to a
	// QML object that could be destroyed under it.
	QPointer<QQuickItem> root;
	QVector<Slot> slotStates;
	// The value last handed to setMirror(): the initial mirror state of a slot created later.
	// What a slot draws is always its own Slot::mirrored, never this combined with it, so there
	// is exactly one place the effective mirror comes from.
	bool mirror = false;
};
