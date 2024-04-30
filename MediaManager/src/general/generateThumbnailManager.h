#pragma once
#include "generateThumbnailThread.h"
#include "SafeQueue.h"
#include <QString>
#include <QMutex>
#include <QList>
#include <QTimer>

struct ThumbnailCommand {
    QString path;
    bool overwrite;
};

class generateThumbnailManager
{
public:
    SafeQueue<ThumbnailCommand> queue = SafeQueue<ThumbnailCommand>();
    QList<generateThumbnailThread*> threadList = QList<generateThumbnailThread*>();
    QMutex data_lock = QMutex();
    int work_count = 0;
    unsigned int threads_number = 0;
    void enqueue_work(ThumbnailCommand work);
    void clear_work();
    void add_work_count(int value);
    void set_work_count(int value);
    void start();
    void rebuildThumbnailCache(QSqlDatabase& db, bool overwrite = false);
    generateThumbnailManager(unsigned int threads_number = 3);
    ~generateThumbnailManager();
};

