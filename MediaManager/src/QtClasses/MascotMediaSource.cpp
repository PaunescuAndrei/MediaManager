#include "stdafx.h"
#include "MascotMediaSource.h"
#include "MainApp.h"
#include "MainWindow.h"
#include "definitions.h"
#include "utils.h"
#include <QJsonObject>
#include <QRandomGenerator>

namespace {
	// One draw from the pool. `weighted` false is a plain uniform pick over the candidate
	// indices: no bias is read, so `mascots_weighted_random` off cannot inherit a weight by
	// accident. (The weighted call takes the same uniform fast path when the general bias is 0 --
	// that is the app's own behaviour in weightedRandomChoice(), not a shared implementation.)
	QString drawVideoPath(const QList<VideoWeightedData>& pool, bool weighted,
		const WeightedBiasSettings& weightedSettings, QRandomGenerator& generator)
	{
		// bounded(0) never returns, so an empty pool is answered here rather than by the caller's
		// good intentions; weightedRandomChoice() already returns "" for it.
		if (pool.isEmpty())
			return "";
		if (!weighted)
			return pool[generator.bounded(pool.size())].path;
		return utils::weightedRandomChoice(pool, generator,
			weightedSettings.bias_views, weightedSettings.bias_rating,
			weightedSettings.bias_tags, weightedSettings.bias_bpm,
			weightedSettings.bias_general, weightedSettings.no_views_weight,
			weightedSettings.no_rating_weight, weightedSettings.no_tags_weight);
	}

	// How many draws one pick may spend looking for a candidate that has not already failed. The
	// skip is a re-draw rather than a filtered pool because the pool is the app's own candidate
	// set and a copy minus the failures would be a second encoding of it (the same objection Task
	// 3's review made to the duplicated empty-pool answer); a handful of unplayable files in a
	// library is the case this serves. Each re-draw costs one advance of the seeded generator,
	// which is why the loop is bounded: a pool where every candidate has failed answers "" and the
	// caller falls back to images, which is §7.8's honest degrade rather than a retry forever.
	constexpr int VIDEO_DRAW_ATTEMPTS = 8;
}

MascotMediaSource::MascotMediaSource(MainApp* App)
{
	this->App = App;
}

void MascotMediaSource::setAllFilesRandom(bool allFilesRandom)
{
	this->allFilesRandom = allFilesRandom;
}

void MascotMediaSource::setSeedSource(const QString& seedString, bool useSeed)
{
	this->seedString = seedString;
	this->seedEnabled = useSeed;
}

void MascotMediaSource::setWeightedChoice(bool weighted)
{
	this->weightedChoice = weighted;
}

void MascotMediaSource::initPaths()
{
	// The retired generator's initMascotsPaths(), unchanged: one level of subdirectories for
	// the per-directory pick, and every file below the mascots root for the flat pick.
	this->mascotsPaths = utils::getDirsQt(MASCOTS_PATH);
	this->mascotsAllPaths = utils::getFilesQt(MASCOTS_PATH, true);
}

void MascotMediaSource::refreshRng()
{
	// The retired generator's refreshRngFromConfig(), branch for branch. Seeding is only
	// redone when the seed string or the seed switch changed, and a seed that hashes to 0 is
	// bumped to 1 so it cannot mean "default-constructed std::mt19937".
	// The video draw's generator is seeded on the same branch and with the same value, so both
	// pools have one seed lifecycle. It is seeded once per seed value and then only advanced: a
	// timer-driven panel needs a reproducible *sequence*, and seeding per pick (what
	// getRandomVideoPath() does for a single `Next` press) would return the identical file on
	// every pick while a seed is on, pinning the panel to one clip.
	if (!this->App || !this->App->config) {
		if (!this->rngInitialized) {
			std::random_device rd;
			this->rng.seed(rd());
			this->rngInitialized = true;
		}
		// No config means no seed to honour, so the video generator starts from entropy too --
		// the same branch, one RNG earlier.
		if (!this->videoGenerator) {
			std::random_device rd;
			this->videoGenerator.emplace(rd());
		}
		return;
	}

	if (this->seedEnabled) {
		if (!this->rngInitialized || !this->lastUseSeed || this->seedString != this->lastSeedString) {
			QString salted = this->seedString;
			if (this->App->mainWindow)
				salted = this->App->mainWindow->saltSeed(this->seedString);
			quint32 seed = utils::stringToSeed(salted);
			if (seed == 0)
				seed = 1;
			this->rng.seed(seed);
			this->rngInitialized = true;
			this->videoGenerator.emplace(seed);
		}
	}
	else if (!this->rngInitialized || this->lastUseSeed) {
		std::random_device rd;
		this->rng.seed(rd());
		this->rngInitialized = true;
		this->videoGenerator.emplace(rd());
	}

	this->lastUseSeed = this->seedEnabled;
	this->lastSeedString = this->seedString;
}

QString MascotMediaSource::nextImagePath()
{
	// MainWindow::updateMascot() used to call initMascotsPaths() again whenever the directory
	// list came back empty (a mascots folder created after startup), so an empty result is
	// deliberately not cached.
	if (!this->pathsInitialized || this->mascotsPaths.isEmpty()) {
		this->initPaths();
		this->pathsInitialized = true;
	}

	this->refreshRng();

	QString path;
	if (this->allFilesRandom) {
		if (!this->mascotsAllPaths.isEmpty())
			path = *utils::select_randomly(this->mascotsAllPaths.begin(), this->mascotsAllPaths.end(), this->rng);
	}
	else if (!this->mascotsPaths.isEmpty()) {
		const QString dirPath = *utils::select_randomly(this->mascotsPaths.begin(), this->mascotsPaths.end(), this->rng);
		const QStringList imageFiles = utils::getFilesQt(dirPath, true);
		if (!imageFiles.isEmpty())
			path = *utils::select_randomly(imageFiles.begin(), imageFiles.end(), this->rng);
	}
	return path;
}

QString MascotMediaSource::nextVideoPath()
{
	this->ensureVideoPool();
	if (this->videoPool.isEmpty())
		return "";

	// The seed lifecycle is refreshRng()'s, shared with the image pool: it seeds the video
	// generator once per seed value, on the same branch and with the same value it seeds the image
	// RNG with, and never inside a pick. Drawing therefore advances one persistent generator, so a
	// seeded panel varies from pick to pick.
	this->refreshRng();
	if (!this->videoGenerator) {
		// refreshRng() engages it on every path; dereferencing a disengaged optional would be UB,
		// so the impossible case is answered instead of assumed.
		return "";
	}

	// The bias settings are read once per pick and then adjusted the way getRandomVideoPath()
	// adjusted them: weighted random off for this category means a uniform pick, not a
	// half-weighted one. They are not read at all when the caller asked for a uniform pick.
	WeightedBiasSettings weightedSettings;
	if (this->weightedChoice) {
		if (this->App && this->App->mainWindow && this->App->config)
			weightedSettings = this->App->mainWindow->getWeightedBiasSettings();
		if (!weightedSettings.weighted_random_enabled)
			weightedSettings.bias_general = 0;
	}

	// One draw, no exclusion. A pick is allowed to land on the file the main player currently has
	// -- the user's explicit call, and the panel is free to show what is already playing -- so
	// nothing here re-rolls and nothing here reads the player's path. Do not "fix" that back in.
	// The one exclusion §7.8 does ask for is applied here, at the draw: a file that already failed
	// is skipped with a bounded re-draw (VIDEO_DRAW_ATTEMPTS), so a broken clip cannot return every
	// 1/pool swaps and cost a failure or a five-second timeout with the panel frozen on its last
	// frame. A pick whose every attempt lands on a known-bad path answers "" rather than a path
	// that is known to fail.
	for (int attempt = 0; attempt < VIDEO_DRAW_ATTEMPTS; ++attempt) {
		const QString path = drawVideoPath(this->videoPool, this->weightedChoice, weightedSettings,
			*this->videoGenerator);
		if (path.isEmpty() || !this->failedVideoPaths.contains(path))
			return path;
	}
	return "";
}

void MascotMediaSource::noteVideoFailure(const QString& path)
{
	if (path.isEmpty())
		return;
	// Session-scoped, and deliberately not time-based: a file the backend cannot decode does not
	// become decodable a minute later, so an interval would only put it back into the draw to fail
	// again -- one failure line plus a frozen panel, or five seconds of timeout, each time it came
	// round. That recurring cost is exactly what §7.8's rule removes. `invalidateVideoPool()` does
	// not clear this set either: the file is the same file after a model reset, a filter apply or a
	// database switch, and the only real recovery is a fix on disk, which the next process start
	// sees because the set dies with this object.
	this->failedVideoPaths.insert(path);
}

void MascotMediaSource::invalidateVideoPool()
{
	// The one thing that drops a cached scan (§7.8: model reset, filter apply, database switch).
	// The next use -- a pick or a size query -- re-reads the database once and caches it again.
	this->videoPoolInitialized = false;
	this->videoPool.clear();
}

int MascotMediaSource::videoPoolSize() const
{
	this->ensureVideoPool();
	return static_cast<int>(this->videoPool.size());
}

void MascotMediaSource::ensureVideoPool() const
{
	if (this->videoPoolInitialized)
		return;
	// Nothing to scan against: leaving the pool uninitialised means the first call that can scan
	// does, instead of caching "no videos" for a source that was simply not wired up yet.
	if (!this->App || !this->App->db || !this->App->mainWindow)
		return;

	// getRandomSettings(RandomModes::Filtered) with `ignore_defaults` forced on: the user's
	// filters apply, the default "watched only" does not (§7.8). getRandomSettings() is what
	// would overwrite `watched` with ["No"] when the key is false, and getVideos() never reads the
	// key itself -- it is the `watched` array in this json that filters -- so forcing it here
	// keeps the settings object saying what the pool means.
	// A getVideos() that fails comes back as an empty list too (it logs the SQL error and returns
	// the rows it collected, which is none), so videoPoolSize() == 0 means "no row passed the
	// filters" *or* "the query failed" and the two are not distinguishable from here. Task 4's
	// Mixed -> images fallback is built on that being written down.
	QJsonObject settings = this->App->mainWindow->filterSettings.json;
	settings.insert("ignore_defaults", "True");
	this->videoPool = this->App->db->getVideos(this->App->currentDB, settings);
	this->videoPoolInitialized = true;
}
