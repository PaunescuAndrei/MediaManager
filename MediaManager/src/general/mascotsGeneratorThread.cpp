#include "stdafx.h"
#include "mascotsGeneratorThread.h"
#include "MainApp.h"
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

ImageData mascotsGeneratorThread::getRandomImagePath() {
	return this->getRandomImagePath(allfiles_random);
}

ImageData mascotsGeneratorThread::getRandomImagePath(bool allfiles_random) {
	ImageData data;
	QString imgpath = QString();
;	if (allfiles_random) {
		if(!this->mascots_allpaths.isEmpty())
			imgpath = *utils::select_randomly(this->mascots_allpaths.begin(), this->mascots_allpaths.end());
	}
	else {
	if (!this->mascots_paths.isEmpty()) {
			QString dirpath = *utils::select_randomly(this->mascots_paths.begin(), this->mascots_paths.end());
			QStringList imgfiles = utils::getFilesQt(dirpath,true);
			if(!imgfiles.isEmpty())
				imgpath = *utils::select_randomly(imgfiles.begin(), imgfiles.end());
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