#include "stdafx.h"
#include "mascotsGeneratorThread.h"
#include "MainApp.h"
#include "MainWindow.h"
#include "utils.h"
#include "BlockingQueue.h"
#include <QList>
#include <QColor>
#include <QPair>

void mascotsGeneratorThread::initMascotsPaths(QString mascots_path)
{
	//for (std::string i : getDirs(mascots_path)) {
	//	this->mascots_paths += getDirs(i);
	//}
	this->mascots_paths = utils::getDirsQt(mascots_path);
	this->mascots_allpaths = utils::getFilesQt(mascots_path, true);
}

void mascotsGeneratorThread::initRun()
{
	for (int i = 0; i < 4; i++) {
		this->loadImage();
	}
}

void mascotsGeneratorThread::refreshRngFromConfig()
{
	if (!this->App || !this->App->config) {
		if (!this->rng_initialized) {
			std::random_device rd;
			this->rng.seed(rd());
			this->rng_initialized = true;
		}
		return;
	}

	bool use_seed = this->App->config->get_bool("random_use_seed") && this->App->config->get_bool("mascots_use_seed");
	QString seed_string = this->App->config->get("random_seed");
	if (use_seed) {
		if (!this->rng_initialized || !this->last_use_seed || seed_string != this->last_seed_string) {
			QString salted = seed_string;
			if (this->App->mainWindow)
				salted = this->App->mainWindow->saltSeed(seed_string);
			quint32 seed = utils::stringToSeed(salted);
			if (seed == 0)
				seed = 1;
			this->rng.seed(seed);
			this->rng_initialized = true;
		}
	}
	else if (!this->rng_initialized || this->last_use_seed) {
		std::random_device rd;
		this->rng.seed(rd());
		this->rng_initialized = true;
	}

	this->last_use_seed = use_seed;
	this->last_seed_string = seed_string;
}

void mascotsGeneratorThread::clearCache()
{
	if (this->mascots_pixmap)
		this->mascots_pixmap->clear();
	this->rng_initialized = false;
	this->last_seed_string.clear();
	this->last_use_seed = false;
}

ImageData mascotsGeneratorThread::getRandomImagePath() {
	return this->getRandomImagePath(allfiles_random);
}

ImageData mascotsGeneratorThread::getRandomImagePath(bool allfiles_random) {
	ImageData data;
	QString imgpath = QString();
	this->refreshRngFromConfig();
	if (allfiles_random) {
		if(!this->mascots_allpaths.isEmpty())
			imgpath = *utils::select_randomly(this->mascots_allpaths.begin(), this->mascots_allpaths.end(), this->rng);
	}
	else {
	if (!this->mascots_paths.isEmpty()) {
			QString dirpath = *utils::select_randomly(this->mascots_paths.begin(), this->mascots_paths.end(), this->rng);
			QStringList imgfiles = utils::getFilesQt(dirpath,true);
			if(!imgfiles.isEmpty())
				imgpath = *utils::select_randomly(imgfiles.begin(), imgfiles.end(), this->rng);
		}
	}
	data.path = imgpath;
	return data;
}

ImageData mascotsGeneratorThread::getImage() {
	if (this->mascots_pixmap->size() > 0)
		return this->mascots_pixmap->pop();
	else
		return ImageData();
}

QPair<QList<color_area>, QList<color_area>> mascotsGeneratorThread::extractColors(QPixmap& img) {
	return palette_extractor(img, 20, 300);
}

void mascotsGeneratorThread::loadImage()
{
	QString imgpath = getRandomImagePath().path;
	if (imgpath.isEmpty()) {
		this->running = false;
		return;
	}
	QPixmap img = utils::openImage(imgpath);
	if (!img.isNull()) {
		if (this->App->MascotsExtractColor) {
			QPair<QList<color_area>, QList<color_area>> color_palette = this->extractColors(img);
			this->mascots_pixmap->push(ImageData{imgpath, img, color_palette.first, color_palette.second});
		}
		else {
			this->mascots_pixmap->push(ImageData{ imgpath, img});
		}
	}
	else {
		qDebug() << "Null mascot pixmap. " << imgpath;
	}
}

mascotsGeneratorThread::mascotsGeneratorThread(MainApp* App, QString mascots_path,bool allfiles_random, QObject* parent) : QThread(parent)
{
	this->App = App;
	this->mascots_path = mascots_path;
	this->allfiles_random = allfiles_random;
	this->mascots_pixmap = new BlockingQueue<ImageData>(10);
	this->initMascotsPaths(this->mascots_path);
	this->initRun();
	this->running = true;
}

void mascotsGeneratorThread::run() {
	while (this->running) {
		this->loadImage();
	}
}

mascotsGeneratorThread::~mascotsGeneratorThread() {
	delete this->mascots_pixmap;
}
