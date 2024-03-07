#pragma once
#include <QPainter>
#include <QPolygonF>
#include <QSize>
#include <QMetaType>
#include <QStyle>

class StarRating
{
public:
    enum class EditMode { Editable, ReadOnly };
    QIcon* active = nullptr;
    QIcon* halfactive = nullptr;
    QIcon* inactive = nullptr;
    StarRating(QIcon* active = nullptr, QIcon* halfactive = nullptr, QIcon* inactive = nullptr,double starCount = 1, double maxStarCount = 5);
    ~StarRating() {};

    void paint(QPainter *painter, const QRect &rect, const QPalette &palette, QStyle::State state = QStyle::StateFlag::State_None, EditMode mode = EditMode::Editable) const;
    QSize sizeHint() const;
    double starCount() const { return myStarCount; }
    double maxStarCount() const { return myMaxStarCount; }
    void setStarCount(double starCount) { myStarCount = starCount; }
    void setMaxStarCount(double maxStarCount) { myMaxStarCount = maxStarCount; }

private:
    QPolygonF starPolygon;
    QPolygonF diamondPolygon;
    double myStarCount;
    double myMaxStarCount;
};
Q_DECLARE_METATYPE(StarRating)
