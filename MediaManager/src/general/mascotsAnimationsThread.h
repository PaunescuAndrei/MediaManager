#pragma once
#include <QThread>
#include "EventLock.h"
#include <QMutex>
#include <QProcess>
#include <QFuture>
#include <atomic>

#include "beat_this_api.h"

class MainApp;

class mascotsAnimationsThread :
    public QThread
{
    Q_OBJECT
public:
    MainApp* App;
    bool random_change = true;
    double random_chance = 0.1;
    // Read by the animation loop, written by the GUI thread on shutdown; atomic so the
    // loop cannot keep reading a cached true.
    std::atomic<bool> running = false;
    BeatThis::BeatThis* beatAnalyzer = nullptr;
    int frequency = 400;
    QList<double> beats = QList<double>();
    int beats_length = 0;
    int current_beat = 0;
    QString current_track = "";
    QString old_track = "";
    QMutex data_lock = QMutex();
    EventLock runningEvent = EventLock();
    QFuture<void> future_rebuildcache;
    mascotsAnimationsThread(MainApp* App, bool random_change = true,double random_chance = 0.1, QObject* parent = nullptr);
    void init();
    void set_random_chance(double chance);
    bool checkBeatsCache(QString path, QList<double>* beats);
    void update_beats(QString track_path, bool cache_check = true);
    void rebuildBeatsCacheNonBlocking();
    void rebuildBeatsCache();
    void beat();
    void update_current_track();
    void reset();
    void clear();
    void set_current_track(QString track);
    void stop_running();
    void start_running();
    // Stop the thread and release it if it is parked in runningEvent.wait(), so that
    // waiting on it cannot deadlock (see MainApp::stop_handle).
    void stop();
    ~mascotsAnimationsThread();
    void run() override;
signals:
    void updateMascotsSignal();
    void updateMascotsAnimationSignal();
};

