#include "stdafx.h"
#include "OverlayGraphicsVideoWidget.h"
#include <QQuickWidget>
#include <QQuickItem>
#include <QVBoxLayout>
#include <QVideoSink>
#include <QUrl>
#include <QQmlError>
#include <algorithm>

OverlayGraphicsVideoWidget::OverlayGraphicsVideoWidget(QWidget* parent) : QWidget(parent)
{
    this->setAttribute(Qt::WA_NoSystemBackground, true);
    this->setAutoFillBackground(false);

    this->quick = new QQuickWidget(this);
    // The video surface is not interactive: let clicks, wheel and hover events
    // reach the parent widgets (card selection, preview seeking) like they did
    // when this widget painted the frames itself.
    this->quick->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    this->quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    this->quick->setSource(QUrl(QStringLiteral("qrc:/qml/PreviewVideo.qml")));

    if (this->quick->status() == QQuickWidget::Error) {
        const QList<QQmlError> errors = this->quick->errors();
        for (const QQmlError& error : errors)
            qWarning() << "PreviewVideo.qml:" << error.toString();
    }

    this->root = this->quick->rootObject();
    if (this->root) {
        this->sink = this->root->findChild<QVideoSink*>();
        this->applyOverlayStyle();
        this->root->setProperty("overlayText", this->currentText);
    }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(this->quick);
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
    if (this->root)
        this->root->setProperty("overlayText", this->currentText);
}

void OverlayGraphicsVideoWidget::setOverlayStyle(double scaleMultiplier, int padX, int padY, int margin)
{
    this->overlayScaleMultiplier = std::clamp(scaleMultiplier, 0.1, 5.0);
    this->overlayPadX = std::max(0, padX);
    this->overlayPadY = std::max(0, padY);
    this->overlayMargin = std::max(0, margin);
    this->applyOverlayStyle();
}

void OverlayGraphicsVideoWidget::applyOverlayStyle()
{
    if (!this->root)
        return;
    this->root->setProperty("overlayScaleMultiplier", this->overlayScaleMultiplier);
    this->root->setProperty("overlayPadX", this->overlayPadX);
    this->root->setProperty("overlayPadY", this->overlayPadY);
    this->root->setProperty("overlayMargin", this->overlayMargin);
}
