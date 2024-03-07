#include "stdafx.h"
#include "watchedFrame.h"
#include "MainWindow.h"
#include <QEvent>
#include "utils.h"

watchedFrame::watchedFrame(QWidget* parent) : QFrame(parent) {

}
void watchedFrame::setMainWindow(MainWindow* window) {
	this->MW = window;
}
void watchedFrame::mousePressEvent(QMouseEvent* e) {
	QFrame::mousePressEvent(e);
	if (e->button() == Qt::LeftButton || e->button() == Qt::RightButton) {
		this->MW->addWatchedDialogButton();
	}
};

watchedFrame::~watchedFrame() {
}