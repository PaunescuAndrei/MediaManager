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

// Decoding runs on a dedicated two-worker QThreadPool (never the global instance) and every result is queued
// back to the GUI thread; the provider reads the cache from the render thread, under the cache's mutex.
class MascotPrefetcher : public QObject {
	Q_OBJECT
public:
	explicit MascotPrefetcher(MascotImageCache* cache, QObject* parent = nullptr);
	~MascotPrefetcher() override;

	void setLimits(int maxItems, qint64 maxBytes);

	// Palette extraction is the most expensive part of a decode, and this class reads no config, so the flag is pushed in.
	void setExtractColors(bool extract);

	void pin(const QString& path, bool pinned);

	void request(const QString& path);

	bool isPending(const QString& path) const;

	// Bumps a generation so a decode finishing after this call cannot resurrect a slot that was just cleared.
	void clear();

signals:
	void mediaReady(MascotMedia media);
	void mediaFailed(QString path);

private:
	struct DecodeResult; // defined in MascotPrefetcher.cpp: the worker's payload

	static DecodeResult decode(const QString& path, const QString& providerId, bool extractColors);
	void handleDecoded(DecodeResult result, quint64 requestGeneration);
	void publish(const MascotMedia& media);
	void pruneUncachedMedia();

	static QString cacheIdForPath(const QString& path);
	static QString providerIdForPath(const QString& path);

	MascotImageCache* cache = nullptr;
	QThreadPool* decodePool = nullptr;
	bool extractColors = false;
	QSet<QString> pendingPaths;                  // GUI thread only
	QSet<QString> failedPaths;                   // GUI thread only, so a failure logs once
	QHash<QString, MascotMedia> rememberedMedia; // GUI thread only, keyed by path
	std::atomic<quint64> generation{ 0 };        // bumped by clear(), read by workers
};
