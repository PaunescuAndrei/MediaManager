#include "stdafx.h"
#include "QCustomButton.h"
#include <QEvent>
#include <Qt>

QCustomButton::QCustomButton(QWidget* parent) : QPushButton(parent)
{
}

QCustomButton::QCustomButton(const QString& text, QWidget* parent) : QPushButton(parent)
{
}

void QCustomButton::mouseReleaseEvent(QMouseEvent* e)
{
    QPushButton::mouseReleaseEvent(e);
    if (e->button() == Qt::RightButton) {
        emit rightClicked();
    }
    else if (e->button() == Qt::MiddleButton) {
        emit middleClicked();
    }
}

QCustomButton::~QCustomButton()
{
}
