#pragma once
#include <QObject>
#include <QList>
#include <QPointer>
#include "NotificationDialog.h"

class MainWindow;

class NotificationManager : public QObject
{
	Q_OBJECT
public:
	explicit NotificationManager(MainWindow* mw);
	~NotificationManager();

	void showVideoInfo();
	void resumeVideoInfo(QPointer<NotificationDialog> dialog);
	void showGeneralMessage(const QString& title, const QString& message);
	void showGoalMet(const QString& title, const QString& message);
	void closeAll();

private slots:
	void onNotificationDestroyed(QObject* obj);
	void repositionAll();

private:
	NotificationDialog* createNotification(NotificationType type);
	void insertNotification(NotificationDialog* dialog);
	QPoint calculateTopCenterPosition(int index) const;

	MainWindow* mw_;
	QList<QPointer<NotificationDialog>> activeNotifications_;
};
