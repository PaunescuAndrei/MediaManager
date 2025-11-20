#include "stdafx.h"
#include "starEditorWidget.h"
#include "starRating.h"
#include <QtWidgets>

starEditorWidget::starEditorWidget(QWidget *parent, QPersistentModelIndex item_index) : QWidget(parent)
{
    setMouseTracking(true);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    this->item_index = item_index;
}

QSize starEditorWidget::sizeHint() const
{
    const QSize starSize = myStarRating.sizeHint();
    const QMargins margins = contentsMargins();
    return starSize + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

QSize starEditorWidget::minimumSizeHint() const
{
    return sizeHint();
}

void starEditorWidget::setStarPixelSize(int pixelSize)
{
    myStarRating.setStarPixelSize(pixelSize);
    this->updateGeometry();
    this->update();
}

int starEditorWidget::starPixelSize() const
{
    return myStarRating.starPixelSize();
}

void starEditorWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    myStarRating.paint(&painter, rect(), palette(), (this->editing_finished == true) ? QStyle::StateFlag::State_None : QStyle::StateFlag::State_Selected, StarRating::EditMode::Editable);
}

void starEditorWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (this->edit_mode == starEditorWidget::EditMode::HoverClick) {
        const double star = starAtPosition(event->position().toPoint().x());

        if (star != -1) {
            myStarRating.setStarCount(star);
            update();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void starEditorWidget::leaveEvent(QEvent* event) {
    myStarRating.setStarCount(this->original_value);
    update();
    QWidget::leaveEvent(event);
}

void starEditorWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (this->edit_mode == starEditorWidget::EditMode::HoverClick) {
        if (rect().contains(event->localPos().toPoint())) {
            this->editing_finished = true;
            emit editingFinished();
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void starEditorWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    //emit doubleClicked();
    if (this->edit_mode == starEditorWidget::EditMode::DoubleClick) {
        const double star = starAtPosition(event->position().toPoint().x());
        if (star != -1) {
            myStarRating.setStarCount(star);
            update();
        }
        this->editing_finished = true;
        emit editingFinished();
    }
    QWidget::mouseDoubleClickEvent(event);
}

double starEditorWidget::starAtPosition(double x) const
{
    double star = (x / (myStarRating.sizeHint().width() / myStarRating.maxStarCount()));
    double fractional,whole;
    fractional = std::modf(star, &whole);
    if (fractional <= 0.33)
        star = whole;
    else if (fractional <= 0.66)
        star = whole + 0.5;
    else if (star < myStarRating.maxStarCount())
        star = whole + 1;
    else
        star = whole;
    if (star < 0 || star > myStarRating.maxStarCount())
        return -1;

    return star;
}
