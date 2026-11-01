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
	// The same threshold contentCenterX() defaults to: the bounds probe and the centre must agree on what counts
	// as content, or hasContentCenter could disagree with the value it describes.
	constexpr int ALPHA_THRESHOLD = 77;
	constexpr int PALETTE_CLUSTERS = 20;
	constexpr int PALETTE_TARGET_SIZE = 300;
	// Decode threads, never QThreadPool::globalInstance(): a burst of prefetches must not occupy every core.
	constexpr int DECODE_THREAD_COUNT = 2;
	// The driver floor is 8192 texels (FL10/integrated), and 4096 leaves headroom for both panels' textures while
	// staying a fixed function of the file -- which is why it is not part of the cache key.
	constexpr int MAX_DECODE_LONG_EDGE = 4096;

	QImage clampLongEdge(const QImage& image)
	{
		if (qMax(image.width(), image.height()) <= MAX_DECODE_LONG_EDGE)
			return image;
		return image.scaled(QSize(MAX_DECODE_LONG_EDGE, MAX_DECODE_LONG_EDGE), Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
}

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
	// MascotMedia travels through a queued signal, so the metatype must be known on both sides of the connection.
	qRegisterMetaType<MascotMedia>("MascotMedia");
}

MascotPrefetcher::~MascotPrefetcher()
{
	// Every worker captures `this`: drop what has not started and wait for the rest before the members it reads are destroyed.
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
	this->extractColors = extract;
}

void MascotPrefetcher::pin(const QString& path, bool pinned)
{
	if (!this->cache || path.isEmpty())
		return;
	this->cache->pin(cacheIdForPath(path), pinned);
}

void MascotPrefetcher::request(const QString& path)
{
	if (!this->cache || path.isEmpty())
		return;

	const auto remembered = this->rememberedMedia.constFind(path);
	// Already decoded and still cached: hand the media back rather than decode again. Posted rather than emitted, so
	// request() never runs a consumer's slot synchronously, and it carries the generation so clear() drops it.
	if (remembered != this->rememberedMedia.constEnd() && this->cache->contains(cacheIdForPath(path))) {
		const MascotMedia media = *remembered;
		const quint64 republishGeneration = this->generation.load();
		QMetaObject::invokeMethod(this, [this, media, republishGeneration]() {
			if (republishGeneration != this->generation.load())
				return;
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
	const bool extractColors = this->extractColors;
	// Captured on this thread with the generation: decode() runs on a worker and must not read a member of this object.
	this->decodePool->start(QRunnable::create([this, path, providerId, requestGeneration, extractColors]() {
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

void MascotPrefetcher::clear()
{
	// Work still in flight belongs to the old generation, so both completion paths drop it on arrival.
	this->generation.fetch_add(1);
	this->decodePool->clear();
	this->pendingPaths.clear();
}

MascotPrefetcher::DecodeResult MascotPrefetcher::decode(const QString& path, const QString& providerId,
	bool extractColors)
{
	DecodeResult result;
	result.path = path;
	result.providerId = providerId;

	const QPixmap pixmap = utils::openImage(path);
	if (pixmap.isNull())
		return result;
	const QImage decoded = pixmap.toImage();
	if (decoded.isNull() || decoded.width() <= 0 || decoded.height() <= 0)
		return result;

	// Clamp before a single pixel is measured, so the centre is expressed in the pixel space that is drawn and uploaded.
	const bool clamped = decoded.width() > MAX_DECODE_LONG_EDGE || decoded.height() > MAX_DECODE_LONG_EDGE;
	const QImage image = clamped ? clampLongEdge(decoded) : decoded;
	if (image.isNull() || image.width() <= 0 || image.height() <= 0)
		return result;

	result.image = image;
	result.nativeSize = image.size();

	// The bounds probe decides hasContentCenter: it stays correct under folded-float settings, unlike an isnan test.
	int minX = 0;
	int maxX = 0;
	result.hasContentCenter = mascotMediaUtils::findAlphaBoundsX(image, ALPHA_THRESHOLD, &minX, &maxX);
	const double centerX = mascotMediaUtils::contentCenterX(image, ALPHA_THRESHOLD);
	result.contentCenterX = result.hasContentCenter ? centerX : std::numeric_limits<double>::quiet_NaN();

	// Skipped while the palette switch is off: both lists stay empty, which is the "no palette" state colorFor() degrades from.
	if (extractColors) {
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
	// Insert before announcing: the provider may be asked for this id the moment a consumer reacts to mediaReady.
	this->cache->insert(cacheIdForPath(media.path), result.image, media.hasContentCenter, media.contentCenterX,
		media.acceptedColors, media.rejectedColors);
	this->pruneUncachedMedia();
	this->publish(media);
}

void MascotPrefetcher::publish(const MascotMedia& media)
{
	emit mediaReady(media);
}

void MascotPrefetcher::pruneUncachedMedia()
{
	for (auto it = this->rememberedMedia.begin(); it != this->rememberedMedia.end(); ) {
		if (this->cache->contains(cacheIdForPath(it.key())))
			++it;
		else
			it = this->rememberedMedia.erase(it);
	}
}

QString MascotPrefetcher::providerIdForPath(const QString& path)
{
	// This form is a URL, not a key: spaces, '#' and non-ASCII names have to be escaped here, while the cache is keyed by the raw path.
	return QStringLiteral("image://mascots/") + QString::fromLatin1(QUrl::toPercentEncoding(path));
}

QString MascotPrefetcher::cacheIdForPath(const QString& path)
{
	// Qt percent-decodes the id before requestImage() (QUrl::path()), so the provider is handed the raw path.
	return path;
}
