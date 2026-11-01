#pragma once
#include "colorPaletteExtractor.h"
#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QString>

// The GUI thread mutates it and the Qt Quick render thread reads it, so one mutex guards all state; image() is
// const while promoting the LRU because a hit is bookkeeping, not a change of content.
class MascotImageCache {
public:
	void setLimits(int maxItems, qint64 maxBytes);

	// The entry just inserted is never evicted for its own overage: an image larger than the whole budget must still be servable.
	void insert(const QString& id, const QImage& image, bool hasContentCenter, double contentCenterX,
		QList<color_area> accepted, QList<color_area> rejected);

	bool contains(const QString& id) const;

	QImage image(const QString& id) const;

	void pin(const QString& id, bool pinned);

	int itemCount() const;
	qint64 byteCount() const;
	int pinnedCount() const;

private:
	struct Entry {
		QImage image;
		bool hasContentCenter = false;
		double contentCenterX = 0.0;
		QList<color_area> acceptedColors;
		QList<color_area> rejectedColors;
		bool pinned = false;
	};

	void evictLocked(const QString& keepId);
	void reportOverageLocked() const;
	qint64 byteCountLocked() const;

	mutable QMutex cacheMutex;
	mutable QHash<QString, Entry> entries;
	mutable QList<QString> lruOrder; // most recently used first
	int maxItems = 0;
	qint64 maxBytes = 0;
};
