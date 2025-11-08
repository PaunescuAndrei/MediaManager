#include "stdafx.h"
#include "generateThumbnailManager.h"

generateThumbnailManager::generateThumbnailManager(unsigned int threads_number, QObject* parent) : QObject(parent)
{
    this->thumbsThreadPool = new QThreadPool(this);
    this->thumbsThreadPool->setMaxThreadCount(threads_number);
}

void generateThumbnailManager::enqueue_work(ThumbnailCommand work)
{
	this->add_work_count(1);
	this->queue.push(work);
}

void generateThumbnailManager::enqueue_work_front(ThumbnailCommand work)
{
	this->add_work_count(1);
	this->queue.pushFront(work);
}

void generateThumbnailManager::clear_work() {
    this->queue.clear();
    this->set_work_count(0);
}

void generateThumbnailManager::add_work_count(int value)
{
	this->work_count += value;
}

void generateThumbnailManager::set_work_count(int value)
{
	this->work_count = value;
}

void generateThumbnailManager::start() {
    if (this->queue.isEmpty()) {
        return;
    }
    for (int i = 0; i < this->thumbsThreadPool->maxThreadCount(); i++) {
        generateThumbnailRunnable* thumbsTask = new generateThumbnailRunnable(&this->queue, this, this);
        connect(thumbsTask, &generateThumbnailRunnable::openFile, this, &generateThumbnailManager::openFile);
        if (!this->thumbsThreadPool->tryStart(thumbsTask)) {
            thumbsTask->deleteLater();
        }
    }
}

//void generateThumbnailManager::onOpenFile(QString path) {
//    emit openFile(path);
//}

void generateThumbnailManager::rebuildThumbnailCache(QSqlDatabase& db, bool overwrite)
{
    QSqlQuery query = QSqlQuery(db);
    query.prepare(QString("SELECT path from videodetails"));
    query.exec();
    while (query.next()) {
        this->enqueue_work({ query.value(0).toString(),overwrite, false });
    }
	this->start();
}

generateThumbnailManager::~generateThumbnailManager() {
    this->clear_work();
    this->thumbsThreadPool->deleteLater();
}