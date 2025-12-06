#include "stdafx.h"
#include "OverlayGraphicsVideoWidget.h"
#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QThread>
#include <QVideoSink>
#include <QMetaType>

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
    this->workerThread = new QThread(this);
    this->worker = new FrameWorker();
    this->worker->moveToThread(this->workerThread);
    QObject::connect(this->worker, &FrameWorker::frameReady, this, &PreviewFrameProcessor::onFrameConverted);
    this->workerThread->start(QThread::LowestPriority);
}

PreviewFrameProcessor::~PreviewFrameProcessor()
{
    if (this->workerThread) {
        this->workerThread->quit();
        this->workerThread->wait();
    }
    delete this->worker;
    this->worker = nullptr;
}

void PreviewFrameProcessor::setVideoSink(QVideoSink* sink)
{
    if (this->sink == sink)
        return;
    if (this->sink) {
        QObject::disconnect(this->sink, nullptr, this, nullptr);
    }
    this->sink = sink;
    this->latestFrame = QVideoFrame();
    this->hasPendingFrame = false;
    this->processing = false;
    if (this->sink) {
        QObject::connect(this->sink, &QVideoSink::videoFrameChanged, this, &PreviewFrameProcessor::onFrameAvailable);
    }
}

void PreviewFrameProcessor::onFrameAvailable(const QVideoFrame& frame)
{
    this->latestFrame = frame;
    this->hasPendingFrame = true;
    if (this->processing || !this->worker)
        return;
    this->dispatchLatest();
}

void PreviewFrameProcessor::dispatchLatest()
{
    if (!this->worker || !this->hasPendingFrame)
        return;
    this->processing = true;
    this->hasPendingFrame = false;
    QVideoFrame frameCopy(this->latestFrame);
    QMetaObject::invokeMethod(this->worker, "processFrame", Qt::QueuedConnection, Q_ARG(QVideoFrame, frameCopy));
}

void PreviewFrameProcessor::onFrameConverted(const QImage& image)
{
    this->processing = false;
    emit this->frameReady(image);
    if (this->hasPendingFrame) {
        this->dispatchLatest();
    }
}

OverlayGraphicsVideoWidget::OverlayGraphicsVideoWidget(QWidget* parent) : QWidget(parent)
{
    this->setAttribute(Qt::WA_NoSystemBackground, true);
    this->setAutoFillBackground(false);

    this->sink = new QVideoSink(this);
    this->processor = new PreviewFrameProcessor(this);
    this->processor->setVideoSink(this->sink);
    QObject::connect(this->processor, &PreviewFrameProcessor::frameReady, this, &OverlayGraphicsVideoWidget::onFrameReady);
}

QVideoSink* OverlayGraphicsVideoWidget::videoSink() const
{
    return this->sink;
}

void OverlayGraphicsVideoWidget::setOverlayText(const QString& text)
{
    if (this->currentText == text)
        return;
    this->currentText = text;
    this->update();
}

void OverlayGraphicsVideoWidget::onFrameReady(const QImage& image)
{
    this->currentFrame = image;
    this->updateTargetRect();
    this->update();
}

void OverlayGraphicsVideoWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(this->rect(), Qt::black);

    if (!this->currentFrame.isNull() && !this->targetRect.isEmpty()) {
        painter.drawImage(this->targetRect, this->currentFrame);
    }

    if (!this->currentText.isEmpty()) {
        const QFontMetrics fm(this->font());
        QRect textRect = fm.boundingRect(this->currentText);
        textRect.adjust(-4, -2, 4, 2);
        QRect bgRect(textRect);
        const int margin = 6;
        bgRect.moveTo(this->width() - bgRect.width() - margin, this->height() - bgRect.height() - margin);
        painter.fillRect(bgRect, QColor(0, 0, 0, 140));
        painter.setPen(Qt::white);
        painter.drawText(bgRect.adjusted(4, 2, -4, -2), this->currentText);
    }
}

void OverlayGraphicsVideoWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    this->updateTargetRect();
}

void OverlayGraphicsVideoWidget::updateTargetRect()
{
    if (this->currentFrame.isNull() || this->size().isEmpty()) {
        this->targetRect = QRect();
        return;
    }

    QSize scaled = this->currentFrame.size().scaled(this->size(), Qt::KeepAspectRatio);
    QPoint topLeft((this->width() - scaled.width()) / 2, (this->height() - scaled.height()) / 2);
    this->targetRect = QRect(topLeft, scaled);
}
