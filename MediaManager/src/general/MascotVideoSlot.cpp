#include "stdafx.h"
#include "MascotVideoSlot.h"
#include "ThreadedMediaPlayer.h"

#include <QDebug>
#include <QPlaybackOptions>
#include <QRandomGenerator>
#include <QTimer>
#include <QVideoSink>
#include <algorithm>

namespace {
	// Output indices must match MascotSurface::slotSink(); the names keep surface and players in step.
	constexpr int OUTPUT_A = 0;
	constexpr int OUTPUT_B = 1;
}

MascotVideoSlot::MascotVideoSlot(QObject* parent)
	: QObject(parent)
{
	this->m_timer = new QTimer(this);
	this->m_timer->setSingleShot(true);
	this->m_timer->setInterval(this->m_swapTimeoutMs);
	QObject::connect(this->m_timer, &QTimer::timeout, this, &MascotVideoSlot::handleStartTimeout);
}

MascotVideoSlot::~MascotVideoSlot()
{
	this->shutdown();
}

void MascotVideoSlot::setSinks(QVideoSink* sinkA, QVideoSink* sinkB)
{
	this->setSink(OUTPUT_A, sinkA);
	this->setSink(OUTPUT_B, sinkB);
}

void MascotVideoSlot::setSink(int ab, QVideoSink* sink)
{
	if (this->m_sinks[ab].data() == sink)
		return;

	if (this->m_sinkConnections[ab])
		QObject::disconnect(this->m_sinkConnections[ab]);
	this->m_sinkConnections[ab] = QMetaObject::Connection();

	if (this->m_players[ab] && !this->m_warnedSinkReplaced) {
		this->m_warnedSinkReplaced = true;
		qWarning() << "MascotVideoSlot: sink" << ab
			<< "was replaced after its player started; that player keeps the sink it was started with";
	}

	this->m_sinks[ab] = sink;
	if (!sink)
		return;

	this->m_sinkConnections[ab] = QObject::connect(sink, &QVideoSink::videoFrameChanged, this,
		[this, ab](const QVideoFrame& frame) { this->handleFrame(ab, frame); });
}

void MascotVideoSlot::setSinglePlayer(bool single)
{
	if (this->m_singlePlayer == single)
		return;
	this->m_singlePlayer = single;
	if (!single)
		return;

	// A B-side load or parked clip can never be shown in this mode: dropped without a signal, not a file failure.
	// The parked record goes before the player it describes is destroyed below.
	if (this->m_pending.serial != 0 && this->m_pending.ab == OUTPUT_B)
		this->releasePending();

	if (this->m_preload.parked && this->m_preload.ab != OUTPUT_A)
		this->m_preload = Preload{};

	ThreadedMediaPlayer* playerB = this->m_players[OUTPUT_B];
	if (!playerB)
		return;
	// An on-screen B stays until the next swap reloads A, because retiring it would blank the panel.
	if (this->m_activeAb == OUTPUT_B)
		return;

	playerB->shutdown();
	this->m_players[OUTPUT_B] = nullptr;
	delete playerB;
}

void MascotVideoSlot::setRandomStart(bool random, int clipSeconds)
{
	this->m_randomStart = random;
	// A non-positive length means no constraint, so a bad config cannot offset past the end of the file.
	this->m_clipMs = clipSeconds > 0 ? qint64(clipSeconds) * 1000 : 0;
}

void MascotVideoSlot::setTimings(int swapTimeoutMs, int retryIntervalMs, int retryAttempts)
{
	// Floor 500 ms (measured warm loads 49-300 ms), ceiling 60 s: longer would hold isBusy() on a dead file.
	// No "0 = wait forever": this timer is the only thing that releases a load that never paints or errors.
	this->m_swapTimeoutMs = std::clamp(swapTimeoutMs, 500, 60000);
	// 20 ms is under a frame at any refresh rate; 2 s is far longer than a delegate takes to appear.
	this->m_retryIntervalMs = std::clamp(retryIntervalMs, 20, 2000);
	// 0 attempts is supported (give up at once), so the floor is 0; 50 bounds the churn.
	this->m_retryAttempts = std::clamp(retryAttempts, 0, 50);
	// A deadline already armed under the old value has to move too, or a shortened one waits a clip.
	if (this->m_timer->isActive())
		this->m_timer->start(this->m_swapTimeoutMs);
	else
		this->m_timer->setInterval(this->m_swapTimeoutMs);
}

void MascotVideoSlot::swapTo(const QString& path, qint64 startOffsetMs)
{
	if (this->m_shutdown || path.isEmpty()) {
		// A caller with no player behind the frame would wait out its own deadline: report a wiring failure.
		emit this->failed(path, FailKind::Wiring);
		return;
	}

	int ab = this->m_activeAb == OUTPUT_A ? OUTPUT_B : OUTPUT_A;
	if (this->m_singlePlayer)
		ab = OUTPUT_A; // one sink, one player: a swap reloads the output that is on screen

	// A parked clip on the output this load takes is spent: setSource() replaces its media, so the record goes.
	// Record only: emptying the player here would post a second load.
	if (this->m_preload.parked)
		this->m_preload = Preload{};

	if (!this->ensurePlayer(ab)) {
		emit this->failed(path, FailKind::Wiring);
		return;
	}

	if (this->m_pending.serial != 0) {
		// A load left on the other output (single-player mid-load) is stopped, not left decoding an unseen clip.
		if (this->m_pending.ab != ab) {
			if (ThreadedMediaPlayer* stale = this->m_players[this->m_pending.ab]) {
				stale->stop();
				stale->setSource(QString());
			}
		}
		this->m_pending = Load{};
	}
	this->m_timer->stop();

	this->m_pending.serial = ++this->m_serial;
	this->m_pending.ab = ab;
	this->m_pending.path = path;
	this->m_pending.requestedOffsetMs = startOffsetMs;

	ThreadedMediaPlayer* player = this->m_players[ab];
	// An equal URL is a no-op in QMediaPlayer, so empty the source first or the gate times out on a good file.
	if (player->currentSource() == path)
		player->setSource(QString());
	player->setSource(path);

	if (!this->m_paused) {
		// Armed before LoadedMedia on purpose; handleMediaStatus() re-arms it once the load half is spent.
		this->m_timer->start();
	}
}

void MascotVideoSlot::preload(const QString& path, qint64 startOffsetMs)
{
	if (this->m_shutdown || path.isEmpty()) {
		// A wiring report, not a verdict on the file: nothing was attempted, so keep the path.
		emit this->failed(path, FailKind::Wiring);
		return;
	}

	// Same rule as swapTo(), so a preload and its swap cannot disagree about which player is idle.
	int ab = this->m_activeAb == OUTPUT_A ? OUTPUT_B : OUTPUT_A;
	if (this->m_singlePlayer)
		ab = OUTPUT_A;

	// Never reload the on-screen player (that ends the visible clip); single-player has no free output after clip 1.
	if (ab == this->m_activeAb)
		return;

	// One load at a time through the pending record; a swap in flight wins.
	if (this->m_pending.serial != 0)
		return;

	if (this->m_preload.parked) {
		// Never open the parked clip twice: a different file, or the same one at a different offset, replaces it.
		// A parked clip is a moment (the mirror pair's invariant); a negative offset cannot be compared, so it stands.
		const bool sameMoment = startOffsetMs < 0 || this->m_preload.offsetMs == startOffsetMs;
		if (this->m_preload.path == path && sameMoment)
			return;
		this->discardPreload();
	}

	if (!this->ensurePlayer(ab)) {
		emit this->failed(path, FailKind::Wiring);
		return;
	}

	this->m_pending.serial = ++this->m_serial;
	this->m_pending.ab = ab;
	this->m_pending.path = path;
	this->m_pending.requestedOffsetMs = startOffsetMs;
	this->m_pending.preload = true;

	ThreadedMediaPlayer* player = this->m_players[ab];
	// Same equal-URL trap as swapTo(): empty it first.
	if (player->currentSource() == path)
		player->setSource(QString());
	player->setSource(path);

	if (!this->m_paused) {
		// Same deadline as a swap: an unbounded preload would hold this record and its output forever.
		this->m_timer->start();
	}
}

bool MascotVideoSlot::swapToPreloaded()
{
	if (this->m_shutdown || !this->m_preload.parked)
		return false;

	const Preload parked = this->m_preload;
	// Checked before the index: `parked.ab` is -1 when empty and anything else would run off `m_players`.
	if (parked.ab < OUTPUT_A || parked.ab > OUTPUT_B) {
		this->discardPreload();
		return false;
	}
	ThreadedMediaPlayer* player = this->m_players[parked.ab];
	if (!player) {
		this->discardPreload();
		return false;
	}

	this->m_preload = Preload{};
	this->m_pending = Load{};
	this->m_timer->stop();
	this->m_activeAb = parked.ab;
	this->m_activePath = parked.path;

	// Parked paused, so the swap is this play() plus the owner's opacity writes; while paused it stays parked.
	if (!this->m_paused)
		player->play();

	emit this->firstFrame(parked.path, parked.ab, parked.frameSize, parked.offsetMs);
	return true;
}

void MascotVideoSlot::discardPreload()
{
	// A preload still opening goes by the same call, or it keeps the slot busy with its deadline armed.
	if (this->m_pending.serial != 0 && this->m_pending.preload)
		this->releasePending();

	if (!this->m_preload.parked)
		return;

	const int ab = this->m_preload.ab;
	this->m_preload = Preload{};

	// A parked clip is never active, so nothing on screen is lost; the bounds check is for records other code dropped.
	if (ab < OUTPUT_A || ab > OUTPUT_B || ab == this->m_activeAb)
		return;
	if (ThreadedMediaPlayer* player = this->m_players[ab]) {
		player->stop();
		player->setSource(QString());
	}
}

void MascotVideoSlot::stopAll()
{
	// A parked record goes too: a surviving one would let swapToPreloaded() activate a player this call emptied.
	this->m_preload = Preload{};
	this->m_pending = Load{};
	this->m_timer->stop();

	for (ThreadedMediaPlayer* player : this->m_players) {
		if (!player)
			continue;
		player->stop();
		player->setSource(QString());
	}

	this->m_activeAb = -1;
	this->m_activePath.clear();
}

void MascotVideoSlot::setLowLatency(bool lowLatency)
{
	if (this->lowLatency == lowLatency)
		return;
	this->lowLatency = lowLatency;
	// A player with media open keeps its clip: the backend reads the intent when it opens the next.
	for (ThreadedMediaPlayer* player : this->m_players)
		this->applyPlaybackIntent(player);
}

void MascotVideoSlot::applyPlaybackIntent(ThreadedMediaPlayer* player) const
{
	if (!player)
		return;
	player->setPlaybackIntent(this->lowLatency
		? QPlaybackOptions::PlaybackIntent::LowLatencyStreaming
		: QPlaybackOptions::PlaybackIntent::Playback);
}

void MascotVideoSlot::setPaused(bool paused)
{
	if (this->m_paused == paused)
		return;
	this->m_paused = paused;

	if (paused) {
		// A paused player paints no frame, so the deadline would fire on a clip that is only waiting: stop it.
		this->m_timer->stop();
		for (ThreadedMediaPlayer* player : this->m_players) {
			if (player)
				player->pause();
		}
		return;
	}

	// Resume a load that finished while paused, then the active output; in single-player both are the same.
	if (this->m_pending.serial != 0) {
		if (this->m_pending.loaded && !this->m_pending.playing) {
			if (ThreadedMediaPlayer* pendingPlayer = this->m_players[this->m_pending.ab])
				pendingPlayer->play();
			this->m_pending.playing = true;
		}
		// Armed whether or not the load finished: a load running when the pause arrived had its deadline stopped.
		this->m_timer->start();
	}
	if (this->m_activeAb >= 0) {
		if (ThreadedMediaPlayer* activePlayer = this->m_players[this->m_activeAb])
			activePlayer->play();
	}
}

void MascotVideoSlot::retireInactive()
{
	for (int ab = OUTPUT_A; ab <= OUTPUT_B; ++ab) {
		ThreadedMediaPlayer* player = this->m_players[ab];
		if (!player || ab == this->m_activeAb)
			continue;
		// A load in flight is the next clip: retiring it turns a slow swap into a missing clip.
		if (this->m_pending.serial != 0 && this->m_pending.ab == ab)
			continue;
		// A parked preload is the next clip too: retiring it throws away the work the preload exists to have done.
		if (this->m_preload.parked && this->m_preload.ab == ab)
			continue;
		// setSource() stops the player first, so this both ends the clip and frees its decoder.
		player->setSource(QString());
	}
}

void MascotVideoSlot::shutdown()
{
	if (this->m_shutdown)
		return;
	this->m_shutdown = true;

	this->m_timer->stop();
	this->m_pending = Load{};
	this->m_preload = Preload{};
	this->m_activeAb = -1;
	this->m_activePath.clear();

	for (ThreadedMediaPlayer* player : this->m_players) {
		// The synchronous detach has to happen now: the QML item owning the sink dies before this object does.
		// The wrapper's destructor only gets its turn at the end of ours; a second shutdown is a no-op inside it.
		if (player)
			player->shutdown();
	}
}

void MascotVideoSlot::handleFrame(int ab, const QVideoFrame& frame)
{
	// Only the pending output's frames count; the outgoing tail must not become the new clip's start.
	if (this->m_pending.serial == 0 || this->m_pending.ab != ab)
		return;
	// Requiring Loading+LoadedMedia also means the offset is applied, so the frame is the one asked for.
	if (!this->m_pending.sawLoading || !this->m_pending.loaded)
		return;
	if (!frame.isValid())
		return;

	this->m_timer->stop();

	const QString path = this->m_pending.path;
	const qint64 offsetMs = this->m_pending.offsetMs;
	const QSize frameSize = frame.size();

	// A preload's first frame is where parking happens; m_activeAb/m_activePath stay alone, nothing swapped.
	if (this->m_pending.preload) {
		const int parkedAb = this->m_pending.ab;
		this->m_pending = Load{};
		this->m_preload.parked = true;
		this->m_preload.ab = parkedAb;
		this->m_preload.path = path;
		this->m_preload.offsetMs = offsetMs;
		this->m_preload.frameSize = frameSize;
		// Paused, not stopped: the source is kept so the swap is a play(), and the frame in the sink makes it a fade.
		if (ThreadedMediaPlayer* parked = this->m_players[parkedAb])
			parked->pause();
		emit this->preloaded(path, offsetMs, frameSize);
		return;
	}

	// State first, emission last: a handler that swaps again from firstFrame() must not be overwritten.
	this->m_pending = Load{};
	this->m_activeAb = ab;
	this->m_activePath = path;

	emit this->firstFrame(path, ab, frameSize, offsetMs);
}

void MascotVideoSlot::handleMediaStatus(int ab, QMediaPlayer::MediaStatus status)
{
	ThreadedMediaPlayer* player = this->m_players[ab];
	if (!player)
		return;

	if (status == QMediaPlayer::InvalidMedia) {
		// A file that cannot be opened fails whoever holds it: the load in flight, or the clip already on screen.
		// In single-player the player is reloaded in place, so currentSource() names the new path for the old status.
		// A load that has not reported Loading has produced no status of its own; its own failure still arrives after it.
		// A backend that reports an unopenable file without LoadingMedia ends as the timeout, not a failure.
		if (this->m_pending.serial != 0 && this->m_pending.ab == ab
			&& player->currentSource() == this->m_pending.path && !this->m_pending.sawLoading) {
			return;
		}
		this->failPlayer(ab);
		return;
	}

	// Any other status from a player not carrying the pending load may neither start nor fail anything.
	if (this->m_pending.serial == 0 || this->m_pending.ab != ab)
		return;
	// currentSource() follows setSource() on this thread, so it rules out a status about the file carried before.
	if (player->currentSource() != this->m_pending.path)
		return;

	if (status == QMediaPlayer::LoadingMedia) {
		this->m_pending.sawLoading = true;
		return;
	}
	// LoadedMedia without a LoadingMedia before it belongs to the previous source, not this load.
	if (status != QMediaPlayer::LoadedMedia || !this->m_pending.sawLoading || this->m_pending.loaded)
		return;

	this->m_pending.loaded = true;
	// duration() is 0 until known and durationChanged can arrive either side; both that and clip>file read as 0.
	const qint64 offsetMs = this->m_pending.requestedOffsetMs >= 0
		? this->m_pending.requestedOffsetMs
		: this->chooseOffset(player->duration());
	this->m_pending.offsetMs = offsetMs;
	player->setPosition(offsetMs);

	if (this->m_paused) {
		// The offset is applied; setPaused(false) starts this clip, so arming the deadline here would run it out.
		return;
	}
	player->play();
	this->m_pending.playing = true;
	// Re-armed because the load half of the wait is spent; the deadline now covers start -> frame.
	this->m_timer->start();
}

void MascotVideoSlot::handleError(int ab, QMediaPlayer::Error error)
{
	// NoError is QMediaPlayer clearing the previous error on a new source; failing on it kills the next swap.
	if (error == QMediaPlayer::NoError)
		return;

	ThreadedMediaPlayer* player = this->m_players[ab];
	if (!player)
		return;

	// An error names no source, so it needs the same load-generation evidence as the status path.
	// A pre-Loading load has produced no error of its own; failing it here would drop a file that is still opening.
	// Its own failure still arrives, as InvalidMedia or as an error after its Loading.
	if (this->m_pending.serial != 0 && this->m_pending.ab == ab
		&& player->currentSource() == this->m_pending.path && !this->m_pending.sawLoading) {
		return;
	}

	this->failPlayer(ab);
}

void MascotVideoSlot::handleStartTimeout()
{
	this->abortPending(true);
}

bool MascotVideoSlot::ensurePlayer(int ab)
{
	if (this->m_players[ab])
		return true;

	QVideoSink* sink = this->m_sinks[ab].data();
	if (!sink) {
		this->warnNoSinkOnce(ab);
		return false;
	}

	auto* player = new ThreadedMediaPlayer(this);
	// Silence is structural, not a setting: mascots never make noise.
	player->setMuted(true);
	player->setVolume(0.0f);
	// Set before the first setSource(): the backend reads it at open, and the first-frame budget assumes low latency.
	// Low latency trades buffering for a higher chance of dropped frames, so it follows `mascots_low_latency`.
	this->applyPlaybackIntent(player);
	// start() runs once and cannot rebind a sink later; the empty source keeps the player idle.
	player->start(sink, QString(), true);

	// Lambdas, not member slots: the signal carries no output index, and that index is why these are per player.
	QObject::connect(player, &ThreadedMediaPlayer::mediaStatusChanged, this,
		[this, ab](QMediaPlayer::MediaStatus status) { this->handleMediaStatus(ab, status); });
	QObject::connect(player, &ThreadedMediaPlayer::errorOccurred, this,
		[this, ab](QMediaPlayer::Error error, const QString&) { this->handleError(ab, error); });

	this->m_players[ab] = player;
	return true;
}

QString MascotVideoSlot::releasePending()
{
	if (this->m_pending.serial == 0)
		return QString();

	const int ab = this->m_pending.ab;
	const QString path = this->m_pending.path;
	this->m_pending = Load{};
	this->m_timer->stop();

	// In single-player the pending load is the player on screen, so a failed start took the visible clip.
	if (this->m_activeAb == ab) {
		this->m_activeAb = -1;
		this->m_activePath.clear();
	}

	if (ThreadedMediaPlayer* player = this->m_players[ab]) {
		player->stop();
		player->setSource(QString());
	}
	return path;
}

void MascotVideoSlot::abortPending(bool timedOut)
{
	// Released before the emission, so a handler that swaps again from failed()/timedOut() is not undone.
	const QString path = this->releasePending();
	if (path.isEmpty())
		return;

	if (timedOut)
		emit this->timedOut(path);
	else
		emit this->failed(path, FailKind::Decode);
}

void MascotVideoSlot::failPlayer(int ab)
{
	ThreadedMediaPlayer* player = this->m_players[ab];
	if (!player)
		return;

	if (this->m_pending.serial != 0 && this->m_pending.ab == ab
		&& player->currentSource() == this->m_pending.path) {
		this->abortPending(false);
		return;
	}

	// A dying parked preload is a decode failure (the backend will refuse it again) and is released before the emit.
	// Nothing was on screen, so no panel state changes.
	if (this->m_preload.parked && this->m_preload.ab == ab) {
		const QString parkedPath = this->m_preload.path;
		this->m_preload = Preload{};
		player->stop();
		player->setSource(QString());
		emit this->failed(parkedPath, FailKind::Decode);
		return;
	}

	if (ab != this->m_activeAb)
		return;

	const QString path = this->m_activePath;
	this->m_activeAb = -1;
	this->m_activePath.clear();
	// The deadline is not stopped while a pending load on the other output owns it.
	if (this->m_pending.serial == 0)
		this->m_timer->stop();
	player->stop();
	player->setSource(QString());
	emit this->failed(path, FailKind::Decode);
}

qint64 MascotVideoSlot::chooseOffset(qint64 durationMs) const
{
	if (!this->m_randomStart)
		return 0;
	const qint64 span = durationMs - this->m_clipMs;
	if (durationMs <= 0 || span <= 0)
		return 0;
	// [0, span) rather than [0, span]: every offset then leaves more than clipMs of file after it.
	return QRandomGenerator::global()->bounded(span);
}

void MascotVideoSlot::warnNoSinkOnce(int ab)
{
	if (this->m_warnedNoSink)
		return;
	this->m_warnedNoSink = true;
	qWarning() << "MascotVideoSlot: no video sink for output" << ab
		<< "- swapTo ignored; setSinks() has to run before the first swap";
}
