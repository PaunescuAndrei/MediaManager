#include "stdafx.h"
#include "VideoPreviewWidget.h"
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QFileInfo>
#include <QRandomGenerator>
#include "OverlayGraphicsVideoWidget.h"
#include "ThreadedMediaPlayer.h"
#include "utils.h"

VideoPreviewWidget::VideoPreviewWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    this->videoWidget = new OverlayGraphicsVideoWidget(this);
    this->videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(this->videoWidget);

    this->startTimer.setSingleShot(true);
    QObject::connect(&this->startTimer, &QTimer::timeout, this, &VideoPreviewWidget::startPlayback);
}

VideoPreviewWidget::~VideoPreviewWidget()
{
    // The QQuickWidget and its QVideoSink are children, so they are destroyed after this
    // destructor body. Detach the worker-thread player from the sink first: deleting a
    // QMediaPlayer or a QVideoSink touches the other one, and neither may see a dead peer.
    if (this->player) {
        this->player->shutdown();
    }
}

void VideoPreviewWidget::setSource(const QString& path)
{
    this->sourcePath = path;
    this->firstFrameSeen = false;
    if (this->player && this->player->currentSource() != path) {
        // The wrapper keeps the same stop -> rewind -> setSource ordering on its worker
        // thread that the player used to get directly here.
        this->player->setSource(path);
    }
    this->lastPosition = 0;
    this->initialPosition = -1;
}

void VideoPreviewWidget::setStartDelay(int ms)
{
    this->startDelayMs = std::max(0, ms);
}

void VideoPreviewWidget::setVolume(int percent)
{
    this->volumePercent = std::clamp(percent, 0, 100);
    this->applyVolume();
}

void VideoPreviewWidget::setRandomStartEnabled(bool enabled)
{
    this->randomStart = enabled;
}

void VideoPreviewWidget::setRandomOnHoverEnabled(bool enabled)
{
    this->randomEachHover = enabled;
    if (enabled) {
        this->lastPosition = 0;
        this->initialPosition = -1;
    }
}

void VideoPreviewWidget::setMuted(bool muted)
{
    this->muted = muted;
    this->applyVolume();
}

void VideoPreviewWidget::setRememberPositionEnabled(bool enabled)
{
    this->rememberPosition = enabled;
    if (!enabled) {
        this->lastPosition = 0;
        this->initialPosition = -1;
    }
}

void VideoPreviewWidget::setSeededRandom(bool enabled, const QString& seedValue)
{
    this->seededRandom = enabled;
    this->seedString = seedValue;
    this->hoverRandomCounter = 0;
}

void VideoPreviewWidget::setOverlayStyle(double scaleMultiplier, int padX, int padY, int margin)
{
    this->overlayScaleMultiplier = scaleMultiplier;
    this->overlayPadX = padX;
    this->overlayPadY = padY;
    this->overlayMargin = margin;
    if (this->videoWidget) {
        this->videoWidget->setOverlayStyle(scaleMultiplier, padX, padY, margin);
    }
}

bool VideoPreviewWidget::isPlaying() const
{
    return this->player && this->player->playbackState() == QMediaPlayer::PlayingState;
}

qint64 VideoPreviewWidget::computeTargetPosition(qint64 duration) const
{
    auto clampPos = [](qint64 pos, qint64 dur) {
        if (dur <= 0)
            return pos;
        qint64 maxPos = std::max<qint64>(0, dur - 200);
        return std::clamp(pos, static_cast<qint64>(0), maxPos);
    };

    bool useRemember = this->rememberPosition && !this->randomEachHover;
    if (useRemember) {
        if (this->lastPosition > 0)
            return clampPos(this->lastPosition, duration);
        if (this->initialPosition >= 0)
            return clampPos(this->initialPosition, duration);
    }
    if (this->randomEachHover) {
        if (this->randomStart && duration > 0) {
            return clampPos(this->pickRandomPosition(duration), duration);
        }
        return 0;
    }
    if (this->randomStart && duration > 0) {
        return clampPos(this->pickRandomPosition(duration), duration);
    }
    if (useRemember && this->lastPosition == 0 && this->initialPosition == 0)
        return 0;
    return 0;
}

qint64 VideoPreviewWidget::pickRandomPosition(qint64 duration, quint64 nonce) const
{
    if (duration <= 0)
        return 0;
    qint64 maxStart = duration;
    quint32 seed = 0;
    if (this->seededRandom) {
        seed = qHash(this->seedString + this->sourcePath + QString::number(nonce));
        QRandomGenerator rng(seed);
        return rng.bounded(maxStart);
    }
    return QRandomGenerator::global()->bounded(maxStart);
}

void VideoPreviewWidget::startAtPosition(qint64 target, bool pauseAfterSeek)
{
    if (!this->player)
        return;

    this->preloadActive = false;
    this->awaitingLoad = false;
    this->initialPosition = target;
    if (this->pauseConn)
        QObject::disconnect(this->pauseConn);

    if (pauseAfterSeek) {
        auto pausedOnce = std::make_shared<bool>(false);
        auto pauseCheck = [this, pausedOnce](qint64 pos) {
            if (*pausedOnce || !this->player)
                return;
            if (this->initialPosition < 0)
                return;
            if (pos < this->initialPosition)
                return;
            if (!this->randomStart && this->initialPosition == 0 && pos <= 1)
                return; // wait for real progress for non-random starts
            *pausedOnce = true;
            if (this->rememberPosition) {
                this->lastPosition = this->player->position();
            }
            this->player->pause();
            QObject::disconnect(this->pauseConn);
        };
        this->pauseConn = QObject::connect(this->player, &ThreadedMediaPlayer::positionChanged, this, pauseCheck);
        if (this->initialPosition > 0 || this->randomStart) {
            pauseCheck(this->player->position());
        }
    }

    this->player->setPosition(target);
    if (pauseAfterSeek && this->player->playbackState() == QMediaPlayer::PlayingState) {
        if (this->rememberPosition) {
            this->lastPosition = target;
        }
        this->player->pause();
    } else {
        this->player->play();
        if (pauseAfterSeek && this->initialPosition > 0) {
            this->player->pause();
        }
    }
}

void VideoPreviewWidget::jumpToRandomPosition()
{
    this->ensurePlayer();
    if (!this->player)
        return;
    qint64 duration = this->player->duration();
    if (duration <= 0)
        return;
    qint64 pos = this->pickRandomPosition(duration, ++this->hoverRandomCounter);
    this->initialPosition = pos;
    if (this->rememberPosition && !this->randomEachHover) {
        this->lastPosition = pos;
    } else if (this->randomEachHover) {
        this->lastPosition = 0;
    }
    this->player->setPosition(pos);
    if (!this->isPlaying()) {
        this->player->play();
    }
}

void VideoPreviewWidget::seekBySeconds(double seconds)
{
    this->ensurePlayer();
    if (!this->player)
        return;
    qint64 duration = this->player->duration();
    if (duration <= 0)
        return;
    qint64 current = this->player->position();
    qint64 delta = static_cast<qint64>(seconds * 1000.0);
    qint64 target = current + delta;
    if (target < 0) {
        // wrap from start to end
        target = duration + target; // target is negative
        if (target < 0) target = 0;
    } else if (target > duration) {
        // wrap from end to start
        target = target - duration;
        if (target > duration) target = duration;
    }
    this->initialPosition = target;
    if (this->rememberPosition && !this->randomEachHover) {
        this->lastPosition = target;
    }
    this->player->setPosition(target);
    if (!this->isPlaying() && this->preloadPause) {
        // keep paused if we were paused due to preload
        return;
    }
    if (!this->isPlaying()) {
        this->player->play();
    }
}

void VideoPreviewWidget::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (!this->preloadActive)
        return;
    if (!(status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia))
        return;

    this->awaitingLoad = false;
    qint64 duration = this->player ? this->player->duration() : 0;
    qint64 target = this->computeTargetPosition(duration);
    if (this->randomStart && duration <= 0) {
        target = 0;
    }
    if (this->rememberPosition && this->lastPosition <= 0 && target > 0)
        this->lastPosition = target;

    this->preloadActive = false;
    this->startAtPosition(target, this->preloadPause);
}

void VideoPreviewWidget::prepareInitialFrame(bool keepPlaying)
{
    this->ensurePlayer();
    if (!this->player)
        return;
    this->applyVolume();

    this->preloadActive = true;
    this->preloadPause = !keepPlaying;
    this->awaitingLoad = true;

    qint64 duration = this->player->duration();
    if (duration > 0) {
        qint64 target = this->computeTargetPosition(duration);
    if (this->rememberPosition && this->lastPosition <= 0 && target > 0)
        this->lastPosition = target;
        this->startAtPosition(target, this->preloadPause);
        this->awaitingLoad = false;
    } else {
        this->player->play(); // trigger load so mediaStatusChanged fires
    }
}

void VideoPreviewWidget::restoreLastPosition(qint64 positionMs)
{
    if (!this->rememberPosition || this->randomEachHover)
        return;
    qint64 clamped = std::max<qint64>(0, positionMs);
    this->lastPosition = clamped;
    this->initialPosition = clamped;
    if (this->player && this->player->duration() > 0) {
        qint64 maxPos = std::max<qint64>(0, this->player->duration() - 200);
        this->player->setPosition(std::clamp(clamped, static_cast<qint64>(0), maxPos));
    }
}

qint64 VideoPreviewWidget::resumePositionHint() const
{
    if (this->randomEachHover || !this->rememberPosition)
        return 0;
    if (this->lastPosition > 0)
        return this->lastPosition;
    if (this->player) {
        qint64 pos = this->player->position();
        if (pos > 0)
            return pos;
    }
    return 0;
}

void VideoPreviewWidget::startPreview()
{
    if (this->sourcePath.isEmpty() || !QFileInfo::exists(this->sourcePath)) {
        this->stopPreview();
        return;
    }
    this->ensurePlayer();
    this->startTimer.stop();
    if (this->startDelayMs > 0) {
        this->startTimer.start(this->startDelayMs);
    } else {
        this->startPlayback();
    }
}

void VideoPreviewWidget::stopPreview(bool forceStop)
{
    this->startTimer.stop();
    if (this->player) {
        qint64 pos = this->player->position();
        if (forceStop) {
            this->player->stop();
        } else {
            if (!this->randomEachHover) {
                this->lastPosition = pos;
            }
            this->lastPositionMs = pos;
            this->player->pause();
            this->updateOverlayText(this->lastPositionMs, this->currentDurationMs);
        }
        emit this->previewStopped(this->sourcePath);
    }
    if (forceStop) {
        this->lastPositionMs = 0;
        this->lastOverlayText.clear();
        this->updateOverlayText(0, -1);
    }
}

void VideoPreviewWidget::ensurePlayer()
{
    if (this->player)
        return;

    this->player = new ThreadedMediaPlayer(this);
    // The player lives on its own worker thread. The wrapper marshals all of these back
    // to this object's thread, so the slots below stay ordinary GUI-thread slots.
    this->statusConn = QObject::connect(this->player, &ThreadedMediaPlayer::mediaStatusChanged, this, &VideoPreviewWidget::onMediaStatusChanged);
    QObject::connect(this->player, &ThreadedMediaPlayer::positionChanged, this, &VideoPreviewWidget::onPositionChanged);
    QObject::connect(this->player, &ThreadedMediaPlayer::durationChanged, this, [this](qint64 dur) {
        this->currentDurationMs = dur;
        this->updateOverlayText(this->lastPositionMs, dur);
    });
    QObject::connect(this->player, &ThreadedMediaPlayer::errorOccurred, this, [](QMediaPlayer::Error, const QString& errorString) {
        qWarning() << "VideoPreviewWidget: player error:" << errorString;
    });
    this->applyVolume();

    QVideoSink* sink = this->videoWidget ? this->videoWidget->videoSink() : nullptr;
    if (sink) {
        QObject::connect(sink, &QVideoSink::videoSizeChanged, this, &VideoPreviewWidget::handleVideoSizeChanged);
        QObject::connect(sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame&) {
            if (!this->firstFrameSeen) {
                this->firstFrameSeen = true;
                emit this->firstFrameReady();
            }
        });
    }
    // Player, audio output, loop mode and the initial source are all created on the
    // wrapper's worker thread, never here on the GUI thread.
    this->player->start(sink, this->sourcePath, true);
}

void VideoPreviewWidget::startPlayback()
{
    if (!this->player)
        this->ensurePlayer();
    if (!this->player)
        return;

    if (this->randomStartDurationConn)
        QObject::disconnect(this->randomStartDurationConn);
    if (this->randomHoverDurationConn)
        QObject::disconnect(this->randomHoverDurationConn);
    this->randomStartDurationConn = QMetaObject::Connection();
    this->randomHoverDurationConn = QMetaObject::Connection();

    if (this->randomEachHover) {
        if (this->player->duration() > 0) {
            qint64 pos = this->pickRandomPosition(this->player->duration(), ++this->hoverRandomCounter);
            this->initialPosition = pos;
            this->lastPosition = 0;
            this->player->setPosition(pos);
        } else {
            this->randomHoverDurationConn = QObject::connect(this->player, &ThreadedMediaPlayer::durationChanged, this, [this](qint64) {
                if (!this->randomEachHover)
                    return;
                if (this->player && this->player->duration() > 0) {
                    qint64 pos = this->pickRandomPosition(this->player->duration(), ++this->hoverRandomCounter);
                    this->initialPosition = pos;
                    this->player->setPosition(pos);
                    QObject::disconnect(this->randomHoverDurationConn);
                    this->randomHoverDurationConn = QMetaObject::Connection();
                }
            });
        }
    }
    else if (this->rememberPosition && this->lastPosition > 0) {
        this->player->setPosition(this->lastPosition);
    } else if (this->initialPosition >= 0) {
        this->player->setPosition(this->initialPosition);
    } else if (this->randomStart) {
        if (this->player->duration() > 0) {
            qint64 pos = this->pickRandomPosition(this->player->duration());
            this->initialPosition = pos;
            if (this->rememberPosition)
                this->lastPosition = pos;
            this->player->setPosition(pos);
        } else {
            this->randomStartDurationConn = QObject::connect(this->player, &ThreadedMediaPlayer::durationChanged, this, [this](qint64) {
                if (!this->randomStart)
                    return;
                if (this->rememberPosition && this->lastPosition > 0)
                    return;
                if (this->player && this->player->duration() > 0) {
                    qint64 pos = this->pickRandomPosition(this->player->duration());
                    this->initialPosition = pos;
                    if (this->rememberPosition)
                        this->lastPosition = pos;
                    QObject::disconnect(this->randomStartDurationConn);
                    this->randomStartDurationConn = QMetaObject::Connection();
                    this->player->setPosition(pos);
                }
            });
        }
    } else {
        this->initialPosition = 0;
        this->player->setPosition(0);
    }

    this->player->play();
    emit this->previewStarted(this->sourcePath);
}

void VideoPreviewWidget::applyVolume()
{
    if (!this->player)
        return;
    // Before start() the wrapper stores these and applies them when the audio output is
    // created on its worker thread; afterwards they are queued like every other command.
    this->player->setMuted(false);
    this->player->setVolume(static_cast<float>(utils::volume_convert(this->muted ? 0 : this->volumePercent)));
}

void VideoPreviewWidget::onPositionChanged(qint64 positionMs)
{
    this->lastPositionMs = positionMs;
    this->updateOverlayText(positionMs, this->currentDurationMs);
}

void VideoPreviewWidget::handleVideoSizeChanged()
{
    if (this->videoWidget && this->videoWidget->videoSink()) {
        const QSize size = this->videoWidget->videoSink()->videoSize();
        if (size.isValid()) {
            emit this->videoSizeKnown(size);
        }
    }
}

void VideoPreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}

void VideoPreviewWidget::updateOverlayText(qint64 positionMs, qint64 durationMs)
{
    if (!this->videoWidget)
        return;

    qint64 effectiveDurationMs = durationMs;
    if (effectiveDurationMs <= 0 && this->player) {
        effectiveDurationMs = this->player->duration();
    }
    effectiveDurationMs = std::max<qint64>(0, effectiveDurationMs);

    qint64 effectivePositionMs = positionMs;
    if (effectivePositionMs < 0 && this->player) {
        effectivePositionMs = this->player->position();
    }
    effectivePositionMs = std::max<qint64>(0, effectivePositionMs);

    const bool showHours = effectiveDurationMs >= 3600 * 1000;
    const QString text = QStringLiteral("%1 / %2")
        .arg(utils::formatSecondsQt(effectivePositionMs / 1000.0, showHours),
            utils::formatSecondsQt(effectiveDurationMs / 1000.0, showHours));
    if (text == this->lastOverlayText)
        return;
    this->lastOverlayText = text;
    this->videoWidget->setOverlayText(text);
}
