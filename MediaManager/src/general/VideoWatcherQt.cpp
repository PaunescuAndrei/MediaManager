#include "stdafx.h"
#include "VideoWatcherQt.h"
#include "utils.h"
#include "winuser.h"
#include "MainApp.h"
#include "MainWindow.h"
#include "MpcApi.h"
#include "windef.h"
#include <QMutexLocker>
#include <QSqlQuery>
#include <QFileInfo>

VideoWatcherQt::VideoWatcherQt(MainApp* App, QObject* parent) : QThread(parent)
{
	this->App = App;
	this->db = new sqliteDB(this->App->db->location, "videowatcher_con");
}

QSharedPointer<BasePlayer> VideoWatcherQt::newPlayer(QString path, int video_id)
{
	QMutexLocker lock(&this->data_lock);
	QSharedPointer<BasePlayer> newVideo = QSharedPointer<MpcPlayer>::create(path, video_id,&this->CLASS_COUNT,this->App,this->App);
    this->Players.append(newVideo);
    this->CLASS_COUNT++;
    newVideo->category = this->App->currentDB;
    newVideo->startProgress = this->db->getVideoProgress(video_id, "0").toDouble();
    newVideo->lastKnownVideoPath = path;
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
        if (*it) {
            if ((*it)->position != -1) {
                this->db->updateVideoProgress((*it)->video_id, (*it)->position);
            }
            double watchedTime = (*it)->videoWatchedTime();
            double sessionTime = (*it)->videoSessionTime();
            if ((watchedTime > 0 || sessionTime > 0) && ((*it)->video_id >= 0 || (*it)->trackExternalVideo)) {
                QDateTime now = QDateTime::currentDateTime();
                double endPos = (*it)->position;
                if (endPos < 0) endPos = ((*it)->duration > 0) ? (*it)->duration : (*it)->startProgress;
                this->db->upsertWatchHistory((*it)->activeWatchHistoryRowId,
                    (*it)->video_id, (*it)->category,
                    (*it)->startProgress, endPos, watchedTime,
                    now.addSecs(-static_cast<qint64>(sessionTime)).toString("yyyy-MM-dd HH:mm:ss"),
                    now.toString("yyyy-MM-dd HH:mm:ss"), sessionTime,
                    false);
            }
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
				currentTime.toString("hh:mm")), 3000, false); // this should have worked with direct = true, but this commit https://github.com/clsid2/mpc-hc/commit/3d06f2798479397acb1a0734a41a246fbfd926ef introduced a bug in mpc-hc that causes the OSD to get replaced by the previous play/pause command and it works with false because of the small delay, so I guess it's better to keep it like this until it's fixed in mpc-hc and then we can change it back to direct = true
		}
	}
}

void VideoWatcherQt::checkpointPlayer(QSharedPointer<BasePlayer> player, int intervalSeconds)
{
    QDateTime now = QDateTime::currentDateTime();
    if (!player->shouldCheckpoint(now, intervalSeconds))
        return;
    if (player->video_path.isEmpty())
        return;
    if (!player->trackExternalVideo && player->video_id < 0)
        return;
    double watched = player->videoWatchedTime();
    double session = player->videoSessionTime();
    double pos = player->position;
    if (pos < 0) pos = player->startProgress;
    this->db->upsertWatchHistory(player->activeWatchHistoryRowId,
        player->video_id, player->category,
        player->startProgress, pos, watched,
        now.addSecs(-static_cast<qint64>(session)).toString("yyyy-MM-dd HH:mm:ss"),
        now.toString("yyyy-MM-dd HH:mm:ss"), session,
        false);
    player->lastCheckpointTime = now;
    if (pos >= 0 && player->video_id >= 0) {
        this->db->updateVideoProgress(player->video_id, pos);
    }
    if (this->App->config->get_bool("counter_use_actual_watch_time")) {
        double delta = player->videoWatchedTime() - player->lastCheckpointWatchedTime;
        if (delta > 0.0) {
            player->lastCheckpointWatchedTime = player->videoWatchedTime();
            emit timeWatchedIncrementSignal(delta);
        }
    }
}

void VideoWatcherQt::handleExternalVideoChange(QSharedPointer<BasePlayer> player)
{
    double watchedTime = player->videoWatchedTime();
    double sessionTime = player->videoSessionTime();
    if ((watchedTime > 0 || sessionTime > 0) && player->video_id >= 0) {
        QDateTime now = QDateTime::currentDateTime();
        double endPos = player->position;
        if (endPos < 0) endPos = (player->duration > 0) ? player->duration : player->startProgress;
        this->db->upsertWatchHistory(player->activeWatchHistoryRowId,
            player->video_id, player->category,
            player->startProgress, endPos, watchedTime,
            now.addSecs(-static_cast<qint64>(sessionTime)).toString("yyyy-MM-dd HH:mm:ss"),
            now.toString("yyyy-MM-dd HH:mm:ss"), sessionTime,
            false);
    }
    if (!player->trackExternalVideo) {
        player->video_id = -1;
        player->lastKnownVideoPath = player->video_path;
        player->activeWatchHistoryRowId = -1;
        player->startProgress = 0;
        return;
    }
    int newVideoId = -1;
    QSqlQuery query(this->db->db);
    query.prepare("SELECT id FROM videodetails WHERE path = ? AND category = ?");
    query.addBindValue(player->video_path);
    query.addBindValue(player->category);
    if (query.exec() && query.next()) {
        newVideoId = query.value(0).toInt();
    }
    player->video_id = newVideoId;
    player->lastKnownVideoPath = player->video_path;
    player->activeWatchHistoryRowId = -1;
    player->startProgress = 0;
    player->resetVideoTiming();
    QDateTime now = QDateTime::currentDateTime();
    player->activeWatchHistoryRowId = this->db->upsertWatchHistory(
        player->activeWatchHistoryRowId,
        newVideoId, player->category,
        0, 0, 0,
        now.toString("yyyy-MM-dd HH:mm:ss"),
        now.toString("yyyy-MM-dd HH:mm:ss"),
        0, false);
}

void VideoWatcherQt::run()
{
    while (this->running) {
        int saveInterval = this->App->config->get("session_save_interval_seconds").toInt();
        for (QSharedPointer<BasePlayer> player : this->Players) {
            if (!player->video_path.isEmpty() && player->video_path != player->lastKnownVideoPath && !player->change_in_progress) {
                this->handleExternalVideoChange(player);
            }
            this->checkpointPlayer(player, saveInterval);
            if (player->video_path == this->App->mainWindow->ui.currentVideo->path) {
                emit updateProgressBarSignal(player->position,player->duration, player, true);
            }
            player->updateWatchedTiming();
            if (!player->isProcessAlive()) {
                if (player->position != -1) {
                    this->db->updateVideoProgress(player->video_id, player->position);
                }
                double watchedTime = player->videoWatchedTime();
                double sessionTime = player->videoSessionTime();
                if ((watchedTime > 0 || sessionTime > 0) && (player->video_id >= 0 || player->trackExternalVideo)) {
                    QDateTime now = QDateTime::currentDateTime();
                    QString sessionEnd = now.toString("yyyy-MM-dd HH:mm:ss");
                    qint64 sessionSecs = static_cast<qint64>(sessionTime);
                    QString sessionStart = now.addSecs(-sessionSecs).toString("yyyy-MM-dd HH:mm:ss");
                    double watched_end = player->position;
                    if (watched_end < 0)
                        watched_end = (player->duration > 0) ? player->duration : player->startProgress;
                    this->db->upsertWatchHistory(player->activeWatchHistoryRowId,
                        player->video_id, player->category,
                        player->startProgress, watched_end, watchedTime,
                        sessionStart, sessionEnd, sessionTime,
                        false);
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
            this->watching = true;
            if (this->App->musicPlayer && this->App->musicPlayer->player->playbackState() == QMediaPlayer::PlayingState)
                emit updateMusicPlayerSignal(false);
        }
        else {
            this->watching = false;
            if (this->App->musicPlayer && (this->App->musicPlayer->player->playbackState() == QMediaPlayer::StoppedState || this->App->musicPlayer->player->playbackState() == QMediaPlayer::PausedState))
                emit updateMusicPlayerSignal(true);
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
        if (player->position != -1) {
            this->db->updateVideoProgress(player->video_id, player->position);
        }
        double watchedTime = player->videoWatchedTime();
        double sessionTime = player->videoSessionTime();
        if ((watchedTime > 0 || sessionTime > 0) && (player->video_id >= 0 || player->trackExternalVideo)) {
            QDateTime now = QDateTime::currentDateTime();
            double endPos = player->position;
            if (endPos < 0) endPos = (player->duration > 0) ? player->duration : player->startProgress;
            this->db->upsertWatchHistory(player->activeWatchHistoryRowId,
                player->video_id, player->category,
                player->startProgress, endPos, watchedTime,
                now.addSecs(-static_cast<qint64>(sessionTime)).toString("yyyy-MM-dd HH:mm:ss"),
                now.toString("yyyy-MM-dd HH:mm:ss"), sessionTime,
                false);
        }
        player->process->terminate();
    }
    this->clearAfterMainVideoEnd();
}

VideoWatcherQt::~VideoWatcherQt()
{
    QDateTime now = QDateTime::currentDateTime();
    for (QSharedPointer<BasePlayer> item : this->Players) {
        if (item->position != -1 && item->video_id >= 0) {
            this->db->updateVideoProgress(item->video_id, item->position);
        }
        double w = item->videoWatchedTime();
        double s = item->videoSessionTime();
        if ((w > 0 || s > 0) && (item->video_id >= 0 || item->trackExternalVideo)) {
            double endPos = item->position;
            if (endPos < 0) endPos = (item->duration > 0) ? item->duration : item->startProgress;
            this->db->upsertWatchHistory(item->activeWatchHistoryRowId,
                item->video_id, item->category,
                item->startProgress, endPos, w,
                now.addSecs(-static_cast<qint64>(s)).toString("yyyy-MM-dd HH:mm:ss"),
                now.toString("yyyy-MM-dd HH:mm:ss"), s,
                false);
        }
        item->process->terminate();
        item.reset();
    }
    delete this->db;
}
