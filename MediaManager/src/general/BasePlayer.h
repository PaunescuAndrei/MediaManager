#pragma once  
#include <QThread>  
#include <windows.h>  
#include <QProcess>  
#include "SafeQueue.h"  
#include "MpcApi.h"  
#include "PlayerCommands.h"  
#include <chrono>  

class MainApp;  

class BasePlayer : public QThread  
{  
    Q_OBJECT  

public:  
    bool running = false;  
    SafeQueue< std::shared_ptr<PlayerCommand>> queue = SafeQueue< std::shared_ptr<PlayerCommand>>();  
    QString video_path = "";  
    QString target_video_path = "";  
    int video_id = -1;  
    int* CLASS_COUNT;  
    MainApp* App = nullptr;  
    qint64 pid = -1;  
    double position = -1;  
    double duration = -1;  
    bool change_in_progress = false;  
    bool paused = false;  
    bool end_of_video = false;  
    HWND player_hwnd = HWND();  
    QProcess* process;  
    std::shared_ptr<std::chrono::microseconds> sessionTimeStart = nullptr;  
    std::shared_ptr<std::chrono::microseconds> watchedTimeStart = nullptr;  
    double totalWatchedTimeSeconds = 0.0;
    bool wasPlayingLastCheck = false;  
    BasePlayer(QString video_path, int video_id, int* CLASS_COUNT, QObject* parent = nullptr, MainApp* App = nullptr);  
    virtual ~BasePlayer();  
    bool isProcessAlive();  
    virtual void openPlayer(QString video_path = "", double position_seconds = -1) = 0;  
    virtual void askForStatus() = 0;  
    virtual void closePlayer() = 0;  
    virtual void setPaused(bool paused, bool direct = false) = 0;  
    virtual void toggleFullScreen(bool direct = false) = 0;  
    virtual void displayOsdMessage(QString message, int duration, bool direct = false) = 0;  
    virtual void drop() = 0;  
    virtual void changeVideo(QString path, int video_id, double position = 0) = 0;  
    void startSessionTiming();  
    void stopSessionTiming();  
    void startWatchedTiming();  
    void stopWatchedTiming();  
    double getSessionTime();  
    double getTotalWatchedTime();  
    void updateWatchedTiming();
    void start();
signals:  
    void endOfVideoSignal();  
    void pausedChangedSignal(bool paused);  

private slots:  
    void onPausedChanged(bool paused);  
};
