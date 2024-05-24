#include "stdafx.h"
#include "customQButton.h"
#include <QEvent>
#include <Qt>

customQButton::customQButton(QWidget* parent) : QPushButton(parent)
{
}

customQButton::customQButton(const QString& text, QWidget* parent) : QPushButton(parent)
{
}

void customQButton::mouseReleaseEvent(QMouseEvent* e)
{
    QPushButton::mouseReleaseEvent(e);
    if (e->button() == Qt::RightButton) {
        emit rightClicked();
    }
    else if (e->button() == Qt::MiddleButton) {
        emit middleClicked();
    }
}

customQButton::~customQButton()
{
}
