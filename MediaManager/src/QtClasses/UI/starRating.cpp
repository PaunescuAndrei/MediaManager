#include "stdafx.h"
#include "starRating.h"
#include <QtWidgets>
#include <cmath>

namespace {
constexpr int kDefaultStarPixelSize = 15;
}

StarRating::StarRating(QIcon* active, QIcon* halfactive, QIcon* inactive, double starCount, double maxStarCount)
    : myStarCount(starCount),
      myMaxStarCount(maxStarCount),
      paintingScaleFactor(kDefaultStarPixelSize)
{
    this->active = active;
    this->halfactive = halfactive;
    this->inactive = inactive;
    starPolygon << QPointF(1.0, 0.5);
    for (int i = 1; i < 5; ++i)
        starPolygon << QPointF(0.5 + 0.5 * std::cos(0.8 * i * 3.14),
                               0.5 + 0.5 * std::sin(0.8 * i * 3.14));

    diamondPolygon << QPointF(0.4, 0.5) << QPointF(0.5, 0.4)
                   << QPointF(0.6, 0.5) << QPointF(0.5, 0.6)
                   << QPointF(0.4, 0.5);
}

QSize StarRating::sizeHint() const
{
    const double width = (static_cast<double>(paintingScaleFactor) + 0.1) * myMaxStarCount;
    return QSize(static_cast<int>(std::ceil(width)), paintingScaleFactor);
}

void StarRating::paint(QPainter *painter, const QRect &rect, const QPalette &palette, QStyle::State state, EditMode mode) const
{
    if (!this->active)
        return;
    if (state & QStyle::State_Selected)
        painter->fillRect(rect, palette.highlight());
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    //painter->setBrush(mode == EditMode::Editable ? palette.highlight() : palette.windowText());

    //const int yOffset = (rect.height() - PaintingScaleFactor) / 2;
    //painter->translate(rect.x(), rect.y() + yOffset);
    //painter->scale(PaintingScaleFactor, PaintingScaleFactor);

    for (int i = 0; i < myMaxStarCount; i++) {
        painter->translate(0.1, 0.0);
        if (myStarCount >= i + 1) {
            painter->drawPixmap(rect.topLeft(), this->active->pixmap(QSize(paintingScaleFactor, paintingScaleFactor)));
        }
        else if (myStarCount > i && myStarCount <= i + 0.5) {
            painter->drawPixmap(rect.topLeft(), this->halfactive->pixmap(QSize(paintingScaleFactor, paintingScaleFactor)));
        }
        else if (myStarCount < i + 1) {
            if (i == 0 && myStarCount > 0)
                painter->drawPixmap(rect.topLeft(), this->halfactive->pixmap(QSize(paintingScaleFactor, paintingScaleFactor)));
            else
                painter->drawPixmap(rect.topLeft(), this->inactive->pixmap(QSize(paintingScaleFactor, paintingScaleFactor)));
        }
        //    painter->drawPolygon(starPolygon, Qt::WindingFill);
        //else if (mode == EditMode::Editable)
        //    painter->drawPolygon(diamondPolygon, Qt::WindingFill);
        painter->translate(paintingScaleFactor, 0.0);
    }

    painter->restore();
}

void StarRating::setStarPixelSize(int pixelSize) {
    if (pixelSize <= 0) {
        this->paintingScaleFactor = kDefaultStarPixelSize;
    } else {
        this->paintingScaleFactor = pixelSize;
    }
}
