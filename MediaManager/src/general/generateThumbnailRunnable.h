#pragma once
#include <QRunnable>
#include <QQueue>
#include <QProcess>
#include <QPair>
#include "SafeQueue.h"

class generateThumbnailManager;
struct ThumbnailCommand;

class generateThumbnailRunnable :
    public QObject, public QRunnable
{
    Q_OBJECT
public:
    SafeQueue<ThumbnailCommand> *queue = nullptr;
    QProcess *process = nullptr;
    generateThumbnailManager* manager = nullptr;
    generateThumbnailRunnable(SafeQueue<ThumbnailCommand> *queue, generateThumbnailManager* manager = nullptr, QObject* parent = nullptr);
    void run() override;
    static void generateThumbnail(QProcess& process, QString suffix, QString path);
    static void deleteThumbnail(QString path);
    static QString getThumbnailSuffix(QString path);
    static QString getThumbnailFilename(QString path);
    ~generateThumbnailRunnable();
signals:
    void openFile(QString path);
};

