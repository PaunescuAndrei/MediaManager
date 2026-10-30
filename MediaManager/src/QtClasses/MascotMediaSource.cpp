#include "stdafx.h"
#include "MascotMediaSource.h"
#include "MainApp.h"
#include "MainWindow.h"
#include "definitions.h"
#include "utils.h"
#include <QJsonObject>
#include <QRandomGenerator>

namespace {
	// §7.8's only exclusion: a pick that lands on the file the main player holds is re-rolled, at
	// most this many times, and the last draw is then accepted -- a one-file pool can only ever
	// return that file, and showing it beats leaving the panel blank.
	constexpr int VIDEO_REROLL_ATTEMPTS = 8;

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

void MascotMediaSource::setExcludePath(const QString& path)
{
	this->excludePath = path;
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
	if (!this->App || !this->App->config) {
		if (!this->rngInitialized) {
			std::random_device rd;
			this->rng.seed(rd());
			this->rngInitialized = true;
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
		}
	}
	else if (!this->rngInitialized || this->lastUseSeed) {
		std::random_device rd;
		this->rng.seed(rd());
		this->rngInitialized = true;
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

	// The image path's seed read, taken at the same point it takes it. refreshRng() is a no-op
	// unless the seed string or the seed switch changed, and the video seed below is read from the
	// same two members, so a changed `random_seed` lands on the next pick of both pools with no
	// cached seed left over from a previous one.
	this->refreshRng();

	// MainWindow::getRandomVideoPath()'s generator, seeded exactly as it seeded it: an empty seed
	// (seeding off) draws fresh entropy, and a seed that is on is salted and hashed. There is no
	// zero bump here -- the 0 -> 1 bump in refreshRng() exists for the default-constructed
	// std::mt19937, and the original never applied it to this generator.
	QRandomGenerator generator;
	if (this->seedEnabled) {
		QString salted = this->seedString;
		if (this->App && this->App->mainWindow)
			salted = this->App->mainWindow->saltSeed(this->seedString);
		generator.seed(utils::stringToSeed(salted));
	}
	else {
		generator.seed(QRandomGenerator::global()->generate());
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

	QString path = drawVideoPath(this->videoPool, this->weightedChoice, weightedSettings, generator);
	for (int attempt = 0; attempt < VIDEO_REROLL_ATTEMPTS && !this->excludePath.isEmpty()
		&& path == this->excludePath; ++attempt) {
		path = drawVideoPath(this->videoPool, this->weightedChoice, weightedSettings, generator);
	}
	return path;
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
	QJsonObject settings = this->App->mainWindow->filterSettings.json;
	settings.insert("ignore_defaults", "True");
	this->videoPool = this->App->db->getVideos(this->App->currentDB, settings);
	this->videoPoolInitialized = true;
}
