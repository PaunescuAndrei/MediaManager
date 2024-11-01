#include "stdafx.h"
#include "MusicPlayer.h"
#include "MainApp.h"
#include "MainWindow.h"
#include <QUrl>
#include "utils.h"
#include <QMimeType>
#include <QMimeDatabase>
#include <QObject>
#include <QAudioDevice>
#include <QMediaFormat>
#include <QFile>

MusicPlayer::MusicPlayer(MainApp* App)
{
	this->App = App;
	this->player = new QMediaPlayer(App);
	this->audioOutput = new QAudioOutput(App);
	this->audioOutput->setMuted(true);
	this->player->setAudioOutput(this->audioOutput);
	this->audioOutput->setVolume(utils::volume_convert(5));
	this->audioOutput->setMuted(false);
	QObject::connect(this->player, &QMediaPlayer::mediaStatusChanged, [=](QMediaPlayer::MediaStatus status) {this->mediaStatusChanged(status); });
	QObject::connect(this->player, &QMediaPlayer::durationChanged, [=](qint64 duration) {this->length = duration/1000.0; });
	//QObject::connect(this->player, &QMediaPlayer::positionChanged, [=](qint64 position) {this->position = position / 1000.0; });
}

void MusicPlayer::init(QString initTrack, QString trackPlaylistFolder, bool loop,bool random_start, bool startingSoundEffect)
{
	if (startingSoundEffect == true) {
		this->startingSoundFlag = true;
		if (this->App->soundPlayer && !this->App->soundPlayer->intro_effects.isEmpty()) {
			QString track = *utils::select_randomly(this->App->soundPlayer->intro_effects.begin(), this->App->soundPlayer->intro_effects.end());
			this->loadTrack(track);
		}
	}
	else {
		this->startingSoundFlag = false;
		this->loadTrack(initTrack);
	}
	this->initTrack = initTrack;
	this->trackPlaylistFolder = trackPlaylistFolder;
	this->loop = loop;
	if (!this->trackPlaylistFolder.isEmpty())
		this->playlist = utils::getFilesQt(this->trackPlaylistFolder,true);
	else {
		this->playlist.clear();
	}
	this->nextTrack = "";
	this->nextTrackId = -1;
	if (!this->playlist.isEmpty() && random_start) {
		utils::shuffle_qlist(this->playlist);
		this->selectNextTrack();
	}
	if (this->nextTrack.isEmpty() && !this->playlist.isEmpty())
		this->selectNextTrack();
}

void MusicPlayer::playSpecificSong(QString path) {
	//qDebug() << QString::fromStdString(path) << QFile(QString::fromStdString(path)).exists();
	qMainApp->logger->log(QString("Playing Music \"%1\"").arg(path),"MusicPlayer", path);
	this->player->setSource(QUrl::fromLocalFile(path));
	this->player->play();
}

double MusicPlayer::get_length() {
	return this->length;
}

double MusicPlayer::get_pos() {
	return this->player->position();
}

void MusicPlayer::selectNextTrack() {
	QMimeDatabase db;
	if (this->loop && !this->startingSoundFlag)
		this->nextTrack = this->currentTrack;
	else if(!this->playlist.isEmpty()) {
		if (this->nextTrackId + 1 < this->playlist.size()) {
			QMimeType guess_type = db.mimeTypeForFile(this->playlist.at(this->nextTrackId+1));
			if (guess_type.name().startsWith("audio")) {
				this->nextTrackId++;
				this->nextTrack = this->playlist.at(this->nextTrackId);
			}
			else {
				this->playlist.removeAt(this->nextTrackId + 1);
				this->selectNextTrack();
			}
		}
		else {
			this->nextTrackId = -1;
			utils::shuffle_qlist(this->playlist);
			this->selectNextTrack();
		}
	}
}

void MusicPlayer::loadTrack(QString path) {
	this->currentTrack = path;
	//this->length = this->get_length();
}

void MusicPlayer::playTrack(QString path) {
	this->loadTrack(path);
	this->playSpecificSong(path);
}

void MusicPlayer::playNextTrack() {
	this->playTrack(this->nextTrack);
	this->selectNextTrack();
}

void MusicPlayer::play() {
	if (this->currentTrack.isEmpty()) {
		this->playNextTrack();
	}
	else {
		this->playTrack(this->currentTrack);
	}
}

void MusicPlayer::stop() {
	this->player->stop();
}

void MusicPlayer::pause() {
	this->player->pause();
}

void MusicPlayer::unPause() {
	if (this->get_pos() / 1000.0 > this->length) {
		this->player->setPosition((this->length-2) * 1000);
	}
	this->player->play();
}

void MusicPlayer::togglePause() {
	//todo maybe
}

void MusicPlayer::mediaStatusChanged(QMediaPlayer::MediaStatus status)
{
	if (status == QMediaPlayer::EndOfMedia) {
		if (this->startingSoundFlag) {
			this->startingSoundFlag = false;
			if(this->App)
				this->App->mainWindow->intro_played = true;
			if (!this->initTrack.isEmpty()) {
				this->playTrack(this->initTrack);
			}
			else {
				this->playNextTrack();
			}
		}
		else {
			this->playNextTrack();
		}
		this->App->MascotsAnimation->reset();
	}
	//qDebug() << status;
	//qDebug() << this->player->error() << this->player->errorString();
	//qDebug() << this->player->source();
}

void MusicPlayer::setVolume(int volume) {
	this->audioOutput->setVolume(utils::volume_convert(volume));
}

MusicPlayer::~MusicPlayer()
{
	delete this->player;
	delete this->audioOutput;
}
