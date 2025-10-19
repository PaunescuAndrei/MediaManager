#include "stdafx.h"
#include "generateThumbnailRunnable.h"
#include <QDebug>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include "generateThumbnailManager.h"
#include <MainApp.h>
#include "definitions.h"

generateThumbnailRunnable::generateThumbnailRunnable(SafeQueue<ThumbnailCommand>* queue, generateThumbnailManager* manager, QObject* parent) : QObject(parent), QRunnable()
{
    this->queue = queue;
    this->manager = manager;
}

void generateThumbnailRunnable::run()
{
    while (!this->queue->isEmpty()) {
        std::optional<ThumbnailCommand> item_ = this->queue->dequeue();
        if (item_) {
            ThumbnailCommand item = item_.value();
            QProcess process;

            QString thumbnail_suffix = generateThumbnailRunnable::getThumbnailSuffix(item.path);
            QString thumbnail_filename = generateThumbnailRunnable::getThumbnailFilename(item.path);
            QString cachepath = QString(THUMBNAILS_CACHE_PATH) + "/" + thumbnail_filename;

            QFileInfo fi(item.path);
            if (!item.overwrite and QFileInfo::exists(QDir::toNativeSeparators(cachepath))) {
                if (item.openWhenFinished) {
                    emit openFile(cachepath);
                }
            }
            else {
                generateThumbnailRunnable::generateThumbnail(process, thumbnail_suffix, item.path);
                process.start();
                process.waitForFinished(-1);

                if (process.exitCode() != MtnSuccess && process.exitCode() != MtnSuccessNoFrame) {
                    if (qMainApp)
                        qMainApp->logger->log(QString("mtn.exe exit code %1 for \"%2\"").arg(QString::number(process.exitCode()), item.path), "Thumbnail", item.path);
                    else
                        qDebug() << "mtn.exe exit code " << process.exitCode();
                }

                if (process.exitCode() == MtnSuccess || process.exitCode() == MtnSuccessNoFrame) {
                    if (item.openWhenFinished) {
                        emit openFile(cachepath);
                    }
                }
            }
            if (this->manager) {
                this->manager->add_work_count(-1);
            }
        }
    }
}

void generateThumbnailRunnable::generateThumbnail(QProcess& process, QString suffix, QString path) {
	QDir().mkpath(THUMBNAILS_CACHE_PATH);
	process.setProgram(QDir(QDir::currentPath()).filePath(THUMBNAILS_PROGRAM_PATH));
	//process.setArguments({ "-P", "-j", "85", "-q", "-r", "5", "-c", "6", "-w", "3840", "-D", "0", "-b", "1", "-k", "181818", "-F", "bebebe:22","-O",THUMBNAILS_CACHE_PATH,"-o",suffix, path });
    process.setArguments({ "-P"});
}

void generateThumbnailRunnable::deleteThumbnail(QString path)
{
    QString thumbnail_name = generateThumbnailRunnable::getThumbnailFilename(path);
    QString thumbnail_path = QString(THUMBNAILS_CACHE_PATH) + "/" + thumbnail_name;
    QFile file(thumbnail_path);
    if (file.exists()) {
        file.remove();
    }
}

QString generateThumbnailRunnable::getThumbnailSuffix(QString path) {
    return "_" + QString(QCryptographicHash::hash(path.toStdString(), QCryptographicHash::Md5).toHex()) + ".jpg";
}

QString generateThumbnailRunnable::getThumbnailFilename(QString path)
{
    QString suffix = generateThumbnailRunnable::getThumbnailSuffix(path);
    QString filename = QFileInfo(path).completeBaseName() + suffix;
    return filename;
}

generateThumbnailRunnable::~generateThumbnailRunnable()
{
}

