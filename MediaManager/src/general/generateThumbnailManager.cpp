#include "stdafx.h"
#include "generateThumbnailManager.h"
#include <QMutexLocker>

generateThumbnailManager::generateThumbnailManager(unsigned int threads_number, QObject* parent) : QObject(parent)
{
    this->thumbsThreadPool = new QThreadPool(this);
    this->thumbsThreadPool->setMaxThreadCount(threads_number);
}

void generateThumbnailManager::enqueue_work(ThumbnailCommand work)
{
	this->add_work_count(1);
	this->queue.enqueue(work);
}

void generateThumbnailManager::enqueue_work_front(ThumbnailCommand work)
{
	this->add_work_count(1);
	this->queue.enqueue_front(work);
}

void generateThumbnailManager::clear_work() {
    this->queue.clear();
    this->set_work_count(0);
}

void generateThumbnailManager::add_work_count(int value)
{
    QMutexLocker lock(&this->data_lock);
	this->work_count = std::max(0, this->work_count + value);
}

void generateThumbnailManager::set_work_count(int value)
{
    QMutexLocker lock(&this->data_lock);
	this->work_count = std::max(0, value);
}

void generateThumbnailManager::start() {
	for (int i = 0; i < this->thumbsThreadPool->maxThreadCount() && !this->queue.isEmpty(); i++) {
        generateThumbnailRunnable* thumbsTask = new generateThumbnailRunnable(&this->queue, this, this);
        connect(thumbsTask, &generateThumbnailRunnable::openFile, this, &generateThumbnailManager::onOpenFile);
        bool started = this->thumbsThreadPool->tryStart(thumbsTask);
        if(not started) {
			delete thumbsTask; // Clean up if the task could not be started, its automatically deleted by the thread pool if it starts successfully
		}
	}
}

void generateThumbnailManager::onOpenFile(QString path)
{
    emit openFile(path);
}

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