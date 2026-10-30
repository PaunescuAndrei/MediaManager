#include "stdafx.h"
#include "MascotImageProvider.h"
#include "MascotImageCache.h"
#include <QMutexLocker>
#include <QUrl>

namespace {
	// The id MascotPrefetcher produces is percent-encoded (that is the cache key), but whether
	// Qt 6.12 hands requestImage() the encoded or the decoded form is a Qt detail this class
	// must not depend on: both are tried, so correctness holds either way (amendment M).
	QString decodedId(const QString& id)
	{
		return QUrl::fromPercentEncoding(id.toUtf8());
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
	}

	if (image.isNull()) {
		{
			const QMutexLocker locker(&this->missLogMutex);
			if (!this->loggedMissIds.contains(id)) {
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
