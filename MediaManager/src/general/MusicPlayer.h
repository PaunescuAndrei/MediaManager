#pragma once
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QStringList>

class MainApp;

class MusicPlayer
{
public:
	MainApp* App = nullptr;
	QMediaPlayer *player = nullptr;
	QAudioOutput *audioOutput = nullptr;
	bool startingSoundFlag = true;
	bool loop = false;
	QStringList playlist = QStringList();
	QString trackPlaylistFolder = "";
	QString initTrack = "";
	QString currentTrack = "";
	QString nextTrack = "";
	int nextTrackId = -1;
	double length = 0;
	//double position = 0;
	MusicPlayer(MainApp *App);
	void init(QString initTrack = "", QString trackPlaylistFolder = "", bool loop = false, bool random_start = false, bool startingSoundEffect = true);
	void playSpecificSong(QString path);
	double get_length();
	double get_pos();
	void selectNextTrack();
	void loadTrack(QString path);
	void playTrack(QString path);
	void playNextTrack();
	void play();
	void stop();
	void pause();
	void unPause();
	void togglePause();
	void mediaStatusChanged(QMediaPlayer::MediaStatus status);
	void setVolume(int volume);
	~MusicPlayer();
};

