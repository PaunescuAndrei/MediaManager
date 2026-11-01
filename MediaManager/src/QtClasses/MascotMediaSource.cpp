#include "stdafx.h"
#include "MascotMediaSource.h"
#include "MainApp.h"
#include "MainWindow.h"
#include "definitions.h"
#include "utils.h"
#include <QJsonObject>
#include <QRandomGenerator>

namespace {
	QString drawVideoPath(const QList<VideoWeightedData>& pool, bool weighted,
		const WeightedBiasSettings& weightedSettings, QRandomGenerator& generator)
	{
		// bounded(0) never returns, so an empty pool is answered here rather than by the caller's good intentions.
		if (pool.isEmpty())
			return "";
		// A uniform pick when the key is off: no bias is read, so a weight cannot be inherited by accident.
		if (!weighted)
			return pool[generator.bounded(pool.size())].path;
		return utils::weightedRandomChoice(pool, generator,
			weightedSettings.bias_views, weightedSettings.bias_rating,
			weightedSettings.bias_tags, weightedSettings.bias_bpm,
			weightedSettings.bias_general, weightedSettings.no_views_weight,
			weightedSettings.no_rating_weight, weightedSettings.no_tags_weight);
	}

	// A bounded re-draw, because the pool is the app's own candidate set; all attempts failed answers "", which is the fallback to images.
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
	this->mascotsPaths = utils::getDirsQt(MASCOTS_PATH);
	this->mascotsAllPaths = utils::getFilesQt(MASCOTS_PATH, true);
}

void MascotMediaSource::refreshRng()
{
	// A seed that hashes to 0 is bumped to 1 so it cannot mean "default-constructed std::mt19937". The video generator
	// is seeded once per seed value and then only advanced: seeding per pick would return the identical file every time.
	if (!this->App || !this->App->config) {
		if (!this->rngInitialized) {
			std::random_device rd;
			this->rng.seed(rd());
			this->rngInitialized = true;
		}
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
	// An empty directory list is deliberately not cached: a mascots folder created after startup has to be seen.
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

	this->refreshRng();
	if (!this->videoGenerator) {
		return "";
	}

	WeightedBiasSettings weightedSettings;
	if (this->weightedChoice) {
		if (this->App && this->App->mainWindow && this->App->config)
			weightedSettings = this->App->mainWindow->getWeightedBiasSettings();
		if (!weightedSettings.weighted_random_enabled)
			weightedSettings.bias_general = 0;
	}

	// One draw, no exclusion: a pick may land on the file the main player has, which is the user's explicit call.
	// A file that already failed is skipped by the bounded re-draw, because the degrade for it is an empty answer.
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
	// Session-scoped, not time-based: a file the backend cannot decode does not become decodable later, and
	// invalidateVideoPool() must not clear it -- the file is the same after a model reset or a database switch.
	this->failedVideoPaths.insert(path);
}

void MascotMediaSource::invalidateVideoPool()
{
	// The one thing that drops a cached scan: a model reset, a filter apply or a database switch.
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
	if (!this->App || !this->App->db || !this->App->mainWindow)
		return;

	// getRandomSettings with ignore_defaults forced on: the user's filters apply and the default "watched only" does not.
	// getVideos() never reads the key itself -- the `watched` array in this json is what filters.
	QJsonObject settings = this->App->mainWindow->filterSettings.json;
	settings.insert("ignore_defaults", "True");
	// A failed getVideos() comes back empty too, so videoPoolSize() == 0 means "no row passed" *or* "the query failed".
	this->videoPool = this->App->db->getVideos(this->App->currentDB, settings);
	this->videoPoolInitialized = true;
}
