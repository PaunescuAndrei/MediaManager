#include "stdafx.h"
#include "WheelEventFilter.h"
#include <QWheelEvent>

WheelEventFilter::WheelEventFilter(QObject* parent) : QObject(parent) {}

bool WheelEventFilter::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        QSlider* slider = qobject_cast<QSlider*>(obj);
        if (slider && !slider->hasFocus()) {
            event->ignore(); // Ignore wheel event if the slider is not focused
            return true;     // Stop event propagation
        }
    }
    return QObject::eventFilter(obj, event);
}