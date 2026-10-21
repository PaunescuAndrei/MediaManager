#pragma once
#include <QWidget>
#include <QString>

class QQuickWidget;
class QQuickItem;
class QVideoSink;

// Video widget backed by a QML VideoOutput (GPU texturing) with overlay text.
class OverlayGraphicsVideoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OverlayGraphicsVideoWidget(QWidget* parent = nullptr);
    QVideoSink* videoSink() const;
    void setOverlayText(const QString& text);
    void setOverlayStyle(double scaleMultiplier, int padX, int padY, int margin);
private:
    void applyOverlayStyle();
    QQuickWidget* quick = nullptr;
    QQuickItem* root = nullptr;
    QVideoSink* sink = nullptr;
    QString currentText;
    double overlayScaleMultiplier = 1.0;
    int overlayPadX = 4;
    int overlayPadY = 2;
    int overlayMargin = 6;
};
