#include "stdafx.h"
#include "MascotImageCache.h"
#include <QMutexLocker>
#include <utility>

void MascotImageCache::setLimits(int maxItems, qint64 maxBytes)
{
	const QMutexLocker locker(&this->cacheMutex);
	this->maxItems = maxItems;
	this->maxBytes = maxBytes;
	this->evictLocked(QString());
}

void MascotImageCache::insert(const QString& id, const QImage& image, bool hasContentCenter, double contentCenterX,
	QList<color_area> accepted, QList<color_area> rejected)
{
	const QMutexLocker locker(&this->cacheMutex);

	Entry& entry = this->entries[id];
	entry.image = image;
	entry.hasContentCenter = hasContentCenter;
	entry.contentCenterX = contentCenterX;
	entry.acceptedColors = std::move(accepted);
	entry.rejectedColors = std::move(rejected);

	// `pinned` survives a re-insert: the slot that pinned the id still refers to it, and dropping the pin would let the
	// next insert evict an image that is on screen.
	this->lruOrder.removeOne(id);
	this->lruOrder.prepend(id);

	this->evictLocked(id);
}

bool MascotImageCache::contains(const QString& id) const
{
	const QMutexLocker locker(&this->cacheMutex);
	return this->entries.contains(id);
}

QImage MascotImageCache::image(const QString& id) const
{
	const QMutexLocker locker(&this->cacheMutex);
	const auto entry = this->entries.constFind(id);
	if (entry == this->entries.constEnd())
		return QImage();
	this->lruOrder.removeOne(id);
	this->lruOrder.prepend(id);
	return entry->image;
}

void MascotImageCache::pin(const QString& id, bool pinned)
{
	const QMutexLocker locker(&this->cacheMutex);
	const auto entry = this->entries.find(id);
	if (entry == this->entries.end())
		return;
	entry->pinned = pinned;
	if (!pinned)
		this->evictLocked(QString());
}

int MascotImageCache::itemCount() const
{
	const QMutexLocker locker(&this->cacheMutex);
	return static_cast<int>(this->entries.size());
}

qint64 MascotImageCache::byteCount() const
{
	const QMutexLocker locker(&this->cacheMutex);
	return this->byteCountLocked();
}

int MascotImageCache::pinnedCount() const
{
	const QMutexLocker locker(&this->cacheMutex);
	int pinned = 0;
	for (const Entry& entry : this->entries) {
		if (entry.pinned)
			++pinned;
	}
	return pinned;
}

qint64 MascotImageCache::byteCountLocked() const
{
	qint64 bytes = 0;
	for (const Entry& entry : this->entries)
		bytes += static_cast<qint64>(entry.image.sizeInBytes());
	return bytes;
}

void MascotImageCache::evictLocked(const QString& keepId)
{
	for (;;) {
		const bool overItems = this->maxItems > 0 && static_cast<int>(this->entries.size()) > this->maxItems;
		const bool overBytes = this->maxBytes > 0 && this->byteCountLocked() > this->maxBytes;
		if (!overItems && !overBytes)
			return;

		int victimIndex = -1;
		for (int i = this->lruOrder.size() - 1; i >= 0; --i) {
			const QString& candidate = this->lruOrder.at(i);
			if (candidate == keepId)
				continue;
			const auto entry = this->entries.constFind(candidate);
			if (entry == this->entries.constEnd() || entry->pinned)
				continue;
			victimIndex = i;
			break;
		}

		if (victimIndex < 0) {
			// Everything left is pinned or is the entry to keep, so one entry is larger than the whole budget: admit it,
			// otherwise the panel would have nothing to show.
			this->reportOverageLocked();
			return;
		}

		const QString victim = this->lruOrder.takeAt(victimIndex);
		this->entries.remove(victim);
	}
}

void MascotImageCache::reportOverageLocked() const
{
	static bool overageReported = false;
	if (overageReported)
		return;
	overageReported = true;

	qWarning() << "Mascot image cache holds" << this->entries.size() << "entries," << this->byteCountLocked()
		<< "bytes and is still over its limits (" << this->maxItems << "entries and" << this->maxBytes
		<< "bytes) with nothing evictable left; admitting the overage. Reported once per process.";
}
