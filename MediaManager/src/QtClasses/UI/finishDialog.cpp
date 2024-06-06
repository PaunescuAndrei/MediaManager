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
	this->setWindowModality(Qt::NonModal);
	this->ui.NextButton->setText(MW->App->config->get_bool("random_next") ? "Next (R)" : "Next");
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
	this->ui.scrollArea_author->setStyleSheet("QScrollBar:horizontal {height:0px;} QScrollBar:vertical {height:0px;}");
	this->ui.scrollArea_author->installEventFilter(new scrollAreaEventFilter(this->ui.scrollArea_author));
	this->ui.scrollArea_name->setStyleSheet("QScrollBar:horizontal {height:0px;} QScrollBar:vertical {height:0px;}");
	this->ui.scrollArea_name->installEventFilter(new scrollAreaEventFilter(this->ui.scrollArea_name));
	this->ui.author_label->setText(MW->ui.currentVideo->author);
	this->ui.name_label->setText(MW->ui.currentVideo->name);
	QList<QTreeWidgetItem*> items = MW->ui.videosWidget->findItems(MW->ui.currentVideo->path, Qt::MatchExactly, ListColumns["PATH_COLUMN"]);
	if (!items.isEmpty()) {
		auto index = QPersistentModelIndex(MW->ui.videosWidget->indexFromItem(items.first(), ListColumns["RATING_COLUMN"]));
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

		this->ui.tags_label->setText(items.first()->text(ListColumns["TAGS_COLUMN"]));

		connect(this->ui.tagsButton, &QPushButton::clicked, this, [this, MW]() {
			QList<QTreeWidgetItem*> items = MW->ui.videosWidget->findItemsCustom(MW->ui.currentVideo->path, Qt::MatchExactly, ListColumns["PATH_COLUMN"],1);
			if (!items.isEmpty()) {
				this->timer.stop();
				Qt::WindowFlags flags = windowFlags();
				bool isOnTop = flags.testFlag(Qt::WindowStaysOnTopHint);
				this->setWindowFlag(Qt::WindowStaysOnTopHint, false);
				this->hide();
				VideosTagsDialog* dialog = MW->editTags({ items.first() }, nullptr);
				if (dialog) {
					connect(dialog, &finishDialog::finished, this, [this,MW, isOnTop](int result) {
						if (this) {
							this->setWindowFlag(Qt::WindowStaysOnTopHint, isOnTop);
							this->show();
							this->timer.start(250);
							QList<QTreeWidgetItem*> items = MW->ui.videosWidget->findItemsCustom(MW->ui.currentVideo->path, Qt::MatchExactly, ListColumns["PATH_COLUMN"],1);
							if (!items.isEmpty()) {
								this->ui.tags_label->setText(items.first()->text(ListColumns["TAGS_COLUMN"]));
							}
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
						utils::bring_hwnd_to_foreground_uiautomation_method(qMainApp->uiAutomation, (HWND)dialog->winId());
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

	connect(&this->timer, &QTimer::timeout, this, [this] {
		if (QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier)
			return;
		//if (not (this->isActiveWindow() and utils::IsMyWindowVisible(this))) {
			utils::bring_hwnd_to_foreground_uiautomation_method(qMainApp->uiAutomation, (HWND)this->winId());
			this->raise();
			this->show();
			this->activateWindow();
		//}
	});
	this->timer.start(250);
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

finishDialog::~finishDialog()
{
}
