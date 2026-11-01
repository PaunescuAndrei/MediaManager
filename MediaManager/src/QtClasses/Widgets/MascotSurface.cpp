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
	// Ordered comparisons, not std::isfinite: /fp:fast may fold a NaN test away, while a NaN compares
	// false against every bound -- so a NaN rect or opacity is rejected by construction.
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

}

MascotSurface::MascotSurface(MascotImageCache* cache, QWidget* parent, bool alwaysStackOnTop)
	: QWidget(parent)
{
	this->setAttribute(Qt::WA_NoSystemBackground, true);
	this->setAutoFillBackground(false);
	// Drops land here, not on the quick widget: a drag walks up the parent chain, and the quick widget is
	// transparent to the mouse so releases reach this widget too.
	this->setAcceptDrops(true);

	QQmlEngine* engine = OverlayGraphicsVideoWidget::previewEngine();
	// One provider per engine: the engine owns it, so a second surface must not replace the first's. Registering
	// before setSource() is what lets the root QML resolve image://mascots/... while it is being created.
	if (!engine->imageProvider(QStringLiteral("mascots")))
		engine->addImageProvider(QStringLiteral("mascots"), new MascotImageProvider(cache));

	this->quick = new QQuickWidget(engine, this);
	this->quick->setClearColor(Qt::transparent);
	// Without this the quick widget blits its offscreen buffer opaquely, turning every transparent pixel of a
	// mascot PNG black instead of showing the window behind it.
	this->setAttribute(Qt::WA_TranslucentBackground, true);
	this->quick->setAttribute(Qt::WA_TranslucentBackground, true);
	if (alwaysStackOnTop) {
		// Draw last so this widget's alpha composites over the window; verified at runtime, without it the panel reads #000000.
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
	// Shrink before QML destroys: the Slot entries and their QPointers go before the Repeater deletes the delegate.
	this->slotStates.resize(n);
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

void MascotSurface::setSlotGroup(int i, SlotGroup group)
{
	if (!this->isValidSlot(i))
		return;
	this->slotStates[i].group = group;
	this->applySlot(i);
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
	if (!this->isValidSlot(i) || !isSaneExtent(dx) || !isSaneExtent(dy))
		return;
	Slot& slot = this->slotStates[i];
	const bool xMoved = slot.scrollOffsetX != dx;
	const bool yMoved = slot.scrollOffsetY != dy;
	slot.scrollOffsetX = dx;
	slot.scrollOffsetY = dy;
	// Two property writes, not an applySlot(): a tick moves the content and changes nothing else, and the per-tick
	// budget is a few writes, not a whole slot re-apply.
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

void MascotSurface::setSlotMirrorSource(int i, int sourceSlot)
{
	if (!this->isValidSlot(i))
		return;
	this->slotStates[i].mirrorSourceSlot = sourceSlot;
	this->applySlot(i);
}

int MascotSurface::slotMirrorSource(int i) const
{
	if (!this->isValidSlot(i))
		return -1;
	return this->slotStates.at(i).mirrorSourceSlot;
}

void MascotSurface::setSlotMedia(int i, const MascotMedia& media)
{
	if (!this->isValidSlot(i))
		return;
	Slot& slot = this->slotStates[i];
	slot.hasExplicitContentOffset = false;
	slot.explicitContentOffsetX = 0.0;
	slot.providerId = media.providerId;
	slot.mediaIsImage = media.type == MascotMedia::Image && !media.providerId.isEmpty();
	slot.nativeSize = media.nativeSize;
	slot.hasContentCenter = media.hasContentCenter && isSaneExtent(media.contentCenterX);
	slot.contentCenterX = slot.hasContentCenter ? media.contentCenterX : 0.0;
	this->applySlot(i);
}

QVideoSink* MascotSurface::slotSink(int i, int ab)
{
	if (!this->isValidSlot(i) || (ab != 0 && ab != 1))
		return nullptr;
	// Resolve here: applySlot() is the only other filler of these pointers, and a delegate QML creates after it leaves
	// them null. resolveSlot() is idempotent, so a settled slot costs four null checks.
	this->resolveSlot(i);
	const Slot& slot = this->slotStates.at(i);
	QQuickItem* video = (ab == 0 ? slot.videoA : slot.videoB).data();
	// A delegate that does not exist yet has no item and no sink; the caller is told to ask again after it exists.
	if (!video)
		return nullptr;
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
	if (!this->isValidSlot(i) || (ab != 0 && ab != 1) || !(opacity >= 0.0))
		return;
	Slot& slot = this->slotStates[i];
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
	this->applySlot(i);
}

void MascotSurface::setMirror(bool mirror)
{
	// Only the default for slots created later. Writing it over the live slots would undo the per-slot parity the
	// controller has already decided, and a slot that shows another band's stage has no media commit to restore it.
	this->mirror = mirror;
}

void MascotSurface::setSurfaceOpacity(double opacity)
{
	// Applied to the layer group's slots only: the root's opacity would take the veil and the columns with it.
	if (!(opacity >= 0.0))
		return;
	this->contentFade = opacity > 1.0 ? 1.0 : opacity;
	for (int i = 0; i < this->slotStates.size(); ++i) {
		if (this->slotStates.at(i).group == SlotGroup::Layer)
			this->applySlot(i);
	}
}

void MascotSurface::setVeil(const QColor& colour, double opacity, bool enabled, bool aboveColumns)
{
	if (!this->root || !(opacity >= 0.0))
		return;
	this->root->setProperty("veilColor", colour);
	this->root->setProperty("veilOpacity", opacity > 1.0 ? 1.0 : opacity);
	this->root->setProperty("veilVisible", enabled);
	this->root->setProperty("veilZ", aboveColumns ? 1 : 3);
}


void MascotSurface::setBackdrop(const QColor& colour, bool enabled)
{
	if (!this->root)
		return;
	this->root->setProperty("backdropColor", colour);
	this->root->setProperty("backdropVisible", enabled);
}

void MascotSurface::mousePressEvent(QMouseEvent* event)
{
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
		// Not the base implementation: QWidget::mouseReleaseEvent() ignores the event, which would undo the accept and
		// hand the click to the host anyway.
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
	// QObject::findChild() cannot reach a Repeater delegate: a QML item's QObject parent is not the item it is drawn
	// inside, so the lookup walks childItems() instead.
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
	// Names, not child order: a delegate QML recreated is found again by the name that carries its index, so a sink
	// can never be matched to the wrong rectangle.
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
	if (slot.stage.isNull()) {
		slot.stage = findItemByObjectName(slot.item, QStringLiteral("stage%1").arg(i));
		if (slot.stage.isNull())
			slot.stage = slot.item->findChild<QQuickItem*>(QStringLiteral("stage%1").arg(i));
	}
	return true;
}

void MascotSurface::applySlot(int i)
{
	if (!this->isValidSlot(i))
		return;
	if (!this->resolveSlot(i))
		return; // no delegate yet: the mirrored state is applied as soon as there is one
	Slot& slot = this->slotStates[i];
	QQuickItem* item = slot.item.data();
	QQuickItem* image = slot.image.data();

	item->setX(slot.rect.x());
	item->setY(slot.rect.y());
	item->setWidth(slot.rect.width());
	item->setHeight(slot.rect.height());
	item->setOpacity(slot.group == SlotGroup::Layer ? slot.opacity * this->contentFade : slot.opacity);
	item->setZ(slot.group == SlotGroup::Layer ? 0.0 : 2.0);

	const bool mirrored = slot.mirrored;
	item->setProperty("mirrored", mirrored);
	const QSizeF contentSize = this->contentSizeFor(slot);
	// Every mode is drawn as its painted box, stretched onto it: that box already carries the picture's aspect (or is
	// the slot itself for Stretch), so stretching never distorts it, and an item larger than the slot -- which is what
	// Cover and the panning modes produce -- is clipped by the slot while still being something the pan can slide.
	item->setProperty("paintedWidth", contentSize.width());
	item->setProperty("paintedHeight", contentSize.height());
	const QSize nativeSize = slot.nativeSize;
	const bool minifying = nativeSize.width() > 0 && nativeSize.height() > 0
		&& (contentSize.width() < static_cast<double>(nativeSize.width()) - 0.5
			|| contentSize.height() < static_cast<double>(nativeSize.height()) - 0.5);
	item->setProperty("minifying", minifying);
	item->setProperty("mediaIsImage", slot.mediaIsImage);
	item->setProperty("contentOffsetX", slot.hasExplicitContentOffset
		? slot.explicitContentOffsetX
		: this->contentShiftFor(slot, mirrored));
	item->setProperty("scrollOffsetX", slot.scrollOffsetX);
	item->setProperty("scrollOffsetY", slot.scrollOffsetY);

	// The duration goes first on purpose: the Behavior reads crossfadeMs when the opacity write arrives, and writing
	// the opacity first would animate it with the previous duration, i.e. not at all.
	item->setProperty("crossfadeMs", slot.crossfadeMs);
	item->setProperty("videoVisible", slot.videoVisible);
	item->setProperty("videoOpacityA", slot.videoOpacityA);
	item->setProperty("videoOpacityB", slot.videoOpacityB);

	// The mirror view samples the other slot's stage, so the QML item is resolved here rather than held: the delegate
	// that owns the stage can be recreated, and a stale pointer would draw nothing.
	QQuickItem* mirrorSource = nullptr;
	if (slot.mirrorSourceSlot >= 0 && this->isValidSlot(slot.mirrorSourceSlot)) {
		this->resolveSlot(slot.mirrorSourceSlot);
		mirrorSource = this->slotStates.at(slot.mirrorSourceSlot).stage.data();
	}
	item->setProperty("mirrorSource", QVariant::fromValue<QQuickItem*>(mirrorSource));


	if (image) {
		// The id is already percent-encoded, and writing the same URL again is a no-op inside QQuickImage.
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
	return this->contentSizeFor(slot).width();
}

QSizeF MascotSurface::contentSizeFor(const Slot& slot) const
{
	const double nativeWidth = static_cast<double>(slot.nativeSize.width());
	const double nativeHeight = static_cast<double>(slot.nativeSize.height());
	const double slotWidth = slot.rect.width();
	const double slotHeight = slot.rect.height();
	if (nativeWidth <= 0.0 || nativeHeight <= 0.0 || slotWidth <= 0.0 || slotHeight <= 0.0)
		return QSizeF(slotWidth, slotHeight);

	switch (slot.fillMode) {
	case FillMode::Stretch:
		return QSizeF(slotWidth, slotHeight); // no aspect ratio to preserve
	case FillMode::HeightFit:
		return QSizeF(nativeWidth * (slotHeight / nativeHeight), slotHeight);
	case FillMode::WidthFit:
		return QSizeF(slotWidth, nativeHeight * (slotWidth / nativeWidth));
	case FillMode::Fit: {
		const double scale = std::min(slotWidth / nativeWidth, slotHeight / nativeHeight);
		return QSizeF(nativeWidth * scale, nativeHeight * scale);
	}
	case FillMode::Crop:
	case FillMode::Cover: {
		// Both are the same box: the picture scaled until it covers the slot, exact on one axis and larger on the
		// other. What separates them is what the controller does with that overflow -- nothing, or the pan.
		const double scale = std::max(slotWidth / nativeWidth, slotHeight / nativeHeight);
		return QSizeF(nativeWidth * scale, nativeHeight * scale);
	}
	}
	return QSizeF(slotWidth, slotHeight);
}

double MascotSurface::contentShiftFor(const Slot& slot, bool mirrored) const
{
	if (!slot.hasContentCenter)
		return 0.0;
	// contentCenterX is in source pixels, so the slot's own scale converts it; mirrored content is drawn reversed
	// inside the same painted area, which is the sign flip.
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
	// The shift is clamped to the crop's slack: a larger one slides an edge into view, which is the "does not cover"
	// report, and a shift that fits still centres the content.
	const double slack = (paintedWidth - slotWidth) / 2.0;
	const double clamped = slack > 0.0 ? std::min(shift, slack) : 0.0;
	const double bounded = clamped < 0.0 ? std::max(clamped, -slack) : clamped;
	return mirrored ? -bounded : bounded;
}

int MascotSurface::slotAt(const QPointF& position) const
{
	// The quick widget fills this widget with zero margins and sizes its root to the view, so widget coordinates are
	// the QML root's coordinates the rectangles were set in.
	for (int i = 0; i < this->slotStates.size(); ++i) {
		if (this->slotStates.at(i).rect.contains(position))
			return i;
	}
	return -1;
}
