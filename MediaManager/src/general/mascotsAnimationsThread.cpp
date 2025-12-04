#include "stdafx.h"
#include "mascotsAnimationsThread.h"
#include "MainApp.h"
#include "utils.h"
#include <QProcess>
#include <QDir>
#include <QtConcurrent>
#include <QFileInfo>
#include <QMutexLocker>
#include "definitions.h"

mascotsAnimationsThread::mascotsAnimationsThread(MainApp* App, bool random_change, double random_chance, QObject* parent) : QThread(parent)
{
	this->App = App;
	this->random_change = random_change;
	this->random_chance = random_chance;
	this->running = true;
	this->frequency = this->App->config->get("mascots_frequency").toInt();
}

void mascotsAnimationsThread::init() {
	if (this->App->musicPlayer)
		this->current_track = this->App->musicPlayer->currentTrack;
	else
		this->current_track = "";
	this->beats.clear();
	this->current_beat = 0;
}

void mascotsAnimationsThread::set_random_chance(double chance) {
	this->random_chance = chance;
}

bool mascotsAnimationsThread::checkBeatsCache(QString path, QList<double>* beats)
{
	QString iconname = QFileInfo(path).baseName();
	QString cachepath = QString(BEATS_CACHE_PATH) + "/" + iconname + ".beatscache_c";
	QFile file(cachepath);
	if (file.exists() && file.size() > 0) {
		if (!file.open(QIODevice::ReadOnly)) {
			return false;
		}
		QDataStream in(&file);
		in >> *beats;
		file.close();
		return true;
	}
	return false;
}

void mascotsAnimationsThread::update_beats(QString track_path,bool cache_check) {
	bool cached_beats = false;
	if (cache_check) {
		cached_beats = this->checkBeatsCache(track_path, &this->beats);
	}
	if (cached_beats) {
		return;
	}
	else {
		if(this->madmom)
			this->process->setArguments({ "tools\\get_beats\\get_beats.py",this->current_track});
		else
			this->process->setArguments({ "tools\\get_beats\\get_beats.py",this->current_track,"--librosa"});
		this->process->start();
		this->process->waitForFinished(-1);
		QString beatstring = QString(process->readAllStandardOutput());
		beatstring = beatstring.trimmed();
		if (beatstring.startsWith('[') && beatstring.endsWith(']')) {
			beatstring = beatstring.mid(1); // Remove first character
			beatstring.chop(1);      // Remove last character
		}
		QStringList beatsstringlist = beatstring.split(",");
		if (beatsstringlist.size() > 1) {
			for (QString &s : beatsstringlist) {
				if (!s.isEmpty()) {
					this->beats.append(s.toDouble());
				}
			}
			QString cachepath = QFileInfo(this->current_track).baseName();
			QDir().mkpath(BEATS_CACHE_PATH);
			cachepath = QString(BEATS_CACHE_PATH) + "/" + cachepath + ".beatscache_c";

			QFile file(cachepath);
			if (file.open(QIODevice::WriteOnly)) {
				QDataStream out(&file);
				out << this->beats;
				file.close();
			}
		}
	}
}

void mascotsAnimationsThread::rebuildBeatsCacheNonBlocking()
{
	if (!this->future_rebuildcache.isRunning()) {
		this->future_rebuildcache = QtConcurrent::run([this] {this->rebuildBeatsCache(); });
	}
}

void mascotsAnimationsThread::rebuildBeatsCache() {
	QStringList files = utils::getFilesQt(this->App->musicPlayer->trackPlaylistFolder,true);
	for (QString trackpath : files) {
		QList<double> newbeats = QList<double>();
		QProcess process = QProcess();
		QString exePath = QDir(QDir::currentPath() + "\\tools\\get_beats\\Scripts\\python.exe").absolutePath();
		process.setProgram(exePath);
		if (this->madmom)
			process.setArguments({ "tools\\get_beats\\get_beats.py", trackpath});
		else
			process.setArguments({ "tools\\get_beats\\get_beats.py", trackpath,"--librosa" });
		process.start();
		process.waitForFinished(-1);
		QString beatstring = QString(process.readAllStandardOutput());
		beatstring = beatstring.trimmed();
		if (beatstring.startsWith('[') && beatstring.endsWith(']')) {
			beatstring = beatstring.mid(1); // Remove first character
			beatstring.chop(1);      // Remove last character
		}
		QStringList beatsstringlist = beatstring.split(",");
		if (beatsstringlist.size() > 1) {
			for (QString& s : beatsstringlist) {
				if (!s.isEmpty()) {
					newbeats.append(s.toDouble());
				}
			}
			QString cachepath = QFileInfo(trackpath).baseName();
			QDir().mkpath(BEATS_CACHE_PATH);
			cachepath = QString(BEATS_CACHE_PATH) + "/" + cachepath + ".beatscache_c";

			QFile file(cachepath);
			if (file.open(QIODevice::WriteOnly)) {
				QDataStream out(&file);
				out << newbeats;
				file.close();
			}
		}
	}
}

void mascotsAnimationsThread::beat()
{
	if (this->random_change && utils::randfloat(0, 1) < this->random_chance) {
		emit this->updateMascotsSignal();
	}
	else {
		emit this->updateMascotsAnimationSignal();
	}
}

void mascotsAnimationsThread::update_current_track() {
	QMutexLocker lock(&this->data_lock);
	this->old_track = this->current_track;
	if (this->App->musicPlayer)
		this->current_track = this->App->musicPlayer->currentTrack;
	else
		this->current_track = "";
}

void mascotsAnimationsThread::reset() {
	this->update_current_track();
	QMutexLocker lock(&this->data_lock);
	this->beats.clear();
	this->beats_length = 0;
	this->current_beat = 0;
}

void mascotsAnimationsThread::clear() {
	QMutexLocker lock(&this->data_lock);
	this->beats.clear();
	this->current_beat = 0;
	this->current_track = "";
}

void mascotsAnimationsThread::set_current_track(QString track) {
	QMutexLocker lock(&this->data_lock);
	this->current_track = track;
}

void mascotsAnimationsThread::stop_running() {
	this->runningEvent.clear();
	this->clear();
}

void mascotsAnimationsThread::start_running() {
	this->runningEvent.set();
}

void mascotsAnimationsThread::run()
{
	this->process = new QProcess();
	QString exePath = QDir(QDir::currentPath() + "\\tools\\get_beats\\Scripts\\python.exe").absolutePath();
	this->process->setProgram(exePath);
	while (this->running) {
		this->runningEvent.wait();
		if (this->App->musicPlayer) {
			if (this->current_track.isEmpty() && !this->App->musicPlayer->currentTrack.isEmpty() && !this->App->musicPlayer->startingSoundFlag) {
				this->set_current_track(this->App->musicPlayer->currentTrack);
			}
			if (this->beats.isEmpty() && !this->current_track.isEmpty()) {
				QMutexLocker lock(&this->data_lock);
				this->update_beats(this->current_track);
				this->beats_length = this->beats.size();
				this->current_beat = utils::take_closest(this->beats, this->App->musicPlayer->player->position() / 1000.0);
				lock.unlock();
			}
			//qDebug() << this->App->musicPlayer->player->position() / 1000.0 << this->beats.value(this->current_beat);
			if (this->current_beat < this->beats_length) {
				if (!this->beats.isEmpty() && !this->App->musicPlayer->startingSoundFlag && ((this->App->musicPlayer->player->position() / 1000.0) >= (this->beats.at(this->current_beat) - 0.05))) {
					this->beat();
					this->current_beat++;
				}
			}
			this->msleep(50);
		}
		else if (this->running) {
			if (!this->beats.isEmpty())
				this->clear();
			if (!this->App->VW->watching)
				this->beat();
			this->msleep(this->frequency);
		}
	}
}

mascotsAnimationsThread::~mascotsAnimationsThread()
{
	this->process->kill();
	delete this->process;
}

