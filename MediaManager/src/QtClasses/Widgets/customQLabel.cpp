#include "stdafx.h"
#include "customQLabel.h"
#include "MainWindow.h"
#include "utils.h"
#include <QLabel>
#include <QEvent>
#include "MainApp.h"

customQLabel::customQLabel(QWidget* parent) : QLabel(parent) {

}

void customQLabel::setText(const QString& text) {
	const QString old = this->text();
	QLabel::setText(text);
	if (old != text) {
		emit textChanged(text);
	}
}
void customQLabel::mousePressEvent(QMouseEvent* e) {
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
void customQLabel::wheelEvent(QWheelEvent* e) {
	QLabel::wheelEvent(e);
	if (e->angleDelta().y() == -120) {
		emit scrollDown();
	}
	if (e->angleDelta().y() == 120) {
		emit scrollUp();
	}
}

customQLabel::~customQLabel() {

}