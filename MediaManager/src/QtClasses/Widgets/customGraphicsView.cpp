#include "stdafx.h"
#include "customGraphicsView.h"
#include <QFrame>
#include <Qt>
#include <QPainter>
#include "utils.h"
#include <QScrollBar>
#include <QMimeData>
#include <QDir>
#include <QGraphicsSceneDragDropEvent>

class QGraphicsScene_custom : public QGraphicsScene {

public:
    QGraphicsScene_custom(QWidget* parent = nullptr) : QGraphicsScene(parent) {
    };
    void dropEvent(QGraphicsSceneDragDropEvent* e) override
    {
        e->ignore();
    }
    void dragMoveEvent(QGraphicsSceneDragDropEvent* e) override{
        e->ignore();
    }
    void dragEnterEvent(QGraphicsSceneDragDropEvent* e)
    {
        e->ignore();
    }
};

customGraphicsView::customGraphicsView(QWidget* parent, QString imgpath) : QGraphicsView(parent) {
    this->setStyleSheet("background: transparent");
    this->setFrameShape(QFrame::Shape(0));
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    this->scene = new QGraphicsScene_custom(this);
    this->setAlignment(Qt::AlignCenter);
    this->setScene(scene);
    this->viewport()->setObjectName("customGraphicsView_viewport");
    this->pixmap_item = new QGraphicsPixmapItem();
    this->pixmap_item->setTransformationMode(Qt::SmoothTransformation);
    this->scene->addItem(this->pixmap_item);
    this->imgpath = imgpath;
    if (!imgpath.isEmpty()) {
        this->original_pixmap = utils::openImage(this->imgpath);
    }
}
customGraphicsView::~customGraphicsView() {
    delete this->pixmap_item;
    delete this->scene;
}
void customGraphicsView::resizeEvent(QResizeEvent* e) {
    if (!this->original_pixmap.isNull()){
        this->resizeImage();
    }
    else {
        QGraphicsView::resizeEvent(e);
    }
}
void customGraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
    emit mouseClicked(event->button(),event->modifiers());
    QGraphicsView::mouseReleaseEvent(event);
}
void customGraphicsView::setPixmap(QPixmap pixmap) {
    this->pixmap_item->setPixmap(pixmap);
    this->setSceneRect(this->pixmap_item->sceneBoundingRect());
    this->horizontalScrollBar()->setValue((pixmap.width() - this->width()) / 2);
}
QPixmap customGraphicsView::getResizedPixmap(QPixmap pixmap) {
    return pixmap.scaled(pixmap.width(), this->height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}
QPixmap customGraphicsView::getResizedPixmapFast(QPixmap pixmap) {
    return pixmap.scaled(pixmap.width(), this->height(), Qt::KeepAspectRatio, Qt::FastTransformation);
}
QPixmap customGraphicsView::getResizedPixmap(QPixmap *pixmap) {
    return pixmap->scaled(pixmap->width(), this->height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}
QPixmap customGraphicsView::getResizedPixmapFast(QPixmap *pixmap) {
    return pixmap->scaled(pixmap->width(), this->height(), Qt::KeepAspectRatio, Qt::FastTransformation);
}

void customGraphicsView::flipPixmap() {
    this->pixmap_item->setPixmap(this->pixmap_item->pixmap().transformed(QTransform().scale(-1, 1)));
    this->setSceneRect(this->pixmap_item->sceneBoundingRect());
    this->horizontalScrollBar()->setValue((this->pixmap_item->pixmap().width() - this->width()) / 2);
}

color_area customGraphicsView::getColor(bool weighted) {
    color_area color;
    if (weighted) {
        if (!this->accepted_colors.isEmpty())
            color = utils::get_weighted_random_color(this->accepted_colors);
        else if (!this->rejected_colors.isEmpty())
            color = *utils::select_randomly(this->rejected_colors.begin(), this->rejected_colors.end());
    }
    else {
        for (color_area& c : this->accepted_colors) {
            if (c.area_percent > color.area_percent)
                color = color_area(c);
        }
        for (color_area& c : this->rejected_colors) {
            if (c.area_percent > color.area_percent)
                color = color_area(c);
        }
    }
    return color;
}

void customGraphicsView::setImage(ImageData data, bool flip) {
    this->clearColors();
    this->imgpath = data.path;
    this->accepted_colors = data.accepted_colors;
    this->rejected_colors = data.rejected_colors;
    //delete this->original_pixmap;
    QPixmap img = utils::openImage(this->imgpath);
    if (flip) {
        img = img.transformed(QTransform().scale(-1, 1));
    }
    this->original_pixmap = img;
    this->resizeImage();
}

void customGraphicsView::setImage(QString path, bool flip) {
    this->clearColors();
    this->imgpath = path;
    //delete this->original_pixmap;
    QPixmap img = utils::openImage(path);
    if (flip){
        img = img.transformed(QTransform().scale(-1, 1));
    }
    this->original_pixmap = img;
    this->resizeImage();
}

void customGraphicsView::setImage(QPixmap pixmap, bool flip) {
    this->clearColors();
    //delete this->original_pixmap;
    if (flip) {
        pixmap = pixmap.transformed(QTransform().scale(-1, 1));
    }
    this->original_pixmap = pixmap;
    this->resizeImage();
}

void customGraphicsView::setImage(QString path, QPixmap pixmap, bool flip) {
    this->clearColors();
    this->imgpath = path;
    this->setImage(pixmap, flip);
}

void customGraphicsView::clearColors() {
    this->accepted_colors.clear();
    this->rejected_colors.clear();
}

void customGraphicsView::resizeImage(){
    if (!this->original_pixmap.isNull()) {
        this->setPixmap(this->getResizedPixmap(this->original_pixmap));
    }
}