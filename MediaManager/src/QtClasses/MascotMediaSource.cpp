#include "stdafx.h"
#include "MascotMediaSource.h"
#include "MainApp.h"
#include "MainWindow.h"
#include "definitions.h"
#include "utils.h"

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

void MascotMediaSource::initPaths()
{
	// mascotsGeneratorThread::initMascotsPaths(), unchanged: one level of subdirectories for
	// the per-directory pick, and every file below the mascots root for the flat pick.
	this->mascotsPaths = utils::getDirsQt(MASCOTS_PATH);
	this->mascotsAllPaths = utils::getFilesQt(MASCOTS_PATH, true);
}

void MascotMediaSource::refreshRng()
{
	// mascotsGeneratorThread::refreshRngFromConfig(), branch for branch. Seeding is only
	// redone when the seed string or the seed switch changed, and a seed that hashes to 0 is
	// bumped to 1 so it cannot mean "default-constructed generator".
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
