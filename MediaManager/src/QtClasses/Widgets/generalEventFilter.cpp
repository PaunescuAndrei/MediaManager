#include "stdafx.h"
#include "generalEventFilter.h"
#include "MainApp.h"

bool generalEventFilter::eventFilter(QObject* object, QEvent* event)
{
    if (object->inherits("QWidgetWindow") && event->type() == QEvent::MouseButtonPress) {
        if (this->mApp && this->mApp->soundPlayer) {
            if (object->objectName() != "LogDialogWindow" && object->objectName() != "LogQMenuClassWindow" && object->objectName() != "TrayContextMenuWindow")
                this->mApp->soundPlayer->playSoundEffect();
        }
    }
    return QObject::eventFilter(object, event);
}
