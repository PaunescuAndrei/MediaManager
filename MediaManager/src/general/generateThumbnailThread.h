#pragma once
#include <QThread>
#include <QQueue>
#include <QProcess>
#include <QPair>
#include "SafeQueue.h"

class generateThumbnailManager;
struct ThumbnailCommand;

class generateThumbnailThread :
    public QThread
{
    Q_OBJECT
public:
    SafeQueue<ThumbnailCommand> *queue = nullptr;
    QProcess *process = nullptr;
    generateThumbnailManager* manager = nullptr;
    generateThumbnailThread(SafeQueue<ThumbnailCommand> *queue, generateThumbnailManager* manager = nullptr, QObject* parent = nullptr);
    void run() override;
    static void generateThumbnail(QProcess& process, QString suffix, QString path);
    ~generateThumbnailThread();
};

