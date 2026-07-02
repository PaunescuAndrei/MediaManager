#include "stdafx.h"
#include "NotificationDialog.h"
#include "MainWindow.h"
#include "MainApp.h"
#include <QPainter>
#include "utils.h"
#include "starEditorWidget.h"
#include "ProgressBarQLabel.h"

// Shared style constants — whiter and slightly bigger
static const char* TITLE_STYLE = "QLabel { font-size: 13pt; font-weight: bold; }";
static const char* BODY_STYLE = "QLabel { font-size: 12pt; }";
static const char* DETAIL_STYLE = "QLabel { font-size: 11pt; color: #AAAAAA; }";
static const int PROGRESS_BAR_HEIGHT = 5;

NotificationWidget::NotificationWidget(NotificationType type, QWidget* parent)
	: QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
	         | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint), type_(type)
{
	this->setAttribute(Qt::WA_ShowWithoutActivating, true);
	this->setAttribute(Qt::WA_TranslucentBackground, true);
	this->setWindowTitle(QStringLiteral("MediaManager Notification"));

	buildLayout();

	// Content opacity effect on the overlay container (all child widgets)
	if (overlayContainer_) {
		contentOpacityEffect_ = new QGraphicsOpacityEffect(overlayContainer_);
		contentOpacityEffect_->setOpacity(1.0);
		overlayContainer_->setGraphicsEffect(contentOpacityEffect_);
	}

	this->timer = new QTimer(this);
	connect(this->timer, &QTimer::timeout, this, [this] {
		auto elapsed = std::chrono::steady_clock::now() - this->time_start;
		this->durationProgressBar_->setProgress(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
		this->durationProgressBar_->update();
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

void NotificationWidget::buildLayout()
{
	QVBoxLayout* rootLayout = new QVBoxLayout(this);
	rootLayout->setContentsMargins(0, 0, 0, 0);
	rootLayout->setSpacing(0);

	// Single overlay container — content opacity applied here so ALL child
	// widgets (text, stars, progress bar, future additions) get it automatically
	overlayContainer_ = new QWidget(this);
	rootLayout->addWidget(overlayContainer_);

	QVBoxLayout* overlayLayout = new QVBoxLayout(overlayContainer_);
	overlayLayout->setContentsMargins(0, 0, 0, 0);
	overlayLayout->setSpacing(0);

	contentWidget_ = new QWidget(overlayContainer_);
	overlayLayout->addWidget(contentWidget_);

	durationProgressBar_ = new ProgressBarQLabel(overlayContainer_);
	durationProgressBar_->setText(QString());
	durationProgressBar_->setMinMax(0, static_cast<int>(this->time_duration.count()), false);
	durationProgressBar_->setProgress(0);
	durationProgressBar_->setFixedHeight(PROGRESS_BAR_HEIGHT);
	durationProgressBar_->vertical_orientation = false; // horizontal bar for timer
	overlayLayout->addWidget(durationProgressBar_);

	switch (type_) {
	case NotificationType::VideoInfo:
		buildVideoInfoContent();
		break;
	case NotificationType::GeneralMessage:
	case NotificationType::GoalMet:
	case NotificationType::StreakAtRisk:
	case NotificationType::PersonalBest:
		buildSimpleContent();
		break;
	}
}

void NotificationWidget::buildVideoInfoContent()
{
	// Outer: side bars + center content, matching the original .ui layout
	QHBoxLayout* outerLayout = new QHBoxLayout(contentWidget_);
	outerLayout->setContentsMargins(0, 0, 0, 0);
	outerLayout->setSpacing(0);

	// Left side — total video count
	totalLabel_ = new ProgressBarQLabel();
	totalLabel_->setText("");
	outerLayout->addWidget(totalLabel_);

	// Center — all the video metadata
	QWidget* centerWidget = new QWidget();
	QVBoxLayout* centerLayout = new QVBoxLayout(centerWidget);
	centerLayout->setContentsMargins(6, 4, 6, 4);
	centerLayout->setSpacing(1);

	authorLabel_ = new QLabel();
	authorLabel_->setStyleSheet(TITLE_STYLE);
	// author does NOT word wrap (matching original .ui)
	centerLayout->addWidget(authorLabel_);

	nameLabel_ = new QLabel();
	nameLabel_->setStyleSheet(TITLE_STYLE);
	// name does NOT word wrap (matching original .ui)
	centerLayout->addWidget(nameLabel_);

	tagsLabel_ = new QLabel();
	tagsLabel_->setStyleSheet(DETAIL_STYLE);
	tagsLabel_->setWordWrap(true);
	tagsLabel_->hide();
	centerLayout->addWidget(tagsLabel_);

	bpmLabel_ = new QLabel();
	bpmLabel_->setStyleSheet("QLabel { font-size: 12pt; font-weight: bold; }");
	bpmLabel_->hide();
	centerLayout->addWidget(bpmLabel_);

	// Stats row: rating stars + last watched + views
	QHBoxLayout* statsRow = new QHBoxLayout();
	statsRow->setSpacing(12);
	statsRow->setContentsMargins(0, 0, 0, 0);

	// Last watched section
	QHBoxLayout* lastWatchedRow = new QHBoxLayout();
	lastWatchedRow->setSpacing(4);
	lastWatchedRow->setContentsMargins(0, 0, 0, 0);
	lastWatchedLabel_ = new QLabel("Last W:");
	lastWatchedLabel_->setStyleSheet(DETAIL_STYLE);
	lastWatchedRow->addWidget(lastWatchedLabel_);
	lastWatchedValueLabel_ = new QLabel("Never");
	lastWatchedValueLabel_->setStyleSheet("QLabel { font-size: 11pt; font-weight: bold; }");
	lastWatchedRow->addWidget(lastWatchedValueLabel_);
	statsRow->addLayout(lastWatchedRow);

	statsRow->addStretch();

	// Rating section
	rating_ = new starEditorWidget();
	rating_->setStarPixelSize(17);
	rating_->setEditMode(starEditorWidget::EditMode::NoEdit);
	statsRow->addWidget(rating_);

	starsLabel_ = new QLabel();
	starsLabel_->setStyleSheet("QLabel { font-size: 11pt; font-weight: bold; }");
	statsRow->addWidget(starsLabel_);

	statsRow->addStretch();

	// Views
	viewsLabel_ = new QLabel();
	viewsLabel_->setStyleSheet("QLabel { font-size: 11pt; font-weight: bold; }");
	viewsLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	statsRow->addWidget(viewsLabel_);

	centerLayout->addLayout(statsRow);

	outerLayout->addWidget(centerWidget, 1);

	// Right side — counter
	counterLabel_ = new ProgressBarQLabel();
	counterLabel_->setText("");
	outerLayout->addWidget(counterLabel_);
}

void NotificationWidget::buildSimpleContent()
{
	QVBoxLayout* layout = new QVBoxLayout(contentWidget_);
	layout->setContentsMargins(10, 8, 10, 8);
	layout->setSpacing(4);

	QHBoxLayout* headerRow = new QHBoxLayout();
	headerRow->setSpacing(6);
	headerRow->setContentsMargins(0, 0, 0, 0);

	iconLabel_ = new QLabel();
	iconLabel_->setFixedSize(24, 24);
	iconLabel_->setScaledContents(true);
	iconLabel_->hide();
	headerRow->addWidget(iconLabel_);

	titleLabel_ = new QLabel();
	titleLabel_->setStyleSheet(TITLE_STYLE);
	titleLabel_->hide();
	headerRow->addWidget(titleLabel_, 1);

	layout->addLayout(headerRow);

	messageLabel_ = new QLabel();
	messageLabel_->setStyleSheet(QString(BODY_STYLE) + QStringLiteral(" color: #AAAAAA;"));
	layout->addWidget(messageLabel_);
}

void NotificationWidget::setMainWindow(MainWindow* MW) {
	this->MW = MW;
}

void NotificationWidget::populateVideoInfo(MainWindow* mw)
{
	if (!authorLabel_ || !nameLabel_) return;

	// Side bars — copy from main window
	totalLabel_->copy(mw->ui.totalListLabel);
	counterLabel_->copy(mw->ui.counterLabel);

	// Author + Name
	authorLabel_->setText(mw->ui.currentVideo->author);
	nameLabel_->setText(mw->ui.currentVideo->name);

	// Tags
	if (mw->ui.currentVideo->tags.isEmpty()) {
		tagsLabel_->hide();
	} else {
		tagsLabel_->setText(mw->ui.currentVideo->tags);
		tagsLabel_->show();
	}

	// Fetch model data
	QString path = mw->ui.currentVideo->path;
	QPersistentModelIndex srcIdx = mw->modelIndexByPath(path);

	if (srcIdx.isValid()) {
		// BPM
		const QPersistentModelIndex bpmIdx = srcIdx.sibling(srcIdx.row(), ListColumns["BPM_COLUMN"]);
		double bpm = bpmIdx.data(CustomRoles::bpm).toDouble();
		if (bpm > 0) {
			bpmLabel_->setText(QString("%1 BPM").arg(qRound(bpm)));
			bpmLabel_->show();
		} else {
			bpmLabel_->hide();
		}

		// Rating stars
		const QPersistentModelIndex ratingIdx = srcIdx.sibling(srcIdx.row(), ListColumns["RATING_COLUMN"]);
		double stars = ratingIdx.data(CustomRoles::rating).toDouble();
		StarRating starRating = StarRating(mw->active, mw->halfactive, mw->inactive, 0, 5.0);
		starRating.setStarCount(stars);
		rating_->setStarRating(starRating);
		starsLabel_->setText(QString(" %1 ").arg(stars));

		// Views
		const QPersistentModelIndex viewsIdx = srcIdx.sibling(srcIdx.row(), ListColumns["VIEWS_COLUMN"]);
		viewsLabel_->setText(QString(" %1 Views ").arg(viewsIdx.data(Qt::DisplayRole).toString()));

		// Last watched
		const QPersistentModelIndex lastWatchedIdx = srcIdx.sibling(srcIdx.row(), ListColumns["LAST_WATCHED_COLUMN"]);
		lastWatchedValueLabel_->setText("Never");
		lastWatchedValueLabel_->setToolTip(QString());
		if (lastWatchedIdx.isValid()) {
			const QVariant lastWatchedData = lastWatchedIdx.data(Qt::DisplayRole);
			const QString lastWatchedRawText = lastWatchedData.toString();
			if (!lastWatchedRawText.isEmpty()) {
				QString lastWatchedDisplay;
				if (lastWatchedData.type() == QVariant::DateTime) {
					const QDateTime lastWatchedDateTime = lastWatchedData.toDateTime();
					if (lastWatchedDateTime.isValid()) {
						const QDateTime currentDateTime = QDateTime::currentDateTime();
						const qint64 secondsDiff = lastWatchedDateTime.secsTo(currentDateTime);
						if (secondsDiff < 0) {
							lastWatchedDisplay = lastWatchedRawText;
						} else {
							lastWatchedDisplay = utils::formatTimeAgo(secondsDiff);
							lastWatchedValueLabel_->setToolTip(lastWatchedRawText);
						}
					}
				} else {
					lastWatchedDisplay = lastWatchedRawText;
				}
				if (!lastWatchedDisplay.isEmpty()) {
					lastWatchedValueLabel_->setText(lastWatchedDisplay);
				}
			}
		}
	} else {
		bpmLabel_->hide();
		viewsLabel_->setText("");
	}
}

void NotificationWidget::populateGeneralMessage(const QString& title, const QString& message)
{
	if (!titleLabel_ || !messageLabel_) return;

	if (!title.isEmpty()) {
		titleLabel_->setText(title);
		titleLabel_->show();
	} else {
		titleLabel_->hide();
	}
	messageLabel_->setText(message);
}

void NotificationWidget::populateGoalMet(const QString& title, const QString& message)
{
	if (!titleLabel_ || !messageLabel_) return;

	titleLabel_->setText(title);
	titleLabel_->show();
	messageLabel_->setText(message);
}
void NotificationWidget::populateStreakAtRisk(const QString& title, const QString& message)
{
	populateGoalMet(title, message);
}

void NotificationWidget::populatePersonalBest(const QString& title, const QString& message)
{
	populateGoalMet(title, message);
}

void NotificationWidget::closeNotification() {
	this->timer->stop();
	this->close();
	this->paused = false;
	this->deleteLater();
}

void NotificationWidget::showNotification()
{
	this->time_start = std::chrono::steady_clock::now();
	this->paused = false;
	this->durationProgressBar_->setMinMax(0, static_cast<int>(this->time_duration.count()));
	this->durationProgressBar_->setProgress(0);

	// Apply configured opacities
	double bgOpacity = 1.0;
	double contentOpacity = 1.0;
	double timerBarOpacity = 1.0;
	double counterOpacity = 1.0;
	if (this->MW && this->MW->App && this->MW->App->config) {
		Config* cfg = this->MW->App->config;
		bgOpacity = cfg->get_double("notification_bg_opacity", 1.0);
		contentOpacity = cfg->get_double("notification_content_opacity", 1.0);
		timerBarOpacity = cfg->get_double("notification_timerbar_opacity", 1.0);
		counterOpacity = cfg->get_double("notification_counter_opacity", 1.0);
	}

	// Paint background manually (WA_TranslucentBackground skips system fill)
	m_bgAlpha = static_cast<int>(bgOpacity * 255.0);
	this->update();

	if (contentOpacityEffect_)
		contentOpacityEffect_->setOpacity(contentOpacity);

	// Timer bar and counter labels use painter opacity (text stays crisp)
	if (durationProgressBar_)
		durationProgressBar_->setBarBackgroundOpacity(timerBarOpacity);
	if (totalLabel_)
		totalLabel_->setBarBackgroundOpacity(counterOpacity);
	if (counterLabel_)
		counterLabel_->setBarBackgroundOpacity(counterOpacity);

	this->show();
	this->timer->start(this->timerInterval);
	this->timer2->start(250);
}

void NotificationWidget::showNotification(int duration, int interval)
{
	this->time_duration = std::chrono::milliseconds(duration);
	this->timerInterval = interval;
	this->showNotification();
}


NotificationWidget::~NotificationWidget()
{
	this->timer->deleteLater();
	this->timer2->deleteLater();
}

void NotificationWidget::mousePressEvent(QMouseEvent* event)
{
	event->accept();
	const bool isRightClick = event->button() == Qt::RightButton;
	if (isRightClick && this->MW) {
		this->pauseNotification();
		this->MW->showEndOfVideoDialog(true, true, this);
	} else {
		this->closeNotification();
	}
}

void NotificationWidget::pauseNotification()
{
	if (this->paused)
		return;
	this->timer->stop();
	this->timer2->stop();
	this->paused = true;
}

void NotificationWidget::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
	QPainter painter(this);
	QColor bgColor = this->palette().color(QPalette::Window);
	bgColor.setAlpha(m_bgAlpha);
	painter.fillRect(this->rect(), bgColor);
}
