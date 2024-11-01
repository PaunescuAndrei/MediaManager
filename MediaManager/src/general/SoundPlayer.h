#pragma once
#include <QtGlobal>
#include <string>
#include <QList>
#include <QObject>
#include <QMediaPlayer>
#include <QStringList>

class QSoundEffect;

class SoundPlayer
{
public:
	QObject* parent = nullptr;
	qreal volume;
	bool running = false;
	QStringList sound_effects;
	QStringList sound_effects_special;
	QStringList intro_effects;
	QStringList end_effects;
	bool effects_playpause = true;
	bool effects_chain_playpause = true;
	double effect_chance = 0.5;
	double effect_chain_chance = 1;
	SoundPlayer(QObject* parent = nullptr, int volume = 3);
	void loadSoundEffects();
	//QSoundEffect* get_player();
	//QSoundEffect* play(QString sound_path, bool log = true);
	//QSoundEffect* playSoundEffect();
	//void playSoundEffectChain();
	//void playSoundEffectChain(double chance);
	QMediaPlayer* get_player();
	QMediaPlayer* play(QString sound_path, bool auto_delete, bool log);
	QMediaPlayer* play(QMediaPlayer* player, QString sound_path, bool auto_delete, bool log);
	QMediaPlayer* playSoundEffect();
	QMediaPlayer* playSoundEffect(QMediaPlayer* player);
	QMediaPlayer* playSpecialSoundEffect();
	QMediaPlayer* playSpecialSoundEffect(QMediaPlayer* player);
	void playSoundEffectChain();
	void playSoundEffectChain(double chance);
	void playSoundEffectChain_continue(double chance, QMediaPlayer* player = nullptr);
	void setVolume(int volume);
	void stop();
	void start();
	~SoundPlayer();
};

