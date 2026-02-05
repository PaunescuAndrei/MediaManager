#pragma once
#include <QRunnable>
#include <QObject>
#include <QString>
#include "NonBlockingQueue.h"

struct BpmWorkItem {
    int id;
    QString path;
};

class CalculateBpmManager;

class CalculateBpmRunnable : public QObject, public QRunnable {
    Q_OBJECT
public:
    CalculateBpmRunnable(NonBlockingQueue<BpmWorkItem>* queue, CalculateBpmManager* manager = nullptr, QObject* parent = nullptr);
    ~CalculateBpmRunnable();
    void run() override;

signals:
    void bpmCalculated(int id, double bpm);

private:
    NonBlockingQueue<BpmWorkItem>* queue;
    CalculateBpmManager* manager;
};
