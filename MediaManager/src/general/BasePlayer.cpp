#include "stdafx.h"
#include "BasePlayer.h"
#include "MainApp.h"
#include "utils.h"

BasePlayer::BasePlayer(QString video_path, int video_id, int* CLASS_COUNT, QObject* parent, MainApp* App)
{
    this->App = App;
    this->setTargetVideoPath(video_path);
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
    bool isCurrentlyPlaying = !this->paused && !this->videoPath().isEmpty();
    
    if (isCurrentlyPlaying && !this->wasPlayingLastCheck) {
        this->startWatchedTiming();
    } else if (!isCurrentlyPlaying && this->wasPlayingLastCheck) {
        this->stopWatchedTiming();
    }
    
    this->wasPlayingLastCheck = isCurrentlyPlaying;
}

void BasePlayer::onPausedChanged(bool paused) {    bool isCurrentlyPlaying = !paused;
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

QString BasePlayer::videoPath() const {
    QMutexLocker lock(&m_stateLock);
    return m_videoPath;
}

QString BasePlayer::targetVideoPath() const {
    QMutexLocker lock(&m_stateLock);
    return m_targetVideoPath;
}

QString BasePlayer::trackedVideoPath() const {
    QMutexLocker lock(&m_stateLock);
    return m_trackedVideoPath;
}

QString BasePlayer::videoType() const {
    QMutexLocker lock(&m_stateLock);
    return m_videoType;
}

QString BasePlayer::category() const {
    QMutexLocker lock(&m_stateLock);
    return m_category;
}

void BasePlayer::setVideoPath(const QString& path) {
    QMutexLocker lock(&m_stateLock);
    m_videoPath = path;
}

void BasePlayer::setTargetVideoPath(const QString& path) {
    QMutexLocker lock(&m_stateLock);
    m_targetVideoPath = path;
}

void BasePlayer::setTrackedVideoPath(const QString& path) {
    QMutexLocker lock(&m_stateLock);
    m_trackedVideoPath = path;
}

void BasePlayer::setVideoType(const QString& type) {
    QMutexLocker lock(&m_stateLock);
    m_videoType = type;
}

void BasePlayer::setCategory(const QString& category) {
    QMutexLocker lock(&m_stateLock);
    m_category = category;
}
