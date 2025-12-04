#include "stdafx.h"
#include "VideoPreviewWidget.h"
#include <QVideoWidget>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QUrl>
#include <QFileInfo>
#include <QRandomGenerator>
#include "utils.h"

VideoPreviewWidget::VideoPreviewWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    this->videoWidget = new QVideoWidget(this);
    this->videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->videoWidget->setStyleSheet(QStringLiteral("background-color: #111; border: 1px solid #333;"));
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

    if (this->rememberPosition && this->lastPosition > 0) {
        return clampPos(this->lastPosition, duration);
    }
    if (this->randomStart && duration > 0) {
        qint64 maxStart = std::max<qint64>(0, duration - 1500);
        qint64 pos = maxStart > 0 ? QRandomGenerator::global()->bounded(maxStart) : 0;
        return clampPos(pos, duration);
    }
    return 0;
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
        if (this->initialPosition > 0 && this->randomStart) {
            pauseCheck(this->player->position());
        }
    }

    this->player->setPosition(target);
    this->player->play();
    if (pauseAfterSeek && (this->randomStart || this->initialPosition > 0)) {
        QMetaObject::invokeMethod(this->player, "pause", Qt::QueuedConnection);
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

void VideoPreviewWidget::stopPreview()
{
    this->startTimer.stop();
    if (this->player) {
        this->lastPosition = this->player->position();
        this->player->pause();
        emit this->previewStopped(this->sourcePath);
    }
}

void VideoPreviewWidget::ensurePlayer()
{
    if (this->player)
        return;

    this->player = new QMediaPlayer(this);
    this->audioOutput = new QAudioOutput(this);
    this->applyVolume();
    this->player->setAudioOutput(this->audioOutput);
    this->player->setVideoOutput(this->videoWidget);
    this->player->setLoops(QMediaPlayer::Infinite);
    this->statusConn = QObject::connect(this->player, &QMediaPlayer::mediaStatusChanged, this, &VideoPreviewWidget::onMediaStatusChanged);
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
    auto setRandomPosition = [this]() -> qint64 {
        qint64 duration = this->player->duration();
        if (duration > 0) {
            qint64 maxStart = std::max<qint64>(0, duration - 1500); // leave ~1.5s to avoid immediate loop
            qint64 pos = maxStart > 0 ? QRandomGenerator::global()->bounded(maxStart) : 0;
            this->player->setPosition(pos);
            return pos;
        }
        else {
            this->player->setPosition(0);
        }
        return 0;
    };

    if (this->rememberPosition && this->lastPosition > 0) {
        this->player->setPosition(this->lastPosition);
    } else if (this->initialPosition >= 0) {
        this->player->setPosition(this->initialPosition);
    } else if (this->randomStart) {
        if (this->player->duration() > 0) {
            qint64 pos = setRandomPosition();
            this->initialPosition = pos;
            if (this->rememberPosition)
                this->lastPosition = pos;
        } else {
            QObject::disconnect(this->player, &QMediaPlayer::durationChanged, nullptr, nullptr);
            QObject::connect(this->player, &QMediaPlayer::durationChanged, this, [this, setRandomPosition](qint64) {
                if (!this->randomStart)
                    return;
                if (this->rememberPosition && this->lastPosition > 0)
                    return;
                if (this->player && this->player->duration() > 0) {
                    qint64 pos = setRandomPosition();
                    this->initialPosition = pos;
                    if (this->rememberPosition)
                        this->lastPosition = pos;
                    QObject::disconnect(this->player, &QMediaPlayer::durationChanged, this, nullptr);
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
