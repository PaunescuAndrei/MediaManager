#include "stdafx.h"
#include "MascotSurface.h"
#include "MascotImageProvider.h"
#include "OverlayGraphicsVideoWidget.h"
#include <QDir>
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
		}
		return 1;
	}
}

MascotSurface::MascotSurface(MascotImageCache* cache, QWidget* parent) : QWidget(parent)
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
	this->quick->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	this->quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
	this->quick->setSource(QUrl(QStringLiteral("qrc:/qml/MascotSurface.qml")));

	if (this->quick->status() == QQuickWidget::Error) {
		const QList<QQmlError> errors = this->quick->errors();
		for (const QQmlError& error : errors)
			qWarning() << "MascotSurface.qml:" << error.toString();
	}

	this->root = this->quick->rootObject();
	if (this->root)
		this->root->setProperty("mirrored", this->mirror);

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
	// Shrink first, then let QML destroy: the Slot entries (and the QPointers inside them) go
	// away before the Repeater deletes the delegate they refer to, so nothing here can outlive
	// an item. Growing is the same order: the Repeater creates the delegates during the
	// property write below, and applyAllSlots() then resolves them and re-applies the state
	// each slot already carries.
	this->slotStates.resize(n);
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

void MascotSurface::setSlotContentOffset(int i, const QPointF& offset)
{
	if (!this->isValidSlot(i) || !isSaneExtent(offset.x()))
		return;
	this->slotStates[i].hasExplicitContentOffset = true;
	this->slotStates[i].explicitContentOffsetX = offset.x();
	this->applySlot(i);
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
	// The root property is written for the QML side to read, and every slot is re-applied
	// because the content shift depends on the effective mirror (a mirrored image's content
	// centre ends up on the other side of the painted area).
	if (this->root)
		this->root->setProperty("mirrored", mirror);
	this->applyAllSlots();
}

void MascotSurface::mouseReleaseEvent(QMouseEvent* event)
{
	const int slot = this->slotAt(event->position());
	if (slot >= 0) {
		// A release over a slot belongs to that slot: accepting it keeps the click from also
		// reaching whatever this panel is embedded in, which is how the retired renderer's
		// click signal behaved too.
		event->accept();
		emit clicked(slot, event->button(), event->modifiers());
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

bool MascotSurface::resolveSlot(int i)
{
	if (!this->isValidSlot(i) || !this->root)
		return false;
	Slot& slot = this->slotStates[i];
	// Names, not child order (amendment O): a delegated item that QML recreated is found again
	// by the name that carries its index, so a sink can never be matched to the wrong
	// rectangle. A null QPointer means "not resolved yet" or "the delegate was destroyed",
	// and both cases resolve the same way.
	if (slot.item.isNull())
		slot.item = this->root->findChild<QQuickItem*>(QStringLiteral("mascotSlot%1").arg(i));
	if (slot.item.isNull())
		return false;
	if (slot.image.isNull())
		slot.image = slot.item->findChild<QQuickItem*>(QStringLiteral("img%1").arg(i));
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

	const bool mirrored = this->mirror || slot.mirrored;
	item->setProperty("mirrored", mirrored);
	item->setProperty("fillMode", qmlFillModeValue(slot.fillMode));
	item->setProperty("mediaIsImage", slot.mediaIsImage);
	item->setProperty("contentOffsetX", slot.hasExplicitContentOffset
		? slot.explicitContentOffsetX
		: this->contentShiftFor(slot, mirrored));

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

	double paintedWidth = slotWidth; // FillMode::Stretch: no aspect ratio to preserve
	if (slot.fillMode != FillMode::Stretch) {
		const double fitScale = std::min(slotWidth / nativeWidth, slotHeight / nativeHeight);
		const double cropScale = std::max(slotWidth / nativeWidth, slotHeight / nativeHeight);
		paintedWidth = nativeWidth * (slot.fillMode == FillMode::Fit ? fitScale : cropScale);
	}
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
