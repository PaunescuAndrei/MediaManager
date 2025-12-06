#include "stdafx.h"
#include "VideoPreviewWidget.h"
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoSink>
#include <QUrl>
#include <QFileInfo>
#include <QRandomGenerator>
#include "OverlayGraphicsVideoWidget.h"
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

void VideoPreviewWidget::setSource(const QString& path)
{
    this->sourcePath = path;
    if (this->player && this->player->source().toLocalFile() != path) {
        this->player->stop();
        this->player->setPosition(0);
        this->player->setSource(QUrl::fromLocalFile(path));
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
        this->pauseConn = QObject::connect(this->player, &QMediaPlayer::positionChanged, this, pauseCheck);
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
            QMetaObject::invokeMethod(this->player, "pause", Qt::QueuedConnection);
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
        if (forceStop) {
            this->player->stop();
        } else if (!this->randomEachHover) {
            this->lastPosition = this->player->position();
            this->player->pause();
        } else {
            this->player->pause();
        }
        emit this->previewStopped(this->sourcePath);
    }
    this->updateOverlayText(0, -1);
}

void VideoPreviewWidget::ensurePlayer()
{
    if (this->player)
        return;

    this->player = new QMediaPlayer(this);
    this->audioOutput = new QAudioOutput(this);
    this->applyVolume();
    this->player->setAudioOutput(this->audioOutput);
    if (this->videoWidget && this->videoWidget->videoSink()) {
        this->player->setVideoOutput(this->videoWidget->videoSink());
    }
    this->player->setLoops(QMediaPlayer::Infinite);
    this->statusConn = QObject::connect(this->player, &QMediaPlayer::mediaStatusChanged, this, &VideoPreviewWidget::onMediaStatusChanged);
    QObject::connect(this->player, &QMediaPlayer::positionChanged, this, &VideoPreviewWidget::onPositionChanged);
    QObject::connect(this->player, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        this->currentDurationMs = dur;
        this->updateOverlayText(this->lastPositionMs, dur);
    });
    if (!this->sourcePath.isEmpty()) {
        this->player->setSource(QUrl::fromLocalFile(this->sourcePath));
    }
}

void VideoPreviewWidget::startPlayback()
{
    if (!this->player)
        this->ensurePlayer();
    if (!this->player)
        return;
    if (this->randomEachHover) {
        if (this->player->duration() > 0) {
            qint64 pos = this->pickRandomPosition(this->player->duration(), ++this->hoverRandomCounter);
            this->initialPosition = pos;
            this->lastPosition = 0;
            this->player->setPosition(pos);
        } else {
            QObject::disconnect(this->player, &QMediaPlayer::durationChanged, nullptr, nullptr);
            QObject::connect(this->player, &QMediaPlayer::durationChanged, this, [this](qint64) {
                if (!this->randomEachHover)
                    return;
                if (this->player && this->player->duration() > 0) {
                    qint64 pos = this->pickRandomPosition(this->player->duration(), ++this->hoverRandomCounter);
                    this->initialPosition = pos;
                    this->player->setPosition(pos);
                    QObject::disconnect(this->player, &QMediaPlayer::durationChanged, this, nullptr);
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
            QObject::disconnect(this->player, &QMediaPlayer::durationChanged, nullptr, nullptr);
            QObject::connect(this->player, &QMediaPlayer::durationChanged, this, [this](qint64) {
                if (!this->randomStart)
                    return;
                if (this->rememberPosition && this->lastPosition > 0)
                    return;
                if (this->player && this->player->duration() > 0) {
                    qint64 pos = this->pickRandomPosition(this->player->duration());
                    this->initialPosition = pos;
                    if (this->rememberPosition)
                        this->lastPosition = pos;
                    QObject::disconnect(this->player, &QMediaPlayer::durationChanged, this, nullptr);
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
    if (!this->audioOutput)
        return;
    this->audioOutput->setMuted(false);
    this->audioOutput->setVolume(utils::volume_convert(this->muted ? 0 : this->volumePercent));
}

void VideoPreviewWidget::onPositionChanged(qint64 positionMs)
{
    this->lastPositionMs = positionMs;
    this->updateOverlayText(positionMs, this->currentDurationMs);
}

void VideoPreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}

void VideoPreviewWidget::updateOverlayText(qint64 positionMs, qint64 durationMs)
{
    if (!this->videoWidget)
        return;
    if (durationMs <= 0) {
        this->videoWidget->setOverlayText(QString());
        this->lastOverlayText.clear();
        return;
    }
    const QString text = QStringLiteral("%1 / %2")
        .arg(utils::formatSecondsQt(positionMs / 1000.0),
            utils::formatSecondsQt(durationMs / 1000.0));
    if (text == this->lastOverlayText)
        return;
    this->lastOverlayText = text;
    this->videoWidget->setOverlayText(text);
}
