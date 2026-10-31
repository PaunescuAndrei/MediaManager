#pragma once
#include <QMetaObject>
#include <QMediaPlayer>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QVideoFrame>

class QTimer;
class QVideoSink;
class ThreadedMediaPlayer;

// The video side of one mascot slot: two players that take turns across a crossfade, a gate that
// only reports a clip once a frame of it has actually reached the sink, and a deadline for a clip
// that never gets there.
//
// GUI thread only. Construct it, call it and destroy it from the thread the panel it decorates
// lives in: the players are built here, the deadline timer runs here, and the frame gate depends
// on QQuickVideoSink re-emitting videoFrameChanged on this thread. Nothing here touches a
// QMediaPlayer directly -- source, seeks and playback all travel through ThreadedMediaPlayer's
// worker -- so no call on this object waits on FFmpeg, media probing or a decoder.
//
// The sinks belong to the QML VideoOutput items and arrive through setSinks(), not the
// constructor, because at the time a slot's owner is created the delegate that owns them may not
// exist yet. They must outlive shutdown(), so the owner destroys this object before the surface
// that owns them.
class MascotVideoSlot : public QObject
{
	Q_OBJECT
public:
	// Why a load ended without a clip, where that changes what the owner may conclude about the
	// *file*. A decode failure is a fact about the media -- the backend refused it and will refuse
	// it again -- so the owner is right to drop that path for the session (§7.8). A wiring failure
	// is a fact about the player or the panel: there was no sink to draw into yet, or the slot had
	// already been shut down. The file a wiring failure names has not been shown to be unplayable,
	// so dropping it would take a good file out of the rotation for the rest of the session.
	// A timeout is neither and keeps its own signal (timedOut() below): the deadline is a deadline,
	// and a transiently slow good file reaches it.
	enum class FailKind {
		Decode,
		Wiring
	};
	Q_ENUM(FailKind)

	explicit MascotVideoSlot(QObject* parent = nullptr);
	~MascotVideoSlot() override;

	// Idempotent and safe to repeat with the same pointers, because the owner re-applies its
	// bindings after every slot-count change. A *different* pointer is wired up for the next
	// player, but a player that has already started keeps the sink it was handed -- start() binds
	// a sink once and cannot rebind it -- so a delegate recreated under a live slot needs a new
	// slot, not a new sink on an old player. That case warns once instead of drawing into a dead
	// sink silently.
	void setSinks(QVideoSink* sinkA, QVideoSink* sinkB);

	// Decoder budget, not a rendering choice: with one player every swap reloads the output that
	// is on screen, so the outgoing clip's last frame stays visible until the new clip's first
	// frame arrives and there is nothing to crossfade. Only output A is ever used.
	void setSinglePlayer(bool single);

	// `clipSeconds` is how long the owner keeps a clip visible; a random start leaves at least
	// that much of the file after the offset so the clip does not end early. With `random` false
	// every clip starts at 0.
	void setRandomStart(bool random, int clipSeconds);

	// The waits that gate a user-visible wait, in one setter because they are one policy: the open
	// + first-frame deadline, the interval a retried pick/load is re-asked at, and how many times
	// it is re-asked before the panel gives up on it. They are config values, not constants,
	// because "how long before we call this clip dead" is a machine-speed judgement the user is
	// better placed to make than the build is. Already stored values are replaced, so a live
	// change reaches a slot that already exists -- the same trap `setRandomStart` exists for.
	// Each value is clamped at its point of use (see the setters' clamps below), and 0 attempts
	// means "do not retry at all" rather than "retry forever".
	void setTimings(int swapTimeoutMs, int retryIntervalMs, int retryAttempts);
	int swapTimeoutMs() const { return this->m_swapTimeoutMs; }
	int retryIntervalMs() const { return this->m_retryIntervalMs; }
	int retryAttempts() const { return this->m_retryAttempts; }

	// Loads `path` into the idle player, seeks it and starts it. `startOffsetMs` >= 0 seeks
	// exactly there -- the mirror pair uses that to start the same file at the same place --
	// while a negative value asks for the random (or zero) start configured above. Nothing is
	// reported until a frame of `path` reaches the sink; firstFrame() then carries the output it
	// landed on, the frame's pixel size and the offset that was actually asked for. A swap
	// arriving mid-load supersedes the one before it.
	void swapTo(const QString& path, qint64 startOffsetMs);

	// Opens `path` on the output the *next* swap will use and parks it there: seeked, played until
	// a frame of it is known, then paused on that frame. It does not become the active output, it
	// is not faded in, and it reports preloaded() rather than firstFrame(), so nothing of it is on
	// screen until a swapToPreloaded() makes it the clip. That is the point of the call: a clip
	// chosen and opened before it is due has no load latency left to hide, so the swap is a
	// play() plus the owner's fade instead of a load.
	//
	// `startOffsetMs` >= 0 seeks exactly there -- the mirror pair parks the same file at the same
	// place with that -- while a negative value asks for the random (or zero) start swapTo() would
	// have chosen. Parked means paused, so the clip costs no CPU and holds no active decode.
	// (Task 6 fix: "a panel that is hidden keeps it paused" was the first draft of that sentence and
	// is not what the code does. The parked clip *is* paused while the panel is hidden, but the
	// owner does not leave it there: MascotController::applyVideoPaused() calls discardPreload() on
	// every pause and arms a fresh preload on resume, so a hidden panel holds no parked decoder at
	// all. Deciding that is the owner's job -- hiding a panel is a policy, not a fact this class
	// can see -- and this class obeys by releasing the record.)
	//
	// A preload is an optimisation, never a requirement, so nothing here is a promise: a preload
	// that cannot be made (no sink yet, the output that is on screen, a load already in flight)
	// emits nothing, and one that fails reports through failed() with a kind whose comment explains
	// that it may not cost the file its place in the pool, or through timedOut(), which has no kind
	// because a deadline is never a verdict on the file. The caller keeps swapTo() to fall back on
	// in every case.
	void preload(const QString& path, qint64 startOffsetMs);

	// Makes the parked clip the one on screen: sets it active, plays it and reports it through
	// firstFrame() -- inside this call, because its frame is already in the sink, so the owner's
	// fade and the swap land in the same turn. False when nothing is parked or the player it was
	// parked on is gone, in which case the caller falls back to swapTo().
	bool swapToPreloaded();

	// The parked clip: empty and 0 while nothing is parked.
	QString preloadedPath() const { return this->m_preload.parked ? this->m_preload.path : QString(); }
	qint64 preloadedOffsetMs() const { return this->m_preload.offsetMs; }
	bool hasPreload() const { return this->m_preload.parked; }
	// True while what is in flight is a preload rather than a swap. The owner asks this to tell
	// "a clip is opening, do not supersede it" from "the next clip is being made ready, which a
	// swap that is due may simply take over".
	bool preloading() const { return this->m_pending.serial != 0 && this->m_pending.preload; }

	// Drops the parked clip and empties the player it was parked on, so its decoder is freed
	// rather than held for a clip that will not be used. Silent, like every preload outcome:
	// nothing was on screen and nothing was reported.
	// (Task 6: a preload that is still *opening* is dropped here too. It is the same clip and the
	// same decoder, and one release path covers both states -- which is what lets a caller drop a
	// preload without first asking which of the two it is in.)
	void discardPreload();

	// Cancels a pending load and stops both players, dropping their sources so no decoder stays
	// open on a panel that is no longer showing video. The active record is cleared too: after
	// this call nothing claims to be playing, so a caller cannot retire a clip that is already
	// gone.
	void stopAll();

	// Pauses both players; `false` resumes only the active output, plus a load that finished
	// while paused and has been waiting for this call to start. A swap requested while paused
	// still loads -- it just does not play, and its deadline starts on resume instead of running
	// down behind a player that is not painting.
	void setPaused(bool paused);
	// (M5: `mascots_low_latency`. The intent a player is created with, and the one the next media open
	// uses; a swap re-applies it, so a live change lands on the next clip rather than the next restart.)
	void setLowLatency(bool lowLatency);
	// (M5: see setLowLatency().)
	bool lowLatency = true;
	void applyPlaybackIntent(ThreadedMediaPlayer* player) const;
	bool paused() const { return m_paused; }

	// Output index (0 = A, 1 = B) and path of the clip last reported through firstFrame(); -1 and
	// empty while nothing has been shown yet.
	int activeAb() const { return m_activeAb; }
	QString activePath() const { return m_activePath; }
	// The path whose first frame is still outstanding, empty when nothing is in flight.
	QString pendingPath() const { return m_pending.serial != 0 ? m_pending.path : QString(); }
	// True while a load is in flight. The mirror pair decides from this whether to copy the other
	// panel's media now or from its firstFrame(), so it must answer for exactly the window
	// between swapTo() and the frame that satisfies it.
	bool isBusy() const { return m_pending.serial != 0; }

	// Stops and unloads the output that is not active, leaving the one on screen untouched. This
	// is what closes a crossfade: the outgoing player is worth its decoder only until the fade
	// ends. A load in flight is never retired, so calling this early cannot turn a swap that is
	// merely slow into a missing clip.
	void retireInactive();

	// Detaches and stops both players while the sinks they draw into are still alive.
	// Idempotent and called from the destructor; after it a swapTo() reports failed() instead of
	// resurrecting a player whose worker is gone.
	void shutdown();

signals:
	// One clip's first frame is on screen: where, how big, and the offset that was asked for.
	// (Task 6: a swap served from a parked preload reports through this same signal, in the call
	// that made it the active clip, because its frame was already in the sink.)
	void firstFrame(QString path, int ab, QSize frameSize, qint64 offsetMs);
	// `path` could not be opened, or the player carrying it failed. Nothing of it is retained.
	// `kind` is what the owner needs to decide whether the *file* has been shown to be unusable:
	// see FailKind's comment -- only a decode failure may cost it its place in the pool.
	void failed(QString path, MascotVideoSlot::FailKind kind);
	// `path` opened but no frame of it arrived within START_TIMEOUT_MS, so it was abandoned.
	// (Task 6: START_TIMEOUT_MS is `mascots_swap_timeout_ms` now -- the sentence above names the
	// constant the build shipped before the deadline became configurable.)
	// The deadline is a deadline rather than a verdict on the file: a transiently slow good clip
	// reaches it, so the owner is told *what* happened and keeps the file.
	void timedOut(QString path);
	// A clip is opened, seeked and paused on its first frame on the output the next swap will use.
	// Nothing of it is on screen: this is the announcement a parked clip makes instead of
	// firstFrame(). `ab` is not passed because nothing became active -- which output was parked is
	// the slot's business until swapToPreloaded() reports the clip through firstFrame().
	void preloaded(QString path, qint64 offsetMs, QSize frameSize);

private slots:
	// The gate. A frame counts only for the load whose output is `ab` and only after that load
	// has reported loading, which is what keeps the outgoing clip's tail from being taken for the
	// new clip's start -- the overlap single-player mode creates by sharing one sink.
	void handleFrame(int ab, const QVideoFrame& frame);
	void handleMediaStatus(int ab, QMediaPlayer::MediaStatus status);
	void handleError(int ab, QMediaPlayer::Error error);
	void handleStartTimeout();

private:
	// One load. `serial` of 0 is the only invalid state and is what makes a superseded load's
	// late status changes and frames harmless.
	struct Load {
		quint64 serial = 0;
		int ab = -1;
		QString path;
		// What the caller asked for, so a retried swap keeps the mirror pair's exact offset.
		qint64 requestedOffsetMs = -1;
		// The offset finally used; reported by firstFrame().
		qint64 offsetMs = 0;
		// Set from the first LoadingMedia, which is the proof that the status changes after it
		// describe this load and not the previous source of the same player.
		bool sawLoading = false;
		// LoadedMedia reached: the offset has been applied. Frames before this are not this
		// clip's first frame.
		bool loaded = false;
		bool playing = false;
		// This load is a preload rather than a swap: its first frame parks the clip instead of
		// making it the clip on screen. Everything before that point is the same work, which is
		// why the two share one pending record, one gate and one deadline.
		bool preload = false;
	};

	// One parked clip: opened, seeked, played until a frame of it was known, then paused on that
	// frame on the output the next swap will use. `parked` false is the only empty state.
	struct Preload {
		bool parked = false;
		int ab = -1;
		QString path;
		qint64 offsetMs = 0;
		// The size of the frame it is parked on, kept because the swap reports it without waiting
		// for another frame: nothing paints while it is paused.
		QSize frameSize;
	};

	// Subscribes one output to its sink, or unsubscribes the one it had. Separate from setSinks()
	// so both outputs go through the same first-supplied-then-replaced logic.
	void setSink(int ab, QVideoSink* sink);
	// Creates the player for one output on first use. False when the sink for it is not there
	// yet, which the caller reports as a failure rather than waiting forever.
	bool ensurePlayer(int ab);
	// Clears the pending load and stops and empties its player. Returns the path that was
	// pending (empty when there was none) so callers can report it *after* the state is clean:
	// a handler that swaps again from failed()/timedOut() must not be undone by this release.
	QString releasePending();
	void abortPending(bool timedOut);
	// Fails whatever `ab` holds: the pending load if the player is carrying one, otherwise the
	// clip that is on screen on it.
	void failPlayer(int ab);
	// Random start offset for a file of `durationMs`, or 0 when there is nowhere to move.
	qint64 chooseOffset(qint64 durationMs) const;
	void warnNoSinkOnce(int ab);

	// The deadlines and the retry budget, from setTimings(). Defaults are the values Task 2 and
	// Task 4 shipped as constants, so a slot nothing has configured yet behaves exactly as it did;
	// they are values rather than constants because how long a clip may take to open is a
	// machine-speed judgement, and the user is the one who knows their machine (§7.11).
	// 5000 = `mascots_swap_timeout_ms`: open + first-frame deadline. Not 0-able: see setTimings().
	int m_swapTimeoutMs = 5000;
	// 150 = `mascots_retry_interval_ms`, and 5 = `mascots_retry_attempts`: how often, and how many
	// times, the owner re-asks for a sink that has not been created yet. Kept here rather than in
	// the owner so both figures move together and one config value cannot desync the pair.
	int m_retryIntervalMs = 150;
	int m_retryAttempts = 5;

	// QPointer because these point into the QML item, not at anything this object owns: a
	// delegate destroyed by a slot-count change leaves null behind instead of dangling.
	QPointer<QVideoSink> m_sinks[2];
	// Kept so a replaced sink can be unsubscribed; connecting again on every setSinks() (the
	// owner repeats them) would otherwise stack duplicate frame deliveries.
	QMetaObject::Connection m_sinkConnections[2];
	// Children of this object, created on first swapTo() for their output. Two of them are the
	// whole point of the class: one keeps playing while the other loads, so the panel never
	// blanks between clips.
	ThreadedMediaPlayer* m_players[2] = { nullptr, nullptr };

	Load m_pending;
	// The next clip, already opened and paused on its frame. Separate from m_pending because the
	// two are different things: a pending load is work in progress that a swap may supersede,
	// while a parked clip is a finished piece of work waiting to be used -- which is exactly why
	// retiredInactive() has to leave it alone.
	Preload m_preload;
	// One deadline for the one load in flight; a swap that supersedes another re-arms it instead
	// of owning a timer of its own.
	QTimer* m_timer = nullptr;

	int m_activeAb = -1;
	QString m_activePath;

	// Monotonic, never reused: a stale signal can only be attributed to the load that is current
	// if its serial still matches, and a reused number would let a superseded load finish the
	// wrong one.
	quint64 m_serial = 0;

	bool m_singlePlayer = false;
	bool m_randomStart = false;
	// Milliseconds of clip the owner needs after the offset. 0 means no constraint.
	qint64 m_clipMs = 0;

	bool m_paused = false;
	bool m_shutdown = false;
	// The two warnings below are wiring mistakes, not per-clip events; each is said once so a
	// panel that is still being configured cannot flood the log.
	bool m_warnedNoSink = false;
	bool m_warnedSinkReplaced = false;
};
