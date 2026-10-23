#pragma once
#include <QObject>
#include <QString>
#include <QMediaPlayer>
#include <functional>
#include <memory>
#include <atomic>

class QThread;
class QVideoSink;
class QAudioOutput;

// Owns a QMediaPlayer/QAudioOutput pair on a dedicated worker thread.
//
// The player objects are created and only ever touched from that worker thread: every
// setter here queues a small lambda onto it and returns immediately, so the GUI thread
// never waits for FFmpeg player construction, media probing, seek/play calls or the
// player's own position timer. Player signals are connected back with a queued
// connection and only update the plain getters below, which are safe to read from the
// owning (GUI) thread.
//
// A QVideoSink stays in the GUI thread and is passed to start(); shutdown() detaches the
// player from it before the sink can be destroyed.
class ThreadedMediaPlayer : public QObject
{
	Q_OBJECT
public:
	explicit ThreadedMediaPlayer(QObject* parent = nullptr);
	~ThreadedMediaPlayer() override;

	// Must be called from the owning thread, before any other command. The sink may be
	// null for audio-only use; otherwise it must outlive shutdown().
	void start(QVideoSink* sink, const QString& source, bool loop = true);
	// Detaches the player from its sink/audio and stops the worker. Idempotent; the
	// destructor calls it too.
	void shutdown();

	void setSource(const QString& path);
	void play();
	void pause();
	void stop();
	void setPosition(qint64 positionMs);
	void setVolume(float volume);
	void setMuted(bool muted);

	QString currentSource() const { return m_source; }
	qint64 position() const { return m_positionMs; }
	qint64 duration() const { return m_durationMs; }
	QMediaPlayer::PlaybackState playbackState() const { return m_playbackState; }
	QMediaPlayer::MediaStatus mediaStatus() const { return m_mediaStatus; }

signals:
	void positionChanged(qint64 position);
	void durationChanged(qint64 duration);
	void mediaStatusChanged(QMediaPlayer::MediaStatus status);
	void playbackStateChanged(QMediaPlayer::PlaybackState state);
	void errorOccurred(QMediaPlayer::Error error, const QString& errorString);

private slots:
	void handlePositionChanged(qint64 position);
	void handleDurationChanged(qint64 duration);
	void handleMediaStatusChanged(QMediaPlayer::MediaStatus status);
	void handlePlaybackStateChanged(QMediaPlayer::PlaybackState state);
	void handleErrorOccurred(QMediaPlayer::Error error, const QString& errorString);

private:
	// The player and audio output live in the worker thread. Holding them in shared state
	// (instead of on the wrapper) lets already-posted commands finish safely even after
	// the wrapper is gone; those commands never touch this object.
	struct WorkerState {
		QMediaPlayer* player = nullptr;
		QAudioOutput* audio = nullptr;
		std::atomic<bool> cancel{ false };
	};

	void post(std::function<void(QMediaPlayer*, QAudioOutput*)> action);

	QThread* m_thread = nullptr;
	QObject* m_context = nullptr;
	std::shared_ptr<WorkerState> m_state = std::make_shared<WorkerState>();
	bool m_started = false;
	bool m_shutdown = false;
	float m_volume = 0.0f;
	bool m_muted = false;
	QString m_source;
	qint64 m_positionMs = 0;
	qint64 m_durationMs = 0;
	QMediaPlayer::PlaybackState m_playbackState = QMediaPlayer::StoppedState;
	QMediaPlayer::MediaStatus m_mediaStatus = QMediaPlayer::NoMedia;
};
