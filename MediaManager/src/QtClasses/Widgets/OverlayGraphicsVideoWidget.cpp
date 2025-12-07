#include "stdafx.h"
#include "OverlayGraphicsVideoWidget.h"
#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QVideoSink>

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
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(this->rect(), QColor(17, 17, 17));

    if (!this->currentFrame.isNull() && !this->targetRect.isEmpty()) {
        painter.drawImage(this->targetRect, this->currentFrame);
    }

    if (!this->currentText.isEmpty()) {
        QFont f = this->font();
        f.setPointSize(11);
        painter.setFont(f);
        QFontMetrics fm(f);
        QRect textRect = fm.boundingRect(this->currentText);
        textRect.adjust(-4, -2, 4, 2);
        const int margin = 6;
        QRect bgRect(this->width() - textRect.width() - margin,
            this->height() - textRect.height() - margin,
            textRect.width(), textRect.height());

        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setOpacity(0.45);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0));
        painter.drawRoundedRect(bgRect, 4, 4);

        painter.setOpacity(1.0);
        painter.setPen(Qt::white);
        painter.drawText(bgRect, Qt::AlignCenter, this->currentText);
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
