#include "stdafx.h"
#include "PreviewFrameProcessor.h"
#include <QMetaType>
#include <QThread>
#include <QVideoSink>

void FrameWorker::processFrame(const QVideoFrame& frame)
{
    QImage image;
    if (frame.isValid()) {
        image = frame.toImage();
    }
    emit frameReady(image);
}

PreviewFrameProcessor::PreviewFrameProcessor(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<QVideoFrame>("QVideoFrame");
    QThread* thread = PreviewFrameProcessor::sharedThread();
    this->worker = new FrameWorker();
    this->worker->moveToThread(thread);
    QObject::connect(this->worker, &FrameWorker::frameReady, this, &PreviewFrameProcessor::onFrameConverted);
}

PreviewFrameProcessor::~PreviewFrameProcessor()
{
    this->closing.store(true, std::memory_order_relaxed);
    if (this->worker) {
        FrameWorker* toDelete = this->worker;
        this->worker = nullptr;
        QMetaObject::invokeMethod(toDelete, "deleteLater", Qt::QueuedConnection);
    }
}

void PreviewFrameProcessor::setVideoSink(QVideoSink* sink)
{
    if (this->sink == sink)
        return;
    this->closing.store(false, std::memory_order_relaxed);
    if (this->sink) {
        QObject::disconnect(this->sink, nullptr, this, nullptr);
    }
    this->sink = sink;
    this->resetState();
    if (this->sink) {
        QObject::connect(this->sink, &QVideoSink::videoFrameChanged, this, &PreviewFrameProcessor::onFrameAvailable, Qt::QueuedConnection);
    } else {
        this->closing.store(true, std::memory_order_relaxed);
    }
}

void PreviewFrameProcessor::onFrameAvailable(const QVideoFrame& frame)
{
    if (this->closing.load(std::memory_order_relaxed))
        return;
    this->latestFrame = frame;
    this->hasPendingFrame = true;
    if (this->processing || !this->worker)
        return;
    this->dispatchLatest();
}

void PreviewFrameProcessor::dispatchLatest()
{
    if (!this->worker || !this->hasPendingFrame || this->closing.load(std::memory_order_relaxed))
        return;
    this->processing = true;
    this->hasPendingFrame = false;
    QVideoFrame frameCopy(this->latestFrame);
    QMetaObject::invokeMethod(this->worker, "processFrame", Qt::QueuedConnection, Q_ARG(QVideoFrame, frameCopy));
}

void PreviewFrameProcessor::onFrameConverted(const QImage& image)
{
    if (this->closing.load(std::memory_order_relaxed)) {
        this->resetState();
        return;
    }
    this->processing = false;
    emit this->frameReady(image);
    if (this->hasPendingFrame) {
        this->dispatchLatest();
    }
}

void PreviewFrameProcessor::resetState()
{
    this->latestFrame = QVideoFrame();
    this->hasPendingFrame = false;
    this->processing = false;
}

QThread* PreviewFrameProcessor::sharedThread()
{
    static QThread* thread = []() {
        QThread* t = new QThread();
        t->setObjectName("PreviewFrameThread");
        t->start(QThread::LowestPriority);
        return t;
    }();
    return thread;
}
