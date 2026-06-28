#include "stdafx.h"
#include "NotificationManager.h"
#include "MainWindow.h"
#include "MainApp.h"

NotificationManager::NotificationManager(MainWindow* mw)
	: QObject(mw), mw_(mw)
{
}

NotificationManager::~NotificationManager()
{
	closeAll();
}

NotificationWidget* NotificationManager::createNotification(NotificationType type)
{
	NotificationWidget* dialog = new NotificationWidget(type, nullptr);
	dialog->setMainWindow(mw_);

	// Track this notification and reposition when it closes
	connect(dialog, &QObject::destroyed, this, &NotificationManager::onNotificationDestroyed);

	// Set max width based on screen
	QScreen* screen = QGuiApplication::primaryScreen();
	if (screen) {
		QRect screenGeometry = screen->geometry();
		if (type == NotificationType::VideoInfo) {
			dialog->setMaximumWidth(static_cast<int>(screenGeometry.width() * 0.98));
		} else {
			dialog->setMaximumWidth(static_cast<int>(screenGeometry.width() * 0.6));
			dialog->setMinimumWidth(300);
		}
	}

	return dialog;
}

void NotificationManager::insertNotification(NotificationWidget* dialog)
{
	// Clean up any nullptrs from destroyed dialogs
	activeNotifications_.erase(
		std::remove_if(activeNotifications_.begin(), activeNotifications_.end(),
			[](const QPointer<NotificationWidget>& ptr) { return ptr.isNull(); }),
		activeNotifications_.end()
	);

	activeNotifications_.append(dialog);
	// Reposition happens after showNotification() so layout is calculated
}

void NotificationManager::showVideoInfo()
{
	if (!mw_->App->config->get_bool("notification_video_info_enabled"))
		return;

	int durationMs = mw_->App->config->get("notification_video_info_duration_ms").toInt();

	NotificationWidget* dialog = createNotification(NotificationType::VideoInfo);
	dialog->populateVideoInfo(mw_);
	insertNotification(dialog);
	dialog->showNotification(durationMs, 5);
	repositionAll();
}

void NotificationManager::resumeVideoInfo(QPointer<NotificationWidget> dialog)
{
	if (!dialog)
		return;
	int durationMs = mw_->App->config->get("notification_video_info_duration_ms").toInt();
	dialog->populateVideoInfo(mw_);
	dialog->showNotification(durationMs, 5);
	repositionAll();
}

void NotificationManager::showGeneralMessage(const QString& title, const QString& message)
{
	if (!mw_->App->config->get_bool("notification_milestone_enabled"))
		return;

	int durationMs = mw_->App->config->get("notification_milestone_duration_ms").toInt();
	NotificationWidget* dialog = createNotification(NotificationType::GeneralMessage);
	dialog->populateGeneralMessage(title, message);
	insertNotification(dialog);
	dialog->showNotification(durationMs, 5);
	repositionAll(); // after show so adjustSize works correctly
}

void NotificationManager::showGoalMet(const QString& title, const QString& message)
{
	if (!mw_->App->config->get_bool("notification_goal_met_enabled"))
		return;

	int durationMs = mw_->App->config->get("notification_goal_met_duration_ms").toInt();
	NotificationWidget* dialog = createNotification(NotificationType::GoalMet);
	dialog->populateGoalMet(title, message);
	insertNotification(dialog);
	dialog->showNotification(durationMs, 5);
	repositionAll(); // after show so adjustSize works correctly
}

void NotificationManager::closeAll()
{
	// Copy the list — closeNotification() triggers destroyed signal which modifies the list
	QList<QPointer<NotificationWidget>> copy = activeNotifications_;
	for (auto& ptr : copy) {
		if (ptr) {
			ptr->closeNotification();
		}
	}
	activeNotifications_.clear();
}

void NotificationManager::onNotificationDestroyed(QObject* obj)
{
	// Remove the destroyed dialog from the active list
	activeNotifications_.erase(
		std::remove_if(activeNotifications_.begin(), activeNotifications_.end(),
			[obj](const QPointer<NotificationWidget>& ptr) {
				return ptr.isNull() || ptr.data() == obj;
			}),
		activeNotifications_.end()
	);

	// Reposition remaining notifications
	repositionAll();
}

void NotificationManager::repositionAll()
{
	// Remove nullptrs
	activeNotifications_.erase(
		std::remove_if(activeNotifications_.begin(), activeNotifications_.end(),
			[](const QPointer<NotificationWidget>& ptr) { return ptr.isNull(); }),
		activeNotifications_.end()
	);

	if (activeNotifications_.isEmpty())
		return;

	QScreen* screen = QGuiApplication::primaryScreen();
	if (!screen)
		return;

	QRect screenGeometry = screen->geometry();
	const int topMargin = static_cast<int>(screenGeometry.height() * 0.03);
	const int gap = 4; // px between stacked notifications

	int currentY = topMargin;
	for (int i = 0; i < activeNotifications_.size(); ++i) {
		NotificationWidget* dialog = activeNotifications_[i];
		if (!dialog || !dialog->isVisible())
			continue;

		dialog->adjustSize();
		QSize hint = dialog->size();

		QRect newRect = QStyle::alignedRect(
			Qt::LeftToRight,
			Qt::AlignHCenter,
			hint,
			screenGeometry
		);
		newRect.moveTop(currentY);
		dialog->setGeometry(newRect);

		currentY += hint.height() + gap;
	}
}
