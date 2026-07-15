#include "stdafx.h"
#include "finishDialog.h"
#include "MainWindow.h"
#include <QPushButton>
#include "MainApp.h"
#include "definitions.h"
#include "utils.h"
#include "starEditorWidget.h"
#include "scrollAreaEventFilter.h"
#include <QPersistentModelIndex>
#include "VideosTagsDialog.h"
#include "VideosModel.h"
#include <QFileInfo>

finishDialog::finishDialog(MainWindow* MW, QSharedPointer<BasePlayer> player, PlayerContext context, QWidget* parent) : QDialog(parent)
{
	ui.setupUi(this);

	this->MW = MW;
	this->m_player = player;
	this->m_context = context;
	this->updateWindowTitle();
	this->setWindowModality(Qt::NonModal);

	// Determine the video path to use for metadata lookups.
	// External player: never pull data from the DB or the app's videos model.
	QString lookupPath;
	if (m_context == PlayerContext::MainPlayer && MW) {
		lookupPath = MW->ui.currentVideo->path;
	} else if (m_player) {
		lookupPath = m_player->video_path;
	}

	QString videoAuthor;
	QString videoName;
	QPersistentModelIndex sourcePathIdx;

	if (m_context == PlayerContext::WatchExternal) {
		// External player — use only the filename, no DB/model lookups
		if (!lookupPath.isEmpty()) {
			QFileInfo fi(lookupPath);
			videoName = fi.completeBaseName();
		}
		videoAuthor = QStringLiteral("(external)");
	} else {
		// MainPlayer or WatchSelected — try model first, fall back to DB
		sourcePathIdx = (!lookupPath.isEmpty() && MW) ? MW->modelIndexByPath(lookupPath) : QPersistentModelIndex();

		if (sourcePathIdx.isValid()) {
			videoAuthor = sourcePathIdx.sibling(sourcePathIdx.row(), ListColumns["AUTHOR_COLUMN"]).data(Qt::DisplayRole).toString();
			videoName = sourcePathIdx.sibling(sourcePathIdx.row(), ListColumns["NAME_COLUMN"]).data(Qt::DisplayRole).toString();
		} else if (!lookupPath.isEmpty() && m_player && m_player->video_id >= 0 && MW) {
			// Fall back to DB lookup for videos not in the current model
			VideoData vd = MW->App->db->getVideoData(lookupPath, MW->App->currentDB);
			if (!vd.author.isEmpty() || !vd.name.isEmpty()) {
				videoAuthor = vd.author;
				videoName = vd.name;
			}
		}

		// If still empty and we have a player with a valid path, use the path as name
		if (videoName.isEmpty() && !lookupPath.isEmpty()) {
			QFileInfo fi(lookupPath);
			videoName = fi.completeBaseName();
		}
		if (videoAuthor.isEmpty()) {
			videoAuthor = QStringLiteral("(unknown)");
		}
	}

	this->ui.author_label->setText(videoAuthor);
	this->ui.name_label->setText(videoName);

	// Configure NextButton mode only for MainPlayer context
	if (m_context == PlayerContext::MainPlayer) {
		MW->initNextButtonMode(this->ui.NextButton);
		connect(this->ui.NextButton, &customQPushButton::rightClicked, this, [this, MW] {
			if (MW) {
				customQPushButton* buttonSender = qobject_cast<customQPushButton*>(sender());
				MW->switchNextButtonMode(buttonSender);
			}
		});
	}

	this->ui.totalLabel->copy(MW->ui.totalListLabel);
	this->ui.counterLabel->copy(MW->ui.counterLabel);
	int total_width = this->ui.totalLabel->sizeHint().width();
	int counter_width = this->ui.counterLabel->sizeHint().width();
	this->ui.totalLabel->setMinimumWidth(std::max(total_width,counter_width));
	this->ui.counterLabel->setMinimumWidth(std::max(total_width, counter_width));
	this->ui.scrollArea_author->installEventFilter(new scrollAreaEventFilter(this->ui.scrollArea_author));
	this->ui.scrollArea_name->installEventFilter(new scrollAreaEventFilter(this->ui.scrollArea_name));

	if (sourcePathIdx.isValid()) {
		const QPersistentModelIndex sourceRatingIdx = QPersistentModelIndex(sourcePathIdx.sibling(sourcePathIdx.row(), ListColumns["RATING_COLUMN"]));

		if (sourceRatingIdx.isValid()) {
			StarRating starRating = StarRating(MW->active, MW->halfactive, MW->inactive, sourceRatingIdx.data(CustomRoles::rating).value<double>(), 5.0);
			starEditorWidget * starEditor = new starEditorWidget(this, sourceRatingIdx);
			starEditor->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
			starEditor->setEditMode(starEditorWidget::EditMode::DoubleClick);
			starEditor->setStarRating(starRating);
			starEditor->setStarPixelSize(19);
			connect(starEditor, &starEditorWidget::editingFinished, this, [MW, starEditor] {
				const double newValue = starEditor->starRating().starCount();
				const double oldValue = starEditor->original_value;
				if (starEditor->item_index.isValid() && MW->videosModel) {
					MW->videosModel->setData(starEditor->item_index, newValue, CustomRoles::rating);
					MW->updateRating(starEditor->item_index, oldValue, newValue);
				}
				starEditor->original_value = newValue;
			});
			starEditor->setFocusPolicy(Qt::NoFocus);
			QHBoxLayout *lt = qobject_cast<QHBoxLayout*>(this->ui.ratingBox->layout());
			double avg_rating = MW->App->db->getAverageRatingAuthor(videoAuthor, MW->App->currentDB);
			QLabel* avg_rating_label = new QLabel("Avg. " + QString::number(avg_rating, 'f', 2), this);
			avg_rating_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
			lt->insertWidget(2, starEditor);
			lt->setAlignment(starEditor, Qt::AlignVCenter);
			lt->insertWidget(3, avg_rating_label);
			lt->setAlignment(avg_rating_label, Qt::AlignVCenter);
		}

		// Tags label
		this->ui.tags_label->setText(sourcePathIdx.sibling(sourcePathIdx.row(), ListColumns["TAGS_COLUMN"]).data(Qt::DisplayRole).toString());

		// Set views count
		QString viewsText = sourcePathIdx.sibling(sourcePathIdx.row(), ListColumns["VIEWS_COLUMN"]).data(Qt::DisplayRole).toString();
		this->ui.viewsValueLabel->setText(viewsText);

		// Set BPM
		double bpmVal = sourcePathIdx.sibling(sourcePathIdx.row(), ListColumns["BPM_COLUMN"]).data(CustomRoles::bpm).toDouble();
		this->ui.bpmValueLabel->setText(bpmVal > 0 ? QString::number(qRound(bpmVal)) : "-");

		// Set last watched date in human-readable format
		QString lastWatchedText = sourcePathIdx.sibling(sourcePathIdx.row(), ListColumns["LAST_WATCHED_COLUMN"]).data(Qt::DisplayRole).toString();
		QVariant lastWatchedData = sourcePathIdx.sibling(sourcePathIdx.row(), ListColumns["LAST_WATCHED_COLUMN"]).data(Qt::DisplayRole);

		if (!lastWatchedText.isEmpty() && lastWatchedData.type() == QVariant::DateTime) {
			QDateTime lastWatchedDateTime = lastWatchedData.toDateTime();

			if (lastWatchedDateTime.isValid()) {
				QDateTime currentDateTime = QDateTime::currentDateTime();
				qint64 secondsDiff = lastWatchedDateTime.secsTo(currentDateTime);
				if (secondsDiff < 0) {
					// If the date is in the future (shouldn't happen but just in case)
					this->ui.lastWatchedValueLabel->setText(lastWatchedText);
				} else {
					QString humanReadableDate = utils::formatTimeAgo(secondsDiff);
					this->ui.lastWatchedValueLabel->setText(humanReadableDate);
					this->ui.lastWatchedValueLabel->setToolTip(lastWatchedText);
				}
			} else {
				// If the date is invalid, display as Never
				this->ui.lastWatchedValueLabel->setText("Never");
			}
		} else {
			this->ui.lastWatchedValueLabel->setText("Never");
		}
	} else {
		// No model index found — clear labels for external/non-DB videos
		this->ui.tags_label->setText(QString());
		this->ui.viewsValueLabel->setText(QStringLiteral("-"));
		this->ui.bpmValueLabel->setText(QStringLiteral("-"));
		this->ui.lastWatchedValueLabel->setText(QStringLiteral("-"));
	}

	// Tags button — use the player's video path (not MW->ui.currentVideo) for non-main contexts
	const QString tagsVideoPath = lookupPath;
	connect(this->ui.tagsButton, &QPushButton::clicked, this, [this, MW, tagsVideoPath]() {
		const QPersistentModelIndex src = MW->modelIndexByPath(tagsVideoPath);
		if (!src.isValid())
			return;
		this->timer.stop();
		const Qt::WindowFlags flags = windowFlags();
		const bool isOnTop = flags.testFlag(Qt::WindowStaysOnTopHint);
		this->setWindowFlag(Qt::WindowStaysOnTopHint, false);
		this->hide();
		const QPersistentModelIndex pathIdx = src.sibling(src.row(), ListColumns["PATH_COLUMN"]);
		const int id = pathIdx.data(CustomRoles::id).toInt();
		VideosTagsDialog* dialog = MW->editTags(QList<int>{ id }, nullptr);
		if (dialog) {
			connect(dialog, &VideosTagsDialog::finished, this, [this, MW, isOnTop, tagsVideoPath](int) {
				this->setWindowFlag(Qt::WindowStaysOnTopHint, isOnTop);
				this->show();
				this->timer.start(250);
				const QPersistentModelIndex updatedSrc = MW->modelIndexByPath(tagsVideoPath);
				if (updatedSrc.isValid()) {
					const QPersistentModelIndex tagsIdx = updatedSrc.sibling(updatedSrc.row(), ListColumns["TAGS_COLUMN"]);
					this->ui.tags_label->setText(tagsIdx.data(Qt::DisplayRole).toString());
				}
			});
		}
		else {
			this->setWindowFlag(Qt::WindowStaysOnTopHint, isOnTop);
			this->show();
			this->timer.start(250);
		}
		QTimer::singleShot(100, [dialog] {
			if (dialog) {
				utils::bring_hwnd_to_foreground_uiautomation_method((HWND)dialog->winId(), qMainApp->uiAutomation);
				dialog->raise();
				dialog->show();
				dialog->activateWindow();
			}
		});
	});

	connect(this->ui.NextButton, &QPushButton::clicked, this, [this]() {this->timer.stop(); this->done(finishDialog::Accepted); } );
	connect(this->ui.cancelButton, &QPushButton::clicked, this, [this]() {this->timer.stop(); this->done(finishDialog::Rejected); });
	connect(this->ui.skipButton, &QPushButton::clicked, this, [this]() { this->timer.stop(); this->done(finishDialog::Skip); });
	connect(this->ui.replayButton, &QPushButton::clicked, this, [this]() {this->timer.stop(); this->done(finishDialog::Replay); });

	connect(&this->timer, &QTimer::timeout, this, [this] {
		if (QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier)
			return;
		//if (not (this->isActiveWindow() and utils::IsMyWindowVisible(this))) {
			utils::bring_hwnd_to_foreground_uiautomation_method((HWND)this->winId(), qMainApp->uiAutomation);
			this->raise();
			this->show();
			this->activateWindow();
		//}
	});
	if (MW->App->config->get_bool("auto_continue")) {
		this->countdownSeconds = MW->App->config->get("auto_continue_delay").toInt();
		this->updateCountdownText();
		connect(&this->countdownTimer, &QTimer::timeout, this, [this] {
			this->countdownSeconds--;
			if (this->countdownSeconds <= 0) {
				this->countdownTimer.stop();
				this->timer.stop();
				this->done(finishDialog::Accepted);
			} else {
				this->updateCountdownText();
			}
		});
		this->countdownTimer.start(1000);
		this->installEventFilter(this);
		this->ui.tagsButton->installEventFilter(this);
	}
	this->timer.start(250);

	// Apply button customization for the context
	this->configureForContext();

	connect(&titleUpdateTimer, &QTimer::timeout, this, &finishDialog::updateWindowTitle);
	titleUpdateTimer.start(1000);
}

void finishDialog::configureForContext() {
	switch (m_context) {
	case PlayerContext::MainPlayer:
		// Default behavior — no changes needed
		break;
	case PlayerContext::WatchSelected:
		// Next → End, gray out Skip
		this->ui.NextButton->setText(QStringLiteral("End"));
		this->ui.NextButton->setToolTip(QStringLiteral("Close the player and log this video as completed"));
		this->ui.skipButton->setEnabled(false);
		this->ui.mainmsg->setText(QStringLiteral("End playback?"));
		break;
	case PlayerContext::WatchExternal:
		// Next → End, gray out Skip and Tags
		this->ui.NextButton->setText(QStringLiteral("End"));
		this->ui.NextButton->setToolTip(QStringLiteral("Unload the current track and log it as completed"));
		this->ui.skipButton->setEnabled(false);
		this->ui.tagsButton->setEnabled(false);
		this->ui.mainmsg->setText(QStringLiteral("End playback?"));
		break;
	}
}

bool finishDialog::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::KeyPress || 
        event->type() == QEvent::KeyRelease ||
        event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::Wheel) {
        this->stopCountdown();
    }
    return QDialog::eventFilter(obj, event);
}

void finishDialog::updateCountdownText() {
	const QString label = (m_context == PlayerContext::MainPlayer)
		? QStringLiteral("Continue?")
		: QStringLiteral("End playback?");
	this->ui.mainmsg->setText(QStringLiteral("%1 (Auto in %2s)").arg(label).arg(this->countdownSeconds));
}

void finishDialog::stopCountdown()
{
	if (this->countdownTimer.isActive()) {
		this->countdownTimer.stop();
		const QString label = (m_context == PlayerContext::MainPlayer)
			? QStringLiteral("Continue?")
			: QStringLiteral("End playback?");
		this->ui.mainmsg->setText(label);
	}
}

void finishDialog::wheelEvent(QWheelEvent* event)
{
	event->accept();
	if (event->angleDelta().y() > 0) {
		this->focusNextChild();
		return;
	}
	else {
		this->focusPreviousChild();
		return;
	}
}

void finishDialog::updateWindowTitle() {
	QString currentTime = QTime::currentTime().toString("hh:mm:ss");
	QString session_time = "";
	QString watched_time = "";
	if (this->MW and this->MW->App->VW) {
		int sessionSeconds = static_cast<int>(this->MW->App->VW->currentSessionTime());
		if (sessionSeconds > 0) {
			session_time = QStringLiteral(" [Session: %1]").arg(utils::formatSecondsCompactQt(sessionSeconds));
		}
		double totalWatched = 0.0;
		for (const auto& player : this->MW->App->VW->Players) {
			if (player)
				totalWatched += player->getTotalWatchedTime();
		}
		int watchedSeconds = static_cast<int>(totalWatched);
		if (watchedSeconds > 0) {
			watched_time = QStringLiteral(" [Watched: %1]").arg(utils::formatSecondsCompactQt(watchedSeconds));
		}
	}
	QString prefix;
	switch (m_context) {
	case PlayerContext::MainPlayer:
		prefix = QStringLiteral("Continue?");
		break;
	case PlayerContext::WatchSelected:
		prefix = QStringLiteral("End?");
		break;
	case PlayerContext::WatchExternal:
		prefix = QStringLiteral("End?");
		break;
	}
	this->setWindowTitle(QStringLiteral("%1%2%3 [Time: %4]").arg(prefix, session_time, watched_time, currentTime));
}

finishDialog::~finishDialog()
{
    titleUpdateTimer.stop();
    timer.stop();
    countdownTimer.stop();
    QToolTip::hideText();
}
