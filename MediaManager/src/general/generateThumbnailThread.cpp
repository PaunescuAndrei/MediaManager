#include "stdafx.h"
#include "generateThumbnailThread.h"
#include <QDebug>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include "generateThumbnailManager.h"
#include <MainApp.h>
#include "definitions.h"

generateThumbnailThread::generateThumbnailThread(SafeQueue<ThumbnailCommand>* queue, generateThumbnailManager* manager, QObject* parent) : QThread(parent)
{
	this->queue = queue;
    this->manager = manager;
}

void generateThumbnailThread::run()
{
    this->process = new QProcess();
	while (!this->queue->isEmpty()) {
        if (this->process->state() == QProcess::NotRunning) {
            std::optional<ThumbnailCommand> item_ = this->queue->dequeue();
            if (item_) {
                ThumbnailCommand item = item_.value();
                connect(this->process, &QProcess::finished, this, [item](int exitCode, QProcess::ExitStatus exitStatus) {
                    if (exitCode != 0) {
                        if(qMainApp)
                            qMainApp->logger->log(QString("mtn.exe exit code %1 for \"%2\"").arg(QString::number(exitCode), item.path), "Thumbnail", item.path);
                        else
                            qDebug() << "mtn.exe exit code " << exitCode;
                    }
                });
                QString suffix = "_" + QString(QCryptographicHash::hash(item.path.toStdString(), QCryptographicHash::Md5).toHex()) + ".jpg";

                QFileInfo fi(item.path);
                if (!item.overwrite and QFileInfo::exists(QDir::toNativeSeparators(QString(THUMBNAILS_CACHE_PATH) + "/" + fi.completeBaseName() + suffix))) {
                    this->process->disconnect();
                }
                else {
                    generateThumbnailThread::generateThumbnail(*process, suffix, item.path);
                    process->start();
                    process->waitForFinished(-1);
                }
                if (this->manager) {
                    this->manager->add_work_count(-1);
                }
            }
        }
	}
}

void generateThumbnailThread::generateThumbnail(QProcess &process, QString suffix, QString path) {
    QDir().mkpath(THUMBNAILS_CACHE_PATH);
    process.setProgram(QDir(QDir::currentPath()).filePath(THUMBNAILS_PROGRAM_PATH));
    process.setArguments({ "-P", "-j", "85", "-q", "-r", "5", "-c", "6", "-w", "3840", "-D", "0", "-b", "1", "-k", "181818", "-F", "bebebe:22","-O",THUMBNAILS_CACHE_PATH,"-o",suffix, path });
}

generateThumbnailThread::~generateThumbnailThread()
{
    if (this->process != nullptr) {
        delete this->process;
    }
}

