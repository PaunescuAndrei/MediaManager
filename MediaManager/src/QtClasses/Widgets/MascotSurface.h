#pragma once
#include "MascotMedia.h"
#include <QElapsedTimer> // AMENDMENT Z (temporary timing instrumentation) - remove with its block
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>

class MascotImageCache;
class QQuickItem;
class QQuickWidget;

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
	enum class FillMode { Crop, Fit, Stretch, HeightFit };

	// `cache` is used to construct the image provider and is not stored; it must outlive the
	// shared QML engine, which owns the provider.
	explicit MascotSurface(MascotImageCache* cache, QWidget* parent = nullptr);

	// Only a count change creates or destroys delegate items; every other setter writes into
	// the slot that already exists.
	void setSlotCount(int n);
	int slotCount() const;

	void setSlotRect(int i, const QRectF& r);
	// Horizontal shift in pixels that brings the media's visual content centre onto the slot's
	// centre. Overrides the shift setSlotMedia() derives until the slot's media changes again.
	void setSlotContentOffset(int i, const QPointF& offset);
	void setSlotOpacity(int i, double opacity);
	void setSlotFillMode(int i, FillMode mode);
	void setSlotMirrored(int i, bool mirrored);
	// Writes the slot's image source, fill mode and content shift. A media without a usable
	// provider id (or a video) leaves the image hidden rather than pointing it nowhere.
	void setSlotMedia(int i, const MascotMedia& media);
	void clearSlot(int i);
	void clearSlots();
	// Surface-wide mirror value (from config, applied by the driver). It is a bulk assignment
	// over every slot and the value a slot created by a later setSlotCount() starts from; the
	// per-slot flag stays the single authoritative value, so a setSlotMirrored() that comes
	// after this one overrides that slot only. There is no separate root property to drift out
	// of step with what is drawn.
	void setMirror(bool mirror);

	// ===== AMENDMENT Z: TEMPORARY TIMING INSTRUMENTATION - DELETE AFTER THE LATENCY GATE =====
	// Inert unless the app was started with `-debug`, because that is the only flag under which
	// beginSwapTiming() is ever called. To remove it: delete this block, the `#include
	// <QElapsedTimer>` marked with AMENDMENT Z at the top of this file, the AMENDMENT Z blocks
	// in MascotSurface.cpp (constructor install, and the two definitions at the end of the
	// file) and the AMENDMENT Z blocks in MascotController.cpp. Nothing else refers to it.
	//
	// A swap is committed by a property write; the frame that carries it is painted later, so
	// that paint is where the commit becomes visible. swapFramePainted() is emitted once the
	// paint handler that drew that frame has returned.
	void beginSwapTiming(const QString& label);
	// Public in QObject, so the override can sit inside this block instead of next to the other
	// event handlers below.
	bool eventFilter(QObject* watched, QEvent* event) override;
signals:
	void swapFramePainted(const QString& label, double msecs);
private:
	QElapsedTimer swapTimer;
	QString swapLabel;
	bool swapTimingPending = false;
public:
	// ===== END AMENDMENT Z ===================================================================

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

		QRectF rect;
		double opacity = 1.0;
		FillMode fillMode = FillMode::Fit;
		bool mirrored = false;

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
	};

	bool isValidSlot(int i) const;
	// Resolves the slot item and its image child by object name and caches them; false while
	// the delegate does not exist (the mirrored state is applied as soon as it does).
	bool resolveSlot(int i);
	void applySlot(int i);
	void applyAllSlots();
	// Pixel shift for the mirrored content centre, computed from the slot's own scale.
	double contentShiftFor(const Slot& slot, bool mirrored) const;
	// Width the media is painted at: the slot's width for Stretch, the height-normalised width
	// for HeightFit, and the fit/crop scale of the slot for Fit/Crop. The single place the
	// painted width is derived, so the shift and the QML geometry cannot disagree.
	double paintedWidthFor(const Slot& slot) const;
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
