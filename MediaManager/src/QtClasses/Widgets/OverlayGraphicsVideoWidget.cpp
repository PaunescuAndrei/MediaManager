#include "stdafx.h"
#include "OverlayGraphicsVideoWidget.h"
#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QVideoSink>
#include <cmath>
#include <algorithm>

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

void OverlayGraphicsVideoWidget::setOverlayStyle(double scaleMultiplier, int padX, int padY, int margin)
{
    this->overlayScaleMultiplier = std::clamp(scaleMultiplier, 0.1, 5.0);
    this->overlayPadX = std::max(0, padX);
    this->overlayPadY = std::max(0, padY);
    this->overlayMargin = std::max(0, margin);
    this->update();
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
        const QRect overlayArea = this->targetRect.isEmpty() ? this->rect() : this->targetRect;
        const double autoScale = overlayArea.height() > 0 ? static_cast<double>(overlayArea.height()) / 200.0 : 1.0;
        const double scale = std::max(0.1, this->overlayScaleMultiplier) * autoScale;
        const int fontSize = qBound(6, static_cast<int>(std::round(11.0 * scale)), 48);
        const int padX = qBound(0, static_cast<int>(std::round(this->overlayPadX * scale)), 96);
        const int padY = qBound(0, static_cast<int>(std::round(this->overlayPadY * scale)), 96);
        const int margin = qBound(0, static_cast<int>(std::round(this->overlayMargin * scale)), 128);

        QFont f = this->font();
        f.setPointSize(fontSize);
        painter.setFont(f);
        QFontMetrics fm(f);
        QRect textRect = fm.boundingRect(this->currentText);
        textRect.adjust(-padX, -padY, padX, padY);
        const int posX = std::max(overlayArea.left(), overlayArea.right() - textRect.width() - margin);
        const int posY = std::max(overlayArea.top(), overlayArea.bottom() - textRect.height() - margin);
        QRect bgRect(QPoint(posX, posY), textRect.size());

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
