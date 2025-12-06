#pragma once
#include <QObject>
#include <QWidget>
#include <QImage>
#include <QPointer>
#include <QVideoFrame>
#include <QVideoSink>

class QThread;

class FrameWorker : public QObject
{
    Q_OBJECT
public slots:
    void processFrame(const QVideoFrame& frame);
signals:
    void frameReady(const QImage& image);
};

class PreviewFrameProcessor : public QObject
{
    Q_OBJECT
public:
    explicit PreviewFrameProcessor(QObject* parent = nullptr);
    ~PreviewFrameProcessor() override;
    void setVideoSink(QVideoSink* sink);
    QVideoSink* videoSink() const { return this->sink; }
signals:
    void frameReady(const QImage& image);
private slots:
    void onFrameAvailable(const QVideoFrame& frame);
    void onFrameConverted(const QImage& image);
private:
    void dispatchLatest();
    QPointer<QVideoSink> sink;
    QThread* workerThread = nullptr;
    FrameWorker* worker = nullptr;
    QVideoFrame latestFrame;
    bool processing = false;
    bool hasPendingFrame = false;
};

// Video widget backed by QVideoSink + worker-based frame conversion with overlay text.
class OverlayGraphicsVideoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OverlayGraphicsVideoWidget(QWidget* parent = nullptr);
    QVideoSink* videoSink() const;
    void setOverlayText(const QString& text);
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private slots:
    void onFrameReady(const QImage& image);
private:
    void updateTargetRect();
    PreviewFrameProcessor* processor = nullptr;
    QVideoSink* sink = nullptr;
    QImage currentFrame;
    QRect targetRect;
    QString currentText;
};
