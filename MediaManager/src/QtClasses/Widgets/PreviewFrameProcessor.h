#pragma once
#include <QObject>
#include <QPointer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QImage>
#include <atomic>

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
    void resetState();
    static QThread* sharedThread();
    QPointer<QVideoSink> sink;
    FrameWorker* worker = nullptr;
    QVideoFrame latestFrame;
    bool processing = false;
    bool hasPendingFrame = false;
    std::atomic<bool> closing = false;
};
