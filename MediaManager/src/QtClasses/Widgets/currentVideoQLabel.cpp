#include "stdafx.h"
#include "currentVideoQLabel.h"
#include "utils.h"
#include <QLabel>
#include <QEvent>
#include "MainApp.h"

currentVideoQLabel::currentVideoQLabel(QWidget* parent) : customQLabel(parent) {

}

void currentVideoQLabel::setValues(int id, QString path, QString name, QString author, QString tags) {
	this->id = id;
	{
		// Only `path` is read from the watcher thread; the rest are GUI-only.
		QMutexLocker lock(&this->path_lock);
		this->path = path;
	}
	this->name = name;
	this->author = author;
	this->tags = tags;
	this->updateText();
}

QString currentVideoQLabel::currentPath() const {
	QMutexLocker lock(&this->path_lock);
	return this->path;
}

void currentVideoQLabel::setValues(std::tuple<int, QString, QString, QString, QString> values) {
	this->setValues(std::get<0>(values), std::get<1>(values), std::get<2>(values), std::get<3>(values), std::get<4>(values));
}

void currentVideoQLabel::updateText() {
	if (toggled_path == false and !this->name.isEmpty())
		this->setText(name);
	else
		this->setText(path);
}

void currentVideoQLabel::mousePressEvent(QMouseEvent* e) {
	customQLabel::mousePressEvent(e);
	if (e->button() == Qt::LeftButton || e->button() == Qt::RightButton) {
		this->toggled_path = !toggled_path;
		this->updateText();
	}
}
currentVideoQLabel::~currentVideoQLabel() {

}