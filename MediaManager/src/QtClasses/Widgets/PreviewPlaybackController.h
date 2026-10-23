#pragma once
#include <QObject>
#include <QTimer>
#include <QString>
#include <QSize>
#include <QMediaPlayer>

class QVideoSink;
class ThreadedMediaPlayer;

// Preview playback/session logic without any widget or QQuickWidget dependency.
//
// The host supplies the QVideoSink of its video surface (for example the VideoOutput of
// a QQuickView) and must keep that sink alive until shutdown()/the destructor has run.
// The controller owns the ThreadedMediaPlayer and emits the overlay text instead of
// painting it, leaving the host to expose it in whatever surface it uses.
class PreviewPlaybackController : public QObject
{
    Q_OBJECT
public:
    explicit PreviewPlaybackController(QObject* parent = nullptr);
    ~PreviewPlaybackController() override;

    // Must be set before startPreview()/prepareInitialFrame() creates the player.
    void setVideoSink(QVideoSink* sink);
    // Detaches the worker-thread player from the sink. The host calls this from its
    // destructor before its QML scene/sink is destroyed.
    void shutdown();

    void setSource(const QString& path);
    void setStartDelay(int ms);
    void setVolume(int percent);
    void setRandomStartEnabled(bool enabled);
    void setMuted(bool muted);
    void setRememberPositionEnabled(bool enabled);
    void setRandomOnHoverEnabled(bool enabled);
    void setSeededRandom(bool enabled, const QString& seedValue);
    void jumpToRandomPosition();
    void seekBySeconds(double seconds);
    void prepareInitialFrame(bool keepPlaying);
    void restoreLastPosition(qint64 positionMs);
    qint64 resumePositionHint() const;
    bool isPlaying() const;

public slots:
    void startPreview();
    void stopPreview(bool forceStop = false);

signals:
    void previewStarted(const QString& path);
    void previewStopped(const QString& path);
    void videoSizeKnown(const QSize& size);
    void firstFrameReady();
    void overlayTextChanged(const QString& text);

private slots:
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onPositionChanged(qint64 positionMs);

private:
    void connectSink();
    void handleVideoSizeChanged();
    void ensurePlayer();
    void startPlayback();
    void applyVolume();
    void updateOverlayText(qint64 positionMs, qint64 durationMs);
    void startAtPosition(qint64 target, bool pauseAfterSeek);
    qint64 computeTargetPosition(qint64 duration) const;
    qint64 pickRandomPosition(qint64 duration, quint64 nonce = 0) const;

    QString sourcePath;
    QVideoSink* sink = nullptr;
    ThreadedMediaPlayer* player = nullptr;
    QTimer startTimer;
    int startDelayMs = 0;
    int volumePercent = 20;
    bool randomStart = false;
    bool muted = false;
    bool rememberPosition = false;
    bool randomEachHover = false;
    bool seededRandom = false;
    QString seedString;
    qint64 lastPosition = 0;
    qint64 initialPosition = -1;
    qint64 currentDurationMs = 0;
    qint64 lastPositionMs = 0;
    QString lastOverlayText;
    bool awaitingLoad = false;
    bool firstFrameSeen = false;
    bool preloadActive = false;
    bool preloadPause = false;
    bool sinkConnected = false;
    QMetaObject::Connection statusConn;
    QMetaObject::Connection pauseConn;
    QMetaObject::Connection randomStartDurationConn;
    QMetaObject::Connection randomHoverDurationConn;
    mutable quint64 hoverRandomCounter = 0;
};
