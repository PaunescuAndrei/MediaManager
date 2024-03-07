#include "stdafx.h"
#include "starRating.h"
#include <QtWidgets>
#include <cmath>

constexpr int PaintingScaleFactor = 15;

StarRating::StarRating(QIcon* active, QIcon* halfactive, QIcon* inactive, double starCount, double maxStarCount)
    : myStarCount(starCount),
      myMaxStarCount(maxStarCount)
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
    return QSize((PaintingScaleFactor+0.1)*myMaxStarCount, PaintingScaleFactor*1);
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
            painter->drawPixmap(rect.topLeft(), this->active->pixmap(QSize(PaintingScaleFactor, PaintingScaleFactor)));
        }
        else if (myStarCount > i && myStarCount <= i + 0.5) {
            painter->drawPixmap(rect.topLeft(), this->halfactive->pixmap(QSize(PaintingScaleFactor, PaintingScaleFactor)));
        }
        else if (myStarCount < i + 1) {
            if (i == 0 && myStarCount > 0)
                painter->drawPixmap(rect.topLeft(), this->halfactive->pixmap(QSize(PaintingScaleFactor, PaintingScaleFactor)));
            else
                painter->drawPixmap(rect.topLeft(), this->inactive->pixmap(QSize(PaintingScaleFactor, PaintingScaleFactor)));
        }
        //    painter->drawPolygon(starPolygon, Qt::WindingFill);
        //else if (mode == EditMode::Editable)
        //    painter->drawPolygon(diamondPolygon, Qt::WindingFill);
        painter->translate(PaintingScaleFactor, 0.0);
    }

    painter->restore();
}
