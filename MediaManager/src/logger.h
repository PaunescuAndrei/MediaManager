#pragma once
#include <QString>
#include <QList>
#include <QObject>
#include <QMutex>

struct log_message
{
	QString message;
	QString date;
	QString type;
	QString extra_data;
};

class Logger : public QObject {
	Q_OBJECT
public:
	Logger(QObject* parent = nullptr, int maxsize = 100);
	void log(QString message, QString type, QString extra_data = QString());
	QList<log_message> getLogs() const;
	~Logger();
	int max_size = 0;
	QList<log_message> logs;
signals:
	void newLog(log_message newLog);
private:
	mutable QMutex m_mutex;
};