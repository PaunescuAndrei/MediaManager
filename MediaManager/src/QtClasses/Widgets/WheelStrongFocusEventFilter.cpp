#include "stdafx.h"
#include "WheelStrongFocusEventFilter.h"

WheelStrongFocusEventFilter::WheelStrongFocusEventFilter(QObject* parent)
    : QObject(parent)
{
}

bool WheelStrongFocusEventFilter::eventFilter(QObject* o, QEvent* e)
{
    const QWidget* widget = static_cast<QWidget*>(o);
    if (e->type() == QEvent::Wheel && widget && !widget->hasFocus())  
    {
        e->ignore();
        return true;
    }

    return QObject::eventFilter(o, e);
}