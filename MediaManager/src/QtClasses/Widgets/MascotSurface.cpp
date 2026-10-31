#include "stdafx.h"
#include "MascotSurface.h"
#include "MascotImageProvider.h"
#include "OverlayGraphicsVideoWidget.h"
#include <algorithm>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoSink>

namespace {
	// Slot rectangles live inside a window, so a sane coordinate is far below this. The test
	// below is a pair of ordered comparisons rather than std::isfinite because /fp:fast is
	// allowed to fold a NaN test away, while a NaN compares false against every bound -- so a
	// NaN rect (or opacity, or offset) is rejected by construction, not by compiler luck.
	constexpr double MAX_SLOT_EXTENT = 1.0e6;

	bool isSaneRect(const QRectF& rect)
	{
		return rect.x() >= -MAX_SLOT_EXTENT && rect.x() <= MAX_SLOT_EXTENT
			&& rect.y() >= -MAX_SLOT_EXTENT && rect.y() <= MAX_SLOT_EXTENT
			&& rect.width() >= 0.0 && rect.width() <= MAX_SLOT_EXTENT
			&& rect.height() >= 0.0 && rect.height() <= MAX_SLOT_EXTENT;
	}

	bool isSaneExtent(double value)
	{
		return value >= -MAX_SLOT_EXTENT && value <= MAX_SLOT_EXTENT;
	}

	// The formats MascotPrefetcher can actually decode (it goes through utils::openImage).
	// Amendment P: a drop is rejected at the boundary instead of being handed to a loader that
	// would fail on it later.
	const QStringList& droppableImageSuffixes()
	{
		static const QStringList suffixes{
			QStringLiteral("png"),
			QStringLiteral("jpg"),
			QStringLiteral("jpeg"),
			QStringLiteral("webp"),
			QStringLiteral("bmp"),
			QStringLiteral("gif")
		};
		return suffixes;
	}

	// True only for exactly one local file with an image suffix the pipeline can decode.
	bool singleLocalImagePath(const QMimeData* mimeData, QString* path)
	{
		if (!mimeData || !mimeData->hasUrls())
			return false;
		const QList<QUrl> urls = mimeData->urls();
		if (urls.size() != 1)
			return false;
		const QUrl& url = urls.first();
		if (!url.isLocalFile())
			return false;
		const QFileInfo file(url.toLocalFile());
		if (!file.isFile())
			return false;
		if (!droppableImageSuffixes().contains(file.suffix().toLower()))
			return false;
		*path = file.absoluteFilePath();
		return true;
	}

	// Qt Quick's Image.FillMode values. Spelled out instead of including the Qt Quick private
	// header that declares the enum; the QML side reads the property as an Image.FillMode and
	// maps nothing itself, so this function stays the single source of the mapping
	// (amendment Q).
	int qmlFillModeValue(MascotSurface::FillMode mode)
	{
		switch (mode) {
		case MascotSurface::FillMode::Crop:
			return 2; // Image.PreserveAspectCrop
		case MascotSurface::FillMode::Fit:
			return 1; // Image.PreserveAspectFit
		case MascotSurface::FillMode::Stretch:
			return 0; // Image.Stretch
		case MascotSurface::FillMode::HeightFit:
			// No Qt Quick fill mode means "scale to the slot's height, keep the aspect ratio":
			// the slot's QML sizes the image from the painted width applySlot() writes, so the
			// Image is already exactly the right size and has nothing left to fit or crop.
			// Stretch is therefore the honest value rather than a substitute.
			return 0; // Image.Stretch
		case MascotSurface::FillMode::WidthFit:
			// The same argument on the other axis (M3 Task 3): the slot's QML sizes the item from
			// the painted height, so again there is nothing left for Qt Quick to fit or crop.
			return 0; // Image.Stretch
		}
		return 1;
	}
}

MascotSurface::MascotSurface(MascotImageCache* cache, QWidget* parent, bool alwaysStackOnTop)
	: QWidget(parent)
{
	this->setAttribute(Qt::WA_NoSystemBackground, true);
	this->setAutoFillBackground(false);
	// Drops land on this widget, not on the quick widget below it: a drag walks up the parent
	// chain to the first widget that accepts drops, and the quick widget stays transparent to
	// the mouse so releases reach this widget too.
	this->setAcceptDrops(true);

	QQmlEngine* engine = OverlayGraphicsVideoWidget::previewEngine();
	// One provider per engine: the engine owns it, so a second surface must not replace (and
	// delete) the first one's provider. Every surface shares the same engine and, through the
	// driver, the same cache, so one provider serves them all. Registering before setSource()
	// is what lets the root QML resolve image://mascots/... ids while it is being created.
	if (!engine->imageProvider(QStringLiteral("mascots")))
		engine->addImageProvider(QStringLiteral("mascots"), new MascotImageProvider(cache));

	this->quick = new QQuickWidget(engine, this);
	// The panel sits on top of the window's own background; the scene graph must not clear to
	// an opaque colour first.
	this->quick->setClearColor(Qt::transparent);
	// The clearing colour alone is not enough: without this the quick widget blits its offscreen
	// buffer as an opaque rectangle, so every transparent pixel of a mascot PNG turns black
	// instead of showing the window behind it. Both the wrapper and the quick widget carry the
	// attribute; the wrapper's own background is already suppressed above.
	this->setAttribute(Qt::WA_TranslucentBackground, true);
	this->quick->setAttribute(Qt::WA_TranslucentBackground, true);
	if (alwaysStackOnTop) {
		// Draw last, so this widget's alpha composites over whatever the window painted beneath.
		// This is the documented way to get a see-through quick widget over ordinary widgets:
		// QQuickWidget otherwise blits its offscreen buffer opaquely, turning every transparent
		// pixel of a mascot PNG black (verified at runtime: without this the panel reads #000000).
		// (M4: the paragraph above is the panel's case, and it is why this became conditional rather
		// than being removed. A QQuickWidget carrying this attribute is drawn above every other
		// widget in the window regardless of stacking order -- which is exactly what a panel beside
		// the UI wants and exactly what a background layer cannot have, because lower() cannot
		// outrank it. The background passes false and draws in ordinary order; it accepts the opaque
		// blit the paragraph describes, which for a layer that video covers edge to edge is the right
		// trade, and the alternative was a wallpaper painted over the list.)
		this->quick->setAttribute(Qt::WA_AlwaysStackOnTop, true);
	}
	this->quick->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	this->quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
	this->quick->setSource(QUrl(QStringLiteral("qrc:/qml/MascotSurface.qml")));

	if (this->quick->status() == QQuickWidget::Error) {
		const QList<QQmlError> errors = this->quick->errors();
		for (const QQmlError& error : errors)
			qWarning() << "MascotSurface.qml:" << error.toString();
	}

	this->root = this->quick->rootObject();

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(this->quick);
}

void MascotSurface::setSlotCount(int n)
{
	if (n < 0)
		n = 0;
	if (n == this->slotStates.size())
		return;
	const int previousCount = this->slotStates.size();
	// Shrink first, then let QML destroy: the Slot entries (and the QPointers inside them) go
	// away before the Repeater deletes the delegate they refer to, so nothing here can outlive
	// an item. Growing is the same order: the Repeater creates the delegates during the
	// property write below, and applyAllSlots() then resolves them and re-applies the state
	// each slot already carries.
	this->slotStates.resize(n);
	// A slot that did not exist when setMirror() last ran starts from that value; a slot that
	// already existed keeps whatever it was given, including a per-slot setSlotMirrored().
	for (int i = previousCount; i < n; ++i)
		this->slotStates[i].mirrored = this->mirror;
	if (this->root)
		this->root->setProperty("slotCount", n);
	this->applyAllSlots();
}

int MascotSurface::slotCount() const
{
	return this->slotStates.size();
}

void MascotSurface::setSlotRect(int i, const QRectF& r)
{
	if (!this->isValidSlot(i) || !isSaneRect(r))
		return;
	this->slotStates[i].rect = r;
	this->applySlot(i);
}

QSizeF MascotSurface::slotContentSize(int i) const
{
	// An out-of-range index has no geometry to report; a zero size keeps a caller's travel
	// arithmetic (max(0, content - slot)) from inventing one -- and it is spelled out rather than
	// left to QSizeF's default, which is (-1, -1). Every valid slot answers from the same
	// function applySlot() writes the QML geometry from, so what a caller measures is what is
	// drawn.
	if (!this->isValidSlot(i))
		return QSizeF(0.0, 0.0);
	return this->contentSizeFor(this->slotStates.at(i));
}

void MascotSurface::setSlotContentOffset(int i, const QPointF& offset)
{
	if (!this->isValidSlot(i) || !isSaneExtent(offset.x()))
		return;
	this->slotStates[i].hasExplicitContentOffset = true;
	this->slotStates[i].explicitContentOffsetX = offset.x();
	this->applySlot(i);
}

void MascotSurface::setSlotScrollOffset(int i, double dx, double dy)
{
	// Both axes go through the same ordered comparisons a rectangle and an opacity use, so a NaN
	// offset is rejected by construction and never reaches QML (see isSaneRect). An unchanged
	// tick is not filtered here: the band's own record of the offset it last wrote
	// (MascotBand::scrollOffset()) is where that happens, and this setter stays a plain write.
	if (!this->isValidSlot(i) || !isSaneExtent(dx) || !isSaneExtent(dy))
		return;
	Slot& slot = this->slotStates[i];
	// (M3 Task 3 fix: the per-axis test below is where an unchanged axis is filtered out now. The
	// sentence above stays as the record of what this setter asserted when it wrote both axes on
	// every call. The *mirror* is still written unconditionally, unchanged axes included, because
	// that is what applySlot() restores into a delegate QML recreates.)
	const bool xMoved = slot.scrollOffsetX != dx;
	const bool yMoved = slot.scrollOffsetY != dy;
	slot.scrollOffsetX = dx;
	slot.scrollOffsetY = dy;
	// Two property writes, not an applySlot(): a tick moves the content and changes nothing else,
	// and §7.10's per-tick budget is a few writes, not a whole slot re-apply (which would also
	// re-resolve the delegate and rewrite every other property on the GUI thread). A delegate
	// that does not exist yet has nothing to move; the mirrored value is applied by applySlot()
	// when QML creates it.
	// (M3 Task 3 fix: "two property writes" is one for the axis that moved and none when neither
	// did, which is what keeps a horizontal band's tick at one write per slot per frame.)
	QQuickItem* item = slot.item.data();
	if (!item)
		return;
	if (xMoved)
		item->setProperty("scrollOffsetX", dx);
	if (yMoved)
		item->setProperty("scrollOffsetY", dy);
}

void MascotSurface::setSlotOpacity(int i, double opacity)
{
	// A NaN fails the comparison below like it fails every comparison, so it never reaches
	// QML; a value above 1 is clamped because Qt Quick clamps opacity itself anyway and the
	// mirrored value should match what the item ends up with.
	if (!this->isValidSlot(i) || !(opacity >= 0.0))
		return;
	this->slotStates[i].opacity = opacity > 1.0 ? 1.0 : opacity;
	this->applySlot(i);
}

void MascotSurface::setSlotFillMode(int i, FillMode mode)
{
	if (!this->isValidSlot(i))
		return;
	this->slotStates[i].fillMode = mode;
	this->applySlot(i);
}

void MascotSurface::setSlotMirrored(int i, bool mirrored)
{
	if (!this->isValidSlot(i))
		return;
	this->slotStates[i].mirrored = mirrored;
	this->applySlot(i);
}

void MascotSurface::setSlotMedia(int i, const MascotMedia& media)
{
	if (!this->isValidSlot(i))
		return;
	Slot& slot = this->slotStates[i];
	// The shift setSlotMedia() derives replaces any explicit offset: a new media brings its own
	// content centre, and keeping the old override would centre the new image by the old
	// image's geometry.
	slot.hasExplicitContentOffset = false;
	slot.explicitContentOffsetX = 0.0;
	slot.providerId = media.providerId;
	slot.mediaIsImage = media.type == MascotMedia::Image && !media.providerId.isEmpty();
	slot.nativeSize = media.nativeSize;
	// hasContentCenter is the structural test (MascotMedia.h): the centre is only a number when
	// it is true, and std::isnan cannot be trusted under /fp:fast. The value is still checked
	// against the sane range so a caller that breaks that contract cannot poison the shift.
	slot.hasContentCenter = media.hasContentCenter && isSaneExtent(media.contentCenterX);
	slot.contentCenterX = slot.hasContentCenter ? media.contentCenterX : 0.0;
	this->applySlot(i);
}

QVideoSink* MascotSurface::slotSink(int i, int ab) const
{
	// An out-of-range index or output selector has no sink to return. The caller's own
	// bookkeeping should never produce one, but answering null costs one comparison and keeps an
	// out-of-range read from deciding anything.
	if (!this->isValidSlot(i) || (ab != 0 && ab != 1))
		return nullptr;
	const Slot& slot = this->slotStates.at(i);
	QQuickItem* video = (ab == 0 ? slot.videoA : slot.videoB).data();
	// A delegate that does not exist yet (or was destroyed by a slot count change) has no item,
	// and an item without the property has no sink: both are null, and neither is something this
	// call can repair -- the caller is told to ask again after the delegate exists.
	if (!video)
		return nullptr;
	// Read straight off the item: videoSink is CONSTANT and the VideoOutput creates the sink in
	// its constructor, so this neither allocates nor changes any state. What comes back is the
	// item's own QQuickVideoSink, which re-emits videoFrameChanged on the GUI thread.
	return video->property("videoSink").value<QVideoSink*>();
}

void MascotSurface::setSlotVideoVisible(int i, bool visible)
{
	if (!this->isValidSlot(i))
		return;
	this->slotStates[i].videoVisible = visible;
	this->applySlot(i);
}

void MascotSurface::setSlotVideoOpacity(int i, int ab, double opacity, int crossfadeMs)
{
	// The same contract as setSlotOpacity(): a NaN fails the ordered comparison below and so
	// never reaches QML, and a value above 1 is clamped because Qt Quick clamps opacity itself
	// anyway -- the mirrored value should be the one the item ends up with. The selector is
	// checked here for the reason given in slotSink(): there is no third output to write.
	if (!this->isValidSlot(i) || (ab != 0 && ab != 1) || !(opacity >= 0.0))
		return;
	Slot& slot = this->slotStates[i];
	// QML hands this to a NumberAnimation as its duration, so a negative value is clamped away
	// rather than passed on. 0 already means what a negative duration would be asked to mean
	// (the hard cut), so nothing is lost by refusing to let a bad value through.
	slot.crossfadeMs = crossfadeMs > 0 ? crossfadeMs : 0;
	const double clamped = opacity > 1.0 ? 1.0 : opacity;
	if (ab == 0)
		slot.videoOpacityA = clamped;
	else
		slot.videoOpacityB = clamped;
	this->applySlot(i);
}

void MascotSurface::clearSlot(int i)
{
	if (!this->isValidSlot(i))
		return;
	Slot& slot = this->slotStates[i];
	slot.providerId.clear();
	slot.mediaIsImage = false;
	slot.nativeSize = QSize();
	slot.hasContentCenter = false;
	slot.contentCenterX = 0.0;
	slot.hasExplicitContentOffset = false;
	slot.explicitContentOffsetX = 0.0;
	// applySlot writes an empty source and mediaIsImage = false, which hides the image. The
	// rectangle and the other display properties are left alone: the slot itself still exists.
	this->applySlot(i);
}

void MascotSurface::clearSlots()
{
	for (int i = 0; i < this->slotStates.size(); ++i)
		this->clearSlot(i);
}

void MascotSurface::setMirror(bool mirror)
{
	this->mirror = mirror;
	// A bulk assignment over every slot, which is what makes the picture flip: the slot's
	// `mirrored` is the value MascotSlot.qml reads (Image.mirror) and the value applySlot()
	// derives the content shift from, so one write per slot keeps what is drawn and where it is
	// drawn in step. A setSlotMirrored() that arrives later overrides that one slot, and a slot
	// created later starts from this value (setSlotCount).
	for (int i = 0; i < this->slotStates.size(); ++i)
		this->slotStates[i].mirrored = mirror;
	this->applyAllSlots();
}

void MascotSurface::setSurfaceOpacity(double opacity)
{
	// The surface-wide value, as distinct from setSlotOpacity()'s per-slot one: the background
	// layer is one number for the whole picture behind the window, so it is written once on the
	// QML root item and inherited by the slots inside it, rather than repeated per slot or given
	// to a QGraphicsOpacityEffect -- which would composite the widget offscreen every frame for
	// a value the scene graph already applies for free.
	//
	// The clamp is setSlotOpacity()'s: a NaN fails the ordered comparison below like it fails
	// every comparison and so never reaches QML, and a value above 1 is clamped because Qt Quick
	// clamps opacity itself anyway -- the recorded notion and the item's value should agree.
	// There is no mirrored Slot value: an opacity is not slot state, and a delegate recreated by
	// a slot count change inherits the root item's value on its own.
	if (!(opacity >= 0.0) || !this->root)
		return;
	this->root->setProperty("opacity", opacity > 1.0 ? 1.0 : opacity);
}

void MascotSurface::mousePressEvent(QMouseEvent* event)
{
	// A press over a slot is consumed here for two reasons: Qt routes the matching release to
	// the widget that took the press, and the panel's host should not see half of a click pair
	// (a press whose release was swallowed by the slot). A press outside every slot goes to the
	// base implementation, whose default ignores it, so it still propagates like any other
	// widget's.
	if (this->slotAt(event->position()) >= 0) {
		event->accept();
		return;
	}
	QWidget::mousePressEvent(event);
}

void MascotSurface::mouseReleaseEvent(QMouseEvent* event)
{
	const int slot = this->slotAt(event->position());
	if (slot >= 0) {
		// A release over a slot belongs to that slot, so it is accepted and not passed on. The
		// base implementation is deliberately not called here: QWidget::mouseReleaseEvent()
		// ignores the event, which would undo the accept and hand the click to the host anyway.
		event->accept();
		emit clicked(slot, event->button(), event->modifiers());
		return;
	}
	QWidget::mouseReleaseEvent(event);
}

void MascotSurface::dragEnterEvent(QDragEnterEvent* event)
{
	QString path;
	if (singleLocalImagePath(event->mimeData(), &path))
		event->acceptProposedAction();
	else
		event->ignore();
}

void MascotSurface::dragMoveEvent(QDragMoveEvent* event)
{
	QString path;
	if (singleLocalImagePath(event->mimeData(), &path))
		event->acceptProposedAction();
	else
		event->ignore();
}

void MascotSurface::dropEvent(QDropEvent* event)
{
	const int slot = this->slotAt(event->position());
	QString path;
	if (slot < 0 || !singleLocalImagePath(event->mimeData(), &path)) {
		event->ignore();
		return;
	}
	event->acceptProposedAction();
	emit imageDropped(slot, path);
}

bool MascotSurface::isValidSlot(int i) const
{
	return i >= 0 && i < this->slotStates.size();
}

namespace {
	// Finds a QML item by object name through the *visual* hierarchy. QObject::findChild()
	// cannot do this: a QML item's QObject parent is not the item it is drawn inside, so
	// findChild() on the root never reaches a Repeater delegate even though childItems() shows
	// it. Names still carry the slot index (amendment O), so the lookup stays order-independent.
	QQuickItem* findItemByObjectName(QQuickItem* parent, const QString& name)
	{
		if (!parent)
			return nullptr;
		const QList<QQuickItem*> children = parent->childItems();
		for (QQuickItem* child : children) {
			if (child && child->objectName() == name)
				return child;
		}
		for (QQuickItem* child : children) {
			if (QQuickItem* found = findItemByObjectName(child, name))
				return found;
		}
		return nullptr;
	}
}

bool MascotSurface::resolveSlot(int i)
{
	if (!this->isValidSlot(i) || !this->root)
		return false;
	Slot& slot = this->slotStates[i];
	// Names, not child order (amendment O): a delegated item that QML recreated is found again
	// by the name that carries its index, so a sink can never be matched to the wrong
	// rectangle. A null QPointer means "not resolved yet" or "the delegate was destroyed",
	// and both cases resolve the same way.
	//
	// The visual walk comes first because that is where QML items live; findChild() is kept as
	// a fallback for a build that parents the delegate as a QObject child instead.
	if (slot.item.isNull()) {
		slot.item = findItemByObjectName(this->root, QStringLiteral("mascotSlot%1").arg(i));
		if (slot.item.isNull())
			slot.item = this->root->findChild<QQuickItem*>(QStringLiteral("mascotSlot%1").arg(i));
	}
	if (slot.item.isNull())
		return false;
	if (slot.image.isNull()) {
		slot.image = findItemByObjectName(slot.item, QStringLiteral("img%1").arg(i));
		if (slot.image.isNull())
			slot.image = slot.item->findChild<QQuickItem*>(QStringLiteral("img%1").arg(i));
	}
	// The two video outputs are found the same three ways and by the same index-carrying names,
	// which is what lets slotSink() answer a caller that only knows the slot number. Looking
	// them up here, rather than in that accessor, keeps the accessor const and makes every
	// delegate applySlot() has touched reachable for its sinks in the same call.
	if (slot.videoA.isNull()) {
		slot.videoA = findItemByObjectName(slot.item, QStringLiteral("videoA%1").arg(i));
		if (slot.videoA.isNull())
			slot.videoA = slot.item->findChild<QQuickItem*>(QStringLiteral("videoA%1").arg(i));
	}
	if (slot.videoB.isNull()) {
		slot.videoB = findItemByObjectName(slot.item, QStringLiteral("videoB%1").arg(i));
		if (slot.videoB.isNull())
			slot.videoB = slot.item->findChild<QQuickItem*>(QStringLiteral("videoB%1").arg(i));
	}
	return true;
}

void MascotSurface::applySlot(int i)
{
	if (!this->isValidSlot(i))
		return;
	// Resolve before taking the reference: resolving writes into the same Slot entry.
	if (!this->resolveSlot(i))
		return; // no delegate yet: the mirrored state is applied as soon as there is one
	Slot& slot = this->slotStates[i];
	QQuickItem* item = slot.item.data();
	QQuickItem* image = slot.image.data();

	item->setX(slot.rect.x());
	item->setY(slot.rect.y());
	item->setWidth(slot.rect.width());
	item->setHeight(slot.rect.height());
	item->setOpacity(slot.opacity);

	// One authoritative value: the slot's own flag, which setMirror() and setSlotMirrored() both
	// write. It flips the image (MascotSlot.qml reads it as Image.mirror) and it is the value
	// the content shift is derived from, so the two can never disagree.
	const bool mirrored = slot.mirrored;
	item->setProperty("mirrored", mirrored);
	item->setProperty("fillMode", qmlFillModeValue(slot.fillMode));
	// The height-normalised mode needs two values QML has no way to derive on its own without
	// repeating this arithmetic: the flag that selects it, and the width the media is painted
	// at. Both come from the one place that owns the geometry.
	// (M3 Task 3: WidthFit needs the same pair on the other axis, a flag and the painted height,
	// and HeightFit's two values are the ones written here before. One contentSizeFor() call
	// answers both axes, so no value is derived twice.)
	const QSizeF contentSize = this->contentSizeFor(slot);
	item->setProperty("heightFill", slot.fillMode == FillMode::HeightFit);
	item->setProperty("widthFill", slot.fillMode == FillMode::WidthFit);
	item->setProperty("paintedWidth", contentSize.width());
	item->setProperty("paintedHeight", contentSize.height());
	item->setProperty("mediaIsImage", slot.mediaIsImage);
	item->setProperty("contentOffsetX", slot.hasExplicitContentOffset
		? slot.explicitContentOffsetX
		: this->contentShiftFor(slot, mirrored));
	// The scroll driver's translation, kept apart from the centring above: QML adds the two, so
	// the centre stays this class's answer and a tick only ever moves the second term.
	item->setProperty("scrollOffsetX", slot.scrollOffsetX);
	item->setProperty("scrollOffsetY", slot.scrollOffsetY);

	// The video path's state, written from the slot for the same reason as everything above: a
	// delegate QML recreated after a slot count change comes back with the outputs the caller
	// already drove. QML owns the animating (one Behavior per opacity), so each of these is a
	// single target value and no animation state is kept on this side.
	//
	// The duration goes first on purpose: the Behavior reads crossfadeMs when the opacity write
	// arrives, and the normal call -- one half of a crossfade -- changes both at once. Writing
	// the opacity first would animate it with the previous duration, i.e. not at all.
	item->setProperty("crossfadeMs", slot.crossfadeMs);
	item->setProperty("videoVisible", slot.videoVisible);
	item->setProperty("videoOpacityA", slot.videoOpacityA);
	item->setProperty("videoOpacityB", slot.videoOpacityB);

	if (image) {
		// The id is already percent-encoded by MascotPrefetcher, and QUrl keeps a percent-escape
		// as written, so parsing it here does not change which cache key the provider sees.
		// Writing the same URL again is a no-op inside QQuickImage, so re-applying a slot never
		// costs another provider request.
		image->setProperty("source", slot.mediaIsImage ? QVariant::fromValue(QUrl(slot.providerId))
			: QVariant::fromValue(QUrl()));
	}
}

void MascotSurface::applyAllSlots()
{
	for (int i = 0; i < this->slotStates.size(); ++i)
		this->applySlot(i);
}

double MascotSurface::paintedWidthFor(const Slot& slot) const
{
	// M3 Task 3: the width half of contentSizeFor(), which is where the geometry is derived now.
	// The answer is the same expression it always was -- HeightFit's width in particular is
	// still nativeWidth * (slotHeight / nativeHeight) -- so the shift derived from it does not
	// move by a pixel.
	return this->contentSizeFor(slot).width();
}

QSizeF MascotSurface::contentSizeFor(const Slot& slot) const
{
	const double nativeWidth = static_cast<double>(slot.nativeSize.width());
	const double nativeHeight = static_cast<double>(slot.nativeSize.height());
	const double slotWidth = slot.rect.width();
	const double slotHeight = slot.rect.height();
	// No media, or nothing to scale by: fall back to the slot's width, which is what an empty
	// slot draws anyway.
	// (M3 Task 3: an empty slot's painted size is its whole rectangle, so the fallback answers
	// the height too; its width is still the slot's, which is the value this comment names.)
	if (nativeWidth <= 0.0 || nativeHeight <= 0.0 || slotWidth <= 0.0 || slotHeight <= 0.0)
		return QSizeF(slotWidth, slotHeight);

	switch (slot.fillMode) {
	case FillMode::Stretch:
		return QSizeF(slotWidth, slotHeight); // no aspect ratio to preserve
	case FillMode::HeightFit:
		// The old renderer's scaling: the height is the slot's, the width follows the aspect
		// ratio, so a narrow image keeps side margins and a wide one overflows horizontally.
		return QSizeF(nativeWidth * (slotHeight / nativeHeight), slotHeight);
	case FillMode::WidthFit:
		// HeightFit on the other axis: the width is the slot's and the height follows the
		// aspect ratio, so it is the vertical extent that overflows (and is clipped) -- the axis
		// a vertically stacked stripe travels along.
		return QSizeF(slotWidth, nativeHeight * (slotWidth / nativeWidth));
	case FillMode::Fit: {
		const double scale = std::min(slotWidth / nativeWidth, slotHeight / nativeHeight);
		return QSizeF(nativeWidth * scale, nativeHeight * scale);
	}
	case FillMode::Crop: {
		const double scale = std::max(slotWidth / nativeWidth, slotHeight / nativeHeight);
		return QSizeF(nativeWidth * scale, nativeHeight * scale);
	}
	}
	return QSizeF(slotWidth, slotHeight);
}

double MascotSurface::contentShiftFor(const Slot& slot, bool mirrored) const
{
	// contentCenterX is a position in the *source* image (mascotMediaUtils::contentCenterX
	// returns source pixels), so the slot's own scale converts it. The painted width follows
	// from the rectangle and the fill mode exactly as Qt Quick's Image computes it, and
	// mirrored content is drawn reversed inside the same painted area, which puts the content
	// centre the same distance the other side of the image's middle: hence the sign flip.
	//
	// Everything is gated on hasContentCenter: with no content above the alpha threshold there
	// is no centre to use, 0 leaves the fill mode's own centring in charge, and this is the
	// only place a double would ever be computed from the centre.
	if (!slot.hasContentCenter)
		return 0.0;
	const double nativeWidth = static_cast<double>(slot.nativeSize.width());
	const double nativeHeight = static_cast<double>(slot.nativeSize.height());
	const double slotWidth = slot.rect.width();
	const double slotHeight = slot.rect.height();
	if (nativeWidth <= 0.0 || nativeHeight <= 0.0 || slotWidth <= 0.0 || slotHeight <= 0.0)
		return 0.0;

	const double paintedWidth = this->paintedWidthFor(slot);
	if (paintedWidth <= 0.0)
		return 0.0;
	const double shift = paintedWidth / 2.0 - slot.contentCenterX * (paintedWidth / nativeWidth);
	return mirrored ? -shift : shift;
}

int MascotSurface::slotAt(const QPointF& position) const
{
	// The quick widget fills this widget with zero margins and sizes its root object to the
	// view, so a position in widget coordinates is a position in the QML root's coordinates and
	// matches the rectangles the driver set.
	for (int i = 0; i < this->slotStates.size(); ++i) {
		if (this->slotStates.at(i).rect.contains(position))
			return i;
	}
	return -1;
}
