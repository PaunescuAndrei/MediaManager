#pragma once
#include "MascotMedia.h"
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <atomic>

class MascotImageCache;
class QThreadPool;

// Decodes mascots away from the GUI thread and keeps the decoded pixels in a
// MascotImageCache. This is the threading contract Task 3's QQuickImageProvider relies on:
//
//   - decoding, sizing, alpha centering and palette extraction run on a dedicated
//     QThreadPool with two workers (never QThreadPool::globalInstance()), so a burst of
//     prefetch requests can neither saturate the machine nor starve unrelated work;
//   - the result returns through a queued call, so every insert into the cache and every
//     mediaReady/mediaFailed emission happens on the GUI thread, which is the thread this
//     object lives in;
//   - the provider reads the cache from the Qt Quick render thread, which the cache's own
//     mutex covers.
//
// Nothing here renders, and no method blocks the GUI thread waiting for a decode.
class MascotPrefetcher : public QObject {
	Q_OBJECT
public:
	// `cache` is not owned and must outlive the prefetcher.
	explicit MascotPrefetcher(MascotImageCache* cache, QObject* parent = nullptr);
	~MascotPrefetcher() override;

	// Forwarded to the cache, which owns the budget. No config is read here: Task 4 wires the
	// configured values in through this call.
	void setLimits(int maxItems, qint64 maxBytes);

	// The palette switch (I2). Master ran palette extraction only while `mascots_color_theme`
	// was on (App->MascotsExtractColor, `config.cpp` defaults it to False), and it is the most
	// expensive part of a decode: 20-cluster k-means plus a QPixmap conversion on the same two
	// workers that serve the miss path. This class reads no config (see setLimits), so MainApp
	// -- which does read it -- pushes the flag in here at construction and wherever the setting
	// is refreshed. Takes effect on the next decode; one already in flight keeps the value it
	// started with. GUI thread only, like every other setter.
	void setExtractColors(bool extract);

	// Pins or unpins the cache entry for `path`. The path -> cache id mapping stays in this
	// class (cacheIdForPath() is private for exactly that reason), so a consumer that must
	// hold an id against eviction -- the controller, for the media a panel displays -- asks
	// here instead of rebuilding the id. GUI thread only, like every other mutation.
	void pin(const QString& path, bool pinned);

	// Asks for `path`. Never decodes inline. Does nothing while the path is already pending,
	// republishes the remembered media (no re-decode) while its pixels are still cached, and
	// announces a file that cannot be decoded through mediaFailed.
	void request(const QString& path);

	// True while a decode for `path` is in flight.
	bool isPending(const QString& path) const;

	// Consumes the oldest decoded media that has not been taken yet, or a default
	// MascotMedia (empty `path`) when none is ready. For pull-style consumers; the same
	// media were also announced through mediaReady.
	MascotMedia takeReady();

	// Drops queued notifications and makes every in-flight decode and every already-posted
	// republication unpublishable, so work that finishes after this call cannot resurrect a
	// slot that was just cleared. Pixels already in the cache stay there; their entries age
	// out through the cache's own LRU.
	void clear();

signals:
	void mediaReady(MascotMedia media);
	void mediaFailed(QString path);

private:
	struct DecodeResult; // defined in MascotPrefetcher.cpp: the worker's payload

	// Runs on a decode worker. Static so it cannot reach this object's GUI-thread state, which
	// is also why the palette switch is a parameter: request() captures the member on the GUI
	// thread and hands the value to the worker.
	static DecodeResult decode(const QString& path, const QString& providerId, bool extractColors);
	// Runs on the GUI thread, queued back from the worker: inserts into the cache, then
	// announces. Discards the result when clear() happened while the decode was in flight.
	void handleDecoded(DecodeResult result, quint64 requestGeneration);
	void publish(const MascotMedia& media);
	// Drops remembered media whose pixels the cache no longer holds.
	void pruneUncachedMedia();

	// The only place in the app where a path becomes a cache id, and (from it) a provider URL.
	static QString cacheIdForPath(const QString& path);
	static QString providerIdForPath(const QString& path);

	MascotImageCache* cache = nullptr;
	QThreadPool* decodePool = nullptr;
	// I2's switch. GUI thread only: request() copies it into the worker's payload, so no decode
	// worker ever reads this member.
	bool extractColors = false;
	QSet<QString> pendingPaths;                  // GUI thread only
	QSet<QString> failedPaths;                   // GUI thread only, so a failure logs once
	QHash<QString, MascotMedia> rememberedMedia; // GUI thread only, keyed by path
	QList<MascotMedia> readyQueue;               // GUI thread only, oldest first
	std::atomic<quint64> generation{ 0 };        // bumped by clear(), read by workers
};
