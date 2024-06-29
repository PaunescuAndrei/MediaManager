#pragma once
#include <QGraphicsView>
#include <QEvent>
#include <QGraphicsItem>
#include <QPixmap>
#include "mascotsGeneratorThread.h"

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
    QList<color_area> accepted_colors;
    QList<color_area> rejected_colors;
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
    color_area getColor(bool weighted = true);
    void setImage(ImageData data, bool flip = false);
    void setImage(QString path,bool flip = false);
    void setImage(QPixmap pixmap, bool flip = false);
    void setImage(QString path, QPixmap pixmap, bool flip = false);
    void clearColors();
    void resizeImage();
    signals:
        void mouseClicked(Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
};
