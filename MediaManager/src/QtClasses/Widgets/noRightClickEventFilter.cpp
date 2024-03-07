#include "stdafx.h"
#include "noRightClickEventFilter.h"

bool noRightClickEventFilter::eventFilter(QObject* object, QEvent* event)
{
	//QMenu* menu = dynamic_cast<QMenu*>(object);
	//qDebug() << event->type();
	if (event->type() == QEvent::MouseButtonPress or event->type() == QEvent::MouseButtonRelease)
	{
		QMouseEvent* ev = dynamic_cast<QMouseEvent*>(event);
		if (ev)
		{
			if (ev->button() == Qt::RightButton)
			{
				ev->ignore();
				return true;
			}
		}
	}
    return QObject::eventFilter(object, event);
}
