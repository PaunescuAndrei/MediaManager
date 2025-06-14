#pragma once
#include <QThread>
#include <QMutex>
#include <QDate>
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
    std::shared_ptr <std::chrono::microseconds> videoWatcherSessionTimeStart = nullptr;
    QList<QSharedPointer<BasePlayer>> Players = QList<QSharedPointer<BasePlayer>>();
    QString lastSessionDateCached = "";
    VideoWatcherQt(MainApp* App, QObject* parent = nullptr);
    QSharedPointer<BasePlayer> newPlayer(QString path, int video_id);
    void clearData(bool include_mainplayer);
    void setMainPlayer(QSharedPointer<BasePlayer> player);
    void clearMainPlayer();
    void clearAfterMainVideoEnd();
    void toggle_window();
    void incrementTimeWatchedTotal(int value);
    void incrementTimeSessionTotal(int value);
    void resetDailyStats();
    void run() override;
    ~VideoWatcherQt();
signals:
    void updateProgressBarSignal(double position,double duration, QSharedPointer<BasePlayer> player, bool running);
    void updateTaskbarIconSignal(bool watching);
    void updateMusicPlayerSignal(bool flag);
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