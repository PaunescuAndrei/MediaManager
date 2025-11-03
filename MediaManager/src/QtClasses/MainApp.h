#pragma once
#include <QApplication>
#include "config.h"
#include "sqliteDB.h"
#include "taskbar.h"
#include "VideoWatcherQt.h"
#include "MusicPlayer.h"
#include "SoundPlayer.h"
#include "mascotsGeneratorThread.h"
#include "mascotsAnimationsThread.h"
#include "RawInputKeyboard.h"
#include "quitEaterEventFilter.h"
#include <QLocalServer>
#include "logger.h"
#include <UIAutomationClient.h>

#define qMainApp (static_cast<MainApp *>(QCoreApplication::instance()))

class MainWindow;
class generalEventFilter;
class LogDialog;
class QErrorMessage;
class shokoAPI;

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
    mascotsGeneratorThread* MascotsGenerator = nullptr;
    mascotsAnimationsThread* MascotsAnimation = nullptr;
    QuitEater* quitEater = nullptr;
    generalEventFilter* genEventFilter = nullptr;
    RawInputKeyboard* keyboard_hook;
    bool exit_sound_called = false;
    bool ready_to_quit = false;
    bool debug_mode = false;
    bool numlock_only_on = true;
    bool MascotsExtractColor = false;
    QString currentDB;
    Logger *logger;
    QErrorMessage* ErrorDialog = nullptr;
    MainApp(int& argc, char** argv);
    void createMissingDirs();
    void toggleLogWindow();
    void initTaskbar();
    void stop_handle();
    void startSingleInstanceServer(QString appid);
    void stopSingleInstanceServer();
    void showErrorMessage(QString message);
    ~MainApp();
};

