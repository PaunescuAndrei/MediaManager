#include "stdafx.h"
#include "MpcPlayer.h"
#include "MainApp.h"
#include <utils.h>

MpcPlayer::MpcPlayer(QString video_path, int video_id, int* CLASS_COUNT, QObject* parent, MainApp* App) : BasePlayer(video_path, video_id, CLASS_COUNT, parent, App)
{
    WNDCLASS windowClass = {};
    windowClass.lpfnWndProc = WndProcWrapper;
    QString name = "mpcplayer_win_" + QString::number(*this->CLASS_COUNT);
    this->windowClassName = new char[strlen(name.toUtf8().data()) + 1];
    strcpy(this->windowClassName, name.toUtf8().data());
    windowClass.lpszClassName = this->windowClassName;
    if (!RegisterClass(&windowClass)) {
        qDebug() << "Failed to register MPC Player window class.";
    }
    HWND messageWindow = CreateWindow(windowClassName, 0, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, this);
    this->class_hwnd = messageWindow;
    if (!messageWindow) {
        qDebug() << "Failed to create message-only window";
    }
	this->last_seek_time = utils::QueryUnbiasedInterruptTimeChrono();
}

MpcPlayer::~MpcPlayer()
{
    this->drop();
    UnregisterClass(this->windowClassName, NULL);
    delete[] this->windowClassName;
    this->running = false;
    this->terminate();
}

LRESULT MpcPlayer::WndProcWrapper(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    MpcPlayer* pThis = nullptr;
    if (uMsg == WM_NCCREATE)
    {
        pThis = static_cast<MpcPlayer*> ((reinterpret_cast<CREATESTRUCT*>(lParam))->lpCreateParams);
        SetLastError(0);
        if (!SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis)))
        {
            if (GetLastError() != 0)
            {
                qDebug() << "MPC Player window wrapper error.";
                return FALSE;
            }
        }
    }
    else
    {
        pThis = reinterpret_cast<MpcPlayer*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }
    if (pThis != nullptr)
    {
        return pThis->OnCopyData(hWnd, uMsg, wParam, lParam);
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

LRESULT MpcPlayer::OnCopyData(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    PCOPYDATASTRUCT pCDS = (PCOPYDATASTRUCT)lParam;
    if (pCDS != nullptr) {
        int command = pCDS->dwData;
        LPCWSTR data = (LPCWSTR)pCDS->lpData;
        //qDebug() << QString::number(pCDS->dwData, 16);
        switch (pCDS->dwData)
        {
            case CMD_CONNECT:
            {
                //qDebug() << QString::fromStdWString(data);
                this->player_hwnd = (HWND)wcstoull(data, nullptr, 10);
                //qDebug() << QString::number((unsigned long long int)this->mpchc_hwnd);
                break;
            }
            case CMD_CURRENTPOSITION:
            {
                if (not this->change_in_progress_video) {
                    if (this->first_getcurrentposition && not this->change_in_progress) {
                        this->first_getcurrentposition = false;
                        //this->send_osd_message(this->path, 20000);
                    }
                    else {
                        this->position = std::stod(data);
                    }
                }
                else {
                    if (this->change_in_progress_video == true and (this->target_video_path.isEmpty() or this->target_video_path == this->video_path))
                        this->change_in_progress_video = false;
                }
                break;
            }
            case CMD_NOWPLAYING:
            {
                QString nowplaying = QString::fromStdWString(data);
                if (nowplaying != this->nowplaying) {
                    QStringList items = nowplaying.split("|");
                    if (!items.isEmpty()) {
                        this->video_path = items.value(3);
                        QString val = items.value(4);
                        if (!val.isEmpty()) {
                            this->duration = val.toDouble(); // this doesnt work when changing video to the same video
                        }
                    }
                    this->nowplaying = nowplaying;
                }
                break;
            }
            case CMD_NOTIFYSEEK:
            {
                this->last_seek_time = utils::QueryUnbiasedInterruptTimeChrono();
                if (this->change_in_progress_seek == true) {
                    this->change_in_progress_seek = false;
                };
                break;
            }
            case CMD_PLAYMODE:
            {
                //qDebug() << QString::fromStdWString(data).toInt();
                int state = QString::fromStdWString(data).toInt();
                this->paused = state != 0;
                emit this->pausedChangedSignal(bool(this->paused));
                if (this->change_in_progress_pause == true and this->paused == true) {
                    this->change_in_progress_pause = false;
                }
                if ((state == PS_PAUSE || state == PS_PLAY) && this->change_in_progress == false && this->first_getcurrentposition == false) {
                    if (this->App && this->App->soundPlayer->effects_playpause && utils::randfloat(0, 1) <= this->App->soundPlayer->effect_chance)
                        if (this->App->soundPlayer->effects_chain_playpause)
                            this->App->soundPlayer->playSoundEffectChain();
                        else
                            this->App->soundPlayer->playSoundEffect();
                }
                break;
            }
        }
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

LRESULT MpcPlayer::send_osd_message(QString message, int duration)
{
    if (this->player_hwnd) {
        COPYDATASTRUCT MyCDS = COPYDATASTRUCT();
        MyCDS.dwData = CMD_OSDSHOWMESSAGE;
        if (!message.isEmpty()) {
            std::wstring s = message.toStdWString();
            const wchar_t* str = s.c_str();
            MPC_OSDDATA datastruct = { 1,duration };
            wcsncpy(datastruct.strMsg, str, 256);
            MyCDS.lpData = (LPVOID)&datastruct;
            MyCDS.cbData = sizeof(datastruct);
        }
        else {
            MyCDS.lpData = NULL;
            MyCDS.cbData = NULL;
        }
        return SendMessageTimeout(this->player_hwnd, WM_COPYDATA, (WPARAM)this->player_hwnd, (LPARAM)(LPVOID)&MyCDS, 0, 500, NULL);
    }
    return -1;
}

LRESULT MpcPlayer::send_message(MPCAPI_COMMAND command, QString data, bool blocking)
{
    if (this->player_hwnd) {
        COPYDATASTRUCT MyCDS = COPYDATASTRUCT();
        MyCDS.dwData = command;
        if (!data.isEmpty()) {
            std::wstring s = data.toStdWString();
            LPCWSTR str = s.c_str();
            MyCDS.lpData = (LPVOID)str;
            MyCDS.cbData = sizeof(WCHAR) * (wcslen(str) + 1);
        }
        else {
            MyCDS.lpData = NULL;
            MyCDS.cbData = NULL;
        }
        if (blocking)
            return SendMessageW(this->player_hwnd, WM_COPYDATA, (WPARAM)this->player_hwnd, (LPARAM)(LPVOID)&MyCDS);
        return SendMessageTimeout(this->player_hwnd, WM_COPYDATA, (WPARAM)this->player_hwnd, (LPARAM)(LPVOID)&MyCDS, 0, 500, NULL);
    }
    return -1;
}

void MpcPlayer::openPlayer(QString video_path, double position_seconds)
{
    this->process->setProgram(this->App->config->get("player_path"));
    if (video_path.isEmpty())
        this->process->setArguments({ "/slave",QString::number((unsigned long long int)this->class_hwnd) });
    else {
        QStringList args;
        if(this->App->config->get_bool("video_autoplay"))
            args << "/play";
        else 
            args << "/open";
        args << video_path << "/start" << QString::number(position_seconds * 1000, 'f',4) << "/slave" << QString::number((unsigned long long int)this->class_hwnd);
        this->process->setArguments(args);
    }

    this->process->start();
    this->pid = this->process->processId();
}

void MpcPlayer::askForStatus()
{
    this->send_message(CMD_GETCURRENTPOSITION);
}

void MpcPlayer::drop()
{
    SendMessageTimeout(this->class_hwnd, WM_CLOSE, 0, 0, 0, 1, NULL);
}
void MpcPlayer::closePlayer()
{
}

void MpcPlayer::setPaused(bool paused, bool direct)
{
    if (direct) {
        if (paused)
            this->send_message(CMD_PAUSE);
        else
            this->send_message(CMD_PLAY);
    }
    else {
        if(paused)
            this->queue.enqueue(std::make_shared<MpcDirectCommand>(CMD_PAUSE));
        else
            this->queue.enqueue(std::make_shared<MpcDirectCommand>(CMD_PLAY));
    }
}

void MpcPlayer::toggleFullScreen(bool direct)
{
    if (direct) {
        this->send_message(CMD_TOGGLEFULLSCREEN);
    }
    else {
        this->queue.enqueue(std::make_shared<MpcDirectCommand>(CMD_TOGGLEFULLSCREEN));
    }
}

void MpcPlayer::displayOsdMessage(QString message, int duration, bool direct)
{
    if (direct) {
        this->send_osd_message(message, duration);
    }
    else {
        this->queue.enqueue(std::make_shared<OsdMessageCommand>(message,duration));
    }
}

void MpcPlayer::changeVideo(QString path, int video_id, double position)
{
    std::unique_lock<std::mutex> guard(this->queue.m);
    for (auto it = this->queue.q.begin(); it != this->queue.q.end();) {
        if ((*it)->command == CHANGE_VIDEO) {
            it = this->queue.q.erase(it);
        }
        else {
            ++it;
        }
    }
    guard.unlock();
    this->queue.enqueue(std::make_shared<ChangeVideoCommand>(path,video_id,position));
    
    if (!path.isEmpty()) {
        bool should_autoplay = this->App->config->get_bool("video_autoplay");
        this->setPaused(!should_autoplay, false);
    }
}

void MpcPlayer::run() {
    this->running = true;
    while (this->running) {
        this->askForStatus();
        std::optional<std::shared_ptr<PlayerCommand>> _command = this->queue.front();
        if (_command) {
            std::shared_ptr<PlayerCommand> base_command = _command.value();
            if (base_command->command == CHANGE_VIDEO) {
                std::shared_ptr <ChangeVideoCommand> command = std::dynamic_pointer_cast<ChangeVideoCommand>(base_command);
                if (not command) {
                    this->queue.dequeue();
                    continue;
                }
                if (command->change_in_progress == false) {
                    this->change_in_progress = true;
                    this->change_in_progress_video = false;
                    this->change_in_progress_pause = false;
                    this->change_in_progress_seek = false;
                    command->change_in_progress = true;
                    this->target_video_path = command->video_path;
                    this->video_id = command->video_id;
                    this->position = -1;
                    if (this->video_path != this->target_video_path) { // dirty fix, this wont work if the file has changed and we try to load it again
                        this->duration = -1;
                        this->paused = false;
                    }
                }
                if (command->video_path.isEmpty()) {
                    this->send_message(CMD_CLOSEFILE);
                    this->queue.dequeue();
                }
                else {
                    if (this->change_in_progress_video == false and this->video_path != this->target_video_path) {
                        this->change_in_progress_video = true;
                        this->send_message(CMD_OPENFILE, command->video_path);
                        this->msleep(250);
                        continue;
                    }
                    if (this->paused == false) {
                        this->change_in_progress_pause = true;
                        this->send_message(CMD_PAUSE);
                        this->msleep(250);
                        continue;
                    }
                    if (command->position >= 0 and abs(this->position - command->position) > 0.1) {
                        this->change_in_progress_seek = true;
                        this->send_message(CMD_SETPOSITION, QString::number(command->position));
                        this->msleep(250);
                        continue;
                    }
                    else if (this->change_in_progress_seek == true and abs(this->position - command->position) <= 0.1) {
                        // fix if change_in_progress_seek == true and mpc notify seek event wasnt sent causing infinite loop
                        this->change_in_progress_seek = false;
                    }
                    if (this->change_in_progress_video == false and this->change_in_progress_pause == false and this->change_in_progress_seek == false) {
                        this->change_in_progress = false;
                    }
                    if (this->change_in_progress == false) {
                        this->queue.dequeue();
                    }
                }
            }
            if (base_command->command == MPC_DIRECT_COMMAND) {
                std::shared_ptr <MpcDirectCommand> command = std::dynamic_pointer_cast<MpcDirectCommand>(base_command);
                if (not command) {
                    this->queue.dequeue();
                    continue;
                }
                this->send_message(command->mpc_command, command->data);
                this->queue.dequeue();
            }
            if (base_command->command == OSD_MESSAGE) {
                std::shared_ptr <OsdMessageCommand> command = std::dynamic_pointer_cast<OsdMessageCommand>(base_command);
                if (not command) {
                    this->queue.dequeue();
                    continue;
                }
                this->send_osd_message(command->message, command->duration);
                this->queue.dequeue();
            }
        }
        if (this->change_in_progress == false and not this->video_path.isEmpty() and this->position >= 0 and this->duration >= 0 and this->position >= this->duration - 0.5) {
            if (this->end_of_video == false) {
                auto now = utils::QueryUnbiasedInterruptTimeChrono();
                auto time_since_seek = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->last_seek_time).count();
                if (time_since_seek >= 500) {
					//weird bug if finish dialog steals mpc-hc window focus while seeking it will hang sending final pause state and final notify end of stream
                    this->end_of_video = true;
                    emit this->endOfVideoSignal();
                }
            }
        }
        else if (this->end_of_video == true) {
            this->end_of_video = false;
        }
        if(this->position >= this->duration and this->position >= 0 and this->duration >= 0 and this->paused == false) {
            //weird bug if finish dialog steals mpc-hc window focus while seeking it will hang sending final pause state and final notify end of stream
			// you need both this and endofvideosignal delay to avoid mpc-hc bug
            // this combined with the delay prevents finish dialog from apearing when holding mouse seek
            this->send_message(CMD_SETPOSITION, QString::number(this->duration));
		}
        this->msleep(100);
    }
}