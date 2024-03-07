#include "stdafx.h"
#include "QCustomLabel.h"
#include "MainWindow.h"
#include "utils.h"
#include <QLabel>
#include <QEvent>
#include "MainApp.h"

QCustomLabel::QCustomLabel(QWidget* parent) : QLabel(parent) {

}
void QCustomLabel::mousePressEvent(QMouseEvent* e) {
	QLabel::mousePressEvent(e);
	if (e->button() == Qt::LeftButton) {
		emit clicked();
	}
	else if (e->button() == Qt::RightButton) {
		emit rightClicked();
	}
	else if (e->button() == Qt::MiddleButton) {
		emit middleClicked();
	}
}
void QCustomLabel::wheelEvent(QWheelEvent* e) {
	QLabel::wheelEvent(e);
	if (e->angleDelta().y() == -120) {
		emit scrollDown();
	}
	if (e->angleDelta().y() == 120) {
		emit scrollUp();
	}
}

QCustomLabel::~QCustomLabel() {

}