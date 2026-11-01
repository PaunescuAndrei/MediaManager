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

// Every setSlot* value is mirrored in Slot: a delegate QML recreated after a slotCount change carries none of it.
class MascotSurface : public QWidget {
	Q_OBJECT
public:
	// `Crop` holds the framing still; `Cover` travels it along the scroll axis as far as the picture overflows, so the
	// whole picture passes the band and none of it is cut off for good.
	enum class FillMode { Crop, Fit, Stretch, HeightFit, WidthFit, Cover };

	// One scene for all three layers: a QQuickWidget carrying Qt::WA_AlwaysStackOnTop is drawn above every
	// sibling whatever the stack says, and one without it blits opaquely.
	enum class SlotGroup { Layer, Columns };

	// `alwaysStackOnTop` is true for a panel, false for the window-sized layer: measured, the attribute drew the layer over the list (50/255 luminance).
	explicit MascotSurface(MascotImageCache* cache, QWidget* parent = nullptr,
		bool alwaysStackOnTop = true);

	void setSlotCount(int n);
	int slotCount() const;

	void setSlotRect(int i, const QRectF& r);
	void setSlotGroup(int i, SlotGroup group);
	QSizeF slotContentSize(int i) const;
	void setSlotContentOffset(int i, const QPointF& offset);
	void setSlotScrollOffset(int i, double dx, double dy);
	// The only per-slot opacity a still image has; an image crossfade needs exactly this.
	void setSlotOpacity(int i, double opacity);
	void setSlotFillMode(int i, FillMode mode);
	void setSlotMirrored(int i, bool mirrored);
	// -1 restores the slot's own players; indices, not item pointers, since the delegate can be recreated.
	void setSlotMirrorSource(int i, int sourceSlot);
	int slotMirrorSource(int i) const;
	// A held slot keeps drawing the other band's stage while the band that owns the slot loads a picture of its own.
	void setSlotMirrorHold(int i, bool held);
	bool slotMirrorHeld(int i) const;
	void setSlotMedia(int i, const MascotMedia& media);
	// Null while the delegate does not exist and after any setSlotCount() that shrank the panel; query again, never hold it.
	QVideoSink* slotSink(int i, int ab);
	void setSlotVideoVisible(int i, bool visible);
	// A duration of 0 is a zero-duration animation -- the hard cut -- so there is no second code path.
	void setSlotVideoOpacity(int i, int ab, double opacity, int crossfadeMs);
	void clearSlot(int i);
	// Bulk default for slots created later; the per-slot flag stays the value that is drawn.
	void setMirror(bool mirror);
	// Applied to the layer group's slots only, inside the scene graph: a QGraphicsOpacityEffect would add a second offscreen composition per frame.
	void setSurfaceOpacity(double opacity);
	void setBackdrop(const QColor& colour, bool enabled);

	// One rectangle at z 1 (columns drawn over it) or 3 (columns veiled once); a per-column fade let the layer through by veil^2.
	void setVeil(const QColor& colour, double opacity, bool enabled, bool aboveColumns);

signals:
	void clicked(int slot, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
	void imageDropped(int slot, QString path);

protected:
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dragMoveEvent(QDragMoveEvent* event) override;
	void dropEvent(QDropEvent* event) override;

private:
	struct Slot {
		QPointer<QQuickItem> item;
		QPointer<QQuickItem> image;
		QPointer<QQuickItem> videoA;
		QPointer<QQuickItem> videoB;
		QPointer<QQuickItem> stage;
		int mirrorSourceSlot = -1;
		bool mirrorHeld = false;

		QRectF rect;
		double opacity = 1.0;
		SlotGroup group = SlotGroup::Columns;
		FillMode fillMode = FillMode::Fit;
		bool mirrored = false;
		bool videoVisible = false;
		double videoOpacityA = 0.0;
		double videoOpacityB = 0.0;
		int crossfadeMs = 0;

		QString providerId;
		bool mediaIsImage = false;
		QSize nativeSize;
		bool hasContentCenter = false;
		double contentCenterX = 0.0;
		bool hasExplicitContentOffset = false;
		double explicitContentOffsetX = 0.0;
		double scrollOffsetX = 0.0;
		double scrollOffsetY = 0.0;
	};

	bool isValidSlot(int i) const;
	bool resolveSlot(int i);
	// Writes every mirrored value unconditionally: a fresh delegate carries none of them, so a change test would skip exactly the restore it exists for.
	void applySlot(int i);
	void applyAllSlots();
	double contentShiftFor(const Slot& slot, bool mirrored) const;
	double paintedWidthFor(const Slot& slot) const;
	QSizeF contentSizeFor(const Slot& slot) const;
	int slotAt(const QPointF& position) const;

	QQuickWidget* quick = nullptr;
	QPointer<QQuickItem> root;
	QVector<Slot> slotStates;
	double contentFade = 1.0;
	bool mirror = false;
};
