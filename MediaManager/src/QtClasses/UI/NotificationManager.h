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
	void resumeVideoInfo(QPointer<NotificationWidget> dialog);
	void showGeneralMessage(const QString& title, const QString& message);
	void showGoalMet(const QString& title, const QString& message);
	void closeAll();

private slots:
	void onNotificationDestroyed(QObject* obj);
	void repositionAll();

private:
	NotificationWidget* createNotification(NotificationType type);
	void insertNotification(NotificationWidget* dialog);
	QPoint calculateTopCenterPosition(int index) const;

	MainWindow* mw_;
	QList<QPointer<NotificationWidget>> activeNotifications_;
};
