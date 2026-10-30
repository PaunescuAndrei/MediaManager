#include "stdafx.h"
#include "MascotImageProvider.h"
#include "MascotImageCache.h"
#include <QMutexLocker>
#include <QUrl>

namespace {
	// A miss is logged once per id, which is only useful while the set stays small: the logging is
	// best-effort, and an id that keeps missing (a path whose file is gone, say) would grow the set
	// for the life of the process. Past this many distinct ids, new ones are no longer recorded --
	// the misses themselves still return a null image, only their lines stop.
	constexpr int MAX_LOGGED_MISS_IDS = 256;

	// The cache is keyed by the raw path (MascotPrefetcher::cacheIdForPath()), but which form of
	// the id Qt hands requestImage() is a Qt detail this class must not depend on: Qt decodes it
	// in the normal case (QUrl::path() percent-decodes), yet nothing promises it. Each candidate
	// form is therefore tried, so correctness holds either way (amendment M).
	QString decodedId(const QString& id)
	{
		return QUrl::fromPercentEncoding(id.toUtf8());
	}

	QString encodedId(const QString& id)
	{
		return QString::fromLatin1(QUrl::toPercentEncoding(id));
	}
}

MascotImageProvider::MascotImageProvider(MascotImageCache* cache)
	: QQuickImageProvider(QQuickImageProvider::Image), cache(cache)
{
}

QImage MascotImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
	Q_UNUSED(requestedSize);

	QImage image;
	if (this->cache) {
		// Step 1: the id as received -- the normal case, Qt already decoded it to the raw path
		// the cache is keyed by.
		image = this->cache->image(id);
		// Step 2: Qt handed the id over still percent-encoded, so the raw key is its decoding.
		if (image.isNull()) {
			const QString decoded = decodedId(id);
			if (decoded != id)
				image = this->cache->image(decoded);
		}
		// Step 3: the id needs re-encoding to match a key (a path holding a literal percent
		// escape, or a build that hands over an unescaped form of an escaped id).
		if (image.isNull()) {
			const QString encoded = encodedId(id);
			if (encoded != id)
				image = this->cache->image(encoded);
		}
	}

	if (image.isNull()) {
		{
			const QMutexLocker locker(&this->missLogMutex);
			if (this->loggedMissIds.size() < MAX_LOGGED_MISS_IDS && !this->loggedMissIds.contains(id)) {
				this->loggedMissIds.insert(id);
				qWarning() << "Mascot image provider cache miss for" << id;
			}
		}
		if (size)
			*size = QSize();
		return QImage();
	}

	// The returned image shares the cache entry's pixel buffer through QImage's atomic
	// refcount, so the GUI thread may evict that entry while the render thread still draws
	// this copy: the pixels stay alive until the last reference goes away.
	if (size)
		*size = image.size();
	return image;
}
