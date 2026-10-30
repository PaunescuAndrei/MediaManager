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
    // Process-lifetime mascot media pipeline (amendment U): the QML engine that owns the image
    // provider outlives every window, so the cache it reads through must too. The prefetcher is
    // created and destroyed by this object; the cache is deliberately never destroyed while the
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
    QString currentDB;
    Logger *logger;
    QErrorMessage* ErrorDialog = nullptr;
    CalculateBpmManager* BpmManager = nullptr;
    MainApp(int& argc, char** argv);
    void createMissingDirs();
    void toggleLogWindow();
    void initTaskbar();
    void stop_handle();
    // Amendment H3: pushes `mascots_image_cache_mb` / `mascots_image_cache_max_items` into the
    // cache, clamping a non-positive value to a sane minimum instead of leaving that axis
    // unbounded. Called at startup and whenever the settings are applied.
    void updateMascotCacheLimits();
    void startSingleInstanceServer(QString appid);
    void stopSingleInstanceServer();
    void showErrorMessage(QString message);
    ~MainApp();
};

