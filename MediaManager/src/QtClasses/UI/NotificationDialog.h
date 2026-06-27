#pragma once
#include <QWidget>
#include <chrono>
#include <QTimer>

class MainWindow;
class QLabel;
class QProgressBar;
class starEditorWidget;
class ProgressBarQLabel;

enum class NotificationType {
	VideoInfo,
	GeneralMessage,
	GoalMet
};

class NotificationWidget :
	public QWidget
{
	Q_OBJECT
public:
	QTimer* timer;
	QTimer* timer2;
	MainWindow *MW = nullptr;
	int timerInterval = 1000;
	std::chrono::time_point<std::chrono::steady_clock> time_start = std::chrono::steady_clock::now();
	std::chrono::milliseconds time_duration = std::chrono::milliseconds(10000);
	NotificationWidget(NotificationType type, QWidget* parent = nullptr);
	void setMainWindow(MainWindow* MW);
	void closeNotification();
	void showNotification();
	void showNotification(int duration, int interval);
	void pauseNotification();
	~NotificationWidget();
	void mousePressEvent(QMouseEvent* event) override;

	NotificationType type() const { return type_; }

	// Populate content based on preset type
	void populateVideoInfo(MainWindow* mw);
	void populateGeneralMessage(const QString& title, const QString& message);
	void populateGoalMet(const QString& title, const QString& message);

signals:
	void showEventSignal();

private:
	NotificationType type_;
	bool paused = false;

	// Shared widgets
	QWidget* contentWidget_ = nullptr;
	QProgressBar* durationProgressBar_ = nullptr;

	// Video info widgets (matching original rich layout)
	ProgressBarQLabel* totalLabel_ = nullptr;
	QLabel* authorLabel_ = nullptr;
	QLabel* nameLabel_ = nullptr;
	QLabel* tagsLabel_ = nullptr;
	QLabel* bpmLabel_ = nullptr;
	starEditorWidget* rating_ = nullptr;
	QLabel* starsLabel_ = nullptr;
	QLabel* lastWatchedLabel_ = nullptr;
	QLabel* lastWatchedValueLabel_ = nullptr;
	QLabel* viewsLabel_ = nullptr;
	ProgressBarQLabel* counterLabel_ = nullptr;

	// Simple layout widgets (GeneralMessage / GoalMet)
	QLabel* iconLabel_ = nullptr;
	QLabel* titleLabel_ = nullptr;
	QLabel* messageLabel_ = nullptr;

	void buildLayout();
	void buildVideoInfoContent();
	void buildSimpleContent();
};
