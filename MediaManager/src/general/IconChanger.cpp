#include "stdafx.h"
#include "IconChanger.h"
#include "utils.h"
#include "MainApp.h"
#include "MainWindow.h"
#include <QCollator>
#include "libzippp/libzippp.h"
#include <QJsonDocument>
#include <QtConcurrent>
#include <QFileInfo>
#include "definitions.h"

IconChanger::IconChanger(MainApp* App,bool random_icon, QObject* parent) : QThread(parent)
{
	this->App = App;
	this->random_icon = random_icon;
	this->setIcon_lock.set();
	this->db = new sqliteDB(this->App->db->location, "iconchanger_con");
	this->fps_modifier = this->App->config->get("animated_icon_fps_modifier").toDouble();
	this->setIconArchiveFiles(ANIMATED_ICONS_PATH);
}

void IconChanger::initIcon(bool instant)
{
	if (this->random_icon) {
		this->setRandomIcon(instant, false);
		return;
	}
	this->currentIconPath = this->db->getMainInfoValue("currentIconPath",this->App->currentDB);
	if (this->currentIconPath.isEmpty() || !QFileInfo::exists(this->currentIconPath)) {
		this->setRandomIcon(instant, true);
	}
	else {
		this->setIconNonBlocking(this->currentIconPath,true, instant);
	}
		
}

void IconChanger::setIcon(QString path, bool cache_check,bool instant)
{
	if(qMainApp)
		qMainApp->logger->log(QStringLiteral("Setting icon to \"%1\"").arg(path), "Icon", path);
	if (QFileInfo::exists(path)) {

		this->setIcon_lock.clear();

		this->frame_dict.clear();
		this->frame_pairlist.clear();
		this->ordered_filenames.clear();
		this->icon.clear();
		this->compression = false;
		libzippp::ZipArchive zf(path.toStdString());
		zf.open(libzippp::ZipArchive::ReadOnly);

		libzippp::ZipEntry info_entry = zf.getEntry("_info.txt");
		
		if (info_entry.isNull()) {
			this->setIcon_lock.set();
			return;
		}
		else {
			int size = info_entry.getSize();
			void* binaryData = info_entry.readAsBinary();
			QString info = QString::fromUtf8(QByteArray::fromRawData(static_cast<const char*>(binaryData), size));
			delete[] binaryData;
			for (QString line : info.split(QRegularExpression("\n|\r\n|\r"))) {
				QString line_left = line.section(':', 0, 0).trimmed();
				QString line_right = line.section(':', 1).trimmed();
				if (line_left == "fps") {
					this->fps = line_right.toDouble();
				}
;;			}
		}

		libzippp::ZipEntry dict_entry = zf.getEntry("_dict.json");
		if (!dict_entry.isNull()) {
			this->compression = true;
		}

		bool cached_icons = false;
		if (cache_check == true) {
			cached_icons = checkIconCache(path, &this->icon, &this->frame_dict, &this->frame_pairlist, &this->ordered_filenames);
		}
		if (!cached_icons) {
			std::vector<libzippp::ZipEntry> entries = zf.getEntries();
			std::vector<libzippp::ZipEntry>::iterator it;
			for (it = entries.begin(); it != entries.end(); ++it) {
				libzippp::ZipEntry entry = *it;
				QString filename = QString::fromStdString(entry.getName());

				if (filename != "_info.txt" && filename != "_dict.pkl" && filename != "_dict.json") {

					this->ordered_filenames.append(filename);

					int size = entry.getSize();
					void* binaryData = entry.readAsBinary();
					QImage img = QImage::fromData(QByteArray::fromRawData(static_cast<const char*>(binaryData), size));
					img = img.scaled(128, 128, Qt::KeepAspectRatio);
					QPixmap pixmapImg = QPixmap::fromImage(img);
					this->icon[filename] = QIcon(pixmapImg);
					delete[] binaryData;
				}
				else if (filename == "_dict.json") {
					this->compression = true;

					int size = entry.getSize();
					void* binaryData = entry.readAsBinary();
					QJsonDocument json = QJsonDocument::fromJson(QByteArray::fromRawData(static_cast<const char*>(binaryData), size));
					delete[] binaryData;
					QMap<QString, QVariant> map = json.toVariant().toMap();
					QList<QVariant> frame_dict_list = map["_frame_count"].toList();
					for (QVariant itempair : frame_dict_list) {
						QList<QVariant> itempairlist = itempair.toList();
						this->frame_pairlist.append(QPair<QString, int>(itempairlist[0].toString(), itempairlist[1].toInt()));
					}
					map.remove("_frame_dict");
					for (QString key : map.keys()) {
						this->frame_dict[key] = map[key].toString();
					}
				}  
			}

			QCollator collator;
			collator.setNumericMode(true);
			std::sort(this->ordered_filenames.begin(), this->ordered_filenames.end(), collator);
		
			if (cache_check == true) {
				QString cachepath = QFileInfo(path).baseName();
				QDir().mkpath(ANIMATED_ICONS_CACHE_PATH);
				cachepath = QString(ANIMATED_ICONS_CACHE_PATH) + "/" + cachepath + ".iconcache_c";

				QFile file(cachepath);
				if (!file.open(QIODevice::WriteOnly)) {
					if (qMainApp)
						qMainApp->logger->log(QStringLiteral("Failed to open cache file for writing: %1").arg(cachepath), "IconChanger", "ERROR");
				} else {
					QDataStream out(&file);  
					out << this->icon;   
					out << this->frame_dict;
					out << this->frame_pairlist;
					out << this->ordered_filenames;
				}
				file.close();
			}
		}

		zf.close();
		if(!this->icon.isEmpty() && instant)
			emit animatedIconSignal(this->icon.first());
		this->currentIconPath = path;
		this->update_copies = true;
		this->setIcon_lock.set();
	}
}

void IconChanger::setIconNonBlocking(QString path, bool cache_check, bool instant)
{
	if (this->future_seticon.isRunning()) {
		this->future_seticon.waitForFinished();
	}
	this->future_seticon = QtConcurrent::run([this,path,cache_check,instant] {this->setIcon(path,cache_check,instant); });
}

void IconChanger::rebuildIconCacheNonBlocking()
{
	if (!this->future_rebuildcache.isRunning()) {
		this->future_rebuildcache = QtConcurrent::run([this] {this->rebuildIconCache(); });
	}
}

void IconChanger::setIconArchiveFiles(QString path)
{
	this->archiveFiles = utils::getFilesQt(path);
}

bool IconChanger::checkIconCache(QString path, QMap<QString, QIcon>* icon, QMap<QString, QString>* frame_dict, QList<QPair<QString, int>>* frame_pairlist, QList<QString>* ordered_filenames)
{
	QString iconname = QFileInfo(path).baseName();
	if (iconname == "tmp") {
		return false;
	}
	QString cachepath = QString(ANIMATED_ICONS_CACHE_PATH) + "/" + iconname + ".iconcache_c";
	QFile file(cachepath);
	if (file.exists() && file.size() > 0) {
		if (!file.open(QIODevice::ReadOnly)) {
			if (qMainApp)
				qMainApp->logger->log(QStringLiteral("Failed to open cache file for reading: %1").arg(cachepath), "IconChanger", "ERROR");
			file.close();
			return false;
		} else {
			QDataStream in(&file);    
			in >> *icon >> *frame_dict >> *frame_pairlist >> *ordered_filenames; 
		}
		file.close();
		return true;
	}
	return false;
}

void IconChanger::setRandomIcon(bool instant, bool updatedb)
{
	if (!this->archiveFiles.isEmpty()) {
		int index = utils::randint(0, this->archiveFiles.size() - 1);
		this->currentIconPath = this->archiveFiles.at(index);
		this->setIconNonBlocking(this->currentIconPath, true, instant);
		if(updatedb)
			this->db->setMainInfoValue("currentIconPath", this->App->currentDB, this->currentIconPath);
	}
}

void IconChanger::rebuildIconCache() {
	QStringList files = utils::getFilesQt(ANIMATED_ICONS_PATH);
	for (QString iconpath : files) {
		if (!this->running) break;
		QMap<QString, QIcon> _icon = QMap<QString, QIcon>();
		QMap<QString, QString> _frame_dict = QMap<QString, QString>();
		QList<QPair<QString, int>> _frame_pairlist = QList<QPair<QString, int>>();
		QList<QString> _ordered_filenames = QList<QString>();

		libzippp::ZipArchive zf(iconpath.toStdString());
		zf.open(libzippp::ZipArchive::ReadOnly);

		std::vector<libzippp::ZipEntry> entries = zf.getEntries();
		std::vector<libzippp::ZipEntry>::iterator it;
		for (it = entries.begin(); it != entries.end(); ++it) {
			libzippp::ZipEntry entry = *it;
			QString filename = QString::fromStdString(entry.getName());

			if (filename != "_info.txt" && filename != "_dict.pkl" && filename != "_dict.json") {

				_ordered_filenames.append(filename);

				int size = entry.getSize();
				void* binaryData = entry.readAsBinary();
				QImage img = QImage::fromData(QByteArray::fromRawData(static_cast<const char*>(binaryData), size));
				img = img.scaled(128, 128, Qt::KeepAspectRatio);
				QPixmap pixmapImg = QPixmap::fromImage(img);
				_icon[filename] = QIcon(pixmapImg);
				delete[] binaryData;
			}
			else if (filename == "_dict.json") {
				int size = entry.getSize();
				void* binaryData = entry.readAsBinary();
				QJsonDocument json = QJsonDocument::fromJson(QByteArray::fromRawData(static_cast<const char*>(binaryData), size));
				delete[] binaryData;
				QMap<QString, QVariant> map = json.toVariant().toMap();
				QList<QVariant> frame_dict_list = map["_frame_count"].toList();
				for (QVariant itempair : frame_dict_list) {
					QList<QVariant> itempairlist = itempair.toList();
					_frame_pairlist.append(QPair<QString, int>(itempairlist[0].toString(), itempairlist[1].toInt()));
				}
				map.remove("_frame_dict");
				for (QString key : map.keys()) {
					_frame_dict[key] = map[key].toString();
				}
			}
		}

		QCollator collator;
		collator.setNumericMode(true);
		std::sort(_ordered_filenames.begin(), _ordered_filenames.end(), collator);

		QString cachepath = QFileInfo(iconpath).baseName();
		QDir().mkpath(ANIMATED_ICONS_CACHE_PATH);
		cachepath = QString(ANIMATED_ICONS_CACHE_PATH) + "/" + cachepath + ".iconcache_c";

		QFile file(cachepath);
		if (!file.open(QIODevice::WriteOnly)) {
			if (qMainApp)
				qMainApp->logger->log(QStringLiteral("Failed to open cache file for writing: %1").arg(cachepath), "IconChanger", "ERROR");
		} else {
			QDataStream out(&file);
			out << _icon;
			out << _frame_dict;
			out << _frame_pairlist;
			out << _ordered_filenames;
		}
		file.close();
		zf.close();
	}
}

void IconChanger::showFirstIcon()
{
	if (!this->icon.isEmpty())
		emit animatedIconSignal(this->icon.first());
}

void IconChanger::run()
{
	this->initIcon(false);

	QMap<QString, QIcon> _icon = QMap<QString, QIcon>(this->icon);
	QMap<QString, QString> _frame_dict = QMap<QString, QString>(this->frame_dict);
	QList<QPair<QString, int>> _frame_pairlist = QList<QPair<QString, int>>(this->frame_pairlist);
	QList<QString> _ordered_filenames = QList<QString>(this->ordered_filenames);

	int failcount = 0;

	while (this->running) {
		if (this->update_copies == true) {
			_icon = QMap<QString, QIcon>(this->icon);
			_frame_dict = QMap<QString, QString>(this->frame_dict);
			_frame_pairlist = QList<QPair<QString, int>>(this->frame_pairlist);
			_ordered_filenames = QList<QString>(this->ordered_filenames);
			this->update_copies = false;
		}
		this->animatedIconEvent.wait();
		if (this->App->mainWindow->iconWatchingState == true) {
			if (failcount != 0)
				failcount = 0;
			if (this->compression) {
				for (QPair<QString, int> &i : _frame_pairlist) {
					if (!this->setIcon_lock.isSet() || running == false)
						break;
					if (this->update_copies == true) {
						_icon = QMap<QString, QIcon>(this->icon);
						_frame_dict = QMap<QString, QString>(this->frame_dict);
						_frame_pairlist = QList<QPair<QString, int>>(this->frame_pairlist);
						_ordered_filenames = QList<QString>(this->ordered_filenames);
						this->update_copies = false;
						break;
					}
					if (!i.first.isEmpty() && !_frame_dict.value(i.first).isEmpty()) {
						emit animatedIconSignal(_icon.value(_frame_dict.value(i.first)));
						this->msleep(lround(((1.0 / this->fps)*this->fps_modifier) * 1000) * i.second);
					}
					else {
						break;
					}
					if (this->App->mainWindow->iconWatchingState == false && running == true) {
						this->animatedIconEvent.clear();
						this->animatedIconEvent.wait();
					}
				}
			}
			else {
				this->setIcon_lock.wait();
				for (const QString &i : _ordered_filenames) {
					if (!this->setIcon_lock.isSet() || running == false)
						break;
					if (this->update_copies == true) {
						_icon = QMap<QString, QIcon>(this->icon);
						_frame_dict = QMap<QString, QString>(this->frame_dict);
						_frame_pairlist = QList<QPair<QString, int>>(this->frame_pairlist);
						_ordered_filenames = QList<QString>(this->ordered_filenames);
						this->update_copies = false;
						break;
					}
					if (!i.isEmpty()) {
						emit animatedIconSignal(_icon.value(i));
						this->msleep(lround(((1.0 / this->fps) * this->fps_modifier) * 1000));
					}
					else {
						break;
					}
					if (this->App->mainWindow->iconWatchingState == false && running == true) {
						this->animatedIconEvent.clear();
						this->animatedIconEvent.wait();
					}
				}
			}
		}
		//possible failsafe in case of event clear failing somewhere.
		else if (this->animatedIconEvent.isSet() && running == true) {
			failcount++;
			//maybe replace this with a proper time difference check
			if (failcount == 1000) {
				this->animatedIconEvent.clear();
				failcount = 0;
			}
		}
	}
}

IconChanger::~IconChanger()
{
	if (this->future_rebuildcache.isRunning()) {
		this->future_rebuildcache.waitForFinished();
	}
	if (this->future_seticon.isRunning()) {
		this->future_seticon.waitForFinished();
	}
}
