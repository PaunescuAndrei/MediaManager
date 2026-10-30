#pragma once
#include "colorPaletteExtractor.h"
#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QString>

// Dual-bounded (entries and bytes) LRU cache of decoded mascot images plus the decoration
// their QML slot needs.
//
// Two threads reach it:
//   - the GUI thread mutates it: MascotPrefetcher inserts here after a decode worker hands
//     its result back, and that same thread is where mediaReady is emitted from;
//   - the Qt Quick render thread reads it through QQuickImageProvider::requestImage, which
//     Qt does not promise to run on the GUI thread.
// One mutex therefore guards all state. It is also why image() is const while still
// reordering the LRU: a cache hit promotes the entry, which is a bookkeeping write, not an
// observable change of the image content.
//
// Ids are the **raw** paths, not their percent-encoded form: MascotPrefetcher::cacheIdForPath()
// returns the path unchanged and is the only place a path becomes an id. Qt percent-decodes the
// id it parses out of image://mascots/... before calling QQuickImageProvider::requestImage(), so
// the id that arrives is the path itself -- but that is a Qt detail, not a promise, so the
// provider normalises whatever it is handed (as received, then percent-decoded, then
// percent-encoded) instead of assuming one form (MascotImageProvider::requestImage()).
// Percent-encoding belongs to the URL alone, which is what MascotPrefetcher::providerIdForPath()
// builds.
class MascotImageCache {
public:
	// A non-positive bound means "no bound on that axis", so a cache that was never
	// configured cannot evict everything on first use. Task 4 supplies both real bounds.
	void setLimits(int maxItems, qint64 maxBytes);

	// Inserts or replaces `id` and then evicts least recently used entries until both bounds
	// hold again. The entry just inserted is never evicted for its own overage: an image
	// larger than the whole byte budget must still be servable, so it is admitted and the
	// overage is reported once per process.
	void insert(const QString& id, const QImage& image, bool hasContentCenter, double contentCenterX,
		QList<color_area> accepted, QList<color_area> rejected);

	bool contains(const QString& id) const;

	// A null QImage when `id` is not cached. A hit promotes the entry to most recent.
	QImage image(const QString& id) const;

	// A pinned entry is never evicted: a visible slot refers to it. Unpinning re-runs the
	// eviction the pin was holding back.
	void pin(const QString& id, bool pinned);

	int itemCount() const;
	qint64 byteCount() const;
	// Entries a visible slot still pins, i.e. what no eviction can reclaim. Reported next to
	// itemCount()/byteCount() by MainApp's MASCOT-CACHE line (I4), so a cache that keeps growing
	// can be told apart from a pin that was never released.
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

	// Evicts from the LRU tail while a bound is exceeded, never touching `keepId` or a
	// pinned entry. Reports the single-entry overage when nothing evictable is left.
	void evictLocked(const QString& keepId);
	void reportOverageLocked() const;
	qint64 byteCountLocked() const;

	mutable QMutex cacheMutex;
	mutable QHash<QString, Entry> entries;
	mutable QList<QString> lruOrder; // most recently used first
	int maxItems = 0;
	qint64 maxBytes = 0;
};
