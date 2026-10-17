#pragma once
#include <QThread>
#include <QMutex>
#include <QDate>
#include <chrono>
#include <atomic>
#include "sqliteDB.h"
#include "BasePlayer.h"
#include "MpcPlayer.h"
#include <QSharedPointer>

class MainApp;

class VideoWatcherQt :
    public QThread
{
    Q_OBJECT
public:
    // Cleared by the GUI thread on shutdown; atomic so the watcher loop cannot keep
    // reading a cached true and outlive the wait() in MainApp::stop_handle.
    std::atomic<bool> running = true;
    // Written by the watcher thread, read by the mascots animation thread.
    std::atomic<bool> watching{ false };
    int CLASS_COUNT = 0;
    QMutex data_lock = QMutex();
    MainApp *App = nullptr;
    sqliteDB* db = nullptr;
    HWND old_foreground_window = nullptr;
    QSharedPointer<BasePlayer> mainPlayer = nullptr;
    QList<QSharedPointer<BasePlayer>> Players = QList<QSharedPointer<BasePlayer>>();

    // Session tracking (one session from first player start to last player stop)
    // Written by the GUI thread (ensureSession/resetSession) and read by the watcher
    // thread, so publish it atomically instead of as a plain int.
    std::atomic<int> m_currentSessionId{ -1 };
    QDateTime m_sessionStartTime;                          // wall clock for DB string
    std::chrono::microseconds m_sessionStartMonotonic{};   // monotonic for duration
    int currentSessionId() const { return m_currentSessionId.load(); }
    double currentSessionTime() const;
    void resetSession();  // invalidate the current session ID (e.g. after a DB restore)
    int ensureSession();  // create a session if none active, returns session ID

    VideoWatcherQt(MainApp* App, QObject* parent = nullptr);
    QSharedPointer<BasePlayer> newPlayer(QString path, int video_id);
    void clearData(bool include_mainplayer);
    void setMainPlayer(QSharedPointer<BasePlayer> player);
    void clearMainPlayer();
    void clearAfterMainVideoEnd();
    void toggle_window();
    void checkpointPlayer(QSharedPointer<BasePlayer> player, int intervalSeconds);
    void handleExternalVideoChange(QSharedPointer<BasePlayer> player);
    bool shouldCountWatchTime(QSharedPointer<BasePlayer> player);
    // Copy of the active players taken under data_lock, for GUI readers that would
    // otherwise iterate the list while the watcher thread adds/removes players.
    QList<QSharedPointer<BasePlayer>> playersSnapshot();
    void run() override;
    ~VideoWatcherQt();

private:
    // Database file the watcher connection is opened on. The connection itself is opened
    // on the watcher thread in run(): Qt's SQLite driver opens connections with
    // SQLITE_OPEN_NOMUTEX, so a connection may only be used by the thread that created it.
    QString m_dbLocation;
    // Roll the calendar day at midnight: flush each active player's watch-history row so
    // the next checkpoint opens a fresh row for the new day. Day queries bucket rows by
    // watch_history.watched_at, which is stamped once at INSERT, so without this a row
    // opened before midnight would keep yesterday's date and its post-midnight time
    // would count for the wrong day. The session is left open - a watch that crosses
    // midnight stays one session.
    void rollDayIfNeeded();
    // Calendar day the watch-history rows were last rolled for.
    QDate m_lastRollDate;
    // Last watching state the watcher emitted for the taskbar; watcher thread only.
    bool m_taskbarWatching = false;
    // Emit the pending watch-time delta for the counter, advancing the watermark
    // first so the same seconds are never counted twice.
    void emitCounterDelta(QSharedPointer<BasePlayer> player, double watched);
    // Close the current session (update session_history), return session duration in seconds.
    // Returns -1 if no session was active. Resets m_currentSessionId to -1.
    // `target` is the connection to write through; it defaults to the watcher's own
    // (worker thread) connection. clearData() runs on the GUI thread and passes the GUI
    // connection instead.
    double endCurrentSession(sqliteDB* target = nullptr);
signals:
    void updateProgressBarSignal(double position,double duration, QSharedPointer<BasePlayer> player, bool running);
    void updateTaskbarIconSignal(bool watching);
    // Emitted on watching transitions; the GUI thread applies it to the taskbar (a COM/UI
    // object), instead of the watcher calling into it.
    void updateTaskbarPauseSignal(bool paused);
    void updateMusicPlayerSignal(bool flag);
    void timeWatchedIncrementSignal(double delta);
    void sessionEndedSignal(QString category, int videoCount, int completedCount, double watchTimeSec, double sessionTimeSec);
};

//#include <iostream>
//#include <chrono>
//
//// long operation to time
//long long fib(long long n) {
//    if (n < 2) {
//        return n;
//    }
//    else {
//        return fib(n - 1) + fib(n - 2);
//    }
//}
//
//int main() {
//    auto start_time = std::chrono::high_resolution_clock::now();
//
//    long long input = 32;
//    long long result = fib(input);
//
//    auto end_time = std::chrono::high_resolution_clock::now();
//    auto time = end_time - start_time;
//
//    while (frameTime > milliseconds(0))
// 
//    std::cout << "result = " << result << '\n';
//    std::cout << "fib(" << input << ") took " <<
//        time / std::chrono::milliseconds(1) << "ms to run.\n";
//}