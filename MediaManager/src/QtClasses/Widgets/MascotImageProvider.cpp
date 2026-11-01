#include "stdafx.h"
#include "MascotImageProvider.h"
#include "MascotImageCache.h"
#include <QMutexLocker>
#include <QUrl>

namespace {
	constexpr int MAX_LOGGED_MISS_IDS = 256;

	// The cache is keyed by the raw path, but which form Qt hands requestImage() is a Qt detail, not a promise.
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
		image = this->cache->image(id);
		if (image.isNull()) {
			const QString decoded = decodedId(id);
			if (decoded != id)
				image = this->cache->image(decoded);
		}
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

	// The returned image shares the cache entry's pixel buffer through QImage's refcount, so eviction on the GUI thread
	// cannot free pixels the render thread is still drawing.
	if (size)
		*size = image.size();
	return image;
}
