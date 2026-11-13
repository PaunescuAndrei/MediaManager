#pragma once
#include "definitions.h"
#include <QHash>
#include <QRandomGenerator>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>
#include <random>

class MainApp;

class MascotMediaSource {
public:
	explicit MascotMediaSource(MainApp* app = nullptr);

	void setAllFilesRandom(bool allFilesRandom);

	// Pass the raw `random_seed` value: the salt is applied here for seeding only, while a change
	// in the raw string is what re-seeds -- a changed salt alone does not.
	void setSeedSource(const QString& seedString, bool useSeed);

	void setWeightedChoice(bool weighted);

	QString nextImagePath();

	// `watched` is the layer's own switch (All/Yes/No). Each value has its own cached pool, so the
	// columns and the layer can filter differently without re-running the query on every pick.
	QString nextVideoPath(const QString& watched);

	void invalidateVideoPool();

	void noteVideoFailure(const QString& path);

	// A pinned source is not drawn from the pool, so it can only be skipped by asking.
	bool isVideoFailed(const QString& path) const;

	int videoPoolSize(const QString& watched) const;

private:
	void initPaths();
	void refreshRng();
	// The pool scan is one `getVideos` query -- the expensive half of a pick -- so it is cached
	// until invalidateVideoPool(). Leaves the cache uninitialised when there is nothing to scan
	// against, so a source used before the app is up is not left empty for good.
	void ensureVideoPool(const QString& watched) const;

	MainApp* App = nullptr;
	bool allFilesRandom = false;
	QStringList mascotsPaths;
	QStringList mascotsAllPaths;
	bool pathsInitialized = false;
	std::mt19937 rng;
	QString seedString;
	bool seedEnabled = false;
	QString lastSeedString;
	bool lastUseSeed = false;
	bool rngInitialized = false;
	bool weightedChoice = true;
	std::optional<QRandomGenerator> videoGenerator;
	// Mutable so the const videoPoolSize() can answer before the first pick: a caller that asks the
	// size to decide whether video is possible would never take the pick that fills the pool.
	// Keyed by the normalised watched value, so the two layers' filters do not evict each other.
	mutable QHash<QString, QList<VideoWeightedData>> videoPools;
	mutable QSet<QString> videoPoolsInitialized;
	// Paths, not pool indices: the pool is re-scanned, so an index would later name a different file.
	QSet<QString> failedVideoPaths;
};
