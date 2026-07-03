#include "stdafx.h"
#include "BasePlayer.h"
#include "MainApp.h"
#include "utils.h"

BasePlayer::BasePlayer(QString video_path, int video_id, int* CLASS_COUNT, QObject* parent, MainApp* App)
{
    this->App = App;
    this->target_video_path = video_path;
    this->video_id = video_id;
    this->CLASS_COUNT = CLASS_COUNT;
    this->process = new QProcess(parent);
    connect(this, &BasePlayer::pausedChangedSignal, this, &BasePlayer::onPausedChanged);
}

BasePlayer::~BasePlayer()
{
    this->process->terminate();
    //this->send_message(CMD_CLOSEAPP);
    //this->process->waitForFinished(5000);
    this->process->deleteLater();
}

bool BasePlayer::isProcessAlive()
{
    return !(this->process->state() == QProcess::NotRunning);
}

void BasePlayer::start() {
    this->resetVideoTiming();
    QThread::start();
}

void BasePlayer::startWatchedTiming() {
    if (this->watchedTimeStart != nullptr) {
        this->watchedTimeStart.reset();
    }
    this->watchedTimeStart = std::make_shared<std::chrono::microseconds>(utils::QueryUnbiasedInterruptTimeChrono());
}

void BasePlayer::stopWatchedTiming() {
    if (this->watchedTimeStart != nullptr) {
        double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(utils::QueryUnbiasedInterruptTimeChrono() - *this->watchedTimeStart).count();
        this->totalWatchedTimeSeconds += elapsed;
        this->watchedTimeStart.reset();
    }
}

double BasePlayer::getTotalWatchedTime() {
    double total = this->totalWatchedTimeSeconds;
    if (this->watchedTimeStart != nullptr) {
        double currentSession = std::chrono::duration_cast<std::chrono::duration<double>>(utils::QueryUnbiasedInterruptTimeChrono() - *this->watchedTimeStart).count();
        total += currentSession;
    }
    return total;
}

void BasePlayer::updateWatchedTiming() {
    bool isCurrentlyPlaying = !this->paused && !this->video_path.isEmpty();
    
    if (isCurrentlyPlaying && !this->wasPlayingLastCheck) {
        this->startWatchedTiming();
    } else if (!isCurrentlyPlaying && this->wasPlayingLastCheck) {
        this->stopWatchedTiming();
    }
    
    this->wasPlayingLastCheck = isCurrentlyPlaying;
}

void BasePlayer::onPausedChanged(bool paused) {
    bool isCurrentlyPlaying = !paused;
    if (isCurrentlyPlaying && this->watchedTimeStart == nullptr) {
        this->startWatchedTiming();
    } else if (!isCurrentlyPlaying && this->watchedTimeStart != nullptr) {
        this->stopWatchedTiming();
    }
}

void BasePlayer::resetVideoTiming()
{
    m_videoWatchedBaseline = getTotalWatchedTime();
    lastCheckpointWatchedTime = 0.0;
    recordVideoStart();
}

double BasePlayer::videoWatchedTime()
{
    return getTotalWatchedTime() - m_videoWatchedBaseline;
}

void BasePlayer::recordVideoStart() {
    videoStartWallClock = QDateTime::currentDateTime();
    lastCheckpointTime = videoStartWallClock;
}

bool BasePlayer::shouldCheckpoint(const QDateTime& now, int intervalSeconds) {
    return intervalSeconds > 0
        && lastCheckpointTime.isValid()
        && lastCheckpointTime.secsTo(now) >= intervalSeconds;
}
