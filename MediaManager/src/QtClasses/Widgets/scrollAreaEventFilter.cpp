#include "stdafx.h"
#include "scrollAreaEventFilter.h"
#include <QScrollArea>

bool scrollAreaEventFilter::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
        int delta = wheelEvent->angleDelta().y() / 120;
        QScrollArea* scrollarea = (QScrollArea*)obj;
        scrollarea->horizontalScrollBar()->setValue(scrollarea->horizontalScrollBar()->value() + delta * 20);
        return true;
    }
    return QObject::eventFilter(obj, event);
}