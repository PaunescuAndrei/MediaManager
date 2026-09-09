#pragma once
#include <QThread>
#include <QMutex>
#include <QDate>
#include <chrono>
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
    bool running = true;
    bool watching = false;
    int CLASS_COUNT = 0;
    QMutex data_lock = QMutex();
    MainApp *App = nullptr;
    sqliteDB* db = nullptr;
    HWND old_foreground_window = nullptr;
    QSharedPointer<BasePlayer> mainPlayer = nullptr;
    QList<QSharedPointer<BasePlayer>> Players = QList<QSharedPointer<BasePlayer>>();

    // Session tracking (one session from first player start to last player stop)
    int m_currentSessionId = -1;
    QDateTime m_sessionStartTime;                          // wall clock for DB string
    std::chrono::microseconds m_sessionStartMonotonic{};   // monotonic for duration
    int currentSessionId() const { return m_currentSessionId; }
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
    void run() override;
    ~VideoWatcherQt();

private:
    // Roll the calendar day at midnight: flush each active player's watch-history row so
    // the next checkpoint opens a fresh row for the new day. Day queries bucket rows by
    // watch_history.watched_at, which is stamped once at INSERT, so without this a row
    // opened before midnight would keep yesterday's date and its post-midnight time
    // would count for the wrong day. The session is left open - a watch that crosses
    // midnight stays one session.
    void rollDayIfNeeded();
    // Calendar day the watch-history rows were last rolled for.
    QDate m_lastRollDate;
    // Emit the pending watch-time delta for the counter, advancing the watermark
    // first so the same seconds are never counted twice.
    void emitCounterDelta(QSharedPointer<BasePlayer> player, double watched);
    // Close the current session (update session_history), return session duration in seconds.
    // Returns -1 if no session was active. Resets m_currentSessionId to -1.
    double endCurrentSession();
signals:
    void updateProgressBarSignal(double position,double duration, QSharedPointer<BasePlayer> player, bool running);
    void updateTaskbarIconSignal(bool watching);
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