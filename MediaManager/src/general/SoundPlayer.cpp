#include "stdafx.h"
#include "SoundPlayer.h"
#include <QSoundEffect>
#include "MainApp.h"
#include "utils.h"
#include <QAudioOutput>
#include "definitions.h"

SoundPlayer::SoundPlayer(QObject* parent ,int volume) {
    this->media_devices = new QMediaDevices(this->parent);
	this->parent = parent;
    this->volume = utils::volume_convert(volume);
    this->loadSoundEffects();
}

QMediaPlayer* SoundPlayer::get_player()
{
    QMediaPlayer* player = new QMediaPlayer(this->parent);
    QAudioOutput* audioOutput = new QAudioOutput(player);
    audioOutput->setDevice(QMediaDevices::defaultAudioOutput());
    QObject::connect(this->media_devices, &QMediaDevices::audioOutputsChanged, player, [player]() {
        QAudioDevice default_device = QMediaDevices::defaultAudioOutput();
        if (player and player->audioOutput() and player->audioOutput()->device() != default_device) {
            player->audioOutput()->setDevice(default_device);
        }
    });
    audioOutput->setVolume(this->volume);
    player->setAudioOutput(audioOutput);
    return player;
}

QMediaPlayer* SoundPlayer::play(QString sound_path, bool auto_delete, bool log)
{
    if (this->running && !sound_path.isEmpty()) {
        if (log)
            qMainApp->logger->log(QString("Playing SoundEffect \"%1\"").arg(sound_path), "SoundEffect", sound_path);
        QMediaPlayer* player = this->get_player();
        return this->play(player, sound_path, auto_delete, log);
    }
    return nullptr;
}

QMediaPlayer* SoundPlayer::play(QMediaPlayer* player, QString sound_path, bool auto_delete, bool log)
{
    if (this->running && !sound_path.isEmpty()) {
        if (log)
            qMainApp->logger->log(QString("Playing SoundEffect \"%1\"").arg(sound_path), "SoundEffect", sound_path);
        if (auto_delete) {
            QObject::connect(player, &QMediaPlayer::mediaStatusChanged, [player](QMediaPlayer::MediaStatus status) {if (status == QMediaPlayer::EndOfMedia) { player->deleteLater(); } });
            QObject::connect(player, &QMediaPlayer::errorOccurred, [player](QMediaPlayer::Error error, const QString& errorString) {qDebug() << errorString; player->deleteLater(); });
        }
        player->setSource(QUrl::fromLocalFile(sound_path));
        player->play();
        return player;
    }
    return nullptr;
}

QMediaPlayer* SoundPlayer::playSoundEffect() {
    QString sound_path = "";
    if (!this->sound_effects.isEmpty()) {
        sound_path = *utils::select_randomly(this->sound_effects.begin(), this->sound_effects.end());
    }
    return this->play(sound_path, true, true);
}

QMediaPlayer* SoundPlayer::playSoundEffect(QMediaPlayer* player) {
    QString sound_path = "";
    if (!this->sound_effects.isEmpty()) {
        sound_path = *utils::select_randomly(this->sound_effects.begin(), this->sound_effects.end());
    }
    return this->play(player, sound_path, true, true);
}

void SoundPlayer::playSoundEffectChain() {
    QMediaPlayer* player = this->playSoundEffect();
    player->disconnect();
    QObject::connect(player, &QMediaPlayer::mediaStatusChanged, [this, player](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            this->playSoundEffectChain_continue(this->effect_chain_chance, player);
        }
    });
    QObject::connect(player, &QMediaPlayer::errorOccurred, [player](QMediaPlayer::Error error, const QString& errorString) {qDebug() << errorString; player->deleteLater(); });
}


void SoundPlayer::playSoundEffectChain(double chance) {
    if (this->running && utils::randfloat(0,1) < chance) {
        QMediaPlayer* player = this->playSoundEffect();
        player->disconnect();
        QObject::connect(player, &QMediaPlayer::mediaStatusChanged, [this,chance, player](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia) {
                this->playSoundEffectChain_continue(chance / 1.1, player);
            }
        });
        QObject::connect(player, &QMediaPlayer::errorOccurred, [player](QMediaPlayer::Error error, const QString& errorString) {qDebug() << errorString; player->deleteLater(); });
    }
}

void SoundPlayer::playSoundEffectChain_continue(double chance, QMediaPlayer* player) {
    if (this->running && utils::randfloat(0, 1) < chance) {
        this->playSoundEffect(player);
        player->disconnect();
        QObject::connect(player, &QMediaPlayer::mediaStatusChanged, [this,chance, player](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia) {
                this->playSoundEffectChain_continue(chance / 1.1, player);
            }
        });
        QObject::connect(player, &QMediaPlayer::errorOccurred, [player](QMediaPlayer::Error error, const QString& errorString) {qDebug() << errorString; player->deleteLater(); });
    }
    else {
        player->deleteLater();
    }
}

QMediaPlayer* SoundPlayer::playSpecialSoundEffect() {
    QString sound_path = "";
    if (!this->sound_effects_special.isEmpty()) {
        sound_path = *utils::select_randomly(this->sound_effects_special.begin(), this->sound_effects_special.end());
    }
    return this->play(sound_path, false, true);
}

QMediaPlayer* SoundPlayer::playSpecialSoundEffect(QMediaPlayer* player) {
    QString sound_path = "";
    if (!this->sound_effects_special.isEmpty()) {
        sound_path = *utils::select_randomly(this->sound_effects_special.begin(), this->sound_effects_special.end());
    }
    return this->play(player, sound_path, false, true);
}

void SoundPlayer::loadSoundEffects() {
    this->sound_effects = utils::getFilesQt(SOUND_EFFECTS_PATH, true);
    this->sound_effects_special = utils::getFilesQt(SOUND_EFFECTS_SPECIAL_PATH, true);
    this->intro_effects = utils::getFilesQt(SOUND_EFFECTS_INTRO_PATH, true);
    this->end_effects = utils::getFilesQt(SOUND_EFFECTS_END_PATH, true);
}

//QSoundEffect* SoundPlayer::get_player()
//{
//    QSoundEffect* effect = new QSoundEffect(this->parent);
//    effect->setVolume(this->volume);
//    return effect;
//}
//
//QSoundEffect* SoundPlayer::play(QString sound_path,bool log)
//{
//    if (this->running && !sound_path.isEmpty()) {
//        if(log)
//            qMainApp->logger->log(QString("Playing SoundEffect \"%1\"").arg(sound_path),"SoundEffect", sound_path);
//        QSoundEffect* effect = new QSoundEffect(this->parent);
//        QObject::connect(effect, &QSoundEffect::playingChanged, [effect] {if (!effect->isPlaying()) { effect->deleteLater(); } });
//        QObject::connect(effect, &QSoundEffect::statusChanged, [effect] {if (effect->status() == QSoundEffect::Error) { effect->deleteLater(); }});
//        effect->setSource(QUrl::fromLocalFile(sound_path));
//        effect->setVolume(this->volume);
//        effect->play();
//        return effect;
//    }
//    return nullptr;
//}
//
//QSoundEffect* SoundPlayer::playSoundEffect() {
//    return this->play(*select_randomly(this->soundEffects.begin(), this->soundEffects.end()));
//}
//
//void SoundPlayer::playSoundEffectChain() {
//    QSoundEffect* effect = this->playSoundEffect();
//    QObject::connect(effect, &QSoundEffect::playingChanged, [effect, this] {if (!effect->isPlaying()) { effect->deleteLater(); this->playSoundEffectChain(this->effect_chain_chance); } });
//}
//
//void SoundPlayer::playSoundEffectChain(double chance) {
//    if (this->running && randfloat(0,1) < chance) {
//        QSoundEffect* effect = this->playSoundEffect();
//        QObject::connect(effect, &QSoundEffect::playingChanged, [effect, chance, this] {if (!effect->isPlaying()) { effect->deleteLater(); this->playSoundEffectChain(chance / 1.1); } });
//    }
//}

void SoundPlayer::setVolume(int volume)
{
    this->volume = utils::volume_convert(volume);
}

void SoundPlayer::stop()
{
    this->running = false;
}

void SoundPlayer::start()
{
    this->running = true;
}

SoundPlayer::~SoundPlayer()
{
    this->media_devices->deleteLater();
}
