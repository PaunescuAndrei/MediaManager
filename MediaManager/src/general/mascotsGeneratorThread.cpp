#include "stdafx.h"
#include "mascotsGeneratorThread.h"
#include "MainApp.h"
#include "utils.h"
#include "BlockingQueue.h"
#include <QList>
#include <QColor>
#include <QPair>

void mascotsGeneratorThread::init_mascots_paths(QString mascots_path)
{
	//for (std::string i : getDirs(mascots_path)) {
	//	this->mascots_paths += getDirs(i);
	//}
	this->mascots_paths = utils::getDirsQt(mascots_path);
	this->mascots_allpaths = utils::getFilesQt(mascots_path, true);
}

void mascotsGeneratorThread::init_run()
{
	for (int i = 0; i < 4; i++) {
		this->load_img();
	}
}

QString mascotsGeneratorThread::get_random_imgpath() {
	return this->get_random_imgpath(allfiles_random);
}

QString mascotsGeneratorThread::get_random_imgpath(bool allfiles_random) {
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
	return imgpath;
}

ImageData mascotsGeneratorThread::get_img() {
	if (this->mascots_pixmap->size() > 0)
		return this->mascots_pixmap->pop();
	else
		return ImageData();
}

void mascotsGeneratorThread::load_img()
{
	QString imgpath = get_random_imgpath();
	if (imgpath.isEmpty()) {
		this->running = false;
		return;
	}
	QPixmap img = utils::openImage(imgpath);
	if (!img.isNull()) {
		if (this->App->MascotsExtractColor) {
			QPair<QList<color_area>, QList<color_area>> color_palette = palette_extractor(img, 20, 300);
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
	this->init_mascots_paths(this->mascots_path);
	this->init_run();
	this->running = true;
}

void mascotsGeneratorThread::run() {
	while (this->running) {
		this->load_img();
	}
}

mascotsGeneratorThread::~mascotsGeneratorThread() {
	delete this->mascots_pixmap;
}