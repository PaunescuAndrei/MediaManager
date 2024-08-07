#include "stdafx.h"
#include "VideoWatcherQt.h"
#include "utils.h"
#include "winuser.h"
#include "MainApp.h"
#include "MainWindow.h"
#include "MpcApi.h"
#include "windef.h"
#include <QMutexLocker>

VideoWatcherQt::VideoWatcherQt(MainApp* App, QObject* parent) : QThread(parent)
{
	this->App = App;
	this->db = new sqliteDB(this->App->db->location, "videowatcher_con");
}

std::shared_ptr<Listener> VideoWatcherQt::newListener(QString path, int video_id)
{
	QMutexLocker lock(&this->data_lock);
	std::shared_ptr<Listener> newVideo = std::make_shared<Listener>(path, video_id,&this->CLASS_COUNT,this->App,this->App);
	this->Listeners.append(newVideo);
	this->CLASS_COUNT++;
	return newVideo;
}

void VideoWatcherQt::clearData(bool include_mainlistener) {
	QList<std::shared_ptr<Listener>>::iterator it = this->Listeners.begin();
	while (it != this->Listeners.end()) {
		if (!include_mainlistener && (*it) != nullptr && (*it) == this->mainListener) {
			++it;
			continue;
		}
		if ((*it)->currentPosition != -1) {
			this->db->updateVideoProgress((*it)->video_id, (*it)->currentPosition);
		}
		(*it)->closeWindow();
		(*it).reset();
		it = this->Listeners.erase(it);
	}
	if(include_mainlistener)
		this->clearAfterMainVideoEnd();
}

void VideoWatcherQt::setMainListener(std::shared_ptr<Listener> listener) {
	this->mainListener = listener;
}

void VideoWatcherQt::clearMainListener() {
	this->mainListener = nullptr;
}

void VideoWatcherQt::clearAfterMainVideoEnd()
{
	this->clearMainListener();
	if (this->App) {
		if (this->App->mainWindow->finish_dialog) {
			this->App->mainWindow->finish_dialog->deleteLater();
			this->App->mainWindow->finish_dialog = nullptr;
		}
	}
}

void VideoWatcherQt::toggle_window()
{
	if (!this->Listeners.isEmpty() && this->mainListener != nullptr) {
		std::shared_ptr<Listener> listener = this->mainListener;
		bool fullscreen = utils::is_hwnd_full_screen(listener->mpchc_hwnd);
		QList<HWND> hwnds = utils::get_hwnds_for_pid(listener->pid);
		bool is_foreground = false;
		HWND foreground_window = GetForegroundWindow();
		for (HWND& hwnd : hwnds) {
			if (foreground_window == hwnd) {
				is_foreground = true;
				break;
			}
		}
		if (is_foreground) {
			listener->send_message(CMD_PAUSE);
			if (fullscreen)
				listener->send_message(CMD_TOGGLEFULLSCREEN);
			if (this->old_foreground_window) {
				utils::bring_hwnd_to_foreground_uiautomation_method(this->App->uiAutomation, this->old_foreground_window);
				if (GetForegroundWindow() != this->old_foreground_window)
					utils::bring_hwnd_to_foreground_uiautomation_method(this->App->uiAutomation, this->old_foreground_window);
				if (GetForegroundWindow() != this->old_foreground_window)
					utils::bring_hwnd_to_foreground(this->old_foreground_window);
				if (GetForegroundWindow() != this->old_foreground_window)
					utils::bring_hwnd_to_foreground_console_method(this->old_foreground_window);
				if (GetForegroundWindow() != this->old_foreground_window)
					utils::bring_hwnd_to_foreground_alt_method(this->old_foreground_window);
				//if (GetForegroundWindow() != this->old_foreground_window)
				//	bring_hwnd_to_foreground_attach_method(this->old_foreground_window);
			}
		}
		else {
			this->old_foreground_window = GetForegroundWindow();
			is_foreground = false;
			int tries = 0;
			while (!is_foreground && tries < 10) {
				utils::bring_hwnd_to_foreground_uiautomation_method(this->App->uiAutomation,listener->mpchc_hwnd);
				tries++;
				QList<HWND> hwnds = utils::get_hwnds_for_pid(listener->pid);
				if (hwnds.contains(GetForegroundWindow()))
					is_foreground = true;
			}
			if (!fullscreen) {
				listener->send_message(CMD_TOGGLEFULLSCREEN);
			}
			listener->send_message(CMD_PLAY);
			listener->send_osd_message(QString("%1 / %2").arg(utils::formatSecondsQt(listener->currentPosition), utils::formatSecondsQt(listener->duration)), 3000);
		}
	}
}

void VideoWatcherQt::incrementTimeWatchedTotal(int value) {
	int time = this->db->getMainInfoValue("timeWatchedTotal", "ALL", "0").toInt();
	time += value;
	this->App->db->setMainInfoValue("timeWatchedTotal", "ALL", QString::number(time));
}

void VideoWatcherQt::run()
{
	while (this->running) {
		for (std::shared_ptr<Listener> listener : this->Listeners) {
			listener->send_message(CMD_GETCURRENTPOSITION);
			if (listener->path == this->App->mainWindow->ui.currentVideo->path) {
				emit updateProgressBarSignal(listener->currentPosition,listener->duration, listener,true);
			}
			if (!listener->is_process_alive()) {
				if (listener->currentPosition != -1) {
					this->db->updateVideoProgress(listener->video_id, listener->currentPosition);
				}
				if (listener == this->mainListener) {
					this->clearAfterMainVideoEnd();
				}
				listener->closeWindow();
				this->Listeners.removeOne(listener);
				listener.reset();
			}
		}
		if (!this->Listeners.isEmpty()) {
			if (this->App->musicPlayer && this->App->musicPlayer->player->playbackState() == QMediaPlayer::PlayingState)
				emit updateMusicPlayerSignal(false);
			if (this->watching == false) {
				this->watching = true;
				if (this->mainListener_time_start != nullptr) {
					this->mainListener_time_start.reset();
				}
				this->mainListener_time_start = std::make_shared<std::chrono::microseconds>(utils::QueryUnbiasedInterruptTimeChrono());
			}
		}
		else {
			if (this->App->musicPlayer && (this->App->musicPlayer->player->playbackState() == QMediaPlayer::StoppedState || this->App->musicPlayer->player->playbackState() == QMediaPlayer::PausedState))
				emit updateMusicPlayerSignal(true);
			if (this->watching == true) {
				this->watching = false;
				int elapsed = std::chrono::duration_cast<std::chrono::seconds>(utils::QueryUnbiasedInterruptTimeChrono() - *this->mainListener_time_start).count();
				this->incrementTimeWatchedTotal(elapsed);
				if (this->mainListener_time_start != nullptr) {
					this->mainListener_time_start.reset();
				}
			}

		}
		if(this->watching){
			if (this->App->debug_mode == false && this->App->mainWindow->iconWatchingState == false) {
				emit this->updateTaskbarIconSignal(true);
			}
			if (this->App->taskbar)
				this->App->taskbar->setPause(this->App->taskbar->hwnd,false);
		}
		else if(!this->watching) {
			if (this->App->debug_mode == false && this->App->mainWindow->iconWatchingState == true) {
				emit this->updateTaskbarIconSignal(false);
			}
			if(this->App->taskbar)
				this->App->taskbar->setPause(this->App->taskbar->hwnd, true);
		}
		this->msleep(100);
	}
	for (std::shared_ptr<Listener> listener : this->Listeners) {
		listener->process->terminate();
		this->db->updateVideoProgress(listener->video_id, listener->currentPosition);
	}
	if (this->mainListener_time_start != nullptr) {
		int elapsed = std::chrono::duration_cast<std::chrono::seconds>(utils::QueryUnbiasedInterruptTimeChrono() - *this->mainListener_time_start).count();
		this->incrementTimeWatchedTotal(elapsed);
		this->mainListener_time_start.reset();
	}
	this->clearAfterMainVideoEnd();
}

VideoWatcherQt::~VideoWatcherQt()
{
	for (std::shared_ptr<Listener> item : this->Listeners) {
		item->process->terminate();
		item.reset();
	}

	if (this->mainListener_time_start != nullptr) {
		int elapsed = std::chrono::duration_cast<std::chrono::seconds>(utils::QueryUnbiasedInterruptTimeChrono() - *this->mainListener_time_start).count();
		this->incrementTimeWatchedTotal(elapsed);
		this->mainListener_time_start.reset();
	}

	delete this->db;
}
