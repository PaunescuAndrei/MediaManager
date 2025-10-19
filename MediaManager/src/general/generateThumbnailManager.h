#pragma once
#include "generateThumbnailRunnable.h"
#include "SafeQueue.h"
#include <QThreadPool>
#include <QString>
#include <QMutex>
#include <QList>
#include <QTimer>
#include <QObject>
#include <atomic>

struct ThumbnailCommand {
    QString path;
    bool overwrite;
    bool openWhenFinished = false;
};

class generateThumbnailManager:
    public QObject
{
    Q_OBJECT
public:
    SafeQueue<ThumbnailCommand> queue = SafeQueue<ThumbnailCommand>();
    QThreadPool *thumbsThreadPool;
    std::atomic<int> work_count = 0;
    void enqueue_work(ThumbnailCommand work);
    void enqueue_work_front(ThumbnailCommand work);
    void clear_work();
    void add_work_count(int value);
    void set_work_count(int value);
    void start();
    void rebuildThumbnailCache(QSqlDatabase& db, bool overwrite = false);
    generateThumbnailManager(unsigned int threads_number, QObject* parent = nullptr);
    ~generateThumbnailManager();
signals:
    void openFile(QString path);
//public slots:
//    void onOpenFile(QString path);
};

