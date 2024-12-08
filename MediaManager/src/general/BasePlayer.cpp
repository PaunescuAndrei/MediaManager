#include "stdafx.h"
#include "BasePlayer.h"
#include <MainApp.h>

BasePlayer::BasePlayer(QString video_path, int video_id, int* CLASS_COUNT, QObject* parent, MainApp* App)
{
    this->App = App;
    this->target_video_path = video_path;
    this->video_id = video_id;
    this->CLASS_COUNT = CLASS_COUNT;
    this->process = new QProcess(parent);
}

BasePlayer::~BasePlayer()
{
    this->process->terminate();
    //this->send_message(CMD_CLOSEAPP);
    //this->process->waitForFinished(5000);
    this->process->deleteLater();
}

bool BasePlayer::isProcessAlive()
{
    return !(this->process->state() == QProcess::NotRunning);
}
