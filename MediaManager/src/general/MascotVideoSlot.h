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

// GUI thread only: the frame gate depends on QQuickVideoSink re-emitting videoFrameChanged on
// the thread the panel lives in. The sinks arrive through setSinks(), not the constructor,
// because the delegate that owns them may not exist yet; they must outlive shutdown(), so the
// owner destroys this object first.
class MascotVideoSlot : public QObject
{
	Q_OBJECT
public:
	// Why a load ended without a clip. A decode failure is a fact about the media, so the owner may
	// drop that path for the session; a wiring failure (no sink yet, or already shut down) has not
	// shown the file unplayable, so dropping it would take a good file out of the rotation. A
	// timeout is neither and keeps its own signal (timedOut()).
	enum class FailKind {
		Decode,
		Wiring
	};
	Q_ENUM(FailKind)

	explicit MascotVideoSlot(QObject* parent = nullptr);
	~MascotVideoSlot() override;

	// Idempotent: the owner re-applies its bindings after every slot-count change. A started player
	// keeps its sink -- start() binds once and cannot rebind -- so a delegate recreated under a live
	// slot needs a new slot; that case warns once rather than drawing into a dead sink.
	void setSinks(QVideoSink* sinkA, QVideoSink* sinkB);

	// Decoder budget, not a rendering choice: the one player is reloaded in place, so there is
	// nothing to crossfade and only output A is ever used.
	void setSinglePlayer(bool single);

	// `clipSeconds` is how long the owner keeps a clip visible; a random start leaves at least
	// that much of the file after the offset.
	void setRandomStart(bool random, int clipSeconds);

	void setTimings(int swapTimeoutMs, int retryIntervalMs, int retryAttempts);
	int swapTimeoutMs() const { return this->m_swapTimeoutMs; }
	int retryIntervalMs() const { return this->m_retryIntervalMs; }
	int retryAttempts() const { return this->m_retryAttempts; }

	// `startOffsetMs` >= 0 seeks exactly there (the mirror pair starts the same file at the same
	// place); a negative value asks for the configured random or zero start. A swap arriving
	// mid-load supersedes the one before it.
	void swapTo(const QString& path, qint64 startOffsetMs);

	// Parks `path` paused on its first frame on the output the next swap will use: it does not
	// become active and reports preloaded(), so a clip opened before it is due has no load latency
	// left to hide. `startOffsetMs` as in swapTo(). Parked means paused: no CPU, no active decode;
	// hiding a panel is the owner's policy -- applyVideoPaused() discards the preload on pause. A
	// preload is an optimisation, never a requirement: one that cannot be made emits nothing, one
	// that fails reports failed() or timedOut(), and the caller keeps swapTo() to fall back on.
	void preload(const QString& path, qint64 startOffsetMs);

	// Makes the parked clip the one on screen and reports firstFrame() inside this call, because its
	// frame is already in the sink. False when nothing is parked or that player is gone.
	bool swapToPreloaded();

	QString preloadedPath() const { return this->m_preload.parked ? this->m_preload.path : QString(); }
	qint64 preloadedOffsetMs() const { return this->m_preload.offsetMs; }
	bool hasPreload() const { return this->m_preload.parked; }
	bool isSinglePlayer() const { return this->m_singlePlayer; }
	bool preloading() const { return this->m_pending.serial != 0 && this->m_pending.preload; }

	// Drops the parked clip, and a preload still opening, through one release path, so its decoder is
	// freed and a caller need not ask which state it is in. Silent, like every preload outcome.
	void discardPreload();

	// Cancels a pending load and stops both players, dropping their sources so no decoder stays open,
	// and clears the active record. Silent: a caller-initiated stop is not a clip's failure.
	void stopAll();

	void setPaused(bool paused);
	// A swap re-applies the intent, so a live change lands on the next clip, not the next restart.
	void setLowLatency(bool lowLatency);
	bool lowLatency = true;
	void applyPlaybackIntent(ThreadedMediaPlayer* player) const;
	bool paused() const { return m_paused; }

	// Output index: 0 = A, 1 = B; -1 and empty until a frame has been reported.
	int activeAb() const { return m_activeAb; }
	QString activePath() const { return m_activePath; }
	QString pendingPath() const { return m_pending.serial != 0 ? m_pending.path : QString(); }
	// True for exactly the window between swapTo() and the frame that satisfies it.
	bool isBusy() const { return m_pending.serial != 0; }

	void retireInactive();

	// After it, a swapTo() reports failed() instead of resurrecting a player whose worker is gone.
	void shutdown();

signals:
	void firstFrame(QString path, int ab, QSize frameSize, qint64 offsetMs);
	void failed(QString path, MascotVideoSlot::FailKind kind);
	// `path` opened but no frame of it arrived within the deadline, so it was abandoned. A
	// transiently slow good clip reaches it, so the owner is told what happened and keeps the file.
	void timedOut(QString path);
	// A clip is parked on the output the next swap will use; `ab` is absent because nothing became
	// active and nothing of it is on screen.
	void preloaded(QString path, qint64 offsetMs, QSize frameSize);

private slots:
	void handleFrame(int ab, const QVideoFrame& frame);
	void handleMediaStatus(int ab, QMediaPlayer::MediaStatus status);
	void handleError(int ab, QMediaPlayer::Error error);
	void handleStartTimeout();

private:
	// One load. `serial` 0 is the only invalid state, and it is what makes a superseded load's late
	// status changes and frames harmless.
	struct Load {
		quint64 serial = 0;
		int ab = -1;
		QString path;
		qint64 requestedOffsetMs = -1;
		qint64 offsetMs = 0;
		bool sawLoading = false;
		bool loaded = false;
		bool playing = false;
		bool preload = false;
	};

	struct Preload {
		bool parked = false;
		int ab = -1;
		QString path;
		qint64 offsetMs = 0;
		QSize frameSize;
	};

	void setSink(int ab, QVideoSink* sink);
	bool ensurePlayer(int ab);
	QString releasePending();
	void abortPending(bool timedOut);
	void failPlayer(int ab);
	qint64 chooseOffset(qint64 durationMs) const;
	void warnNoSinkOnce(int ab);

	// 5000 = `mascots_swap_timeout_ms`: open + first-frame deadline.
	int m_swapTimeoutMs = 5000;
	// 150 / 5 = `mascots_retry_interval_ms` / `mascots_retry_attempts`: how often, and how many
	// times, the owner re-asks a pick or load that found no sink yet.
	int m_retryIntervalMs = 150;
	int m_retryAttempts = 5;

	// QPointer because they point into the QML item: a delegate destroyed by a slot-count change
	// leaves null, not a dangling pointer.
	QPointer<QVideoSink> m_sinks[2];
	QMetaObject::Connection m_sinkConnections[2];
	ThreadedMediaPlayer* m_players[2] = { nullptr, nullptr };

	Load m_pending;
	Preload m_preload;
	QTimer* m_timer = nullptr;

	int m_activeAb = -1;
	QString m_activePath;

	// Monotonic, never reused: a stale signal is only attributable to the current load if its
	// serial still matches, and a reused number would let a superseded load finish the wrong one.
	quint64 m_serial = 0;

	bool m_singlePlayer = false;
	bool m_randomStart = false;
	qint64 m_clipMs = 0;

	bool m_paused = false;
	bool m_shutdown = false;
	bool m_warnedNoSink = false;
	bool m_warnedSinkReplaced = false;
};
