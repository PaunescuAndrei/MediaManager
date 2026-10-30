#include "stdafx.h"
#include "MascotPrefetcher.h"
#include "MascotImageCache.h"
#include "mascotMediaUtils.h"
#include "utils.h"
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>
#include <limits>
#include <utility>

namespace {
	// The alpha threshold mascotMediaUtils::contentCenterX() defaults to (30% alpha). The
	// bounds probe below and the centre calculation must agree on what counts as content,
	// otherwise hasContentCenter could disagree with the value it describes.
	constexpr int ALPHA_THRESHOLD = 77;
	// palette_extractor() arguments exactly as the retired generator's extractColors() used
	// them, so the replacement pipeline produces the palette the old one did.
	constexpr int PALETTE_CLUSTERS = 20;
	constexpr int PALETTE_TARGET_SIZE = 300;
	// Decode threads, never QThreadPool::globalInstance(): a burst of prefetches must not
	// occupy every core or delay unrelated work.
	constexpr int DECODE_THREAD_COUNT = 2;
	// Depth of the pull queue, matching the retired generator's queue depth of 10.
	// Overflow drops the oldest media, keeping the freshest: the dropped pixels are still in
	// the cache, only the queue entry is lost.
	constexpr int READY_QUEUE_LIMIT = 10;
	// The long edge a decoded mascot is clamped to (I1). A 4000x6000 PNG is ~96 MB of pixels
	// per panel, which fails in two different ways: above the driver's maxTextureSize the
	// texture is never created, so the slot draws nothing at all (the floor among the tiers the
	// spec names is 8192 -- FL10 / integrated / virtual machines -- with 16384 on FL11); and the
	// scale into the slot becomes one bilinear tap instead of master's
	// QPixmap::scaled(SmoothTransformation), which is the "different scaling" anti-goal.
	// 4096 stays well under that 8192 floor with headroom for both panels' textures at once, and
	// it is a fixed function of the file, so it is not part of the cache key (the key stays the
	// path: MascotPrefetcher::cacheIdForPath()).
	constexpr int MAX_DECODE_LONG_EDGE = 4096;

	// Scales `image` down until its long edge is MAX_DECODE_LONG_EDGE at most, aspect ratio
	// kept, smoothing like master's QPixmap::scaled(SmoothTransformation). An image already
	// inside the bound is returned unchanged, sharing its buffer: scaled() would copy it.
	QImage clampLongEdge(const QImage& image)
	{
		if (qMax(image.width(), image.height()) <= MAX_DECODE_LONG_EDGE)
			return image;
		// A square bounding size with KeepAspectRatio: Qt fits the image inside it, so the long
		// edge lands exactly on the bound and the short edge follows the aspect ratio.
		return image.scaled(QSize(MAX_DECODE_LONG_EDGE, MAX_DECODE_LONG_EDGE), Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
}

// The payload a decode worker produces. Kept out of the header: it is an implementation
// detail of how a worker hands its result back, not part of the consumer contract.
struct MascotPrefetcher::DecodeResult {
	QString path;
	QString providerId;
	QImage image;
	QSize nativeSize;
	bool hasContentCenter = false;
	double contentCenterX = 0.0;
	QList<color_area> acceptedColors;
	QList<color_area> rejectedColors;
	bool loaded = false;
};

MascotPrefetcher::MascotPrefetcher(MascotImageCache* cache, QObject* parent) : QObject(parent)
{
	this->cache = cache;
	this->decodePool = new QThreadPool(this);
	this->decodePool->setMaxThreadCount(DECODE_THREAD_COUNT);
	// MascotMedia travels through a queued signal, so the type has to be known to the
	// metatype system on both sides of that connection.
	qRegisterMetaType<MascotMedia>("MascotMedia");
}

MascotPrefetcher::~MascotPrefetcher()
{
	// Every worker captures `this`. Drop what has not started and wait for what has, so no
	// worker can still be running while the members it reads are destroyed. The wait is as
	// long as at most one decode and only happens at shutdown.
	this->decodePool->clear();
	this->decodePool->waitForDone();
}

void MascotPrefetcher::setLimits(int maxItems, qint64 maxBytes)
{
	if (!this->cache)
		return;
	this->cache->setLimits(maxItems, maxBytes);
}

void MascotPrefetcher::setExtractColors(bool extract)
{
	// No lock: the flag is written and read on the GUI thread only. request() hands a copy to
	// the worker, so a decode never touches this member.
	this->extractColors = extract;
}

void MascotPrefetcher::pin(const QString& path, bool pinned)
{
	if (!this->cache || path.isEmpty())
		return;
	// The id is derived here and nowhere else, which is why this call exists instead of the
	// consumer building the id itself.
	this->cache->pin(cacheIdForPath(path), pinned);
}

void MascotPrefetcher::request(const QString& path)
{
	if (!this->cache || path.isEmpty())
		return;

	const auto remembered = this->rememberedMedia.constFind(path);
	if (remembered != this->rememberedMedia.constEnd() && this->cache->contains(cacheIdForPath(path))) {
		// Already decoded and its pixels are still cached: hand the media back instead of
		// decoding again. Without this a consumer that revisits a path would get no
		// mediaReady at all, because the pixels are in the cache but the decoration
		// (content centre, palette) is not exposed by the cache.
		//
		// Posted rather than emitted so request() never runs a consumer's slot code
		// synchronously, which means the announcement has to survive the event-loop turn:
		// it carries the generation it was made in, exactly like a decoded result, so a
		// clear() in the same turn drops it instead of resurrecting a cleared slot.
		const MascotMedia media = *remembered;
		const quint64 republishGeneration = this->generation.load();
		QMetaObject::invokeMethod(this, [this, media, republishGeneration]() {
			if (republishGeneration != this->generation.load())
				return;
			// An insert between the post and this turn may have evicted the pixels; never
			// announce an id the provider cannot serve. This is the only publish path that
			// can be separated from its cache entry by a turn boundary, because
			// handleDecoded() inserts and publishes in one turn and eviction protects the
			// entry it just added.
			if (!this->cache->contains(cacheIdForPath(media.path)))
				return;
			this->publish(media);
		}, Qt::QueuedConnection);
		return;
	}

	if (this->pendingPaths.contains(path))
		return;

	this->pendingPaths.insert(path);
	const quint64 requestGeneration = this->generation.load();
	const QString providerId = providerIdForPath(path);
	// Captured on this thread, with the generation: decode() runs on a worker and must not read
	// a member of this object (I2).
	const bool extractColors = this->extractColors;
	this->decodePool->start(QRunnable::create([this, path, providerId, requestGeneration, extractColors]() {
		// Worker thread. The only shared state read here is the atomic generation, so a
		// clear() on the GUI thread can cancel a decode that has not started yet.
		if (requestGeneration != this->generation.load())
			return;
		DecodeResult result = decode(path, providerId, extractColors);
		QMetaObject::invokeMethod(this, [this, result = std::move(result), requestGeneration]() {
			this->handleDecoded(std::move(result), requestGeneration);
		}, Qt::QueuedConnection);
	}));
}

bool MascotPrefetcher::isPending(const QString& path) const
{
	return this->pendingPaths.contains(path);
}

MascotMedia MascotPrefetcher::takeReady()
{
	if (this->readyQueue.isEmpty())
		return MascotMedia();
	return this->readyQueue.takeFirst();
}

void MascotPrefetcher::clear()
{
	// Invalidating the generation is what makes clear() safe: work that is still in flight
	// belongs to the old generation, so both completion paths drop it on arrival -- the
	// decoded result in handleDecoded() and the queued republication posted by request().
	// Neither can repopulate a slot the caller has just cleared. Bumping the counter before
	// dropping the queue keeps the two consistent from the GUI thread's point of view.
	this->generation.fetch_add(1);
	this->decodePool->clear();
	this->pendingPaths.clear();
	this->readyQueue.clear();
}

MascotPrefetcher::DecodeResult MascotPrefetcher::decode(const QString& path, const QString& providerId,
	bool extractColors)
{
	DecodeResult result;
	result.path = path;
	result.providerId = providerId;

	// Everything below is the work this class exists to keep off the GUI thread: file
	// decode, alpha centering (a two-pass distance transform) and k-means palette
	// extraction. This function only ever runs on a decode worker. `extractColors` is a
	// parameter rather than a member for the same reason the function is static.
	const QPixmap pixmap = utils::openImage(path);
	if (pixmap.isNull())
		return result;
	const QImage decoded = pixmap.toImage();
	if (decoded.isNull() || decoded.width() <= 0 || decoded.height() <= 0)
		return result;

	// I1: clamp before a single pixel is measured, so the centre below is expressed in the same
	// pixel space as the image that is actually drawn and uploaded as a texture. A file already
	// within the bound is used as decoded.
	const bool clamped = decoded.width() > MAX_DECODE_LONG_EDGE || decoded.height() > MAX_DECODE_LONG_EDGE;
	const QImage image = clamped ? clampLongEdge(decoded) : decoded;
	if (image.isNull() || image.width() <= 0 || image.height() <= 0)
		return result;

	result.image = image;
	result.nativeSize = image.size();

	// findAlphaBoundsX() fails exactly when contentCenterX() would return NaN, and unlike an
	// isnan test it stays correct whatever the folded-float settings are, so it decides
	// hasContentCenter (see MascotMedia.h). Consumers key off that bool.
	int minX = 0;
	int maxX = 0;
	result.hasContentCenter = mascotMediaUtils::findAlphaBoundsX(image, ALPHA_THRESHOLD, &minX, &maxX);
	const double centerX = mascotMediaUtils::contentCenterX(image, ALPHA_THRESHOLD);
	result.contentCenterX = result.hasContentCenter ? centerX : std::numeric_limits<double>::quiet_NaN();

	// I2: skipped entirely while `mascots_color_theme` is off, which is its configured default.
	// Both lists then stay empty, which is the "no palette" state MascotController::colorFor()
	// already degrades from (an invalid QColor the theme-colour handler ignores).
	if (extractColors) {
		// palette_extractor() takes a QPixmap, so a clamped file pays for one conversion back.
		const QPixmap paletteSource = clamped ? QPixmap::fromImage(image) : pixmap;
		const QPair<QList<color_area>, QList<color_area>> palette =
			palette_extractor(paletteSource, PALETTE_CLUSTERS, PALETTE_TARGET_SIZE);
		result.acceptedColors = palette.first;
		result.rejectedColors = palette.second;
	}

	result.loaded = true;
	return result;
}

void MascotPrefetcher::handleDecoded(DecodeResult result, quint64 requestGeneration)
{
	// Queued back from the worker, so this runs on the GUI thread: the only thread that
	// touches pendingPaths, rememberedMedia, the pull queue, the cache and the signals.
	if (requestGeneration != this->generation.load())
		return; // clear() ran while this decode was in flight: never publish it
	this->pendingPaths.remove(result.path);

	if (!result.loaded) {
		// Once per path: a directory holding a video would otherwise log on every swap.
		if (!this->failedPaths.contains(result.path)) {
			this->failedPaths.insert(result.path);
			qWarning() << "Mascot prefetcher could not decode" << result.path;
		}
		emit mediaFailed(result.path);
		return;
	}

	MascotMedia media;
	media.type = MascotMedia::Image;
	media.path = result.path;
	media.providerId = result.providerId;
	media.nativeSize = result.nativeSize;
	media.hasContentCenter = result.hasContentCenter;
	media.contentCenterX = result.contentCenterX;
	media.acceptedColors = result.acceptedColors;
	media.rejectedColors = result.rejectedColors;
	media.startOffsetMs = 0;

	this->rememberedMedia.insert(media.path, media);
	// Insert before announcing: the provider may be asked for this id the moment a consumer
	// reacts to mediaReady, so the pixels have to be servable by then.
	this->cache->insert(cacheIdForPath(media.path), result.image, media.hasContentCenter, media.contentCenterX,
		media.acceptedColors, media.rejectedColors);
	this->pruneUncachedMedia();
	this->publish(media);
}

void MascotPrefetcher::publish(const MascotMedia& media)
{
	if (this->readyQueue.size() >= READY_QUEUE_LIMIT)
		this->readyQueue.removeFirst();
	this->readyQueue.append(media);
	emit mediaReady(media);
}

void MascotPrefetcher::pruneUncachedMedia()
{
	// Remember at most what the cache still holds: once an image is evicted the provider can
	// no longer serve its id, so its remembered media would only be republished into a slot
	// that cannot render it.
	for (auto it = this->rememberedMedia.begin(); it != this->rememberedMedia.end(); ) {
		if (this->cache->contains(cacheIdForPath(it.key())))
			++it;
		else
			it = this->rememberedMedia.erase(it);
	}
}

QString MascotPrefetcher::providerIdForPath(const QString& path)
{
	// The URL keeps the percent-encoded form: Qt parses this as a URL, so spaces, '#', '?' and
	// non-ASCII names have to be escaped here. This form is a URL, not a key -- the cache is
	// keyed by the raw path (see cacheIdForPath()).
	return QStringLiteral("image://mascots/") + QString::fromLatin1(QUrl::toPercentEncoding(path));
}

QString MascotPrefetcher::cacheIdForPath(const QString& path)
{
	// The cache is keyed by the raw path. Qt decodes the id before calling
	// QQuickImageProvider::requestImage() -- QUrl::path() percent-decodes -- so the provider is
	// handed the path itself, and that is the form it has to find in the cache. Percent-encoding
	// belongs to the URL only, in providerIdForPath().
	return path;
}
