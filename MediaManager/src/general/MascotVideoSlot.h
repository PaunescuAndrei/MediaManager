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

	// Loads `path` into the idle player, seeks it and starts it. `startOffsetMs` >= 0 seeks
	// exactly there -- the mirror pair uses that to start the same file at the same place --
	// while a negative value asks for the random (or zero) start configured above. Nothing is
	// reported until a frame of `path` reaches the sink; firstFrame() then carries the output it
	// landed on, the frame's pixel size and the offset that was actually asked for. A swap
	// arriving mid-load supersedes the one before it.
	void swapTo(const QString& path, qint64 startOffsetMs);

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
	void firstFrame(QString path, int ab, QSize frameSize, qint64 offsetMs);
	// `path` could not be opened, or the player carrying it failed. Nothing of it is retained.
	void failed(QString path);
	// `path` opened but no frame of it arrived within START_TIMEOUT_MS, so it was abandoned.
	void timedOut(QString path);

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

	static constexpr int START_TIMEOUT_MS = 5000;

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
