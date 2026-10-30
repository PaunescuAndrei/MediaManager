#include "stdafx.h"
#include "MascotVideoSlot.h"
#include "ThreadedMediaPlayer.h"

#include <QDebug>
#include <QPlaybackOptions>
#include <QRandomGenerator>
#include <QTimer>
#include <QVideoSink>

namespace {
	// The same output indices MascotSurface::slotSink() takes (0 = A, 1 = B), named here so the
	// meaning cannot drift between the surface that hands out the sinks and the players that draw
	// into them.
	constexpr int OUTPUT_A = 0;
	constexpr int OUTPUT_B = 1;
}

MascotVideoSlot::MascotVideoSlot(QObject* parent)
	: QObject(parent)
{
	// One timer for the slot, not one per load: only one load is ever in flight, and a swap that
	// supersedes another re-arms this one.
	this->m_timer = new QTimer(this);
	this->m_timer->setSingleShot(true);
	this->m_timer->setInterval(START_TIMEOUT_MS);
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
	// The owner repeats its bindings after every slot-count change, so the same pointer is the
	// common case and must not stack a second connection on the same sink.
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

	// Connected here rather than in the constructor because the delegate that owns the sink may
	// not exist yet at construction time. The output index is bound into the connection, so a
	// frame is attributed without comparing the sender against a sink that may have been replaced
	// since.
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

	// Switched on mid-flight: a load parked on output B can never be shown now, because every
	// later swap reloads A. Dropped without a signal -- the caller has decided this output is not
	// wanted, which is not a failure of the file.
	if (this->m_pending.serial != 0 && this->m_pending.ab == OUTPUT_B)
		this->releasePending();

	ThreadedMediaPlayer* playerB = this->m_players[OUTPUT_B];
	if (!playerB)
		return;
	// Output B is what is on screen (switched on while the second player was live): it stays until
	// the next swap reloads A, because retiring it would blank the panel.
	if (this->m_activeAb == OUTPUT_B)
		return;

	// Nothing is playing on B and nothing is loading into it, so its decoder is pure cost against
	// the budget the caller is fitting into.
	playerB->shutdown();
	this->m_players[OUTPUT_B] = nullptr;
	delete playerB;
}

void MascotVideoSlot::setRandomStart(bool random, int clipSeconds)
{
	this->m_randomStart = random;
	// A non-positive length is read as "no constraint" rather than as a negative range: a bad
	// config value must not be able to produce an offset past the end of the file.
	this->m_clipMs = clipSeconds > 0 ? qint64(clipSeconds) * 1000 : 0;
}

void MascotVideoSlot::swapTo(const QString& path, qint64 startOffsetMs)
{
	if (this->m_shutdown || path.isEmpty()) {
		// Nothing can be loaded, and a caller waiting for a first frame that has no player behind
		// it would wait until its own deadline: say so instead.
		emit this->failed(path);
		return;
	}

	// The idle output is the one that is not on screen. Before anything has been shown that is A,
	// so the first clip lands on A and the second on B.
	int ab = this->m_activeAb == OUTPUT_A ? OUTPUT_B : OUTPUT_A;
	if (this->m_singlePlayer)
		ab = OUTPUT_A; // one sink, one player: a swap reloads the output that is on screen

	if (!this->ensurePlayer(ab)) {
		emit this->failed(path);
		return;
	}

	if (this->m_pending.serial != 0) {
		// Superseded. A load left on the other output (single-player switched on mid-load) has no
		// future either, so it is stopped here rather than left decoding a clip nobody will see.
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
	// Asking a player for the file it already holds is a no-op inside QMediaPlayer (equal URL, no
	// stream): no loading status, so the gate would wait for a frame a stopped player never
	// paints and report a timeout for a perfectly good file. Emptying it first forces a real load.
	if (player->currentSource() == path)
		player->setSource(QString());
	player->setSource(path);

	if (!this->m_paused) {
		// Armed before LoadedMedia on purpose: a source that neither loads nor errors would
		// otherwise wait forever, which is the case the deadline exists for. handleMediaStatus()
		// re-arms it once the load half of the wait has been spent.
		this->m_timer->start();
	}
}

void MascotVideoSlot::stopAll()
{
	// No signal: this is a caller-initiated stop, not a clip's failure.
	this->m_pending = Load{};
	this->m_timer->stop();

	for (ThreadedMediaPlayer* player : this->m_players) {
		if (!player)
			continue;
		player->stop();
		// Emptied as well, so a stopped slot costs no decoder while the panel waits for the next
		// swap.
		player->setSource(QString());
	}

	this->m_activeAb = -1;
	this->m_activePath.clear();
}

void MascotVideoSlot::setPaused(bool paused)
{
	if (this->m_paused == paused)
		return;
	this->m_paused = paused;

	if (paused) {
		// A paused player paints no frame, so the deadline would fire on a clip that is only
		// waiting: it stops with the playback and is re-armed on resume.
		this->m_timer->stop();
		for (ThreadedMediaPlayer* player : this->m_players) {
			if (player)
				player->pause();
		}
		return;
	}

	// Resume a load that finished while paused, then whatever is on screen. In single-player mode
	// those are the same player, and a second play() on a playing player is a no-op.
	if (this->m_pending.serial != 0) {
		if (this->m_pending.loaded && !this->m_pending.playing) {
			if (ThreadedMediaPlayer* pendingPlayer = this->m_players[this->m_pending.ab])
				pendingPlayer->play();
			this->m_pending.playing = true;
		}
		// Armed whether or not the load has finished: a load that was still running when the pause
		// arrived had had its deadline stopped, and from here the wait is a wait again.
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
		// A load in flight is the next clip, not an outgoing one: retiring it here would turn a
		// swap that is merely slow into a missing clip.
		if (this->m_pending.serial != 0 && this->m_pending.ab == ab)
			continue;
		// setSource() stops the player first, so this one command both ends the clip and frees its
		// decoder; the wrapper keeps the position and duration it reports in step with it.
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
	this->m_activeAb = -1;
	this->m_activePath.clear();

	for (ThreadedMediaPlayer* player : this->m_players) {
		// The synchronous detach from the sink has to happen now: the QML item that owns the sink
		// is destroyed before this object is, and the wrapper's own destructor only gets its turn
		// at the end of ours. A second call is a no-op inside the wrapper as well.
		if (player)
			player->shutdown();
	}
}

void MascotVideoSlot::handleFrame(int ab, const QVideoFrame& frame)
{
	// Only the pending output's frames count. An outgoing or retired player keeps painting onto
	// its own sink, and taking its last frame for the new clip's start is the exact failure this
	// gate exists to prevent -- most of all in single-player mode, where both clips share a sink.
	if (this->m_pending.serial == 0 || this->m_pending.ab != ab)
		return;
	// Before this load reports Loading, any frame on this player still belongs to the outgoing
	// clip. Requiring the load to have reached LoadedMedia also means the offset below is already
	// applied, so the frame reported is the one the caller asked to start at.
	if (!this->m_pending.sawLoading || !this->m_pending.loaded)
		return;
	if (!frame.isValid())
		return;

	this->m_timer->stop();

	const QString path = this->m_pending.path;
	const qint64 offsetMs = this->m_pending.offsetMs;
	const QSize frameSize = frame.size();

	// State first, emission last: a handler that swaps again from firstFrame() must not have its
	// new load overwritten by the bookkeeping of the load that just finished.
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
		// A file that cannot be opened fails whoever holds it: the load in flight, or the clip
		// that was already on screen.
		this->failPlayer(ab);
		return;
	}

	// Any other status from a player that is not carrying the pending load is either an outgoing
	// clip's last words or a retired player's silence; it may neither start nor fail anything.
	if (this->m_pending.serial == 0 || this->m_pending.ab != ab)
		return;
	// currentSource() follows setSource() on this thread, so it rules out a status that still
	// describes the file this player carried before.
	if (player->currentSource() != this->m_pending.path)
		return;

	if (status == QMediaPlayer::LoadingMedia) {
		this->m_pending.sawLoading = true;
		return;
	}
	// LoadedMedia without a LoadingMedia before it belongs to the previous source of this player,
	// not to this load.
	if (status != QMediaPlayer::LoadedMedia || !this->m_pending.sawLoading || this->m_pending.loaded)
		return;

	this->m_pending.loaded = true;
	// duration() is 0 until the backend knows it, and durationChanged may arrive on either side of
	// LoadedMedia: chooseOffset() reads both that and "clip longer than file" as offset 0.
	const qint64 offsetMs = this->m_pending.requestedOffsetMs >= 0
		? this->m_pending.requestedOffsetMs
		: this->chooseOffset(player->duration());
	this->m_pending.offsetMs = offsetMs;
	player->setPosition(offsetMs);

	if (this->m_paused) {
		// The offset is applied; setPaused(false) starts this clip. Arming the deadline here would
		// let a paused player run out the clock on a load that is only waiting.
		return;
	}
	player->play();
	this->m_pending.playing = true;
	// Re-armed because the load half of the wait is spent; the deadline now covers start -> frame.
	this->m_timer->start();
}

void MascotVideoSlot::handleError(int ab, QMediaPlayer::Error error)
{
	// NoError is QMediaPlayer clearing the previous error when a new source is set, not a failure:
	// treating it as one would fail the swap that follows a bad file.
	if (error == QMediaPlayer::NoError)
		return;

	ThreadedMediaPlayer* player = this->m_players[ab];
	if (!player)
		return;

	// An error names no source, so it gets the same load-generation evidence the status path uses.
	// currentSource() alone cannot tell the previous source's error from this load's -- in
	// single-player mode the player is reloaded in place, and currentSource() already names the new
	// path when a late error of the old clip arrives. A load that has not reported Loading yet has
	// therefore produced no error of its own, and failing it here would drop a file that is still
	// opening. Its own failure still arrives, as InvalidMedia or as an error after its Loading.
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
		// The delegate behind the sink does not exist (or is gone). A player without a sink has
		// nowhere to draw and would never produce the frame the gate waits for.
		this->warnNoSinkOnce(ab);
		return false;
	}

	auto* player = new ThreadedMediaPlayer(this);
	// Silence is structural, not a setting: mascots never make noise.
	player->setMuted(true);
	player->setVolume(0.0f);
	// Low-latency intent before the first setSource(): the backend reads it when it opens the
	// media, and the first-frame budget is measured against a low-latency open.
	player->setPlaybackIntent(QPlaybackOptions::PlaybackIntent::LowLatencyStreaming);
	// Bound here because start() runs once and cannot rebind a sink later. The empty source keeps
	// the player idle until a swapTo() names a file.
	player->start(sink, QString(), true);

	// Lambdas, not member slots: the signal does not carry which output reported it, and that
	// index is the whole reason these handlers exist per player.
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

	// In single-player mode the pending load is the player that is on screen, so a failed start
	// has taken the visible clip with it: forget it rather than reporting a clip nothing holds.
	if (this->m_activeAb == ab) {
		this->m_activeAb = -1;
		this->m_activePath.clear();
	}

	if (ThreadedMediaPlayer* player = this->m_players[ab]) {
		// Stopped and emptied: the player stays pooled for the next swap, but no decoder stays
		// open on a file that never started.
		player->stop();
		player->setSource(QString());
	}
	return path;
}

void MascotVideoSlot::abortPending(bool timedOut)
{
	// Released before the emission, so a handler that swaps again from failed()/timedOut() cannot
	// be undone by the release that follows its own call.
	const QString path = this->releasePending();
	if (path.isEmpty())
		return;

	if (timedOut)
		emit this->timedOut(path);
	else
		emit this->failed(path);
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

	if (ab != this->m_activeAb)
		return;

	// The clip that was on screen cannot continue. Released and forgotten before the emission, so
	// a handler that swaps from failed() starts from a clean slot.
	const QString path = this->m_activePath;
	this->m_activeAb = -1;
	this->m_activePath.clear();
	// Only when nothing is waiting: a pending load on the other output owns this deadline, and
	// stopping it here would cancel the only thing that can report that load -- leaving it busy
	// forever, with no timedOut() and retireInactive() skipping its player. With no pending load
	// this is already a no-op, so it is kept for the plain "active clip failed" case only.
	if (this->m_pending.serial == 0)
		this->m_timer->stop();
	player->stop();
	player->setSource(QString());
	emit this->failed(path);
}

qint64 MascotVideoSlot::chooseOffset(qint64 durationMs) const
{
	if (!this->m_randomStart)
		return 0;
	const qint64 span = durationMs - this->m_clipMs;
	// An unknown duration and a clip as long as the file both leave nowhere to move.
	if (durationMs <= 0 || span <= 0)
		return 0;
	// [0, span) rather than [0, span]: every offset then leaves more than clipMs of file after it,
	// and bounded() is never asked for an empty range.
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
