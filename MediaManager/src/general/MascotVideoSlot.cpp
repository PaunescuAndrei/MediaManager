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

	// A parked clip on B is unreachable for the same reason and by the same rule: in this mode
	// every swap reloads A, so the decoder it holds buys nothing. Dropped before the player it was
	// parked on is destroyed below, so no record survives the player it describes.
	if (this->m_preload.parked && this->m_preload.ab != OUTPUT_A)
		this->m_preload = Preload{};

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

void MascotVideoSlot::setTimings(int swapTimeoutMs, int retryIntervalMs, int retryAttempts)
{
	// The clamps live here, at the point of use, so a config file cannot produce a deadline that
	// abandons clips that are merely opening. 500 ms is the floor because a local file that has
	// not painted a frame in half a second is already outside every measured warm load (49-300 ms
	// in the M2 gate); 60 s is the ceiling because a deadline longer than that is no deadline at
	// all and would hold `isBusy()` on a dead file for a minute, freezing the panel with it.
	// There is deliberately no "0 = wait forever": this timer is the only thing that releases a
	// load that neither paints nor errors, and without it the slot stays busy and retireInactive()
	// skips its player forever.
	this->m_swapTimeoutMs = std::clamp(swapTimeoutMs, 500, 60000);
	// 20 ms is under a frame at any refresh rate, so a smaller value would re-ask for a sink that
	// cannot have appeared yet; 2 s is longer than a delegate takes to be created by a wide margin.
	this->m_retryIntervalMs = std::clamp(retryIntervalMs, 20, 2000);
	// 0 attempts is a supported setting -- it means "do not retry, give up at once" -- so the floor
	// is that, not 1. 50 is above any count a human would configure and bounds the churn.
	this->m_retryAttempts = std::clamp(retryAttempts, 0, 50);
	// The running timer, not only the next one: a deadline already armed under the old value has
	// to move with the setting, otherwise a shortened deadline would not apply until the clip
	// after the next one -- the "two consumers of one key" trap setRandomStart() documents.
	if (this->m_timer->isActive())
		this->m_timer->start(this->m_swapTimeoutMs);
	else
		this->m_timer->setInterval(this->m_swapTimeoutMs);
}

void MascotVideoSlot::swapTo(const QString& path, qint64 startOffsetMs)
{
	if (this->m_shutdown || path.isEmpty()) {
		// Nothing can be loaded, and a caller waiting for a first frame that has no player behind
		// it would wait until its own deadline: say so instead.
		// (Task 6: say so as a *wiring* failure -- the file was never attempted, so the owner keeps
		// it in the pool; see FailKind.)
		emit this->failed(path, FailKind::Wiring);
		return;
	}

	// The idle output is the one that is not on screen. Before anything has been shown that is A,
	// so the first clip lands on A and the second on B.
	int ab = this->m_activeAb == OUTPUT_A ? OUTPUT_B : OUTPUT_A;
	if (this->m_singlePlayer)
		ab = OUTPUT_A; // one sink, one player: a swap reloads the output that is on screen

	// A parked clip on the output this load is about to take is spent: setSource() below replaces
	// the media it held, and leaving the record behind would let a later swapToPreloaded() claim
	// the player carries a clip it no longer has. Its state only -- the load below is the thing
	// that changes the player, so emptying it here would post a second, pointless load.
	if (this->m_preload.parked)
		this->m_preload = Preload{};

	if (!this->ensurePlayer(ab)) {
		emit this->failed(path, FailKind::Wiring);
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

void MascotVideoSlot::preload(const QString& path, qint64 startOffsetMs)
{
	if (this->m_shutdown || path.isEmpty()) {
		// A wiring report, not a verdict on the file: nothing was attempted. Reported rather than
		// dropped because the owner is the only object that knows whether it still wants this
		// clip; what it must not do with it is forget the path (see FailKind).
		emit this->failed(path, FailKind::Wiring);
		return;
	}

	// The output the next swap will use, chosen by the same rule swapTo() uses, so the clip and
	// the swap can never disagree about which player is idle.
	int ab = this->m_activeAb == OUTPUT_A ? OUTPUT_B : OUTPUT_A;
	if (this->m_singlePlayer)
		ab = OUTPUT_A;

	// The output to park on has to be one nothing is visible on: reloading the player that is on
	// screen would end the clip the user is watching, which is the one thing a preload must never
	// cause. In the clamped one-player mode that is every case after the first clip, so no preload
	// is available there and the swap keeps its load -- the fallback this class already has.
	if (ab == this->m_activeAb)
		return;

	// One load at a time through the same pending record as a swap, so the preload shares the
	// gate, the deadline and the supersede rules. A swap in flight wins: it is the clip the user
	// is about to see, and the preload can be made again after the next commit.
	if (this->m_pending.serial != 0)
		return;

	if (this->m_preload.parked) {
		// Never twice for the same clip: a second open of the file already parked would throw away
		// a ready clip and pay for it a second time. A different path does replace it, because the
		// parked clip is only useful if it is the clip the next swap will ask for.
		// The *offset* is part of "the same clip", not just the path: a parked clip is a specific
		// moment of a file, so a request for the same file at a different moment must replace it --
		// keeping it would let a caller that asked for `startOffsetMs` be served the moment it did
		// not ask for, and for the mirror pair that is the one thing startMirrorVideo() prevents.
		// A negative `startOffsetMs` means "whatever swapTo() would have chosen": that is not a value
		// this call can compare, and the parked clip may be exactly that choice, so it stands.
		const bool sameMoment = startOffsetMs < 0 || this->m_preload.offsetMs == startOffsetMs;
		if (this->m_preload.path == path && sameMoment)
			return;
		this->discardPreload();
	}

	if (!this->ensurePlayer(ab)) {
		// The delegate that owns the sink does not exist yet. Same wiring report as above: the
		// file has not been shown to be unplayable, so it stays in the pool.
		emit this->failed(path, FailKind::Wiring);
		return;
	}

	this->m_pending.serial = ++this->m_serial;
	this->m_pending.ab = ab;
	this->m_pending.path = path;
	this->m_pending.requestedOffsetMs = startOffsetMs;
	this->m_pending.preload = true;

	ThreadedMediaPlayer* player = this->m_players[ab];
	// The same trap swapTo() documents: an equal URL is a no-op inside QMediaPlayer, so a player
	// that already held this file would never report loading and the gate would time out on a
	// perfectly good clip. Emptying it first forces a real open.
	if (player->currentSource() == path)
		player->setSource(QString());
	player->setSource(path);

	if (!this->m_paused) {
		// The same deadline a swap gets, for the same reason: a preload that neither paints nor
		// errors would otherwise hold this record -- and the output it is parked on -- forever.
		this->m_timer->start();
	}
}

bool MascotVideoSlot::swapToPreloaded()
{
	if (this->m_shutdown || !this->m_preload.parked)
		return false;

	const Preload parked = this->m_preload;
	// The bounds are checked before the index, not beside it: `parked.ab` is -1 in the empty state
	// and any other value would index outside `m_players` -- dormant UB is still UB, and this is the
	// one place the record's output is used as an index before anything has vouched for it.
	if (parked.ab < OUTPUT_A || parked.ab > OUTPUT_B) {
		this->discardPreload();
		return false;
	}
	ThreadedMediaPlayer* player = this->m_players[parked.ab];
	if (!player) {
		// The player the clip was parked on is gone (a single-player switch, a shutdown, a sink
		// that was replaced): nothing can be made of it, and the caller still has swapTo().
		this->discardPreload();
		return false;
	}

	// State first, emission last, exactly as handleFrame() does it: a handler that swaps again
	// from firstFrame() must not be overwritten by the bookkeeping of the swap that just
	// completed. Nothing is "pending" any more either -- this call is the whole load.
	this->m_preload = Preload{};
	this->m_pending = Load{};
	this->m_timer->stop();
	this->m_activeAb = parked.ab;
	this->m_activePath = parked.path;

	// The clip was parked paused, so the swap is this play() plus the owner's opacity writes, and
	// the frame is already in the sink -- which is why the report below happens now rather than
	// one load later. While paused it stays parked-in-place: the owner's fade still runs, and
	// setPaused(false) is what starts it, exactly as for a clip that loaded while paused.
	if (!this->m_paused)
		player->play();

	emit this->firstFrame(parked.path, parked.ab, parked.frameSize, parked.offsetMs);
	return true;
}

void MascotVideoSlot::discardPreload()
{
	// A preload that is still opening is dropped by the same call: it is the same clip and the same
	// decoder, and leaving it in flight would keep the slot busy -- and its deadline armed -- for a
	// panel that has just stopped being able to use it. Released silently, like everything else a
	// preload does when it is dropped: releasePending() emits nothing.
	if (this->m_pending.serial != 0 && this->m_pending.preload)
		this->releasePending();

	if (!this->m_preload.parked)
		return;

	const int ab = this->m_preload.ab;
	this->m_preload = Preload{};

	// A parked clip is never the active one (preload() refuses that output), so its player has
	// nothing on screen to lose: stopping and emptying it frees the decoder the parking held.
	// The bounds check is the same one swapToPreloaded() makes before it indexes: this method runs
	// on records that other code has already decided to drop, so it cannot be the place that trusts
	// `ab`.
	if (ab < OUTPUT_A || ab > OUTPUT_B || ab == this->m_activeAb)
		return;
	if (ThreadedMediaPlayer* player = this->m_players[ab]) {
		player->stop();
		player->setSource(QString());
	}
}

void MascotVideoSlot::stopAll()
{
	// No signal: this is a caller-initiated stop, not a clip's failure.
	// Task 6: a parked preload goes with it -- a stopped slot claims nothing is loaded, and a record
	// that disagreed would let a later swapToPreloaded() activate a player this call emptied.
	this->m_preload = Preload{};
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
		// A parked preload is the next clip too -- finished work rather than an outgoing one -- and
		// it is inactive by construction, so this call would otherwise throw away exactly the load
		// the preload exists to have already done. Emptied only when it is dropped on purpose.
		if (this->m_preload.parked && this->m_preload.ab == ab)
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
	this->m_preload = Preload{};
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

	// A preload's first frame is where the parking happens: the clip is opened, seeked and known to
	// paint, so it is paused on that frame and announced as ready. `m_activeAb`/`m_activePath` are
	// deliberately left alone -- nothing has been swapped, and a swap that never happens must leave
	// the panel exactly as it was.
	if (this->m_pending.preload) {
		const int parkedAb = this->m_pending.ab;
		this->m_pending = Load{};
		this->m_preload.parked = true;
		this->m_preload.ab = parkedAb;
		this->m_preload.path = path;
		this->m_preload.offsetMs = offsetMs;
		this->m_preload.frameSize = frameSize;
		// Paused rather than stopped: a parked clip must cost no CPU and hold no active decode,
		// while keeping its source is what makes the next swap a play() on a player that already
		// has the media open. The frame it is paused on stays in the sink, which is what turns the
		// swap into an opacity change.
		if (ThreadedMediaPlayer* parked = this->m_players[parkedAb])
			parked->pause();
		emit this->preloaded(path, offsetMs, frameSize);
		return;
	}

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
		// Same load-generation evidence handleError() below uses, applied to the same case: in
		// clamped (single-player) mode one player is reloaded in place, so a late InvalidMedia from
		// the previous source arrives with currentSource() already naming this load's path -- and
		// failing that load here would abort a perfectly good file for a status that describes the
		// clip before it. A load that has not reported Loading yet has produced no status of its
		// own, and its own failure still arrives as InvalidMedia after that Loading. A backend that
		// reports an unopenable file without ever emitting LoadingMedia ends as the 5 s timeout
		// instead of a failure -- the same, louder-not-silent trade the error path below makes.
		if (this->m_pending.serial != 0 && this->m_pending.ab == ab
			&& player->currentSource() == this->m_pending.path && !this->m_pending.sawLoading) {
			return;
		}
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

	// A parked preload that dies while it waits is released before the emission, for the reason
	// every other release is: a handler that reacts by asking for another clip must find clean
	// state. The *file* is reported as a decode failure -- the backend refused it, and it will
	// refuse it again, which is the one kind of failure that may cost a path its place in the pool
	// (§7.8). Nothing was on screen, so no panel state changes.
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
	emit this->failed(path, FailKind::Decode);
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
