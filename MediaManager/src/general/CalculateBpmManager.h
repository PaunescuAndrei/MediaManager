#pragma once
#include <QObject>
#include <QThreadPool>
#include <QSet>
#include <QMutex>
#include "NonBlockingQueue.h"
#include "CalculateBpmRunnable.h"
#include <atomic>

class CalculateBpmManager : public QObject {
    Q_OBJECT
public:
    explicit CalculateBpmManager(int threads_number, QObject* parent = nullptr);
    ~CalculateBpmManager();

    void enqueue_work(BpmWorkItem work);
    void clear_work();
    void start();
    void stop();
    bool isRunning() const;
    
    void add_work_count(int value);
    void set_work_count(int value);
    int get_work_count();

    bool is_id_active(int id);
    void remove_active_id(int id);

    void setMaxThreadCount(int count);

signals:
    void bpmCalculated(int id, double bpm);

private:
    QThreadPool* threadPool;
    NonBlockingQueue<BpmWorkItem> queue;
    std::atomic<int> work_count{0};
    std::atomic<bool> is_running{false};
    
    QSet<int> activeIds;
    QMutex activeIdsMutex;
};
