#pragma once
#include <QWidget>
#include <QGraphicsView>

class QGraphicsScene;
class QGraphicsVideoItem;
class QGraphicsRectItem;
class QGraphicsSimpleTextItem;
class QVideoSink;

// GraphicsView-based video widget with translucent overlay text.
class OverlayGraphicsVideoWidget : public QWidget {
public:
    explicit OverlayGraphicsVideoWidget(QWidget* parent = nullptr);
    QVideoSink* videoSink() const;
    void setOverlayText(const QString& text);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void updateLayout();
    QGraphicsView* view = nullptr;
    QGraphicsScene* scene = nullptr;
    QGraphicsVideoItem* videoItem = nullptr;
    QGraphicsRectItem* overlayBg = nullptr;
    QGraphicsSimpleTextItem* overlayText = nullptr;
    QString currentText;
};
