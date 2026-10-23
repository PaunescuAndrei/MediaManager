#pragma once
#include <QQuickView>
#include <QString>
#include <QSize>

class QQuickItem;
class QWidget;
class PreviewPlaybackController;

// Native Qt Quick preview popup.
//
// Unlike a QDialog hosting a QQuickWidget, this is a real QQuickWindow (Qt::Tool with
// a transient parent), which avoids the known QQuickWidget-as-popup white flicker
// (QTBUG-88706 / QTBUG-69558) and keeps a proper window-level parent/owner relationship
// with the main window or InsertSettingsDialog.
class PreviewVideoWindow : public QQuickView
{
    Q_OBJECT
public:
    explicit PreviewVideoWindow(QWidget* transientParent = nullptr);
    ~PreviewVideoWindow() override;

    QString path;

    void setSeekSeconds(double seconds);
    void setSource(const QString& path);
    void setStartDelay(int ms);
    void setVolume(int percent);
    void setRandomStartEnabled(bool enabled);
    void setMuted(bool muted);
    void setRememberPositionEnabled(bool enabled);
    void setRandomOnHoverEnabled(bool enabled);
    void setSeededRandom(bool enabled, const QString& seedValue);
    void setOverlayStyle(double scaleMultiplier, int padX, int padY, int margin);
    void jumpToRandomPosition();
    void seekBySeconds(double seconds);
    void prepareInitialFrame(bool keepPlaying);
    void restoreLastPosition(qint64 positionMs);
    qint64 positionForResume() const;
    bool isPlaying() const;

public slots:
    void startPreview();
    void stopPreview(bool forceStop = false);

signals:
    void previewStarted(const QString& path);
    void previewStopped(const QString& path);
    void videoSizeKnown(const QSize& size);
    void firstFrameReady();
    // Emitted from the closing handler before the window is deleted, so callers can
    // still read positionForResume()/path.
    void finished();

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void handleClose();
    void uncloak();
    void applyOverlayStyle();
    void applyOverlayText(const QString& text);

    PreviewPlaybackController* controller = nullptr;
    QQuickItem* root = nullptr;
    double seekSeconds = 5.0;
    double overlayScaleMultiplier = 1.0;
    int overlayPadX = 4;
    int overlayPadY = 2;
    int overlayMargin = 6;
    bool closing = false;
    bool uncloaked = false;
};
