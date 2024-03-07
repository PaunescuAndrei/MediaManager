#include "stdafx.h"
#include "quitEaterEventFilter.h"
#include "MainApp.h"

QuitEater::QuitEater(QObject* parent) : QObject(parent)
{
}

bool QuitEater::eventFilter(QObject* obj, QEvent* event)
{
	if (event->type() == QEvent::Quit) {
		MainApp* App = (MainApp*)obj;
		if (App->ready_to_quit)
			return QObject::eventFilter(obj, event);
		else {
			App->stop_handle();
			return true;
		}
	}
	else {
		return QObject::eventFilter(obj, event);
	}
}