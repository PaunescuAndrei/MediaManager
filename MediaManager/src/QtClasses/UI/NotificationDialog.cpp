#include "stdafx.h"
#include "NotificationDialog.h"
#include "MainWindow.h"
#include "utils.h"
#include "starEditorWidget.h"
#include "ProgressBarQLabel.h"

// Shared style constants — whiter and slightly bigger
static const char* TITLE_STYLE = "QLabel { font-size: 13pt; font-weight: bold; }";
static const char* BODY_STYLE = "QLabel { font-size: 12pt; }";
static const char* DETAIL_STYLE = "QLabel { font-size: 11pt; color: #AAAAAA; }";
static const int PROGRESS_BAR_HEIGHT = 5;

NotificationDialog::NotificationDialog(NotificationType type, QWidget* parent) : QDialog(parent), type_(type)
{
	this->setWindowModality(Qt::WindowModal);
	this->setAttribute(Qt::WA_ShowWithoutActivating, true);
	this->setWindowFlags(this->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::X11BypassWindowManagerHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);

	buildLayout();

	this->timer = new QTimer(this);
	connect(this->timer, &QTimer::timeout, this, [this] {
		auto elapsed = std::chrono::steady_clock::now() - this->time_start;
		this->durationProgressBar_->setValue(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
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

void NotificationDialog::buildLayout()
{
	QVBoxLayout* rootLayout = new QVBoxLayout(this);
	rootLayout->setContentsMargins(0, 0, 0, 0);
	rootLayout->setSpacing(0);

	contentWidget_ = new QWidget(this);
	rootLayout->addWidget(contentWidget_);

	durationProgressBar_ = new QProgressBar(this);
	durationProgressBar_->setMaximumSize(16777215, PROGRESS_BAR_HEIGHT);
	durationProgressBar_->setTextVisible(false);
	durationProgressBar_->setMaximum(static_cast<int>(this->time_duration.count()));
	durationProgressBar_->setValue(0);
	rootLayout->addWidget(durationProgressBar_);

	switch (type_) {
	case NotificationType::VideoInfo:
		buildVideoInfoContent();
		break;
	case NotificationType::GeneralMessage:
	case NotificationType::GoalMet:
		buildSimpleContent();
		break;
	}
}

void NotificationDialog::buildVideoInfoContent()
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

void NotificationDialog::buildSimpleContent()
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
	titleLabel_->setWordWrap(true);
	titleLabel_->hide();
	headerRow->addWidget(titleLabel_, 1);

	layout->addLayout(headerRow);

	messageLabel_ = new QLabel();
	messageLabel_->setStyleSheet(QString(BODY_STYLE) + QStringLiteral(" color: #AAAAAA;"));
	messageLabel_->setWordWrap(true);
	layout->addWidget(messageLabel_);
}

void NotificationDialog::setMainWindow(MainWindow* MW) {
	this->MW = MW;
}

void NotificationDialog::populateVideoInfo(MainWindow* mw)
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

void NotificationDialog::populateGeneralMessage(const QString& title, const QString& message)
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

void NotificationDialog::populateGoalMet(const QString& description)
{
	if (!titleLabel_ || !messageLabel_) return;

	titleLabel_->setText("Goal Reached!");
	titleLabel_->show();
	messageLabel_->setText(description);
}

void NotificationDialog::closeNotification() {
	this->timer->stop();
	this->close();
	this->paused = false;
	this->deleteLater();
}

void NotificationDialog::showNotification()
{
	this->time_start = std::chrono::steady_clock::now();
	this->paused = false;
	this->durationProgressBar_->setMaximum(static_cast<int>(this->time_duration.count()));
	this->durationProgressBar_->setValue(0);
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
}

NotificationDialog::~NotificationDialog()
{
	this->timer->deleteLater();
	this->timer2->deleteLater();
}

void NotificationDialog::mousePressEvent(QMouseEvent* event)
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

void NotificationDialog::pauseNotification()
{
	if (this->paused)
		return;
	this->timer->stop();
	this->timer2->stop();
	this->paused = true;
}
