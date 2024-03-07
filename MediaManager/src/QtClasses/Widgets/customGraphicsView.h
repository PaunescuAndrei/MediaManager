#pragma once
#include <QGraphicsView>
#include <QEvent>
#include <QGraphicsItem>
#include <QPixmap>

class QGraphicsScene_custom;

class customGraphicsView :
    public QGraphicsView
{
    Q_OBJECT
public:
    QGraphicsPixmapItem* pixmap_item;
    QGraphicsScene_custom* scene;
    QPixmap original_pixmap = QPixmap();
    QString imgpath;
    customGraphicsView(QWidget* parent = nullptr,QString imgpath = "");
    ~customGraphicsView();
    void resizeEvent(QResizeEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void setPixmap(QPixmap pixmap);
    QPixmap getResizedPixmap(QPixmap pixmap);
    QPixmap getResizedPixmapFast(QPixmap pixmap);
    QPixmap getResizedPixmap(QPixmap *pixmap);
    QPixmap getResizedPixmapFast(QPixmap *pixmap);
    void flipPixmap();
    void setImage(QString path,bool flip = false);
    void setImage(QPixmap pixmap, bool flip = false);
    void setImage(QString path, QPixmap pixmap, bool flip = false);
    void resizeImage();
    signals:
        void mouseClicked(Qt::MouseButton button);
};
