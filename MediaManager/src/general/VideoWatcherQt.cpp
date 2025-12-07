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
	this->lastSessionDateCached = this->db->getMainInfoValue("lastSessionDate", "ALL", "");
}

QSharedPointer<BasePlayer> VideoWatcherQt::newPlayer(QString path, int video_id)
{
	QMutexLocker lock(&this->data_lock);
	QSharedPointer<BasePlayer> newVideo = QSharedPointer<MpcPlayer>::create(path, video_id,&this->CLASS_COUNT,this->App,this->App);
	this->Players.append(newVideo);
	this->CLASS_COUNT++;
	newVideo->start();
	return newVideo;
}

void VideoWatcherQt::clearData(bool include_mainplayer) {
	QList<QSharedPointer<BasePlayer>>::iterator it = this->Players.begin();
	while (it != this->Players.end()) {
		if (!include_mainplayer && (*it) != nullptr && (*it) == this->mainPlayer) {
			++it;
			continue;
		}
		if ((*it)->position != -1) {
			this->db->updateVideoProgress((*it)->video_id, (*it)->position);
		}
		(*it)->drop();
		(*it).reset();
		it = this->Players.erase(it);
	}
	if(include_mainplayer)
		this->clearAfterMainVideoEnd();
}

void VideoWatcherQt::setMainPlayer(QSharedPointer<BasePlayer> player) {
	this->mainPlayer = player;
}

void VideoWatcherQt::clearMainPlayer() {
	this->mainPlayer = nullptr;
}

void VideoWatcherQt::clearAfterMainVideoEnd()
{
	this->clearMainPlayer();
	if (this->App) {
		if (this->App->mainWindow->finish_dialog) {
			this->App->mainWindow->finish_dialog->close();
			this->App->mainWindow->finish_dialog = nullptr;
		}
	}
}

void VideoWatcherQt::toggle_window()
{
	if (!this->Players.isEmpty() && this->mainPlayer != nullptr) {
		QSharedPointer<BasePlayer> player = this->mainPlayer;
		bool fullscreen = utils::is_hwnd_full_screen(player->player_hwnd);
		QList<HWND> hwnds = utils::get_hwnds_for_pid(player->pid);
		bool is_foreground = false;
		HWND foreground_window = GetForegroundWindow();
		for (HWND& hwnd : hwnds) {
			if (foreground_window == hwnd) {
				is_foreground = true;
				break;
			}
		}
		if (is_foreground) {
			player->setPaused(true,true);
			if (fullscreen)
				player->toggleFullScreen(true);
			SetWindowPos(player->player_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			SetWindowPos(player->player_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			if (this->old_foreground_window) {
				utils::bring_hwnd_to_foreground_all(this->old_foreground_window, this->App->uiAutomation);
			}
		}
		else {
			this->old_foreground_window = GetForegroundWindow();
			is_foreground = false;
			int tries = 0;
			while (!is_foreground && tries < 10) {
				utils::bring_hwnd_to_foreground_uiautomation_method(player->player_hwnd, this->App->uiAutomation);
				tries++;
				QList<HWND> hwnds = utils::get_hwnds_for_pid(player->pid);
				if (hwnds.contains(GetForegroundWindow()))
					is_foreground = true;
			}
			if (!fullscreen) {
				player->toggleFullScreen(true);
			}
			player->setPaused(false,true);
			QTime currentTime = QTime::currentTime();
			player->displayOsdMessage(QStringLiteral("%1 / %2 | %3").arg(
				utils::formatSecondsCompactQt(player->position), 
				utils::formatSecondsCompactQt(player->duration),
				currentTime.toString("hh:mm")), 3000, true);
		}
	}
}

void VideoWatcherQt::incrementTimeWatchedTotal(double value) {
	double time = this->db->getMainInfoValue("timeWatchedTotal", "ALL", "0.0").toDouble();
	time += value;
	this->App->db->setMainInfoValue("timeWatchedTotal", "ALL", QString::number(time));
	this->App->db->incrementTimeWatchedToday(value);
}

void VideoWatcherQt::incrementTimeSessionTotal(double value) {
	double time = this->db->getMainInfoValue("timeSessionTotal", "ALL", "0.0").toDouble();
	time += value;
	this->App->db->setMainInfoValue("timeSessionTotal", "ALL", QString::number(time));
	this->App->db->incrementTimeSessionToday(value);
}

void VideoWatcherQt::resetDailyStats() {
	QString today = QDate::currentDate().toString("yyyy-MM-dd");
	if (this->lastSessionDateCached != today) {
		this->App->db->setMainInfoValue("videosWatchedToday", "PLUS", "0");
		this->App->db->setMainInfoValue("videosWatchedToday", "MINUS", "0");
		this->App->db->setMainInfoValue("timeWatchedToday", "ALL", "0");
		this->App->db->setMainInfoValue("timeSessionToday", "ALL", "0");
		this->App->db->setMainInfoValue("lastSessionDate", "ALL", today);
		this->lastSessionDateCached = today;
	}
}

void VideoWatcherQt::run()
{
	while (this->running) {
		this->resetDailyStats();
		for (QSharedPointer<BasePlayer> player : this->Players) {
			if (player->video_path == this->App->mainWindow->ui.currentVideo->path) {
				emit updateProgressBarSignal(player->position,player->duration, player, true);
			}
			player->updateWatchedTiming();
			if (!player->isProcessAlive()) {
				if (player->position != -1) {
					this->db->updateVideoProgress(player->video_id, player->position);
				}
				// Track watched time from this player before removing it
				double watchedTime = player->getTotalWatchedTime();
				if (watchedTime > 0) {
					this->incrementTimeWatchedTotal(watchedTime);
				}
				if (player == this->mainPlayer) {
					this->clearAfterMainVideoEnd();
				}
				player->drop();
				this->Players.removeOne(player);
				player.reset();
			}
		}
		if (!this->Players.isEmpty()) {
			if (this->App->musicPlayer && this->App->musicPlayer->player->playbackState() == QMediaPlayer::PlayingState)
				emit updateMusicPlayerSignal(false);
			if (this->watching == false) {
				this->watching = true;
				if (this->videoWatcherSessionTimeStart != nullptr) {
					this->videoWatcherSessionTimeStart.reset();
				}
				this->videoWatcherSessionTimeStart = std::make_shared<std::chrono::microseconds>(utils::QueryUnbiasedInterruptTimeChrono());
			}
		}
		else {
			if (this->App->musicPlayer && (this->App->musicPlayer->player->playbackState() == QMediaPlayer::StoppedState || this->App->musicPlayer->player->playbackState() == QMediaPlayer::PausedState))
				emit updateMusicPlayerSignal(true);
			if (this->watching == true) {
				this->watching = false;
				double sessionElapsed = std::chrono::duration_cast<std::chrono::duration<double>>(utils::QueryUnbiasedInterruptTimeChrono() - *this->videoWatcherSessionTimeStart).count();
				this->incrementTimeSessionTotal(sessionElapsed);
				if (this->videoWatcherSessionTimeStart != nullptr) {
					this->videoWatcherSessionTimeStart.reset();
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
	for (QSharedPointer<BasePlayer> player : this->Players) {
		player->process->terminate();
		this->db->updateVideoProgress(player->video_id, player->position);
		// Track watched time from remaining players
		double watchedTime = player->getTotalWatchedTime();
		if (watchedTime > 0) {
			this->incrementTimeWatchedTotal(watchedTime);
		}
	}
	if (this->videoWatcherSessionTimeStart != nullptr) {
		double sessionElapsed = std::chrono::duration_cast<std::chrono::duration<double>>(utils::QueryUnbiasedInterruptTimeChrono() - *this->videoWatcherSessionTimeStart).count();
		this->incrementTimeSessionTotal(sessionElapsed);
		this->videoWatcherSessionTimeStart.reset();
	}
	this->clearAfterMainVideoEnd();
}

VideoWatcherQt::~VideoWatcherQt()
{
	for (QSharedPointer<BasePlayer> item : this->Players) {
		item->process->terminate();
		// Track watched time before cleanup
		double watchedTime = item->getTotalWatchedTime();
		if (watchedTime > 0) {
			this->incrementTimeWatchedTotal(watchedTime);
		}
		item.reset();
	}

	if (this->videoWatcherSessionTimeStart != nullptr) {
		double sessionElapsed = std::chrono::duration_cast<std::chrono::duration<double>>(utils::QueryUnbiasedInterruptTimeChrono() - *this->videoWatcherSessionTimeStart).count();
		this->incrementTimeSessionTotal(sessionElapsed);
		this->videoWatcherSessionTimeStart.reset();
	}

	delete this->db;
}
