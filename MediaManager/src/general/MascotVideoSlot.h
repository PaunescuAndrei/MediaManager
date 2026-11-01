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

// GUI thread only: the frame gate needs QQuickVideoSink re-emitting videoFrameChanged on the panel's thread.
// Sinks arrive through setSinks() because the delegate may not exist yet, and must outlive shutdown().
class MascotVideoSlot : public QObject
{
	Q_OBJECT
public:
	// A decode failure is a fact about the media, so the owner may drop the path for the session.
	// A wiring failure (no sink yet, or shut down) is no verdict on the file, so it stays in the rotation.
	enum class FailKind {
		Decode,
		Wiring
	};
	Q_ENUM(FailKind)

	explicit MascotVideoSlot(QObject* parent = nullptr);
	~MascotVideoSlot() override;

	// A started player keeps its sink -- start() binds once -- so a recreated delegate needs a new slot, not a new sink.
	void setSinks(QVideoSink* sinkA, QVideoSink* sinkB);

	// Only output A is used here: the one player is reloaded in place, so nothing can crossfade.
	void setSinglePlayer(bool single);

	// `clipSeconds` is the visible duration: a random start leaves at least that much file after the offset.
	void setRandomStart(bool random, int clipSeconds);

	void setTimings(int swapTimeoutMs, int retryIntervalMs, int retryAttempts);
	int swapTimeoutMs() const { return this->m_swapTimeoutMs; }
	int retryIntervalMs() const { return this->m_retryIntervalMs; }
	int retryAttempts() const { return this->m_retryAttempts; }

	// `startOffsetMs` >= 0 seeks exactly there (the mirror pair); negative = the configured random/zero start.
	// A swap arriving mid-load supersedes the one before it.
	void swapTo(const QString& path, qint64 startOffsetMs);

	// Parks `path` paused on the next swap's output and reports preloaded(), not firstFrame(): no load latency left.
	// Parked means no CPU and no active decode; hiding is the owner's policy -- applyVideoPaused() discards it.
	// An optimisation, never a requirement: one that cannot be made emits nothing; the caller keeps swapTo().
	void preload(const QString& path, qint64 startOffsetMs);

	// Reports firstFrame() inside this call because the frame is already in the sink; false = fall back to swapTo().
	bool swapToPreloaded();

	QString preloadedPath() const { return this->m_preload.parked ? this->m_preload.path : QString(); }
	qint64 preloadedOffsetMs() const { return this->m_preload.offsetMs; }
	bool hasPreload() const { return this->m_preload.parked; }
	bool isSinglePlayer() const { return this->m_singlePlayer; }
	bool preloading() const { return this->m_pending.serial != 0 && this->m_pending.preload; }

	// Drops the parked clip and any preload still opening through one release path; silent.
	void discardPreload();

	// Clears the active record too, and stays silent: a caller-initiated stop is not a clip's failure.
	void stopAll();

	void setPaused(bool paused);
	// A swap re-applies the intent, so a live change lands on the next clip, not the next restart.
	void setLowLatency(bool lowLatency);
	bool lowLatency = true;
	void applyPlaybackIntent(ThreadedMediaPlayer* player) const;
	bool paused() const { return m_paused; }

	int activeAb() const { return m_activeAb; }
	QString activePath() const { return m_activePath; }
	QString pendingPath() const { return m_pending.serial != 0 ? m_pending.path : QString(); }
	bool isBusy() const { return m_pending.serial != 0; }

	void retireInactive();

	// After it, a swapTo() reports failed() rather than resurrect a player whose worker is gone.
	void shutdown();

signals:
	void firstFrame(QString path, int ab, QSize frameSize, qint64 offsetMs);
	void failed(QString path, MascotVideoSlot::FailKind kind);
	// No frame arrived within the deadline; a slow good clip reaches it, so this is no verdict on the file.
	void timedOut(QString path);
	// A clip is parked on the next swap's output; `ab` is absent because nothing became active.
	void preloaded(QString path, qint64 offsetMs, QSize frameSize);

private slots:
	void handleFrame(int ab, const QVideoFrame& frame);
	void handleMediaStatus(int ab, QMediaPlayer::MediaStatus status);
	void handleError(int ab, QMediaPlayer::Error error);
	void handleStartTimeout();

private:
	// `serial` 0 is the only invalid state; it makes a superseded load's late statuses and frames harmless.
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

	// 5000 = `mascots_swap_timeout_ms` (open + first frame); 150/5 = the retry interval/attempts for a no-sink pick.
	int m_swapTimeoutMs = 5000;
	int m_retryIntervalMs = 150;
	int m_retryAttempts = 5;

	QPointer<QVideoSink> m_sinks[2];
	QMetaObject::Connection m_sinkConnections[2];
	ThreadedMediaPlayer* m_players[2] = { nullptr, nullptr };

	Load m_pending;
	Preload m_preload;
	QTimer* m_timer = nullptr;

	int m_activeAb = -1;
	QString m_activePath;

	// Never reused: a reused number would let a superseded load finish the wrong one.
	quint64 m_serial = 0;

	bool m_singlePlayer = false;
	bool m_randomStart = false;
	qint64 m_clipMs = 0;

	bool m_paused = false;
	bool m_shutdown = false;
	bool m_warnedNoSink = false;
	bool m_warnedSinkReplaced = false;
};
