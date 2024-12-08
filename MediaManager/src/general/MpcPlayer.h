#pragma once
#include "BasePlayer.h"
#include "MpcApi.h"

class MpcPlayer :
    public BasePlayer
{
    Q_OBJECT
public:
    char* windowClassName;
    HWND class_hwnd = HWND();
    QString nowplaying = "";
    bool first_getcurrentposition = true;
    bool change_in_progress_video = false;
    bool change_in_progress_seek = false;
    bool change_in_progress_pause = false;
    MpcPlayer(QString video_path, int video_id, int* CLASS_COUNT, QObject* parent, MainApp* App);
    ~MpcPlayer();
    static LRESULT CALLBACK WndProcWrapper(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnCopyData(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT send_osd_message(QString message, int duration);
    LRESULT send_message(MPCAPI_COMMAND command, QString data = "", bool blocking = true);
    void openPlayer(QString video_path = "", double position_seconds = -1);
    void askForStatus();
    void drop();
    void closePlayer();
    void setPaused(bool paused, bool direct = false);
    void toggleFullScreen(bool direct = false);
    void displayOsdMessage(QString message, int duration, bool direct = false);
    void changeVideo(QString path, int video_id, double position = 0);
    void run() override;
};

