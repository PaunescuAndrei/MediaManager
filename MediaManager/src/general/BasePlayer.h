#pragma once  
#include <QThread>  
#include <windows.h>  
#include <QProcess>  
#include <QMutex>
#include "NonBlockingQueue.h"  
#include "MpcApi.h"  
#include "PlayerCommands.h"  
#include <chrono>
#include <atomic>
#include <QDateTime>

class MainApp;  

// One consistent view of a player's video state for the watcher thread. Gathered under the
// player's lock so a tick cannot mix fields from before and after a video change. Plain
// numeric/flag values are copied under the lock too; they are written without it, so a
// torn value is possible but harmless (only implicitly shared types can corrupt memory).
struct PlayerSnapshot {
    QString videoPath;
    QString trackedVideoPath;
    QString targetVideoPath;
    QString videoType;
    QString category;
    int videoId = -1;
    int activeWatchHistoryRowId = -1;
    double position = -1;
    double duration = -1;
    double startProgress = 0.0;
    bool changeInProgress = false;
    bool trackExternalVideo = false;
    bool endOfVideo = false;
    QDateTime lastCheckpointTime;
};

class BasePlayer : public QThread  
{  
    Q_OBJECT  

public:  
    // Written by the thread that owns the player and read by the player thread's run
    // loop, so it has to be atomic: a cached read here is a thread that never stops.
    std::atomic<bool> running = false;
    NonBlockingQueue< std::shared_ptr<PlayerCommand>> queue = NonBlockingQueue< std::shared_ptr<PlayerCommand>>();
    int video_id = -1;  
    int* CLASS_COUNT;  
    MainApp* App = nullptr;  
    qint64 pid = -1;  
    double position = -1;  
    double startProgress = 0.0;
    double duration = -1;
    bool change_in_progress = false;  
    bool paused = false;  
    bool end_of_video = false;  
    HWND player_hwnd = HWND();  
    QProcess* process;  
    double totalWatchedTimeSeconds = 0.0;
    bool wasPlayingLastCheck = false;
    double m_videoWatchedBaseline = 0.0;
    double lastCheckpointWatchedTime = 0.0;
    // Finish state preserved across the end-of-video Next flow so a Replay in
    // the Next chooser can still credit the completed watch — position is reset
    // to -1 and the checkpoint advanced before the chooser appears. Only valid
    // between the finish-dialog Next handler and replayVideo's finalize branch.
    double replayEndPosition = -1;
    double replayCheckpoint = -1;
    int activeWatchHistoryRowId = -1;
    bool trackExternalVideo = false;
    BasePlayer(QString video_path, int video_id, int* CLASS_COUNT, QObject* parent = nullptr, MainApp* App = nullptr);  
    virtual ~BasePlayer();  
    // Cached from QProcess::stateChanged so the watcher never touches the QProcess object.
    bool isProcessAlive() const;  
    virtual void openPlayer(QString video_path = "", double position_seconds = -1) = 0;  
    virtual void askForStatus() = 0;  
    virtual void closePlayer() = 0;
    virtual void unloadCurrentFile() = 0;  
    virtual void setPaused(bool paused, bool direct = false) = 0;  
    virtual void toggleFullScreen(bool direct = false) = 0;  
    virtual void displayOsdMessage(QString message, int duration, bool direct = false) = 0;  
    virtual void drop() = 0;  
    virtual void changeVideo(QString path, int video_id, double position = 0) = 0;  
    void startWatchedTiming();
    void stopWatchedTiming();
    double getTotalWatchedTime();
    void updateWatchedTiming();
    void start();
    void resetVideoTiming();
    double videoWatchedTime();
    bool shouldCheckpoint(const QDateTime& now, int intervalSeconds);
    void recordVideoStart();
    // The video strings are shared with the watcher thread: private storage, every access
    // through these. The setters lock; the getters return a copy.
    QString videoPath() const;
    QString targetVideoPath() const;
    QString trackedVideoPath() const;
    QString videoType() const;
    QString category() const;
    void setVideoPath(const QString& path);
    void setTargetVideoPath(const QString& path);
    void setTrackedVideoPath(const QString& path);
    void setVideoType(const QString& type);
    void setCategory(const QString& category);
    // QDateTime is implicitly shared, so the two timestamps are guarded like the strings.
    QDateTime videoStartWallClock() const;
    QDateTime lastCheckpointTime() const;
    void setVideoStartWallClock(const QDateTime& time);
    void setLastCheckpointTime(const QDateTime& time);
    // One locked copy of everything the watcher needs for a tick.
    PlayerSnapshot snapshot() const;
    // Serializes watch-history row persistence for this player: clearData() (GUI) and the
    // watcher can otherwise flush the same player at the same time.
    QMutex& historyLock() const { return m_historyLock; }
signals:  
    void endOfVideoSignal();  
    void pausedChangedSignal(bool paused);  

private slots:  
    void onPausedChanged(bool paused);  

private:
    QString m_videoPath;
    QString m_targetVideoPath;
    QString m_trackedVideoPath;
    QString m_videoType;
    QString m_category;
    QDateTime m_videoStartWallClock;
    QDateTime m_lastCheckpointTime;
    std::shared_ptr<std::chrono::microseconds> m_watchedTimeStart;
    std::atomic<bool> m_processAlive{ false };
    mutable QMutex m_stateLock;
    mutable QMutex m_historyLock;
};
