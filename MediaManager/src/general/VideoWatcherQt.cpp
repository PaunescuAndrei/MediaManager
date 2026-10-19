#include "stdafx.h"
#include "VideoWatcherQt.h"
#include "WatchHistory.h"
#include "utils.h"
#include "winuser.h"
#include "MainApp.h"
#include "MainWindow.h"
#include "MpcApi.h"
#include "windef.h"
#include <QMutexLocker>
#include <QSqlQuery>
#include <QFileInfo>
#include <QMessageBox>

VideoWatcherQt::VideoWatcherQt(MainApp* App, QObject* parent) : QThread(parent)
{
	this->App = App;
	// Do not open the SQLite connection here: this constructor runs on the GUI thread and
	// Qt's SQLite driver opens connections with SQLITE_OPEN_NOMUTEX, so a connection may
	// only be used by the thread that created it. run() opens it on the watcher thread.
	this->m_dbLocation = this->App->db->location;
	this->m_lastRollDate = QDate::currentDate();
}

QSharedPointer<BasePlayer> VideoWatcherQt::newPlayer(QString path, int video_id)
{
	// Start a new session if none is active (DB work stays outside data_lock).
	int sid = ensureSession();
	if (sid <= 0) {
		if (qMainApp) {
			qMainApp->logger->log(
				QStringLiteral("Failed to create new session — watch history will not be linked to a session"),
				"VideoWatcherQt");
			QMessageBox* mb = new QMessageBox(QMessageBox::Warning,
				QStringLiteral("Session Error"),
				QStringLiteral("Failed to create a new watch session.\n\n"
					"Watch history will not be tracked until the application is restarted.\n"
					"Check that the database is not full or locked."),
				QMessageBox::Ok, this->App->mainWindow);
			mb->setAttribute(Qt::WA_DeleteOnClose);
			mb->show();
		}
	}

	QSharedPointer<BasePlayer> newVideo = QSharedPointer<MpcPlayer>::create(path, video_id,&this->CLASS_COUNT,this->App,this->App);
	newVideo->setCategory(this->App->currentDB);
	// newPlayer() is called from the GUI thread (video start / next / watch external),
	// never from the watcher thread, so these reads go through the GUI connection. The
	// watcher's own connection belongs to the watcher thread.
	newVideo->setVideoType(this->App->db->getVideoType(video_id));
	newVideo->startProgress = this->App->db->getVideoProgress(video_id, "0").toDouble();
	newVideo->setTrackedVideoPath(path);
	newVideo->start();

	// Publish only once the player is fully set up, and hold the lock for the append only.
	QMutexLocker lock(&this->data_lock);
	this->Players.append(newVideo);
	this->CLASS_COUNT++;
	return newVideo;
}

void VideoWatcherQt::clearData(bool include_mainplayer) {
	// Detach the players under the lock; the database work and window teardown happen
	// outside it, so the watcher/raw-input threads never wait on our SQLite calls.
	QList<QSharedPointer<BasePlayer>> cleared;
	bool empty = false;
	{
		QMutexLocker lock(&this->data_lock);
		for (int i = 0; i < this->Players.size(); ) {
			const QSharedPointer<BasePlayer>& player = this->Players.at(i);
			if (!include_mainplayer && player != nullptr && player == this->mainPlayer) {
				++i;
				continue;
			}
			cleared.append(player);
			this->Players.removeAt(i);
		}
		empty = this->Players.isEmpty();
	}

	for (const QSharedPointer<BasePlayer>& player : cleared) {
		if (!player) continue;
		if (player->position != -1) {
			// GUI thread: use the GUI connection, never the watcher's own one.
			this->App->db->updateVideoProgress(player->video_id, player->position);
		}
		double watchedTime = player->videoWatchedTime();
		if (watchhistory::hasWatchToPersist(player) && watchhistory::isTrackedVideo(player)) {
			watchhistory::flushPlayer(this->App->db, *player, m_currentSessionId, false, true);
		}
		this->emitCounterDelta(player, watchedTime);
		player->drop();
	}

	// End session if all players are gone
	if (m_currentSessionId > 0 && empty) {
		// GUI thread: close the session through the GUI connection.
		endCurrentSession(this->App->db);
	}

	if(include_mainplayer)
		this->clearAfterMainVideoEnd();
}

QList<QSharedPointer<BasePlayer>> VideoWatcherQt::playersSnapshot()
{
	QMutexLocker lock(&this->data_lock);
	return this->Players;
}

void VideoWatcherQt::setMainPlayer(QSharedPointer<BasePlayer> player) {
	// mainPlayer is read by the watcher thread and by toggle_window() (raw-input thread).
	QMutexLocker lock(&this->data_lock);
	this->mainPlayer = player;
}

void VideoWatcherQt::clearMainPlayer() {
	QMutexLocker lock(&this->data_lock);
	this->mainPlayer = nullptr;
}

void VideoWatcherQt::clearAfterMainVideoEnd()
{
	// clearData()/~VideoWatcherQt call this from the GUI thread. run() also calls it from
	// the watcher thread when the main player's process ends, and finish_dialog is a GUI
	// object, so hop back to the GUI thread before touching it.
	if (QThread::currentThread() != this->thread()) {
		QMetaObject::invokeMethod(this, [this]() { this->clearAfterMainVideoEnd(); },
			Qt::QueuedConnection);
		return;
	}
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
	// Copy the player while holding the lock, then release it before the slow window work
	// below (HWND enumeration, foreground retries, OSD). This way the raw-input thread
	// never waits on the watcher's checkpoint pass, and the watcher never waits on MPC.
	QSharedPointer<BasePlayer> player;
	{
		QMutexLocker lock(&this->data_lock);
		if (!this->Players.isEmpty() && this->mainPlayer != nullptr)
			player = this->mainPlayer;
	}
	if (player) {
		bool fullscreen = utils::is_hwnd_full_screen(player->player_hwnd);

		// Identify the foreground by process instead of enumerating every
		// top-level window: GetWindowThreadProcessId is O(1) and also covers
		// the player's dialogs/other windows. The window we bring back remains
		// the player-provided player_hwnd.
		const HWND foreground_window = GetForegroundWindow();
		DWORD foreground_pid = 0;
		if (foreground_window)
			GetWindowThreadProcessId(foreground_window, &foreground_pid);
		const bool is_foreground =
			player->pid > 0 && foreground_pid == static_cast<DWORD>(player->pid);

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
			this->old_foreground_window = foreground_window;
			int tries = 0;
			while (tries < 10) {
				utils::bring_hwnd_to_foreground_uiautomation_method(player->player_hwnd, this->App->uiAutomation);
				tries++;
				HWND fg = GetForegroundWindow();
				DWORD fg_pid = 0;
				if (fg)
					GetWindowThreadProcessId(fg, &fg_pid);
				if (player->pid > 0 && fg_pid == static_cast<DWORD>(player->pid))
					break;
				if (!player->isProcessAlive())
					break;
				// Give the shell a chance to apply the activation before the
				// next attempt; previously all ten tries could fire before the
				// first one had any effect.
				Sleep(1);
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
	const PlayerSnapshot s = player->snapshot();
	if (s.videoPath.isEmpty())
		return;
	if (!s.trackExternalVideo && s.videoId < 0)
		return;
	double watched = player->videoWatchedTime();
	double pos = s.position;
	if (pos < 0) pos = s.startProgress;
	// Bump session_time first so that if we crash after upsertWatchHistory
	// the session duration on recovery is at worst slightly longer than the
	// last persisted row, not shorter (closeOrphanedSessions derives
	// session_end from session_time).
	if (m_currentSessionId > 0) {
		this->db->bumpSessionTime(m_currentSessionId.load(), currentSessionTime());
	}
	if (watchhistory::hasWatchToPersist(player)) {
		watchhistory::flushPlayer(this->db, *player, m_currentSessionId, false, false);
	}
	player->setLastCheckpointTime(now);
	if (pos >= 0 && s.videoId >= 0) {
		this->db->updateVideoProgress(s.videoId, pos);
	}
	this->emitCounterDelta(player, watched);
}

void VideoWatcherQt::handleExternalVideoChange(QSharedPointer<BasePlayer> player)
{
	// If MPC reports a path that matches what we told it to open (target_video_path),
	// this is a programmatic change, not an external one. Syncing trackedVideoPath is
	// all that's needed — changePlayerVideo / newPlayer already set video_id,
	// startProgress, activeWatchHistoryRowId, and saved the previous video's watch
	// history. The old code used to run the full external-change path here, which
	// corrupted video_id (set to -1) and created watch-history rows with mismatched
	// video_id/path combos (row 255 in the DB proved the race was real).
	//
	// The primary fix lives in MpcPlayer::run() — when CMD_NOWPLAYING confirms the
	// file we asked for, it syncs trackedVideoPath there, so the watcher's run() loop
	// never sees video_path != trackedVideoPath for programmatic changes. This guard
	// is defense-in-depth: if handleExternalVideoChange is ever called from a code
	// path that lacks the trackedVideoPath sync, the guard prevents state corruption.
	const QString currentPath = player->videoPath();
	if (currentPath == player->targetVideoPath()) {
		player->setTrackedVideoPath(currentPath);
		return;
	}

	if (watchhistory::hasWatchToPersist(player) && watchhistory::isTrackedVideo(player)) {
		// The player is still labelled with the video we are leaving (video_id and
		// trackedVideoPath) while video_path already names the new file, and flushPlayer
		// names the row from trackedVideoPath — so the outgoing video's accumulated watch
		// time keeps its own path instead of being credited to the new video.
		watchhistory::flushPlayer(this->db, *player, m_currentSessionId, false, true);
	}
	if (!player->trackExternalVideo) {
		player->video_id = -1;
		player->setVideoType(QString());
		player->setTrackedVideoPath(currentPath);
		player->activeWatchHistoryRowId = -1;
		player->startProgress = 0;
		return;
	}
	int newVideoId = -1;
	QSqlQuery query(this->db->db);
	query.prepare("SELECT id FROM videodetails WHERE path = ? AND category = ?");
	query.addBindValue(currentPath);
	query.addBindValue(player->category());
	if (query.exec() && query.next()) {
		newVideoId = query.value(0).toInt();
	}
	player->video_id = newVideoId;
	player->setVideoType(newVideoId >= 0 ? this->db->getVideoType(newVideoId) : QString());
	player->setTrackedVideoPath(currentPath);
	player->activeWatchHistoryRowId = -1;
	player->startProgress = 0;
	player->resetVideoTiming();
	// Only open a row for a video that will actually be tracked; an untracked
	// external file (empty_player_tracking off) would otherwise leave a row that
	// no flush path is allowed to update.
	if (watchhistory::isTrackedVideo(player)) {
		watchhistory::RowState newRow;
		newRow.videoId = newVideoId;
		newRow.category = player->category();
		newRow.videoPath = currentPath;
		watchhistory::flush(this->db, newRow, m_currentSessionId);
		player->activeWatchHistoryRowId = newRow.rowId;
	}
}

bool VideoWatcherQt::shouldCountWatchTime(QSharedPointer<BasePlayer> player)
{
	if (player->trackExternalVideo)
		return true;
	if (this->App->currentDB != "MINUS")
		return true;
	const bool isMinusPlus = this->App->config->get("sv_mode").compare("PLUS", Qt::CaseInsensitive) == 0;
	return isMinusPlus && svTypes.contains(player->videoType(), Qt::CaseInsensitive);
}

void VideoWatcherQt::emitCounterDelta(QSharedPointer<BasePlayer> player, double watched)
{
	if (!this->App->config->get_bool("counter_use_actual_watch_time")) return;
	if (!shouldCountWatchTime(player)) return;
	double delta = watched - player->lastCheckpointWatchedTime;
	if (delta <= 0.0) return;
	player->lastCheckpointWatchedTime = watched;
	emit timeWatchedIncrementSignal(delta);
}

double VideoWatcherQt::currentSessionTime() const
{
	if (m_currentSessionId < 0) return 0.0;
	return std::chrono::duration_cast<std::chrono::duration<double>>(
		utils::QueryUnbiasedInterruptTimeChrono() - m_sessionStartMonotonic).count();
}

int VideoWatcherQt::ensureSession()
{
	if (m_currentSessionId > 0) return m_currentSessionId;

	m_sessionStartTime = QDateTime::currentDateTime();
	m_sessionStartMonotonic = utils::QueryUnbiasedInterruptTimeChrono();
	// ensureSession() is called from the GUI thread (newPlayer and
	// MainWindow::NextButtonClicked), so the session row is created through the GUI
	// connection. The watcher thread only reads the resulting id afterwards.
	m_currentSessionId = this->App->db->insertSession(
		m_sessionStartTime.toString("yyyy-MM-dd HH:mm:ss"));
	if (m_currentSessionId <= 0) {
		m_sessionStartMonotonic = std::chrono::microseconds{};
		if (qMainApp) {
			qMainApp->logger->log(
				QStringLiteral("Failed to create session — watch history may not be linked"),
				"VideoWatcherQt");
		}
	}
	return m_currentSessionId;
}

void VideoWatcherQt::resetSession()
{
	m_currentSessionId = -1;
}

void VideoWatcherQt::rollDayIfNeeded()
{
	if (m_lastRollDate == QDate::currentDate()) return;

	QList<QSharedPointer<BasePlayer>> players;
	{
		QMutexLocker lock(&this->data_lock);
		players = this->Players;
	}

	// Flush every active player's accumulated watch time into its current row while
	// that row still belongs to the old day, then rebase the per-play timing so the
	// next checkpoint opens a fresh row for the new day. Day queries bucket rows by
	// watch_history.watched_at, which is stamped once at INSERT and never rewritten,
	// so without this a row opened before midnight would keep yesterday's date and
	// its post-midnight time would count for the wrong day. The session is left open
	// on purpose: a watch that crosses midnight stays one session.
	for (const QSharedPointer<BasePlayer>& player : players) {
		if (!player) continue;
		const PlayerSnapshot s = player->snapshot();
		if (s.videoPath.isEmpty()) continue;
		if (!s.trackExternalVideo && s.videoId < 0) continue;
		// A video change in flight leaves video_id and video_path describing different
		// videos, so flushing now would persist a mismatched row. Retry on the next
		// tick: m_lastRollDate only advances after a pass that skipped nobody.
		if (s.changeInProgress || s.videoPath != s.trackedVideoPath) return;

		double watched = player->videoWatchedTime();
		if (watchhistory::hasWatchToPersist(player)) {
			// Mid-watch snapshot of a live player: if the playhead has not been reported
			// yet, claim no progress rather than the full duration.
			watchhistory::flushPlayer(this->db, *player, m_currentSessionId, false, false);
		}
		// Emit the pending counter delta before the baseline moves, so the
		// pre-midnight portion is counted exactly once.
		this->emitCounterDelta(player, watched);
		player->activeWatchHistoryRowId = -1;   // next checkpoint inserts a new row
		player->resetVideoTiming();             // rebase watched-time / checkpoint clock
	}

	m_lastRollDate = QDate::currentDate();
	if (qMainApp) {
		qMainApp->logger->log(
			QStringLiteral("Day changed - rolled watch history rows for the new day"),
			"VideoWatcherQt");
	}
}

double VideoWatcherQt::endCurrentSession(sqliteDB* target)
{
	const int sessionId = m_currentSessionId.load();
	if (sessionId <= 0) return -1.0;

	QDateTime now = QDateTime::currentDateTime();
	double sessionTime = currentSessionTime();

	// The watcher thread closes sessions through its own connection. clearData() runs on
	// the GUI thread and passes the GUI connection, because the watcher's connection may
	// only be used from the watcher thread.
	sqliteDB* targetDb = target ? target : this->db;
	if (targetDb) {
		targetDb->updateSessionEnd(sessionId,
			now.toString("yyyy-MM-dd HH:mm:ss"), sessionTime);
	}

	m_currentSessionId = -1;
	return sessionTime;
}

void VideoWatcherQt::run()
{
	// Open the connection on the thread that will use it. Qt's SQLite driver opens every
	// connection with SQLITE_OPEN_NOMUTEX, so a connection shared with the GUI thread gets
	// its internal allocator (the lookaside free lists) corrupted.
	this->db = new sqliteDB(this->m_dbLocation, "videowatcher_con");

	while (this->running) {
		int saveInterval = this->App->config->get("session_save_interval_seconds").toInt();
		// Every lock below only copies or edits the player list; database work and signals
		// happen outside it, so the GUI and raw-input threads never wait on a checkpoint's
		// SQLite call.
		this->rollDayIfNeeded();

		QList<QSharedPointer<BasePlayer>> players;
		{
			QMutexLocker lock(&this->data_lock);
			players = this->Players;
		}
		for (const QSharedPointer<BasePlayer>& player : players) {
			if (!player) continue;
			const PlayerSnapshot s = player->snapshot();
			if (!s.videoPath.isEmpty() && s.videoPath != s.trackedVideoPath && !s.changeInProgress) {
				this->handleExternalVideoChange(player);
			}
			this->checkpointPlayer(player, saveInterval);
			// Compare against the current video path published by the label; both the
			// label and the player hand out locked copies, so this is safe from the
			// watcher thread.
			if (player->videoPath() == this->App->mainWindow->ui.currentVideo->currentPath()) {
				emit updateProgressBarSignal(player->position,player->duration, player, true);
			}
			player->updateWatchedTiming();
			if (!player->isProcessAlive()) {
				if (player->position != -1) {
					this->db->updateVideoProgress(player->video_id, player->position);
				}
				double watchedTime = player->videoWatchedTime();
				if (watchhistory::hasWatchToPersist(player) && watchhistory::isTrackedVideo(player)) {
					watchhistory::flushPlayer(this->db, *player, m_currentSessionId, false, true);
				}
				this->emitCounterDelta(player, watchedTime);
				bool wasMainPlayer = false;
				{
					QMutexLocker lock(&this->data_lock);
					wasMainPlayer = (player == this->mainPlayer);
				}
				if (wasMainPlayer) {
					this->clearAfterMainVideoEnd();
				}
				player->drop();
				{
					QMutexLocker lock(&this->data_lock);
					this->Players.removeOne(player);
				}
			}
		}

		bool hasPlayers = false;
		{
			QMutexLocker lock(&this->data_lock);
			hasPlayers = !this->Players.isEmpty();
		}
		if (hasPlayers) {
			this->watching = true;
			if (this->App->musicPlayer && this->App->musicPlayer->player->playbackState() == QMediaPlayer::PlayingState)
				emit updateMusicPlayerSignal(false);
		}
		else {
			// Session ended — all players gone
			if (m_currentSessionId > 0) {
				int closingSessionId = m_currentSessionId.load();
				double sessionTime = endCurrentSession();

				// Emit session summary notification
				int minSessionSec = this->App->config->get("notification_session_summary_min_session_seconds").toInt();
				if (sessionTime >= static_cast<double>(minSessionSec)) {
					auto [videosWatched, videosCompleted, totalWatch, _] =
						this->db->getSessionSummaryStats(closingSessionId, this->App->config->get("notification_session_summary_min_video_watch_seconds").toDouble());
					if (videosWatched > 0) {
						QStringList cats = this->db->getSessionCategories(closingSessionId);
						QString category = cats.isEmpty() ? this->App->currentDB : cats.join(", ");
						emit sessionEndedSignal(category, videosWatched, videosCompleted, totalWatch, sessionTime);
					}
				}
			}
			this->watching = false;
			if (this->App->musicPlayer && (this->App->musicPlayer->player->playbackState() == QMediaPlayer::StoppedState || this->App->musicPlayer->player->playbackState() == QMediaPlayer::PausedState))
				emit updateMusicPlayerSignal(true);
		}
		if (this->watching != m_taskbarWatching) {
			m_taskbarWatching = this->watching;
			if (this->App->debug_mode == false)
				emit this->updateTaskbarIconSignal(this->watching);
			emit this->updateTaskbarPauseSignal(!this->watching);
		}
		this->msleep(100);
	}
	QList<QSharedPointer<BasePlayer>> players;
	{
		QMutexLocker lock(&this->data_lock);
		players = this->Players;
	}
	for (const QSharedPointer<BasePlayer>& player : players) {
		if (player->position != -1) {
			this->db->updateVideoProgress(player->video_id, player->position);
		}
		if (watchhistory::hasWatchToPersist(player) && watchhistory::isTrackedVideo(player)) {
			watchhistory::flushPlayer(this->db, *player, m_currentSessionId, false, true);
		}
		player->process->terminate();
	}

	// End session on shutdown
	if (m_currentSessionId > 0) {
		endCurrentSession();
	}

	this->clearAfterMainVideoEnd();

	// Close the connection on the thread that opened it.
	delete this->db;
	this->db = nullptr;
}

VideoWatcherQt::~VideoWatcherQt()
{
	// run() deletes the connection when the thread exits (and already flushed the
	// players), so only touch the database here if the thread never ran.
	if (this->db) {
		for (QSharedPointer<BasePlayer> item : this->Players) {
			if (item->position != -1 && item->video_id >= 0) {
				this->db->updateVideoProgress(item->video_id, item->position);
			}
			if (watchhistory::hasWatchToPersist(item) && watchhistory::isTrackedVideo(item)) {
				watchhistory::flushPlayer(this->db, *item, m_currentSessionId, false, true);
			}
			item->process->terminate();
			item.reset();
		}

		// End session on destruction
		if (m_currentSessionId > 0) {
			endCurrentSession();
		}

		delete this->db;
		this->db = nullptr;
	}
}
