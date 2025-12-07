#pragma once
#include <QWidget>
#include <QImage>
#include <QPointer>
#include "PreviewFrameProcessor.h"

// Video widget backed by QVideoSink + worker-based frame conversion with overlay text.
class OverlayGraphicsVideoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OverlayGraphicsVideoWidget(QWidget* parent = nullptr);
    QVideoSink* videoSink() const;
    void setOverlayText(const QString& text);
    void setOverlayStyle(double scaleMultiplier, int padX, int padY, int margin);
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
    double overlayScaleMultiplier = 1.0;
    int overlayPadX = 4;
    int overlayPadY = 2;
    int overlayMargin = 6;
};
