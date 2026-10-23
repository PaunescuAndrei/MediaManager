#include "stdafx.h"
#include "ThreadedMediaPlayer.h"

#include <QAudioOutput>
#include <QThread>
#include <QUrl>
#include <QVideoSink>
#include <QDebug>
#include <utility>

ThreadedMediaPlayer::ThreadedMediaPlayer(QObject* parent)
	: QObject(parent)
{
}

ThreadedMediaPlayer::~ThreadedMediaPlayer()
{
	this->shutdown();
}

void ThreadedMediaPlayer::start(QVideoSink* sink, const QString& source, bool loop)
{
	if (this->m_started || this->m_shutdown)
		return;
	this->m_started = true;

	// A path set before start() is the initial source; a path passed here wins.
	const QString initialSource = source.isEmpty() ? this->m_source : source;
	this->m_source = initialSource;

	this->m_thread = new QThread();
	this->m_thread->setObjectName(QStringLiteral("ThreadedMediaPlayerWorker"));
	this->m_context = new QObject();
	this->m_context->moveToThread(this->m_thread);
	// The context lives in the worker thread; the thread object itself belongs to the
	// GUI thread, so killing or deleting it while it runs is never correct.
	QObject::connect(this->m_thread, &QThread::finished, this->m_context, &QObject::deleteLater);
	QObject::connect(this->m_thread, &QThread::finished, this->m_thread, &QObject::deleteLater);

	auto state = this->m_state;
	QObject* context = this->m_context;
	const float volume = this->m_volume;
	const bool muted = this->m_muted;

	this->m_thread->start();

	// Creating the player on the worker thread is the whole point: QMediaPlayer's
	// constructor, its FFmpeg playback engine, its 50 ms position timer and setSource()
	// all stay off the GUI thread from here on.
	QMetaObject::invokeMethod(context, [this, state, sink, initialSource, loop, volume, muted]() {
		// The wrapper may have been destroyed before this thread started; in that case it
		// has set cancel and must never see the (possibly destroyed) sink or wrapper.
		if (state->cancel.load(std::memory_order_acquire))
			return;

		state->audio = new QAudioOutput();
		state->audio->setVolume(volume);
		state->audio->setMuted(muted);

		state->player = new QMediaPlayer();
		state->player->setAudioOutput(state->audio);
		if (sink) {
			state->player->setVideoSink(sink);
		}
		else {
			qWarning() << "ThreadedMediaPlayer: started without a video sink";
		}
		if (loop)
			state->player->setLoops(QMediaPlayer::Infinite);

		// Queued, because the player now lives in this thread and the wrapper in the
		// GUI thread. The wrapper slots only update cached state and re-emit.
		QObject::connect(state->player, &QMediaPlayer::positionChanged, this, &ThreadedMediaPlayer::handlePositionChanged, Qt::QueuedConnection);
		QObject::connect(state->player, &QMediaPlayer::durationChanged, this, &ThreadedMediaPlayer::handleDurationChanged, Qt::QueuedConnection);
		QObject::connect(state->player, &QMediaPlayer::mediaStatusChanged, this, &ThreadedMediaPlayer::handleMediaStatusChanged, Qt::QueuedConnection);
		QObject::connect(state->player, &QMediaPlayer::playbackStateChanged, this, &ThreadedMediaPlayer::handlePlaybackStateChanged, Qt::QueuedConnection);
		QObject::connect(state->player, &QMediaPlayer::errorOccurred, this, &ThreadedMediaPlayer::handleErrorOccurred, Qt::QueuedConnection);

		if (!initialSource.isEmpty())
			state->player->setSource(QUrl::fromLocalFile(initialSource));
	}, Qt::QueuedConnection);
}

void ThreadedMediaPlayer::shutdown()
{
	if (this->m_shutdown)
		return;
	this->m_shutdown = true;

	if (!this->m_started || !this->m_thread || !this->m_context)
		return;

	// From here on a late-starting worker must not create a player or touch the sink.
	this->m_state->cancel.store(true, std::memory_order_release);

	QThread* thread = this->m_thread;
	QObject* context = this->m_context;
	auto state = this->m_state;

	if (thread->isRunning()) {
		// Detach synchronously: the QVideoSink (owned by the QML VideoOutput) is about to
		// be destroyed, and QMediaPlayer's own destructor reaches back into it. This is
		// the only blocking call made from the GUI thread, and it is a short one. The
		// player deletion below is left asynchronous because the FFmpeg backend can spend
		// time waiting for its load job.
		QMetaObject::invokeMethod(context, [state]() {
			if (!state->player)
				return;
			state->player->stop();
			state->player->setVideoSink(nullptr);
			state->player->setAudioOutput(nullptr);
		}, Qt::BlockingQueuedConnection);
	}

	// Deletion runs on the worker thread too. If the thread had not started yet, this
	// lambda runs after the (cancel-checked) init and stops it; otherwise it is queued
	// behind the detach above.
	QMetaObject::invokeMethod(context, [state, thread]() {
		delete state->player;
		state->player = nullptr;
		delete state->audio;
		state->audio = nullptr;
		if (thread)
			thread->quit();
	}, Qt::QueuedConnection);
}

void ThreadedMediaPlayer::post(std::function<void(QMediaPlayer*, QAudioOutput*)> action)
{
	if (!this->m_started || this->m_shutdown || !this->m_context)
		return;
	auto state = this->m_state;
	QMetaObject::invokeMethod(this->m_context, [state, action = std::move(action)]() {
		if (state->cancel.load(std::memory_order_acquire))
			return;
		action(state->player, state->audio);
	}, Qt::QueuedConnection);
}

void ThreadedMediaPlayer::setSource(const QString& path)
{
	this->m_source = path;
	this->m_positionMs = 0;
	this->m_durationMs = 0;

	// Before start() the source is picked up by the init lambda; afterwards the command
	// below runs in the worker thread.
	this->post([path](QMediaPlayer* player, QAudioOutput*) {
		if (!player)
			return;
		player->stop();
		player->setPosition(0);
		player->setSource(QUrl::fromLocalFile(path));
	});
}

void ThreadedMediaPlayer::play()
{
	this->post([](QMediaPlayer* player, QAudioOutput*) {
		if (player)
			player->play();
	});
}

void ThreadedMediaPlayer::pause()
{
	this->post([](QMediaPlayer* player, QAudioOutput*) {
		if (player)
			player->pause();
	});
}

void ThreadedMediaPlayer::stop()
{
	this->post([](QMediaPlayer* player, QAudioOutput*) {
		if (player)
			player->stop();
	});
}

void ThreadedMediaPlayer::setPosition(qint64 positionMs)
{
	this->post([positionMs](QMediaPlayer* player, QAudioOutput*) {
		if (player)
			player->setPosition(positionMs);
	});
}

void ThreadedMediaPlayer::setVolume(float volume)
{
	this->m_volume = volume;
	this->post([volume](QMediaPlayer*, QAudioOutput* audio) {
		if (audio)
			audio->setVolume(volume);
	});
}

void ThreadedMediaPlayer::setMuted(bool muted)
{
	this->m_muted = muted;
	this->post([muted](QMediaPlayer*, QAudioOutput* audio) {
		if (audio)
			audio->setMuted(muted);
	});
}

void ThreadedMediaPlayer::handlePositionChanged(qint64 position)
{
	this->m_positionMs = position;
	emit this->positionChanged(position);
}

void ThreadedMediaPlayer::handleDurationChanged(qint64 duration)
{
	this->m_durationMs = duration;
	emit this->durationChanged(duration);
}

void ThreadedMediaPlayer::handleMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
	this->m_mediaStatus = status;
	emit this->mediaStatusChanged(status);
}

void ThreadedMediaPlayer::handlePlaybackStateChanged(QMediaPlayer::PlaybackState state)
{
	this->m_playbackState = state;
	emit this->playbackStateChanged(state);
}

void ThreadedMediaPlayer::handleErrorOccurred(QMediaPlayer::Error error, const QString& errorString)
{
	emit this->errorOccurred(error, errorString);
}
