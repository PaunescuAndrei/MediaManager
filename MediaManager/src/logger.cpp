#include "stdafx.h"
#include "logger.h"
#include <QDateTime>
#include "MainApp.h"

Logger::Logger(QObject* parent, int maxsize) : QObject(parent)
{
	this->max_size = maxsize;
	this->logs = QList<log_message>();
}

void Logger::log(QString message,QString type, QString extra_data)
{
	if (this->logs.size() >= this->max_size)
		this->logs.removeFirst();
	log_message new_message = { message, QDateTime::currentDateTime().toString("dd.MM.yyyy hh:mm:ss"), type, extra_data};
	this->logs.append(new_message);
	if(qMainApp && qMainApp->debug_mode == true)
		qDebug().noquote() << QDateTime::currentDateTime().toString("dd.MM.yyyy hh:mm:ss") + " " + message;
	emit newLog(new_message);
}

Logger::~Logger()
{
}
