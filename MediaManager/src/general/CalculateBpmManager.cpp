#include "stdafx.h"
#include "CalculateBpmManager.h"

CalculateBpmManager::CalculateBpmManager(int threads_number, QObject* parent)
    : QObject(parent) {
    this->is_running = false;
    this->cancellation_requested = false;
    this->threadPool = new QThreadPool(this);
    this->threadPool->setMaxThreadCount(threads_number);
}

CalculateBpmManager::~CalculateBpmManager() {
    this->stop();
    this->threadPool->deleteLater();
}

void CalculateBpmManager::enqueue_work(BpmWorkItem work) {
    QMutexLocker locker(&activeIdsMutex);
    if (activeIds.contains(work.id)) {
        return;
    }
    activeIds.insert(work.id);
    locker.unlock();

    this->add_work_count(1);
    this->queue.push(work);
}

void CalculateBpmManager::enqueue_work_front(BpmWorkItem work) {
    QMutexLocker locker(&activeIdsMutex);
    if (activeIds.contains(work.id)) {
        // Already active/queued. Try to move to front if in queue.
        int removedCount = this->queue.removeIf([work](const BpmWorkItem& item) {
            return item.id == work.id;
        });

        if (removedCount > 0) {
            this->queue.pushFront(work);
            // We removed 'removedCount' items and added 1.
            if (removedCount > 1) {
                this->add_work_count(1 - removedCount);
            }
        }
        // If not removed (count 0), it's processing. We do nothing.
        return;
    }

    activeIds.insert(work.id);
    locker.unlock();

    this->add_work_count(1);
    this->queue.pushFront(work);
}

void CalculateBpmManager::clear_work() {
    this->queue.clear();
    this->set_work_count(0);
    QMutexLocker locker(&activeIdsMutex);
    activeIds.clear();
}

void CalculateBpmManager::start() {
    this->is_running = true;
    this->cancellation_requested = false;
    if (this->queue.isEmpty()) {
        return;
    }
    // Start threads up to max thread count
    // Since runnables loop until queue empty, we just need to ensure enough runnables are alive
    
    int current_threads = this->threadPool->activeThreadCount();
    int max_threads = this->threadPool->maxThreadCount();
    
    for (int i = current_threads; i < max_threads; i++) {
        if (this->queue.isEmpty()) break; 
        
        CalculateBpmRunnable* task = new CalculateBpmRunnable(&this->queue, this);
        task->setCancelFlag(&this->cancellation_requested);
        
        {
            QMutexLocker locker(&runnablesMutex);
            runnables.append(task);
        }
        
        connect(task, &CalculateBpmRunnable::bpmCalculated, this, &CalculateBpmManager::bpmCalculated);
        
        // QThreadPool takes ownership if autoDelete is true (default)
        if (!this->threadPool->tryStart(task)) {
            task->deleteLater(); // Cleanup if couldn't start
        }
    }
}

void CalculateBpmManager::stop() {
    this->is_running = false;
    this->cancellation_requested = true;
    this->clear_work();
    if (this->threadPool) {
        this->threadPool->waitForDone();
    }
    
    // Clear runnables list
    {
        QMutexLocker locker(&runnablesMutex);
        runnables.clear();
    }
}

bool CalculateBpmManager::isRunning() const {
    return this->is_running.load();
}

void CalculateBpmManager::add_work_count(int value) {
    this->work_count += value;
}

void CalculateBpmManager::set_work_count(int value) {
    this->work_count = value;
}

int CalculateBpmManager::get_work_count() {
    return this->work_count.load();
}

bool CalculateBpmManager::is_id_active(int id) {
    QMutexLocker locker(&activeIdsMutex);
    return activeIds.contains(id);
}

void CalculateBpmManager::remove_active_id(int id) {
    QMutexLocker locker(&activeIdsMutex);
    activeIds.remove(id);
}

void CalculateBpmManager::setMaxThreadCount(int count) {
    this->threadPool->setMaxThreadCount(count);
}

void CalculateBpmManager::requestCancellation() {
    this->cancellation_requested = true;
}

void CalculateBpmManager::clearCancellation() {
    this->cancellation_requested = false;
}
