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
#include <QtMath>
#include "MainApp.h"

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
        this->updateContentCenterFromOriginal();
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
    if (qMainApp && qMainApp->config && !qMainApp->config->get_bool("mascots_center_content")) {
        this->horizontalScrollBar()->setValue((pixmap.width() - this->width()) / 2);
        this->has_scaled_center = false;
        this->scaled_center_x = 0.0;
        return;
    }
    double scaledCenter = 0.0;
    if (this->scaledCenterForPixmap(pixmap, &scaledCenter)) {
        this->has_scaled_center = true;
        this->scaled_center_x = scaledCenter;
        this->horizontalScrollBar()->setValue(qRound(scaledCenter - (static_cast<double>(this->width()) / 2.0)));
    }
    else {
        this->has_scaled_center = false;
        this->scaled_center_x = 0.0;
        this->horizontalScrollBar()->setValue((pixmap.width() - this->width()) / 2);
    }
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
    if (qMainApp && qMainApp->config && !qMainApp->config->get_bool("mascots_center_content")) {
        this->horizontalScrollBar()->setValue((this->pixmap_item->pixmap().width() - this->width()) / 2);
        this->has_scaled_center = false;
        this->scaled_center_x = 0.0;
        return;
    }
    QPixmap current = this->pixmap_item->pixmap();
    if (this->has_scaled_center) {
        double flippedCenter = (static_cast<double>(current.width()) - 1.0) - this->scaled_center_x;
        this->scaled_center_x = flippedCenter;
        this->horizontalScrollBar()->setValue(qRound(flippedCenter - (static_cast<double>(this->width()) / 2.0)));
    }
    else {
        this->horizontalScrollBar()->setValue(this->horizontalScrollForPixmapAlpha(current));
    }
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
    this->updateContentCenterFromOriginal();
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
    this->updateContentCenterFromOriginal();
    this->resizeImage();
}

void customGraphicsView::setImage(QPixmap pixmap, bool flip) {
    this->clearColors();
    //delete this->original_pixmap;
    if (flip) {
        pixmap = pixmap.transformed(QTransform().scale(-1, 1));
    }
    this->original_pixmap = pixmap;
    this->updateContentCenterFromOriginal();
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

bool customGraphicsView::findAlphaBoundsX(const QImage& image, int alphaThreshold, int* minX, int* maxX) {
    if (image.isNull())
        return false;
    int min_bound = image.width();
    int max_bound = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) > alphaThreshold) {
                if (x < min_bound)
                    min_bound = x;
                if (x > max_bound)
                    max_bound = x;
            }
        }
    }
    if (max_bound < 0)
        return false;
    *minX = min_bound;
    *maxX = max_bound;
    return true;
}

bool customGraphicsView::computeWeightedCenterXFromAlpha(const QImage& image, int alphaThreshold, double* centerX) {
    if (image.isNull())
        return false;
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0)
        return false;

    const int inf = width + height + 10;
    const int size = width * height;
    QVector<int> dist(size, inf);

    auto index = [width](int x, int y) { return y * width + x; };

    for (int y = 0; y < height; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            if (qAlpha(line[x]) <= alphaThreshold)
                dist[index(x, y)] = 0;
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int& d = dist[index(x, y)];
            if (d == 0)
                continue;
            if (x > 0)
                d = std::min(d, dist[index(x - 1, y)] + 1);
            if (y > 0)
                d = std::min(d, dist[index(x, y - 1)] + 1);
            if (x > 0 && y > 0)
                d = std::min(d, dist[index(x - 1, y - 1)] + 2);
            if (x + 1 < width && y > 0)
                d = std::min(d, dist[index(x + 1, y - 1)] + 2);
        }
    }

    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) {
            int& d = dist[index(x, y)];
            if (d == 0)
                continue;
            if (x + 1 < width)
                d = std::min(d, dist[index(x + 1, y)] + 1);
            if (y + 1 < height)
                d = std::min(d, dist[index(x, y + 1)] + 1);
            if (x + 1 < width && y + 1 < height)
                d = std::min(d, dist[index(x + 1, y + 1)] + 2);
            if (x > 0 && y + 1 < height)
                d = std::min(d, dist[index(x - 1, y + 1)] + 2);
        }
    }

    double sumW = 0.0;
    double sumX = 0.0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int d = dist[index(x, y)];
            if (d > 0 && d < inf) {
                double weight = static_cast<double>(d);
                sumW += weight;
                sumX += weight * static_cast<double>(x);
            }
        }
    }

    if (sumW <= 0.0)
        return false;
    *centerX = sumX / sumW;
    return true;
}

bool customGraphicsView::scaledCenterForPixmap(const QPixmap& pixmap, double* scaledCenter) const {
    if (pixmap.isNull())
        return false;
    if (!this->has_content_center || this->original_pixmap.isNull() || this->original_pixmap.width() <= 0)
        return false;
    double scale = static_cast<double>(pixmap.width()) / static_cast<double>(this->original_pixmap.width());
    *scaledCenter = this->content_center_x * scale;
    return true;
}

void customGraphicsView::updateContentCenterFromOriginal() {
    this->has_content_center = false;
    this->content_center_x = 0.0;
    if (this->original_pixmap.isNull())
        return;
    QImage image = this->original_pixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int alphaThreshold = 77; // 30% alpha
    double centerX = 0.0;
    if (computeWeightedCenterXFromAlpha(image, alphaThreshold, &centerX)) {
        this->has_content_center = true;
        this->content_center_x = centerX;
        return;
    }
    int minX = 0;
    int maxX = 0;
    if (findAlphaBoundsX(image, alphaThreshold, &minX, &maxX)) {
        this->has_content_center = true;
        this->content_center_x = (static_cast<double>(minX) + static_cast<double>(maxX)) / 2.0;
    }
}

int customGraphicsView::horizontalScrollForContent(const QPixmap& pixmap) const {
    if (pixmap.isNull())
        return 0;
    double scaledCenter = 0.0;
    if (!this->scaledCenterForPixmap(pixmap, &scaledCenter)) {
        return (pixmap.width() - this->width()) / 2;
    }
    return qRound(scaledCenter - (static_cast<double>(this->width()) / 2.0));
}

int customGraphicsView::horizontalScrollForPixmapAlpha(const QPixmap& pixmap) const {
    if (pixmap.isNull())
        return 0;
    QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int alphaThreshold = 77; // 30% alpha
    double centerX = 0.0;
    if (!computeWeightedCenterXFromAlpha(image, alphaThreshold, &centerX)) {
        int minX = 0;
        int maxX = 0;
        if (!findAlphaBoundsX(image, alphaThreshold, &minX, &maxX)) {
            return (pixmap.width() - this->width()) / 2;
        }
        centerX = (static_cast<double>(minX) + static_cast<double>(maxX)) / 2.0;
    }
    return qRound(centerX - (static_cast<double>(this->width()) / 2.0));
}

void customGraphicsView::resizeImage(){
    if (!this->original_pixmap.isNull()) {
        this->setPixmap(this->getResizedPixmap(this->original_pixmap));
    }
}
