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
    connect(this->process, &QProcess::stateChanged, this, [this](QProcess::ProcessState state) {
        m_processAlive.store(state != QProcess::NotRunning);
    });
    connect(this, &BasePlayer::pausedChangedSignal, this, &BasePlayer::onPausedChanged);
}

BasePlayer::~BasePlayer()
{
    // The graceful close is done by shutdown() while the app is still running; by the time
    // a player is destroyed (switching DB, teardown) there is nothing left to save, so do
    // not run QProcess::terminate()'s WM_CLOSE-to-every-window close path here.
    this->running = false;
    if (this->process->state() != QProcess::NotRunning) {
        this->process->kill();
        this->process->waitForFinished(500);
    }
    //this->send_message(CMD_CLOSEAPP);
    //this->process->waitForFinished(5000);
    this->process->deleteLater();
}

bool BasePlayer::isProcessAlive() const
{
    return m_processAlive.load();
}

void BasePlayer::handleReportedVolume(int volume)
{
    const int clamped = qBound(0, volume, 100);
    this->m_volume.store(clamped);
    emit this->volumeChanged(clamped);
}

void BasePlayer::start() {
    // Set before starting the thread: shutdown() may be called right after start() and a
    // run() that set it back to true could overwrite that request.
    this->running = true;
    this->resetVideoTiming();
    QThread::start();
}

void BasePlayer::shutdown()
{
    // Stop the polling loop: it sends askForStatus() to the player every 100 ms and those
    // commands must not keep arriving while the player is closing.
    this->running = false;

    if (this->process && this->process->state() != QProcess::NotRunning) {
        // Destroy our slave window before asking the player to close. The player notifies
        // the host with a blocking SendMessage during its close, and nothing is pumping
        // messages on our side at that point, so a live window would make the player wait
        // for us instead of exiting.
        this->drop();
        // CMD_CLOSEAPP makes the player close its own main window, i.e. the same path as a
        // normal window close. QProcess::terminate() posts WM_CLOSE to every top-level
        // window of the player (and a WM_CLOSE thread message), which is not how a user
        // closes it and which its teardown can fail on.
        this->closePlayer();
    }
    // Deliberately no wait here: the close request is asynchronous and the exit sound is
    // already playing. The caller waits for the process where it needs the player's window
    // to be gone first, and the process is killed at teardown if still running.
}

bool BasePlayer::waitForProcessExit(int timeoutMs)
{
    if (!this->process || this->process->state() == QProcess::NotRunning)
        return true;
    return this->process->waitForFinished(timeoutMs);
}

void BasePlayer::startWatchedTiming() {
    QMutexLocker lock(&m_stateLock);
    if (m_watchedTimeStart != nullptr)
        return;
    m_watchedTimeStart = std::make_shared<std::chrono::microseconds>(utils::QueryUnbiasedInterruptTimeChrono());
}

void BasePlayer::stopWatchedTiming() {
    QMutexLocker lock(&m_stateLock);
    if (m_watchedTimeStart != nullptr) {
        double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(utils::QueryUnbiasedInterruptTimeChrono() - *m_watchedTimeStart).count();
        this->totalWatchedTimeSeconds += elapsed;
        m_watchedTimeStart.reset();
    }
}

double BasePlayer::getTotalWatchedTime() {
    QMutexLocker lock(&m_stateLock);
    double total = this->totalWatchedTimeSeconds;
    if (m_watchedTimeStart != nullptr) {
        double currentSession = std::chrono::duration_cast<std::chrono::duration<double>>(utils::QueryUnbiasedInterruptTimeChrono() - *m_watchedTimeStart).count();
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

void BasePlayer::onPausedChanged(bool paused) {
    if (paused)
        this->stopWatchedTiming();
    else
        this->startWatchedTiming();
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
    const QDateTime now = QDateTime::currentDateTime();
    QMutexLocker lock(&m_stateLock);
    m_videoStartWallClock = now;
    m_lastCheckpointTime = now;
}

bool BasePlayer::shouldCheckpoint(const QDateTime& now, int intervalSeconds) {
    QMutexLocker lock(&m_stateLock);
    return intervalSeconds > 0
        && m_lastCheckpointTime.isValid()
        && m_lastCheckpointTime.secsTo(now) >= intervalSeconds;
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

QDateTime BasePlayer::videoStartWallClock() const {
    QMutexLocker lock(&m_stateLock);
    return m_videoStartWallClock;
}

QDateTime BasePlayer::lastCheckpointTime() const {
    QMutexLocker lock(&m_stateLock);
    return m_lastCheckpointTime;
}

void BasePlayer::setVideoStartWallClock(const QDateTime& time) {
    QMutexLocker lock(&m_stateLock);
    m_videoStartWallClock = time;
}

void BasePlayer::setLastCheckpointTime(const QDateTime& time) {
    QMutexLocker lock(&m_stateLock);
    m_lastCheckpointTime = time;
}

PlayerSnapshot BasePlayer::snapshot() const {
    PlayerSnapshot s;
    QMutexLocker lock(&m_stateLock);
    // Implicitly shared members: must be copied under the lock.
    s.videoPath = m_videoPath;
    s.trackedVideoPath = m_trackedVideoPath;
    s.targetVideoPath = m_targetVideoPath;
    s.videoType = m_videoType;
    s.category = m_category;
    s.lastCheckpointTime = m_lastCheckpointTime;
    // Plain values: their writers do not take this lock, so these can be a tick stale or
    // torn, but that cannot corrupt memory and is no worse than the previous reads.
    s.videoId = video_id;
    s.activeWatchHistoryRowId = activeWatchHistoryRowId;
    s.position = position;
    s.duration = duration;
    s.startProgress = startProgress;
    s.changeInProgress = change_in_progress;
    s.trackExternalVideo = trackExternalVideo;
    s.endOfVideo = end_of_video;
    return s;
}
