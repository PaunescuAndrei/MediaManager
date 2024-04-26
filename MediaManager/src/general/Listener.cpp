#include "stdafx.h"
#include "Listener.h"
#include "utils.h"
#include <QDebug>
#include "MainApp.h"

Listener::Listener(QString path,int video_id, int *CLASS_COUNT, QObject* parent, MainApp* App)
{
    this->App = App;
    this->path = path;
    this->video_id = video_id;
	this->CLASS_COUNT = CLASS_COUNT;
    this->process = new QProcess(parent);
    WNDCLASS windowClass = {};
    windowClass.lpfnWndProc = WndProcWrapper;
    QString name = "listener_win_" + QString::number(*this->CLASS_COUNT);
    this->windowClassName = new char[strlen(name.toUtf8().data()) + 1];
    strcpy(this->windowClassName, name.toUtf8().data());
    windowClass.lpszClassName = this->windowClassName;
    if (!RegisterClass(&windowClass)) {
        qDebug() << "Failed to register Listener window class.";
    }
    HWND messageWindow = CreateWindow(windowClassName, 0, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, this);
    this->hwnd = messageWindow;
    if (!messageWindow) {
        qDebug() << "Failed to create message-only window";
    }
    //this->duration = utils::getVideoDuration(path).toDouble(); // slow, maybe find a solution in the future
}

LRESULT CALLBACK Listener::WndProcWrapper(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Listener* pThis = nullptr;
    if (msg == WM_NCCREATE)
    {
        pThis = static_cast<Listener*> ((reinterpret_cast<CREATESTRUCT*>(lParam))->lpCreateParams);
        SetLastError(0);
        if (!SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis)))
        {
            if (GetLastError() != 0)
            {
                qDebug() << "Listener window wrapper error.";
                return FALSE;
            }
        }
    }
    else
    {
        pThis = reinterpret_cast<Listener*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }
    if (pThis != nullptr)
    {
        return pThis->OnCopyData(hWnd, msg, wParam, lParam);
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT Listener::OnCopyData(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    PCOPYDATASTRUCT pCDS = (PCOPYDATASTRUCT)lParam;
    if (pCDS != nullptr) {
        int command = pCDS->dwData;
        LPCWSTR data = (LPCWSTR)pCDS->lpData;
        //qDebug() << QString::number(pCDS->dwData, 16);
        switch (pCDS->dwData)
        {
        case CMD_CONNECT:
            //qDebug() << QString::fromStdWString(data);
            this->mpchc_hwnd = (HWND)wcstoull(data,nullptr,10);
            //qDebug() << QString::number((unsigned long long int)this->mpchc_hwnd);
            break;
        case CMD_CURRENTPOSITION:
            if (!this->change_in_progress) {
                if (this->first_getcurrentpositon && !this->change_in_progress) {
                    this->first_getcurrentpositon = false;
                    //this->send_osd_message(this->path, 20000);
                }
                else {
                    this->currentPosition = std::stod(data);
                }
            }
            else {
                if (this->path.isEmpty() && this->change_in_progress == true)
                    this->change_in_progress = false;
                this->send_message(CMD_PAUSE);
                if ((this->state == PS_PAUSE || this->state == PS_STOP) && this->seek_done == true) {
                    this->change_in_progress = false;
                }
            }
            break;
        case CMD_NOWPLAYING:
        {
            //qDebug() << QString::fromStdWString(data);
            QStringList items = QString::fromStdWString(data).split("|");
            if (!items.isEmpty()) {
                QString val = items.value(4);
                if (!val.isEmpty()) {
                    this->duration = val.toDouble();
                }
            }
            if (this->change_in_progress) {
                this->state = 2;
                this->seek_done = false;
                this->send_message(CMD_PAUSE);
                this->send_message(CMD_SETPOSITION, QString::number(this->currentPosition));
            }
            break;
        }
        case CMD_NOTIFYSEEK:
            if (change_in_progress == true) {
                this->seek_done = true;
                this->send_message(CMD_PAUSE);
            };
            break;
        case CMD_PLAYMODE:
            //qDebug() << QString::fromStdWString(data).toInt();
            this->state = QString::fromStdWString(data).toInt();
            if ((this->state == PS_PAUSE || this->state == PS_PLAY) && this->change_in_progress == false && this->first_getcurrentpositon == false) {
                if (this->App && this->App->soundPlayer->effects_playpause && utils::randfloat(0, 1) <= this->App->soundPlayer->effect_chance)
                    if (this->App->soundPlayer->effects_chain_playpause)
                        this->App->soundPlayer->playSoundEffectChain();
                    else
                        this->App->soundPlayer->playSoundEffect();
            }
            break;
        }
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

LRESULT Listener::send_osd_message(QString message,int duration)
{
    if (this->mpchc_hwnd) {
        COPYDATASTRUCT MyCDS = COPYDATASTRUCT();
        MyCDS.dwData = CMD_OSDSHOWMESSAGE;
        if (!message.isEmpty()) {
            std::wstring s = message.toStdWString();
            const wchar_t *str = s.c_str();
            MPC_OSDDATA datastruct = {1,duration};
            wcsncpy(datastruct.strMsg, str, 256);
            MyCDS.lpData = (LPVOID)&datastruct;
            MyCDS.cbData = sizeof(datastruct);
        }
        else {
            MyCDS.lpData = NULL;
            MyCDS.cbData = NULL;
        }
        return SendMessageTimeout(this->mpchc_hwnd, WM_COPYDATA, (WPARAM)this->mpchc_hwnd, (LPARAM)(LPVOID)&MyCDS, 0, 500, NULL);
    }
    return -1;
}

LRESULT Listener::send_message(MPCAPI_COMMAND command, QString data, bool blocking)
{
    if (this->mpchc_hwnd) {
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
        if(blocking)
            return SendMessageW(this->mpchc_hwnd, WM_COPYDATA, (WPARAM)this->mpchc_hwnd, (LPARAM)(LPVOID)&MyCDS);
        return SendMessageTimeout(this->mpchc_hwnd, WM_COPYDATA, (WPARAM)this->mpchc_hwnd, (LPARAM)(LPVOID)&MyCDS,0,500,NULL);
    }
    return -1;
}

void Listener::closeWindow()
{
    SendMessageTimeout(this->hwnd, WM_CLOSE, 0, 0, 0, 1, NULL);
}

void Listener::change_path(QString path, int video_id)
{
    this->change_in_progress = true;
    this->currentPosition = -1;
    this->duration = -1;
    this->path = path;
    this->video_id = video_id;
    this->first_getcurrentpositon = true;
    this->endvideo = false;
    this->state = 2;
    this->seek_done = false;
}

bool Listener::is_process_alive()
{
    return !(this->process->state() == QProcess::NotRunning);
}

void Listener::change_video(QString path, int video_id, double position)
{
    this->change_path(path, video_id);
    if(position >= 0)
        this->currentPosition = position;
    if (path.isEmpty())
        this->send_message(CMD_CLOSEFILE);
    else
        this->send_message(CMD_OPENFILE, path);
    //this->duration = utils::getVideoDuration(path).toDouble(); // slow, maybe find a solution in the future
    qMainApp->logger->log(QString("Changing Video to \"%1\"").arg(path), "Video", path);
}

Listener::~Listener()
{
    this->closeWindow();
    this->process->terminate();
    //this->send_message(CMD_CLOSEAPP);
    //this->process->waitForFinished(5000);
    this->process->deleteLater();
    UnregisterClass(this->windowClassName, NULL);
    delete[] this->windowClassName;
}

