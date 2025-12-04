#pragma once
#include <QWidget>
#include <QTimer>
#include <QString>

class QMediaPlayer;
class QVideoWidget;
class QAudioOutput;

// Lightweight video preview widget that can be reused outside NextChoiceDialog.
class VideoPreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoPreviewWidget(QWidget* parent = nullptr);
    void setSource(const QString& path);
    void setStartDelay(int ms);
    void setVolume(int percent);
    void setRandomStartEnabled(bool enabled);
    void setMuted(bool muted);
    void setRememberPositionEnabled(bool enabled);
    void setRandomOnHoverEnabled(bool enabled);
    void setSeededRandom(bool enabled, const QString& seedValue);
    void jumpToRandomPosition();
    void prepareInitialFrame(bool keepPlaying);
    bool isPlaying() const;
private slots:
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
public slots:
    void startPreview();
    void stopPreview(bool forceStop = false);
signals:
    void previewStarted(const QString& path);
    void previewStopped(const QString& path);
private:
    void ensurePlayer();
    void startPlayback();
    void applyVolume();
    void startAtPosition(qint64 target, bool pauseAfterSeek);
    qint64 computeTargetPosition(qint64 duration) const;
    qint64 pickRandomPosition(qint64 duration, quint64 nonce = 0) const;
    QString sourcePath;
    QMediaPlayer* player = nullptr;
    QVideoWidget* videoWidget = nullptr;
    QAudioOutput* audioOutput = nullptr;
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
    bool awaitingLoad = false;
    bool preloadActive = false;
    bool preloadPause = false;
    QMetaObject::Connection statusConn;
    QMetaObject::Connection pauseConn;
    mutable quint64 hoverRandomCounter = 0;
};
