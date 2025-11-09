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

finishDialog::finishDialog(MainWindow* MW, QWidget* parent) : QDialog(parent)
{
	ui.setupUi(this);
	this->MW = MW;
	this->updateWindowTitle();
	this->setWindowModality(Qt::NonModal);
	MW->initNextButtonMode(this->ui.NextButton);
	connect(this->ui.NextButton, &customQButton::rightClicked, this, [this,MW] {
		if (MW) {
			customQButton* buttonSender = qobject_cast<customQButton*>(sender());
			MW->switchNextButtonMode(buttonSender);
		}
	});
	this->ui.totalLabel->copy(MW->ui.totalListLabel);
	this->ui.counterLabel->copy(MW->ui.counterLabel);
	int total_width = this->ui.totalLabel->sizeHint().width();
	int counter_width = this->ui.counterLabel->sizeHint().width();
	this->ui.totalLabel->setMinimumWidth(std::max(total_width,counter_width));
	this->ui.counterLabel->setMinimumWidth(std::max(total_width, counter_width));
	this->ui.scrollArea_author->installEventFilter(new scrollAreaEventFilter(this->ui.scrollArea_author));
	this->ui.scrollArea_name->installEventFilter(new scrollAreaEventFilter(this->ui.scrollArea_name));
    this->ui.author_label->setText(MW->ui.currentVideo->author);
    this->ui.name_label->setText(MW->ui.currentVideo->name);
    QPersistentModelIndex index = QPersistentModelIndex(MW->proxyIndexByPath(MW->ui.currentVideo->path).sibling(ListColumns["RATING_COLUMN"], 0));
    if (index.isValid()) {
        StarRating starRating = StarRating(MW->active, MW->halfactive, MW->inactive, index.data(CustomRoles::rating).value<double>(), 5.0);
        starEditorWidget * starEditor = new starEditorWidget(this, index);
		starEditor->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
		starEditor->setEditMode(starEditorWidget::EditMode::DoubleClick);
		starEditor->setStarRating(starRating);
		connect(starEditor, &starEditorWidget::editingFinished, this, [MW, starEditor] {
			MW->updateRating(starEditor->item_index, starEditor->original_value,starEditor->starRating().starCount());
			MW->ui.videosWidget->model()->setData(starEditor->item_index, starEditor->starRating().starCount(), CustomRoles::rating);
			starEditor->original_value = starEditor->starRating().starCount();
		});
		starEditor->setFocusPolicy(Qt::NoFocus);
		QHBoxLayout *lt = qobject_cast<QHBoxLayout*>(this->ui.ratingBox->layout());
		double avg_rating = MW->App->db->getAverageRatingAuthor(MW->ui.currentVideo->author, MW->App->currentDB);
		QLabel* avg_rating_label = new QLabel("Avg. " + QString::number(avg_rating, 'f', 2), this);
		lt->insertWidget(2, starEditor);
		lt->insertWidget(3, avg_rating_label);

        // Tags label
        this->ui.tags_label->setText(index.sibling(ListColumns["TAGS_COLUMN"], 0).data(Qt::DisplayRole).toString());
		
		// Set views count
        QString viewsText = index.sibling(ListColumns["VIEWS_COLUMN"], 0).data(Qt::DisplayRole).toString();
        this->ui.viewsValueLabel->setText(viewsText);
		
		// Set last watched date in human-readable format
        QString lastWatchedText = index.sibling(ListColumns["LAST_WATCHED_COLUMN"], 0).data(Qt::DisplayRole).toString();
        QVariant lastWatchedData = index.sibling(ListColumns["LAST_WATCHED_COLUMN"], 0).data(Qt::DisplayRole);
		
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

        connect(this->ui.tagsButton, &QPushButton::clicked, this, [this, MW]() {
            QModelIndex src = MW->sourceIndexByPath(MW->ui.currentVideo->path);
            if (src.isValid()) {
                this->timer.stop();
                Qt::WindowFlags flags = windowFlags();
                bool isOnTop = flags.testFlag(Qt::WindowStaysOnTopHint);
                this->setWindowFlag(Qt::WindowStaysOnTopHint, false);
                this->hide();
                int id = src.sibling(ListColumns["PATH_COLUMN"], 0).data(CustomRoles::id).toInt();
                VideosTagsDialog* dialog = new VideosTagsDialog(QList<int>{ id }, MW, nullptr);
                if (dialog) {
                    connect(dialog, &finishDialog::finished, this, [this,MW, isOnTop](int result) {
                        if (this) {
                            this->setWindowFlag(Qt::WindowStaysOnTopHint, isOnTop);
                            this->show();
                            this->timer.start(250);
                            QModelIndex src = MW->sourceIndexByPath(MW->ui.currentVideo->path);
                            if (src.isValid()) this->ui.tags_label->setText(src.sibling(ListColumns["TAGS_COLUMN"], 0).data(Qt::DisplayRole).toString());
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
            }
        });
	}
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

	connect(&titleUpdateTimer, &QTimer::timeout, this, &finishDialog::updateWindowTitle);
	titleUpdateTimer.start(1000);
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
	this->ui.mainmsg->setText(QString("Continue? (Auto in %1s)").arg(this->countdownSeconds));
}

void finishDialog::stopCountdown()
{
	if (this->countdownTimer.isActive()) {
		this->countdownTimer.stop();
		this->ui.mainmsg->setText("Continue?");
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
	if (this->MW and this->MW->App->VW and this->MW->App->VW->mainPlayer) {
		int sessionSeconds = this->MW->App->VW->mainPlayer->getSessionTime();
		int watchedSeconds = this->MW->App->VW->mainPlayer->getTotalWatchedTime();
		if (sessionSeconds > 0) {
			session_time = " [Session: " % QString::fromStdString(utils::formatSeconds(sessionSeconds)) % "]";
		}
		if (watchedSeconds > 0) {
			watched_time = " [Watched: " % QString::fromStdString(utils::formatSeconds(watchedSeconds)) % "]";
		}
	}
	this->setWindowTitle("Continue?" + session_time + watched_time + " [Time: " + currentTime + "]");
}

finishDialog::~finishDialog()
{
    titleUpdateTimer.stop();
}
