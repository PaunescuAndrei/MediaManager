#include "stdafx.h"
#include "NotificationDialog.h"
#include "MainWindow.h"
#include "utils.h"

NotificationDialog::NotificationDialog(QWidget* parent) : QDialog(parent)
{
	ui.setupUi(this);
	this->ui.rating->setStarPixelSize(17);
	this->setWindowModality(Qt::NonModal);
	this->ui.rating->setEditMode(starEditorWidget::EditMode::NoEdit);
	this->ui.durationProgressBar->setMaximum(this->time_duration.count());
	this->timer = new QTimer(this);
	connect(this->timer, &QTimer::timeout, this, [this] {
		auto elapsed = std::chrono::steady_clock::now() - this->time_start;
		this->ui.durationProgressBar->setValue(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
		this->ui.durationProgressBar->update();
		if (elapsed >= this->time_duration) {
			this->timer2->stop();
			this->closeNotification();
		}
	});
	this->timer2 = new QTimer(this);
	connect(this->timer2, &QTimer::timeout, this, [this] {
		if (!utils::IsMyWindowVisible(this)) {
			this->raise();
			this->show();
		}
	});
}

void NotificationDialog::setMainWindow(MainWindow* MW) {
	this->MW = MW;
}

void NotificationDialog::closeNotification() {
	this->timer->stop();
	this->close();
	if (this) {
		this->deleteLater();
	}
}

void NotificationDialog::showNotification()
{
	this->time_start = std::chrono::steady_clock::now();
	this->show();
	this->timer->start(this->timerInterval);
	this->timer2->start(250);
}

void NotificationDialog::showNotification(int duration, int interval)
{
	this->time_duration = std::chrono::milliseconds(duration);
	this->timerInterval = interval;
	this->showNotification();
}

void NotificationDialog::resizeEvent(QResizeEvent* event)
{
	QDialog::resizeEvent(event);
	//emit showEventSignal();
}

NotificationDialog::~NotificationDialog()
{
	this->timer->deleteLater();
	this->timer2->deleteLater();
}

void NotificationDialog::mousePressEvent(QMouseEvent* event)
{
	event->accept();
	this->closeNotification();
	//return QWidget::mousePressEvent(event);
}
