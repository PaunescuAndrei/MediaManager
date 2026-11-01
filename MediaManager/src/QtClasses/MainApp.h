#pragma once
#include <QApplication>
#include "config.h"
#include "sqliteDB.h"
#include "taskbar.h"
#include "VideoWatcherQt.h"
#include "MusicPlayer.h"
#include "SoundPlayer.h"
#include "mascotsAnimationsThread.h"
#include "RawInputKeyboard.h"
#include "quitEaterEventFilter.h"
#include <QLocalServer>
#include "logger.h"
#include <UIAutomationClient.h>
#include "CalculateBpmManager.h"

#define qMainApp (static_cast<MainApp *>(QCoreApplication::instance()))

class MainWindow;
class generalEventFilter;
class TooltipEventFilter;
class LogDialog;
class QErrorMessage;
class shokoAPI;
class MascotImageCache;
class MascotPrefetcher;

class MainApp : public QApplication
{
public:
    HWND hwnd;
    QLocalServer* instanceServer = nullptr;
    IUIAutomation* uiAutomation = nullptr;
    Taskbar *taskbar = nullptr;
    MainWindow *mainWindow = nullptr;
    QPointer<LogDialog> logDialog = nullptr;
    Config *config = nullptr;
    sqliteDB *db = nullptr;
    shokoAPI* shoko_API = nullptr;
    VideoWatcherQt* VW = nullptr;
    MusicPlayer* musicPlayer = nullptr;
    SoundPlayer* soundPlayer = nullptr;
    mascotsAnimationsThread* MascotsAnimation = nullptr;
    // The QML engine that owns the image provider lives for the process lifetime and outlives
    // every window, so the cache it reads through must too: it is never destroyed while the
    // engine can still ask for an image.
    MascotImageCache* mascotImageCache = nullptr;
    MascotPrefetcher* mascotPrefetcher = nullptr;
    QuitEater* quitEater = nullptr;
    generalEventFilter* genEventFilter = nullptr;
    TooltipEventFilter* tooltipFilter = nullptr;
    RawInputKeyboard* keyboard_hook;
    bool exit_sound_called = false;
    bool ready_to_quit = false;
    bool debug_mode = false;
    bool numlock_only_on = true;
    bool MascotsExtractColor = false;
    qint64 lastMascotCacheLogBytes = 0;
    qint64 lastMascotCacheLogMs = 0;
    QString currentDB;
    Logger *logger;
    QErrorMessage* ErrorDialog = nullptr;
    CalculateBpmManager* BpmManager = nullptr;
    MainApp(int& argc, char** argv);
    void createMissingDirs();
    void toggleLogWindow();
    void initTaskbar();
    void stop_handle();
    void updateMascotCacheLimits();
    // Debug-gating and throttling live here rather than in the cache, which stays free of any
    // knowledge of the app or of debug mode.
    void logMascotCacheOccupancy();
    void startSingleInstanceServer(QString appid);
    void stopSingleInstanceServer();
    void showErrorMessage(QString message);
    ~MainApp();
};

