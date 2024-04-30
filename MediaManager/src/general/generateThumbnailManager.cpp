#include "stdafx.h"
#include "generateThumbnailManager.h"
#include <QMutexLocker>

generateThumbnailManager::generateThumbnailManager(unsigned int threads_number)
{
	this->threads_number = threads_number;
}

void generateThumbnailManager::enqueue_work(ThumbnailCommand work)
{
	this->add_work_count(1);
	this->queue.enqueue(work);
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
    while (this->threadList.size() < this->threads_number) {
        generateThumbnailThread* t = new generateThumbnailThread(&this->queue, this);
        t->connect(t, &generateThumbnailThread::finished, [this, t] {t->deleteLater(); this->threadList.removeOne(t); });
        this->threadList.append(t);
    }
    for (auto t : this->threadList)
        t->start();
}

void generateThumbnailManager::rebuildThumbnailCache(QSqlDatabase& db, bool overwrite)
{
    QSqlQuery query = QSqlQuery(db);
    query.prepare(QString("SELECT path from videodetails"));
    query.exec();
    while (query.next()) {
        this->enqueue_work({ query.value(0).toString(),overwrite });
    }
	this->start();
}

generateThumbnailManager::~generateThumbnailManager() {
    this->clear_work();
    for (auto t : this->threadList) {
        if (t) {
            if (t->process)
                t->process->kill();
            t->deleteLater();
        }
    }
}