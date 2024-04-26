#include "stdafx.h"
#include "MainWindow.h"
#include "MainApp.h"
#include "version.h"
#include <iostream>
#include <QDebug>
#include "ini.h"
#include "definitions.h"
#include "utils.h"
#include <QHeaderView>
#include "addWatchedDialog.h"
#include <QProcess>
#include <chrono>
#include <QMenu>
#include <QAction>
#include <QModelIndex>
#include <QPushButton>
#include "videosTreeWidget.h"
#include "DeleteDialog.h"
#include <QScrollBar>
#include <QMimeDatabase>
#include <QMimeType>
#include "InsertDialog.h"
#include "setCounterDialog.h"
#include "IconChanger.h"
#include "BlockingQueue.h"
#include <QTimer>
#include "SettingsDialog.h"
#include <QWindow>
#include <QScreen>
#include <QRect>
#include "ResetDBDialog.h"
#include <QFileInfo>
#include <QMessageBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include "generateThumbnailThread.h"
#include "AutoToolTipDelegate.h"
#include "StatsDialog.h"
#include <QDir>
#include <QCloseEvent>
#include <QSystemTrayIcon>
#include <QShortcut>
#include <QRegularExpression>
#include <QInputDialog>
#include "InsertSettingsDialog.h"
#include "generalEventFilter.h"
#include "QCustomButton.h"
#include "scrollAreaEventFilter.h"
#include "TreeWidgetItem.h"
#include "ProgressBarQLabel.h"
#include <QColorDialog>
#include "FilterDialog.h"
#include "noRightClickEventFilter.h"
#include "DateItemDelegate.h"
#include "colorPaletteExtractor.h"
#include "loadBackupDialog.h"

#pragma warning(push ,3)
#include "rapidfuzz_all.hpp"
#pragma warning(pop)

#include "stardelegate.h"

MainWindow::MainWindow(QWidget *parent,MainApp *App)
    : QMainWindow(parent)
{
    this->App = App;
    this->initRatingIcons();
    ui.setupUi(this);
    this->ui.searchWidget->hide();
    new QShortcut(QKeySequence("F1"), this, [this] {this->switchCurrentDB(); });
    new QShortcut(QKeySequence("F2"), this, [this] {this->toggleSearchBar(); });
    new QShortcut(QKeySequence("F3"), this, [this] {this->toggleDates(); });
    QShortcut *shortcutLogs = new QShortcut(QKeySequence("F4"), this, [this] {this->App->toggleLogWindow(); });
    shortcutLogs->setContext(Qt::ApplicationShortcut);
    new QShortcut(QKeySequence("ESC"), this->ui.searchBar, [this] {this->ui.searchBar->setText(""); });
    this->UpdateWindowTitle();
    
    this->animatedIconFlag = this->App->config->get_bool("animated_icon_flag");
    this->time_watched_limit = this->App->config->get("time_watched_limit").toInt();
    this->ui.watchedTimePB->setMaximum(this->time_watched_limit);
    this->filterSettings = FilterSettings(this->App->db->getFilterSettings(this->App->currentDB));

    this->trayIcon = new QSystemTrayIcon(this);
    connect(this->trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::iconActivated);
    //this->trayIcon->setContextMenu(this->trayIconContextMenu(this));
    // to do, implement random to normal icons and dont hard-code it
    this->loadIcons(QString(NORMAL_ICONS_PATH) + "/1");
    this->setIcon(QIcon(":/main/resources/icon.ico"));
    this->trayIcon->show();

    this->initStyleSheets();
    this->ui.currentVideoScrollArea->installEventFilter(new scrollAreaEventFilter(this->ui.currentVideoScrollArea));

    this->ui.watchedTimeFrame->setMainWindow(this);
    this->ui.next_button->setText(this->App->config->get_bool("random_next") ? "Next (R)" : "Next");
    if (this->App->config->get("get_random_mode") == "All") {
        this->ui.random_button->setText("Get Random (All)");
    }
    else if (this->App->config->get("get_random_mode") == "Filtered") {
        this->ui.random_button->setText("Get Random (F)");
    }
    else {
        this->ui.random_button->setText("Get Random");
    }
    this->ui.shoko_button->setMainWindow(this);

    DateItemDelegate* datedelegate = new DateItemDelegate(this->ui.videosWidget,"dd/MM/yyyy hh:mm");

    this->ui.videosWidget->setItemDelegateForColumn(ListColumns["RATING_COLUMN"], new StarDelegate(this->active,this->halfactive,this->inactive,this->ui.videosWidget,this));
    this->ui.videosWidget->setItemDelegateForColumn(ListColumns["DATE_CREATED_COLUMN"], datedelegate);
    this->ui.videosWidget->setItemDelegateForColumn(ListColumns["LAST_WATCHED_COLUMN"], datedelegate);
    this->ui.videosWidget->setEditTriggers(this->ui.videosWidget->NoEditTriggers);
    this->ui.videosWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    connect(this->ui.videosWidget, &videosTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item,int column) {
        if (column == ListColumns["RATING_COLUMN"]) {
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            this->ui.videosWidget->editItem(item, column);
        }
        else
            this->watchSelected(item->data(ListColumns["PATH_COLUMN"],CustomRoles::id).toInt(), item->text(ListColumns["PATH_COLUMN"]));
    });
    connect(this->ui.videosWidget, &videosTreeWidget::itemMiddleClicked, this, [this](QTreeWidgetItem* item) { this->openThumbnails(item->text(ListColumns["PATH_COLUMN"]).toStdString()); });
    this->ui.videosWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    this->ui.videosWidget->header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this->ui.videosWidget, &QTreeWidget::customContextMenuRequested, this, &MainWindow::videosWidgetContextMenu);
    connect(this->ui.videosWidget->header(), &QTreeWidget::customContextMenuRequested, this, &MainWindow::videosWidgetHeaderContextMenu);
    auto header = this->ui.videosWidget->header();
    header->setSortIndicator(ListColumns[this->App->config->get("sort_column")], (Qt::SortOrder)sortingDict[this->App->config->get("sort_order")]);
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ListColumns["AUTHOR_COLUMN"], QHeaderView::Interactive);
    this->ui.videosWidget->headerItem()->setText(ListColumns["WATCHED_COLUMN"], "W");
    header->setSectionResizeMode(ListColumns["NAME_COLUMN"], QHeaderView::Stretch);
    header->setSectionResizeMode(ListColumns["PATH_COLUMN"], QHeaderView::Stretch);
    connect(header, &QHeaderView::sectionClicked, this, [this] { if(!this->toggleDatesFlag) this->updateSortConfig(); });
    connect(this, &MainWindow::fileDropped, this, [this](QStringList files, QWidget *widget) {
        if (files.size() == 1) {
            QMimeDatabase db;
            QMimeType guess_type = db.mimeTypeForFile(files[0]);
            if (not guess_type.isValid())
                return;
            if (guess_type.name() == "application/zip") {
                this->animatedIcon->setIcon(files[0],false);
                return;
            }
            if (guess_type.name().startsWith("audio/", Qt::CaseInsensitive)) {
                if (this->App->soundPlayer) {
                    this->App->soundPlayer->play(files[0]);
                }
                return;
            }
            if (guess_type.name().startsWith("audio/", Qt::CaseInsensitive)) {
                if (this->App->soundPlayer) {
                    this->App->soundPlayer->play(files[0]);
                }
                return;
            }
            if (guess_type.name().startsWith("image/", Qt::CaseInsensitive)) {
                if (widget and (widget->objectName() == "leftImg" or widget->objectName() == "rightImg")) {
                    this->setMascotDebug(files[0], (customGraphicsView*)widget);
                }
                return;
            }
        }
        this->InsertVideoFiles(files,false); 
    });

    connect(this->ui.random_button, &QPushButton::clicked, this, [this] {
        bool all = this->App->config->get("get_random_mode") == "All";
        bool video_changed = this->randomVideo(all);
        if (this->App->VW->mainListener and video_changed) {
            this->changeListenerVideo(this->App->VW->mainListener, this->ui.currentVideo->path, this->position);
        }
    });
    connect(this->ui.random_button, &QCustomButton::middleClicked, this, [this] {
        FilterDialog dialog = FilterDialog(this,this->filterSettings.json,this);
        int value = dialog.exec();
        if (value == QDialog::Accepted) {
            QJsonObject json = dialog.toJson();
            this->filterSettings.setJson(json);
            this->App->db->setFilterSettings(QJsonDocument(dialog.toJson()).toJson(QJsonDocument::Compact), this->App->currentDB);
        }
    });
    connect(this->ui.random_button, &QCustomButton::rightClicked, this, [this] {
        QCustomButton* buttonSender = qobject_cast<QCustomButton*>(sender());
        this->switchRandomButtonMode(buttonSender);
     });
    connect(this->ui.watch_button, &QPushButton::clicked, this, [this] { 
        this->watchCurrent(); 
    });
    connect(this->ui.insert_button, &QPushButton::clicked, this, [this] { 
        this->insertDialogButton(); 
    });
    connect(this->ui.next_button, &QPushButton::clicked, this, [this] {
        this->NextButtonClicked(); 
    });
    connect(this->ui.next_button, &QCustomButton::middleClicked, this, [this] {
        this->NextButtonClicked(false);
    });
    connect(this->ui.next_button, &QCustomButton::rightClicked, this, [this] {
        QCustomButton* buttonSender = qobject_cast<QCustomButton*>(sender());
        this->switchNextButtonMode(buttonSender);
     });
    connect(this->ui.settings_button, &QPushButton::clicked, this, [this] {
        this->settingsDialogButton(); 
    });
    connect(this->ui.settings_button, &QCustomButton::rightClicked, this, [this] { 
        this->openStats();
    });

    connect(this->ui.totalListLabel, &ProgressBarQLabel::clicked, this, [this] {
        this->switchCurrentDB();
    });
    this->ui.counterLabel->highlight_mode = true;
    connect(this->ui.counterLabel, &ProgressBarQLabel::clicked, this, [this] {
        this->setCounterDialogButton();
    });
    connect(this->ui.counterLabel, &ProgressBarQLabel::scrollUp, this, [this] {
        this->App->soundPlayer->playSoundEffect();
        this->incrementCounterVar(1);
    });
    connect(this->ui.counterLabel, &ProgressBarQLabel::scrollDown, this, [this] {
        this->App->soundPlayer->playSoundEffect();
        this->incrementCounterVar(-1);
    });

    this->initListDetails();
    this->refreshHeadersVisibility();
    this->populateList();
    
    if (!this->initMusic() && this->App->config->get_bool("sound_effects_start") && this->App->soundPlayer && !this->App->soundPlayer->intro_effects.isEmpty()) {
        QString track = *utils::select_randomly(this->App->soundPlayer->intro_effects.begin(), this->App->soundPlayer->intro_effects.end());
        this->App->soundPlayer->play(track);
    }
    
    this->initMascotAnimation();
    if (this->App->config->get_bool("mascots"))
        this->setMascots();
    else
        this->hideMascots();
    connect(this->ui.leftImg, &customGraphicsView::mouseClicked, this, [this](Qt::MouseButton button) {this->flipMascotDebug(this->ui.leftImg, button); });
    connect(this->ui.rightImg, &customGraphicsView::mouseClicked, this, [this](Qt::MouseButton button) {this->flipMascotDebug(this->ui.rightImg, button); });

    connect(&this->thumbnailManager.thumbs_timer, &QTimer::timeout, this, [this] {
        if (this->thumbnailManager.work_count <= 0) { 
            this->thumbnailManager.thumbs_timer.stop();
        } 
        this->UpdateWindowTitle();
    });

    connect(this->ui.searchButton, &QPushButton::clicked, this, [this] {this->refreshVisibility(this->ui.searchBar->text()); });
    QList<QAction*> actionList = this->ui.searchBar->findChildren<QAction*>();
    connect(this->ui.searchBar, &QLineEdit::returnPressed, this, [this] {this->refreshVisibility(this->ui.searchBar->text()); });
    if (!actionList.isEmpty()) {
        connect(actionList.first(), &QAction::triggered, this, [this]() {qDebug() << "test"; this->ui.searchBar->setText(""); });
    }
    //connect(this->ui.searchBar, &QLineEdit::textChanged, this, [this] {this->search(this->ui.searchBar->text()); });

    this->search_timer = new QTimer(this);
    connect(this->search_timer, &QTimer::timeout, this, [this] {
        if(this->ui.searchBar->text() != this->old_search)
            this->refreshVisibility(this->ui.searchBar->text());
    });

    this->ui.videosWidget->setItemDelegate(new AutoToolTipDelegate(this->ui.videosWidget));
    
    this->animatedIcon = new IconChanger(this->App, this->App->config->get_bool("random_icon_start"));
    connect(this->animatedIcon, &IconChanger::animatedIconSignal, this, [this](QIcon icon) {
        if (this->animatedIconFlag)
            this->setIcon(icon);
    });
    this->animatedIcon->start();

    this->setDebugMode(this->App->debug_mode);

    //QFont font1 = QFont();
    //font1.setPointSize(10);
    //QCustomButton *buttonTest1 = new QCustomButton(this->ui.centralwidget);
    //buttonTest1->setObjectName("buttonTest");
    //buttonTest1->setFont(font1);
    //buttonTest1->setText("TEST 1");
    //this->ui.MenuButtons->layout()->addWidget(buttonTest1);
    //connect(buttonTest1, &QCustomButton::clicked, this, [this] { 
    //    //QPalette p = this->App->palette();
    //    //p.setColor(QPalette::Highlight, QColor(randint(0,255), randint(0, 255), randint(0, 255)));
    //    //this->changePalette(p);
    //});
    //connect(buttonTest1, &QCustomButton::rightClicked, this, [this] {
    //    //QPalette p = this->App->style()->standardPalette();
    //    //this->changePalette(p);
    //});
    //connect(buttonTest1, &QCustomButton::middleClicked, this, [this] {
    //    //QPalette p = this->App->style()->standardPalette();
    //    //this->changePalette(p);
    //});
}

QString MainWindow::getCategoryName(QString currentdb) {
    return currentdb == "PLUS" ? this->App->config->get("plus_category_name") : (currentdb == "MINUS" ? this->App->config->get("minus_category_name") : currentdb);
}

QString MainWindow::getCategoryName() {
    return this->getCategoryName(this->App->currentDB);
}

void MainWindow::UpdateWindowTitle() {
    if (this->thumbnailManager.work_count <= 0) {
        this->setWindowTitle(QString("Media Manager %1 %2").arg(this->getCategoryName()).arg(VERSION_TEXT));
    }
    else {
        this->setWindowTitle(QString("Media Manager %1 %2 (%3)").arg(this->getCategoryName()).arg(VERSION_TEXT).arg(this->thumbnailManager.work_count));
    }
}

void MainWindow::VideoInfoNotification() {
    QScreen* screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->geometry();
    if (this->notification_dialog) {
        this->notification_dialog->closeNotification();
        this->notification_dialog = nullptr;
    }
    this->notification_dialog = new NotificationDialog();
    this->notification_dialog->ui.mainWidget->setMaximumWidth((int)(screenGeometry.width() * 0.98));
    this->notification_dialog->setMainWindow(this);
    this->notification_dialog->timer->stop();
    this->notification_dialog->timer2->stop();
    this->notification_dialog->ui.totalLabel->copy(this->ui.totalListLabel);
    this->notification_dialog->ui.counterLabel->copy(this->ui.counterLabel);
    this->notification_dialog->ui.name->setText(this->ui.currentVideo->name);
    this->notification_dialog->ui.author->setText(this->ui.currentVideo->author);
    StarRating starRating = StarRating(this->active, this->halfactive, this->inactive, 0, 5.0);
    if (this->ui.videosWidget->last_selected) {
        starRating.setStarCount(this->ui.videosWidget->last_selected->data(ListColumns["RATING_COLUMN"], CustomRoles::rating).toDouble());
    }
    this->notification_dialog->ui.rating->setStarRating(starRating);
    this->notification_dialog->setWindowModality(Qt::WindowModal);
    this->notification_dialog->setAttribute(Qt::WA_ShowWithoutActivating, true);
    this->notification_dialog->setWindowFlags(this->notification_dialog->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::X11BypassWindowManagerHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    
    QRect newRect = QStyle::alignedRect(
        Qt::LeftToRight,
        Qt::AlignHCenter,
        this->notification_dialog->sizeHint(),
        screenGeometry
    );
    newRect.moveTop((int)(screenGeometry.height() * 0.03));
    this->notification_dialog->setGeometry(newRect);

    this->notification_dialog->showNotification(10000,50);
}

void MainWindow::resetPalette() {
    QPalette p = get_palette("main");
    this->changePalette(p);
}

void MainWindow::changePalette(QPalette palette) {
    this->App->setPalette(palette);
    this->initRatingIcons();
    this->initStyleSheets();
    this->selectCurrentItem(nullptr, false);
}

void MainWindow::initStyleSheets() {
    this->ui.currentVideoScrollArea->setStyleSheet("QScrollBar:horizontal {height:0px;} QScrollBar:vertical {height:0px;}");
    this->ui.videosWidget->setStyleSheet(get_stylesheet("videoswidget"));
    this->ui.shoko_button->setStyleSheet(QString::fromUtf8("padding: 0; border: 0; margin:0;"));
}

void MainWindow::initRatingIcons() {
    QColor highlight_color = this->App->palette().color(QPalette::Highlight);
    QColor comp_color = utils::complementary_color(highlight_color);
    QPainter p;
    QIcon* oldicon = nullptr;
    QImage star_inactive = QImage(":/main/resources/star_inactive.png");

    if (this->inactive->isNull()) {
        oldicon = new QIcon(QPixmap::fromImage(star_inactive));
        this->inactive->swap(*oldicon);
        delete oldicon;
    }
    //
    QImage img_base = QImage(star_inactive);
    QImage img_star = QImage(":/main/resources/star_active.png");
    QImage star_border = QImage(":/main/resources/star_border_full.png");
    utils::changeQImageHue(img_star, highlight_color,0.9);
    //changeQImageHue(star_border, comp_color,0.9);
    p.begin(&img_base);
    p.drawImage(0, 0, img_star);
    p.drawImage(0, 0, star_border);
    p.end();
    oldicon = new QIcon(QPixmap::fromImage(img_base));
    this->active->swap(*oldicon);
    delete oldicon;
    //
    img_base = QImage(star_inactive);
    img_star = QImage(":/main/resources/star_half.png");
    star_border = QImage(":/main/resources/star_border_half.png");
    utils::changeQImageHue(img_star, highlight_color,0.9);
    //changeQImageHue(star_border, comp_color,0.9);
    p.begin(&img_base);
    p.drawImage(0, 0, img_star);
    p.drawImage(0, 0, star_border);
    p.end();
    oldicon = new QIcon(QPixmap::fromImage(img_base));
    this->halfactive->swap(*oldicon);
    delete oldicon;
}

void MainWindow::updateRating(QPersistentModelIndex index, double old_value, double new_value) {
    QTreeWidgetItem* item = this->ui.videosWidget->itemFromIndex(index);
    if (not item)
        return;
    qMainApp->logger->log(QString("Updating rating from %1 to %2 for \"%3\"").arg(old_value).arg(new_value).arg(item->text(ListColumns["PATH_COLUMN"])), "Stats", item->text(ListColumns["PATH_COLUMN"]));
    this->App->db->updateRating(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), new_value);
}

void MainWindow::updateAuthors(QList<QTreeWidgetItem*> items) {
    QString last = items.last()->text(ListColumns["PATH_COLUMN"]);
    int lastScroll = this->ui.videosWidget->verticalScrollBar()->value();
    if (!items.isEmpty()) {
        this->App->db->db.transaction();
        for (auto const& item : items) {
            QString author = InsertSettingsDialog::get_author(item->text(ListColumns["PATH_COLUMN"]));
            this->App->db->updateAuthor(item->data(ListColumns["PATH_COLUMN"],CustomRoles::id).toInt(), author);
        }
        this->App->db->db.commit();
        this->refreshVideosWidget(false, true);
        this->ui.videosWidget->findAndScrollToDelayed(last, false);
        this->ui.videosWidget->scrollToVerticalPositionDelayed(lastScroll);
    }
}

void MainWindow::updateAuthors(QString value, QList<QTreeWidgetItem*> items) {
    QString last = items.last()->text(ListColumns["PATH_COLUMN"]);
    int lastScroll = this->ui.videosWidget->verticalScrollBar()->value();
    if (!items.isEmpty()) {
        this->App->db->db.transaction();
        for (auto const& item : items) {
            this->App->db->updateAuthor(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), value);
        }
        this->App->db->db.commit();
        this->refreshVideosWidget(false, true);
        this->ui.videosWidget->findAndScrollToDelayed(last, false);
        this->ui.videosWidget->scrollToVerticalPositionDelayed(lastScroll);
    }
}

void MainWindow::updateNames(QList<QTreeWidgetItem*> items) {
    QString last = items.last()->text(ListColumns["PATH_COLUMN"]);
    int lastScroll = this->ui.videosWidget->verticalScrollBar()->value();
    if (!items.isEmpty()) {
        this->App->db->db.transaction();
        for (auto const& item : items) {
            QString name = InsertSettingsDialog::get_name(item->text(ListColumns["PATH_COLUMN"]));
            this->App->db->updateName(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), name);
        }
        this->App->db->db.commit();
        this->refreshVideosWidget(false, true);
        this->ui.videosWidget->findAndScrollToDelayed(last, false);
        this->ui.videosWidget->scrollToVerticalPositionDelayed(lastScroll);
    }
}

void MainWindow::updateNames(QString value, QList<QTreeWidgetItem*> items) {
    QString last = items.last()->text(ListColumns["PATH_COLUMN"]);
    int lastScroll = this->ui.videosWidget->verticalScrollBar()->value();
    if (!items.isEmpty()) {
        this->App->db->db.transaction();
        for (auto const& item : items) {
            this->App->db->updateName(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), value);
        }
        this->App->db->db.commit();
        this->refreshVideosWidget(false, true);
        this->ui.videosWidget->findAndScrollToDelayed(last, false);
        this->ui.videosWidget->scrollToVerticalPositionDelayed(lastScroll);
    }
}

void MainWindow::updatePath(int video_id, QString new_path) {
    QMimeDatabase db;
    QMimeType guess_type = db.mimeTypeForFile(new_path);
    if (guess_type.isValid() && guess_type.name().startsWith("video")) {
        this->App->db->db.transaction();
        this->App->db->updateItem(video_id, new_path);
        QString author = InsertSettingsDialog::get_author(new_path);
        QString name = InsertSettingsDialog::get_name(new_path);
        this->App->db->updateAuthor(video_id, author);
        this->App->db->updateName(video_id, name);
        this->App->db->db.commit();
        this->thumbnailManager.enqueue_work({ new_path, false });
        this->thumbnailManager.start();
        this->refreshVideosWidget(false, false);
        this->ui.videosWidget->findAndScrollToDelayed(new_path, true);
    }
}

void MainWindow::syncItems(QTreeWidgetItem* main_item, QList<QTreeWidgetItem*> items) {
    QString last = items.last()->text(ListColumns["PATH_COLUMN"]);
    int lastScroll = this->ui.videosWidget->verticalScrollBar()->value();
    if (!items.isEmpty()) {
        this->App->db->db.transaction();
        for (auto const& item : items) {
            this->App->db->updateItem(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), "", "", "", "", "", "", main_item->text(ListColumns["WATCHED_COLUMN"]), main_item->text(ListColumns["VIEWS_COLUMN"]), main_item->data(ListColumns["RATING_COLUMN"], CustomRoles::rating).toString());
        }
        this->App->db->db.commit();
        this->refreshVideosWidget(false, true);
        this->ui.videosWidget->findAndScrollToDelayed(last, false);
        this->ui.videosWidget->scrollToVerticalPositionDelayed(lastScroll);
    }
}

void MainWindow::toggleSearchBar() {
    if (!this->ui.searchWidget->isVisible()) {
        this->lastScrolls.append({ "toggleSearchBar" ,this->ui.videosWidget->verticalScrollBar()->value() });
        this->ui.searchWidget->setVisible(true);
        if (!this->search_timer->isActive())
            this->search_timer->start(500);
        this->ui.searchBar->setFocus();
        if (!this->ui.searchBar->text().isEmpty())
            this->refreshVisibility(this->ui.searchBar->text());
    }
    else {
        this->ui.searchWidget->setVisible(false);
        this->refreshVisibility("");
        if (this->search_timer->isActive())
            this->search_timer->stop();
        do {
            if (this->lastScrolls.isEmpty()) {
                this->ui.videosWidget->scrollToItemCustom(this->ui.videosWidget->last_selected, false, QAbstractItemView::PositionAtCenter);
                break;
            }
            QPair<QString, int> scroll_value = this->lastScrolls.takeLast();
            if (scroll_value.first == "toggleSearchBar") {
                this->ui.videosWidget->scrollToVerticalPositionDelayed(scroll_value.second);
                break;
            }
        } while (!this->lastScrolls.isEmpty());
    }
}

void MainWindow::toggleDates(bool scroll) {
    auto header = this->ui.videosWidget->header();
    if (!this->toggleDatesFlag) {
        header->setSectionHidden(ListColumns["DATE_CREATED_COLUMN"], false);
        header->setSectionHidden(ListColumns["LAST_WATCHED_COLUMN"], false);
        if (scroll) {
            this->lastScrolls.append({"toggleDates" ,this->ui.videosWidget->verticalScrollBar()->value()});
            this->ui.videosWidget->scrollToTop();
        }
        header->setSortIndicator(ListColumns["LAST_WATCHED_COLUMN"],Qt::SortOrder::DescendingOrder);
        this->toggleDatesFlag = true;
    }
    else {
        QString settings_str = QString();
        if (this->App->currentDB == "PLUS")
            settings_str = this->App->config->get("headers_plus_visible");
        else if (this->App->currentDB == "MINUS")
            settings_str = this->App->config->get("headers_minus_visible");
        QStringList settings_list = settings_str.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (settings_list.contains("date_created"))
            header->setSectionHidden(ListColumns["DATE_CREATED_COLUMN"], false);
        else
            header->setSectionHidden(ListColumns["DATE_CREATED_COLUMN"], true);
        if (settings_list.contains("last_watched"))
            header->setSectionHidden(ListColumns["LAST_WATCHED_COLUMN"], false);
        else
            header->setSectionHidden(ListColumns["LAST_WATCHED_COLUMN"], true);
        header->setSortIndicator(ListColumns[this->App->config->get("sort_column")], (Qt::SortOrder)sortingDict[this->App->config->get("sort_order")]);
        do {
            if (this->lastScrolls.isEmpty()) {
                if(scroll)
                    this->ui.videosWidget->scrollToItemCustom(this->ui.videosWidget->last_selected, false, QAbstractItemView::PositionAtCenter);
                break;
            }
            QPair<QString, int> scroll_value = this->lastScrolls.takeLast();
            if (scroll_value.first == "toggleDates") {
                if(scroll)
                    this->ui.videosWidget->scrollToVerticalPositionDelayed(scroll_value.second);
                break;
            }
        } while (!this->lastScrolls.isEmpty());
        this->toggleDatesFlag = false;
    }
}

void MainWindow::refreshVisibility(QString search_text) {
    QStringList mixed_done = {};
    QString settings_str = QString();
    if (this->App->currentDB == "PLUS")
        settings_str = this->App->config->get("headers_plus_visible");
    else if (this->App->currentDB == "MINUS")
        settings_str = this->App->config->get("headers_minus_visible");
    QStringList settings_list = settings_str.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    bool watched_yes = settings_list.contains("watched_yes");
    bool watched_no = settings_list.contains("watched_no");
    bool watched_mixed = settings_list.contains("watched_mixed");
    QString option = this->getWatchedVisibilityOption(watched_yes, watched_no, watched_mixed);

    QTreeWidgetItemIterator it(this->ui.videosWidget);
    while (*it) {
        this->filterVisibilityItem(*it, option, mixed_done, search_text);
        ++it;
    }
    this->old_search = search_text;
}

void MainWindow::refreshVisibility()
{
    if (this->ui.searchBar->isVisible()) {
        this->refreshVisibility(this->old_search);
    }
    else {
        this->refreshVisibility("");
    }
}

void MainWindow::init_icons() {
    if (this->animatedIconFlag && this->animatedIcon)
        this->animatedIcon->showFirstIcon();
    else {
        this->setIcon(this->getIconByStage(0));
    }
}

void MainWindow::openStats() {
    StatsDialog *dialog = new StatsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    int time = this->App->db->getMainInfoValue("timeWatchedTotal", "ALL", "0").toInt();
    dialog->ui.time_placeholder_label->setText(QString::fromStdString(utils::convert_time_to_text(time)));

    QFont font1 = QFont();
    font1.setPointSize(11);

    int ow = this->App->db->getVideosWatched("PLUS", 0);
    int hfw = this->App->db->getVideosWatched("MINUS", 0);

    QLabel *total_videos_watched = new QLabel(QString("Videos watched: <b>%1</b> (<b>%2</b> %4, <b>%3</b> %5)").arg(QString::number(ow + hfw)).arg(QString::number(ow)).arg(QString::number(hfw)).arg(this->App->config->get("plus_category_name")).arg(this->App->config->get("minus_category_name")), dialog);
    total_videos_watched->setTextFormat(Qt::RichText);
    total_videos_watched->setFont(font1);

    double ort = this->App->db->getAverageRating("PLUS", 0);
    double hfrt = this->App->db->getAverageRating("MINUS", 0);

    QLabel *average_total_rating = new QLabel(QString("Average rating: <b>%1</b> (<b>%2</b> %4, <b>%3</b> %5)").arg(QString::number((ort + hfrt) / 2, 'f', 2)).arg(QString::number(ort, 'f', 2)).arg(QString::number(hfrt, 'f', 2)).arg(this->App->config->get("plus_category_name")).arg(this->App->config->get("minus_category_name")), dialog);
    average_total_rating->setTextFormat(Qt::RichText);
    average_total_rating->setFont(font1);

    dialog->ui.stats_container->layout()->addWidget(total_videos_watched);
    dialog->ui.stats_container->layout()->addWidget(average_total_rating);

    this->App->soundPlayer->playSoundEffectChain(1.1);
    this->App->soundPlayer->playSoundEffectChain(1.1);
    this->App->soundPlayer->playSoundEffectChain(1.1);
    this->App->soundPlayer->playSoundEffectChain(1);
    this->App->soundPlayer->playSoundEffectChain(1);
    this->App->soundPlayer->playSoundEffectChain(1);

    dialog->show();
    dialog->raise();
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason) {
    switch (reason) {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick: {
        this->showNormal();
        this->raise();
        this->activateWindow();
        this->updateProgressBar(this->position, this->duration);
        break;
    }
    case QSystemTrayIcon::MiddleClick: {
        this->App->soundPlayer->playSoundEffect();
        this->watchCurrent();
        break;
    }
    case QSystemTrayIcon::Context: {
        QMenu *menu = this->trayIconContextMenu(this);
        menu->popup(QCursor::pos());
        this->connect(menu, &QMenu::aboutToHide, menu, [menu] {menu->deleteLater(); });
        break;
    }
    default:
        ;
    }
}

QMenu* MainWindow::trayIconContextMenu(QWidget* parent) {
    QMenu* traycontextmenu = new QMenu(parent);
    noRightClickEventFilter* filter = new noRightClickEventFilter(traycontextmenu);
    traycontextmenu->installEventFilter(filter);
    traycontextmenu->setObjectName("TrayContextMenu");
    traycontextmenu->setContextMenuPolicy(Qt::CustomContextMenu);
    traycontextmenu->addAction("Watch", [this] {
        this->watchCurrent();
        });
    traycontextmenu->addAction(this->App->config->get_bool("random_next") ? "Next (R)" : "Next", [this] {
        this->NextButtonClicked();
        });
    traycontextmenu->addAction(QString("Change (%1)").arg(this->getCategoryName()), [this] {
        this->switchCurrentDB();
    });
    traycontextmenu->addSeparator();
    traycontextmenu->addAction("Settings", [this] {this->settingsDialogButton(); });
    traycontextmenu->addAction("Stats", [this] {this->openStats(); });
    traycontextmenu->addAction(QIcon(":/shoko/resources/shoko_icon.png"), "Shoko", [this] {this->ui.shoko_button->button_clicked(); });
    traycontextmenu->addSeparator();
    traycontextmenu->addAction("Exit", [this] {this->quit(); });
    return traycontextmenu;
}

void MainWindow::setCounterDialogButton() {
    setCounterDialog dialog = setCounterDialog(this);
    int value = dialog.exec();
    if (value == QDialog::Accepted) {
        if (dialog.flag == "set")
            this->setCounterVar(dialog.ui.spinBox->value());
        if (dialog.flag == "add")
            this->incrementCounterVar(dialog.ui.spinBox->value());
        if (dialog.flag == "substract")
            this->incrementCounterVar(-dialog.ui.spinBox->value());
    }
}

void MainWindow::loadIcons(QString path)
{
    if (QDir(path).exists()) {
        this->IconsStage[0] = utils::getFilesQt(path + "/0");
        this->IconsStage[1] = utils::getFilesQt(path + "/1");
        this->IconsStage[2] = utils::getFilesQt(path + "/2");
    }
    else {
        this->IconsStage[0] = { "" };
        this->IconsStage[1] = { "" };
        this->IconsStage[2] = { "" };
    }
}

QIcon MainWindow::getIconByStage(int stage)
{
    int size = this->IconsStage[stage].size();
    if (size > 0){
        int random_index = utils::randint(0, size - 1);
        return QIcon(this->IconsStage[stage][random_index]);
    }
    return QIcon();
}

void MainWindow::setIconWatchingState(bool watching)
{
    this->iconWatchingState = watching;
}

void MainWindow::updateIconByWatchingState() {
    if (this->animatedIconFlag and this->animatedIcon) {
        if (this->iconWatchingState == true)
            this->animatedIcon->animatedIconEvent.set();
        else
            this->animatedIcon->animatedIconEvent.clear();
    }
    else {
        if (this->iconWatchingState == true)
            this->setIcon(this->getIconByStage(1));
        else
            this->setIcon(this->getIconByStage(2));
    }
}

void MainWindow::setIcon(QIcon icon)
{
    if (!icon.isNull()) {
        QGuiApplication::setWindowIcon(icon);
        //this->setWindowIcon(icon);
        if (this->trayIcon != nullptr) {
            this->trayIcon->setIcon(icon);
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    event->ignore();
    if (this->trayIcon->isVisible() && this->isVisible()) {
        this->App->soundPlayer->playSoundEffect();
        this->hide();
    }
    else {
        this->quit();
    }
}

void MainWindow::quit() {
    this->App->stop_handle();
    QCoreApplication::quit();
}

void MainWindow::initMascotAnimation()
{
    if (this->App->config->get_bool("mascots_animated")) {
        this->App->MascotsAnimation->start_running();
    }
}

void MainWindow::flipMascotDebug(customGraphicsView* sender, Qt::MouseButton button) {
    if (this->App->debug_mode) {
        if (button == Qt::LeftButton)
            sender->flipPixmap();
        else if(button == Qt::RightButton)
            this->flipMascots();
    }
}

void MainWindow::setMascotDebug(QString path, customGraphicsView* sender) {
    if (this->App->debug_mode) {
        sender->setImage(path);
        if (this->App->config->get_bool("mascots_mirror")) {
            if (sender == this->ui.leftImg)
                this->ui.rightImg->setImage(path, true);
            else
                this->ui.leftImg->setImage(path, true);
        }
    }
}

void MainWindow::setLeftMascot(QString path, QPixmap pixmap) {
    if (utils::getParentDirectoryName(path, 1) == "right")
        this->ui.leftImg->setImage(path, pixmap, true);
    else
        this->ui.leftImg->setImage(path, pixmap);
}

void MainWindow::setRightMascot(QString path, QPixmap pixmap) {
    if (utils::getParentDirectoryName(path, 1) == "left")
        this->ui.rightImg->setImage(path, pixmap, true);
    else
        this->ui.rightImg->setImage(path, pixmap);
}

void MainWindow::setLeftMascot(QString path) {
    if (utils::getParentDirectoryName(path, 1) == "right")
        this->ui.leftImg->setImage(path, true);
    else
        this->ui.leftImg->setImage(path);
}

void MainWindow::setRightMascot(QString path) {
    if (utils::getParentDirectoryName(path, 1) == "left")
        this->ui.rightImg->setImage(path, true);
    else
        this->ui.rightImg->setImage(path);
}

void MainWindow::setMascots(bool cache)
{
    try {
        if (this->App->MascotsGenerator->mascots_paths.isEmpty()) {
            this->App->MascotsGenerator->init_mascots_paths(MASCOTS_PATH);
        }
        if (cache) {
            ImageData leftimg_data;
            ImageData rightimg_data;
            leftimg_data = this->App->MascotsGenerator->get_img();
            if (this->App->config->get_bool("mascots_mirror")) {
                rightimg_data = leftimg_data;
            }
            else {
                rightimg_data = this->App->MascotsGenerator->get_img();
            }
            if (!leftimg_data.pixmap.isNull())
                this->setLeftMascot(leftimg_data.path, leftimg_data.pixmap);
            if (!rightimg_data.pixmap.isNull())
                this->setRightMascot(rightimg_data.path, rightimg_data.pixmap);
            if (this->App->MascotsExtractColor) {
                color_area color;
                if (!leftimg_data.accepted_colors.isEmpty())
                    color = utils::get_weighted_random_color(leftimg_data.accepted_colors);
                else if(!leftimg_data.rejected_colors.isEmpty())
                    color = *utils::select_randomly(leftimg_data.rejected_colors.begin(), leftimg_data.rejected_colors.end());
                if(color.color.isValid())
                    this->setThemeHighlightColor(color.color);
            }
        }
        else {
            QString leftimg = "";
            QString rightimg = "";
            leftimg = this->App->MascotsGenerator->get_random_imgpath();
            if (this->App->config->get_bool("mascots_mirror")) {
                rightimg = leftimg;
            }
            else {
                rightimg = this->App->MascotsGenerator->get_random_imgpath();
            }
            if (!leftimg.isEmpty())
                this->setLeftMascot(leftimg);
            if (!rightimg.isEmpty())
                this->setRightMascot(rightimg);
        }
    }
    catch (...) {
        this->hideMascots();
        qDebug("Set Mascots exception.");
    }
}

void MainWindow::setThemeHighlightColor(QColor color) {
    QPalette p = this->App->palette();
    p.setColor(QPalette::Highlight, color);
    p.setColor(QPalette::HighlightedText, utils::complementary_color(color));
    this->changePalette(p);
}

void MainWindow::openThumbnails(std::string path) {
    QString suffix = "_" + QString(QCryptographicHash::hash(path, QCryptographicHash::Md5).toHex()) + ".jpg";
    QString cachename = QFileInfo(QString::fromStdString(path)).completeBaseName() + suffix;
    QString cachepath = QString(THUMBNAILS_CACHE_PATH) + "/" + cachename;
    QFileInfo file(cachepath);
    if (file.exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(file.absoluteFilePath()));
    }
    else {
        QProcess *process = new QProcess();
        generateThumbnailThread::generateThumbnail(*process, suffix, QString::fromStdString(path));
        process->start();
        connect(process, &QProcess::finished, this, [this,file, process](int exitCode, QProcess::ExitStatus exitStatus) {
            process->deleteLater(); 
            if (exitCode == 0) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(file.absoluteFilePath()));
            }
        });
    }
}

void MainWindow::flipMascots()
{
    this->ui.leftImg->flipPixmap();
    this->ui.rightImg->flipPixmap();
}

void MainWindow::showMascots()
{
    this->ui.leftImg->show();
    this->ui.rightImg->show();
    //QTimer::singleShot(20, [this] {this->resize_center(1300, this->height()); });
}

void MainWindow::hideMascots()
{
    this->ui.leftImg->hide();
    this->ui.rightImg->hide();
    //QTimer::singleShot(20, [this] {this->resize_center(800, this->height()); });
}

bool MainWindow::initMusic()
{
    if (this->App->musicPlayer != nullptr) {
        QString inittrack = this->App->config->get("music_init_track_path");
        QString playlist = this->App->config->get("music_playlist_path");
        this->App->musicPlayer->setVolume(this->App->config->get("music_volume").toInt());
        bool music_random_start = this->App->config->get_bool("music_random_start");
        bool music_loop_first = this->App->config->get_bool("music_loop_first");
        if (music_random_start && !music_loop_first) {
            this->App->musicPlayer->init("", playlist,false,true, this->App->config->get_bool("sound_effects_start"));
        }
        else if (music_random_start && music_loop_first) {
            this->App->musicPlayer->init("", playlist,true,true, this->App->config->get_bool("sound_effects_start"));
        }
        else if (!music_random_start && !music_loop_first) {
            this->App->musicPlayer->init(inittrack, playlist,false,false, this->App->config->get_bool("sound_effects_start"));
        }
        else if (!music_random_start && music_loop_first) {
            this->App->musicPlayer->init(inittrack, playlist, true,false, this->App->config->get_bool("sound_effects_start"));
        }
        this->App->musicPlayer->play();
        return true;
    }
    return false;
}

void MainWindow::bringWindowToFront()
{
    this->setWindowState((this->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    this->raise();
    this->activateWindow();
}

void MainWindow::center() {
    QRect frame_geo = this->frameGeometry();
    QScreen *screen = this->window()->windowHandle()->screen();
    QPoint center_loc = screen->geometry().center();
    frame_geo.moveCenter(center_loc);
    this->move(frame_geo.topLeft());
}

void MainWindow::resize_center(int w, int h)
{
    if (this->windowState() != Qt::WindowMaximized) {
        this->resize(w, h);
        this->center();
    }
}

void MainWindow::NextButtonClicked(bool increment) {
    this->NextButtonClicked(this->App->VW->mainListener, increment);
}

void MainWindow::NextButtonClicked(std::shared_ptr<Listener> listener, bool increment) {
    bool video_changed = this->NextVideo(this->App->config->get_bool("random_next"), increment);
    if (listener) {
        this->changeListenerVideo(listener, this->ui.currentVideo->path, 0);
    }
    if (video_changed) {
        if (this->animatedIconFlag)
            this->animatedIcon->setRandomIcon(true);
        if (this->App->config->get_bool("mascots"))
            this->setMascots();
    }
}

bool MainWindow::NextVideo(bool random, bool increment) {
    QList<QTreeWidgetItem*> items = this->ui.videosWidget->findItemsCustom(this->ui.currentVideo->path, Qt::MatchFixedString, ListColumns["PATH_COLUMN"], 1);
    bool video_changed = false;
    if (!items.isEmpty()) {
        this->App->db->db.transaction();
        this->App->db->setMainInfoValue("current",this->App->currentDB,"");
        if(increment)
            this->App->db->updateWatchedState(items.first()->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), this->position, "Yes", increment);
        else
            this->App->db->updateWatchedState(items.first()->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), "Yes", increment);
        if (random) {
            if (this->App->currentDB == "MINUS") {
                if (this->sv_count >= this->sv_target_count) {
                    bool sv_available = this->randomVideo(false, { this->App->config->get("sv_type") });
                    video_changed = sv_available;
                    this->sv_count = 0;
                    this->ui.counterLabel->setProgress(this->sv_count);
                    this->App->db->setMainInfoValue("sv_count", "ALL", QString::number(this->sv_count));
                    if (!sv_available)
                        video_changed = this->randomVideo(false);
                }
                else {
                    video_changed = this->randomVideo(false, {}, { this->App->config->get("sv_type") });
                    this->sv_count++;
                    this->ui.counterLabel->setProgress(this->sv_count);
                    this->App->db->setMainInfoValue("sv_count", "ALL", QString::number(this->sv_count));
                }
            }
            else {
                video_changed = this->randomVideo(false);
            }
        }
        else {
            video_changed = this->setNextVideo(items.first());
        }
        this->App->db->db.commit();
        if (video_changed == false && items.first()->text(ListColumns["PATH_COLUMN"]) != this->ui.currentVideo->path) {
            video_changed = true;
        }

        QDateTime currenttime = QDateTime::currentDateTime();
        items.first()->setData(ListColumns["LAST_WATCHED_COLUMN"], Qt::DisplayRole, currenttime);
        items.first()->setText(ListColumns["WATCHED_COLUMN"], "Yes");
        if(increment)
            items.first()->setText(ListColumns["VIEWS_COLUMN"], QString::number(items.first()->text(ListColumns["VIEWS_COLUMN"]).toInt() + 1));
        items.first()->setForeground(ListColumns["WATCHED_COLUMN"], QBrush(QColor("#00e640")));
        this->refreshVisibility();

        this->App->db->db.transaction();
        if (this->App->currentDB == "MINUS") {
            this->incrementCounterVar(-1);
            if (items.first()->text(ListColumns["TYPE_COLUMN"]) == this->App->config->get("sv_type")) {
                int val = this->calculate_sv_target();
                this->App->db->setMainInfoValue("sv_target_count", "ALL", QString::number(val));
                this->sv_target_count = val;
                this->ui.counterLabel->setMinMax(0, val);
            }
        }
        else if (this->App->currentDB == "PLUS" && increment)
            this->incrementtimeWatchedIncrement(this->App->db->getVideoProgress(items.first()->data(ListColumns["PATH_COLUMN"],CustomRoles::id).toInt(), "0").toDouble());
        this->updateTotalListLabel();
        this->checktimeWatchedIncrement();
        this->updateWatchedProgressBar();
        this->App->db->db.commit();
    }
    else {
        QList<QTreeWidgetItem*> items = this->ui.videosWidget->findItemsCustom("No", Qt::MatchFixedString, ListColumns["WATCHED_COLUMN"], 1);
        if (!items.isEmpty()) {
            this->App->db->db.transaction();
            if (random) {
                if (this->App->currentDB == "MINUS") {
                    if (this->sv_count >= this->sv_target_count) {
                        bool sv_available = this->randomVideo(false, { this->App->config->get("sv_type") });
                        video_changed = sv_available;
                        this->sv_count = 0;
                        this->ui.counterLabel->setProgress(sv_count);
                        this->App->db->setMainInfoValue("sv_count", "ALL", QString::number(this->sv_count));
                        if (!sv_available)
                            video_changed = this->randomVideo(false);
                    }
                    else {
                        video_changed = this->randomVideo(false, {}, { this->App->config->get("sv_type") });
                        this->sv_count++;
                        this->ui.counterLabel->setProgress(sv_count);
                        this->App->db->setMainInfoValue("sv_count", "ALL", QString::number(this->sv_count));
                    }
                }
                else {
                    video_changed = this->randomVideo(false);
                }
            }
            else {
                this->setCurrent(items.first()->data(ListColumns["PATH_COLUMN"],CustomRoles::id).toInt(), items.first()->text(ListColumns["PATH_COLUMN"]), items.first()->text(ListColumns["NAME_COLUMN"]), items.first()->text(ListColumns["AUTHOR_COLUMN"]));
                this->selectCurrentItem(items.first());
                video_changed = true;
            }
            this->App->db->db.commit();
        }
    }
    return video_changed;
}

bool MainWindow::setNextVideo(QTreeWidgetItem* item) {
    QTreeWidgetItem* item_below = this->ui.videosWidget->itemBelow(item);
    while (true) {
        if (item_below == nullptr) {
            item_below = this->ui.videosWidget->topLevelItem(0);
        }
        if (item == item_below) {
            this->setCurrent(-1,"", "", "");
            this->selectCurrentItem(nullptr);
            return false;
            break;
        }
        if (item_below->text(ListColumns["WATCHED_COLUMN"]) == "No") {
            this->setCurrent(item_below->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), item_below->text(ListColumns["PATH_COLUMN"]), item_below->text(ListColumns["NAME_COLUMN"]), item_below->text(ListColumns["AUTHOR_COLUMN"]));
            this->selectCurrentItem(item_below);
            return true;
            break;
        }
        item_below = this->ui.videosWidget->itemBelow(item_below);
    }
}

int MainWindow::calculate_sv_target() {
    if (this->App->currentDB == "MINUS") {
        QTreeWidgetItem* root = this->ui.videosWidget->invisibleRootItem();
        int val = 0;
        int child_count = root->childCount();
        QString sv_type = this->App->config->get("sv_type");
        QMap<QString, int> values = { {sv_type,0},{"OTHERS",0} };
        for (int i = 0; i < child_count; i++) {
            QTreeWidgetItem* item = root->child(i);
            if (item->text(ListColumns["WATCHED_COLUMN"]) == "No" && item->text(ListColumns["TYPE_COLUMN"]) == sv_type)
                values[sv_type]++;
            else if (item->text(ListColumns["WATCHED_COLUMN"]) == "No")
                values["OTHERS"]++;
        }
        if (values[sv_type] > 0) {
            val = floor((double)values["OTHERS"] / (double)values[sv_type]);
        }
        else {
            val = values["OTHERS"];
        }
        return val;
    }
    else {
        return this->sv_target_count;
    }
}

void MainWindow::insertDialogButton() {
    InsertDialog dialog = InsertDialog(this);
    int value = dialog.exec();
    if (value == QDialog::Accepted) {
        QStringList files = dialog.ui.pathText->text().split(" , ");
        QStringList new_files = QStringList();
        for (QString& file : QStringList(files)) {
            QFileInfo fi(file);
            if (fi.exists() && fi.isDir()) {
                files.removeAll(file);
                new_files += utils::getFilesQt(file , true);
            }
        }
        files += new_files;
        this->InsertVideoFiles(files,false);
    }
}

void MainWindow::resetDB(QString directory) {
    this->App->db->resetDB(this->App->currentDB);
    this->ui.videosWidget->clear();
    this->InsertVideoFiles(QStringList({ directory }));
    this->App->db->setMainInfoValue("sv_target_count", "ALL", QString::number(this->calculate_sv_target()));
    this->initListDetails();
    this->updateTotalListLabel();
}

void MainWindow::resetDBDialogButton(QWidget* parent) {
    ResetDBDialog dialog = ResetDBDialog(this->App->config->get("default_directory"), parent);
    int value = dialog.exec();
    if (value == QDialog::Accepted) {
        if (dialog.flag == "set") {
            this->resetDB(dialog.ui.pathText->text());
        }
        if (dialog.flag == "add") {
            this->InsertVideoFiles(QStringList({ dialog.ui.pathText->text() }));
        }
    }
}

bool MainWindow::loadDB(QString path, QWidget* parent) {
    QFileInfo file(path);
    qMainApp->logger->log(QString("Loading Backup Database from \"%1\"").arg(path), "Database", path);
    QMessageBox *msg = new QMessageBox(parent);
    msg->setAttribute(Qt::WA_DeleteOnClose, true);
    msg->setWindowTitle("Load DB");
    int return_code = -1;
    try {
        if (file.exists() and file.isFile()) {
            return_code = this->App->db->loadOrSaveDb(this->App->db->db, path.toStdString().c_str(), false);
            this->initListDetails();
            this->refreshVideosWidget(false, true);
            this->updateTotalListLabel();
            if (this->App->VW->mainListener) {
                this->changeListenerVideo(this->App->VW->mainListener, this->ui.currentVideo->path, this->position);
            }
            if (return_code == 0) {
                //msg->setIcon(QMessageBox::Information); // they trigger windows notifications sound, stupid
                msg->setText(QString("<p align='center'>Load complete!<br>%1</p>").arg(file.fileName()));
            }
            else {
                //msg->setIcon(QMessageBox::Warning); // they trigger windows notifications sound, stupid
                msg->setText(QString("<p align='center'>Load failed!<br>%1</p>").arg(file.fileName()));
            }
        }
        else {
            //msg->setIcon(QMessageBox::Warning); // they trigger windows notifications sound, stupid
            msg->setText(QString("<p align='center'>Load failed! Backup file not found.<br>%1</p>").arg(file.fileName()));
        }
    }
    catch (...) {
        //msg->setIcon(QMessageBox::Warning); // they trigger windows notifications sound, stupid
        msg->setText(QString("<p align='center'>Load failed!<br>%1</p>").arg(file.fileName()));
    }
    msg->show();
    return return_code == 0;
}

bool MainWindow::loadDB(QWidget* parent)
{
    int return_code = -1;
    QDir bak_dir(DATABASE_BACKUPS_PATH);
    QStringList backups = bak_dir.entryList(QStringList() << "*.bak", QDir::Files);
    std::sort(backups.begin(), backups.end(), [](auto const& l, auto const& r) {
        return QDateTime::fromString(QFileInfo(l).baseName(), "yyyy-MM-dd hh mm ss") > QDateTime::fromString(QFileInfo(r).baseName(), "yyyy-MM-dd hh mm ss");
    });
    loadBackupDialog loadDialog = loadBackupDialog(parent);
    loadDialog.ui.comboBox_backups->addItems(backups);
    if (not this->last_backup.isNull() and backups.contains(this->last_backup)) {
        loadDialog.ui.comboBox_backups->setCurrentText(this->last_backup);
        loadDialog.ui.checkBox_remember->setChecked(true);
    }
    if (loadDialog.exec() == QDialog::Accepted) {
        return_code = this->loadDB(QString(DATABASE_BACKUPS_PATH) + "/" + loadDialog.ui.comboBox_backups->currentText(), parent);
        if (loadDialog.ui.checkBox_remember->isChecked()) {
            this->last_backup = loadDialog.ui.comboBox_backups->currentText();
        }
    }
    if (not loadDialog.ui.checkBox_remember->isChecked()) {
        this->last_backup.clear();
    }
    return return_code == 0;
}

bool MainWindow::backupDB(QString path, QWidget* parent) {
    qMainApp->logger->log(QString("Creating Backup Database to \"%1\"").arg(path), "Database", path);
    QMessageBox *msg = new QMessageBox(parent);
    msg->setAttribute(Qt::WA_DeleteOnClose, true);
    msg->setWindowTitle("Backup DB");
    int return_code = -1;
    try {
        return_code = this->App->db->loadOrSaveDb(this->App->db->db, path.toStdString().c_str(), true);
    }
    catch (...) {
    }
    if (return_code == 0) {
        //msg->setIcon(QMessageBox::Information); // they trigger windows notifications sound, stupid
        msg->setText(QString("<p align='center'>Backup complete!<br>%1</p>").arg(QFileInfo(path).fileName()));
    }
    else {
        //msg->setIcon(QMessageBox::Warning); // they trigger windows notifications sound, stupid
        msg->setText(QString("<p align='center'>Backup failed!<br>%1</p>").arg(QFileInfo(path).fileName()));
    }
    msg->show();
    return return_code == 0;
}

bool MainWindow::backupDB(QWidget* parent)
{
    QDir bak_dir(DATABASE_BACKUPS_PATH);
    QStringList backups = bak_dir.entryList(QStringList() << "*.bak", QDir::Files);
    std::sort(backups.begin(), backups.end(), [](auto const& l, auto const& r) {
        return QDateTime::fromString(QFileInfo(l).baseName(), "yyyy-MM-dd hh mm ss") > QDateTime::fromString(QFileInfo(r).baseName(), "yyyy-MM-dd hh mm ss");
    });
    while (backups.size() >= 15) {
        QString oldest_backup = backups.takeLast();
        QString oldest_backup_path = QString(DATABASE_BACKUPS_PATH) + "/" + oldest_backup;
        QFile file(oldest_backup_path);
        if (file.exists()) {
            file.remove();
        }
    }

    QDateTime date = QDateTime::currentDateTime();
    QString backup_time = date.toString("yyyy-MM-dd hh mm ss");
    QString new_backup_path = QString(DATABASE_BACKUPS_PATH) + "/" + backup_time + ".bak";
    return this->backupDB(new_backup_path, parent);
}

void MainWindow::resetWatchedDB(QWidget* parent) {
    FilterSettings settings = FilterSettings();
    settings.json.insert("watched", QJsonArray{ "All" });
    FilterDialog dialog = FilterDialog(this, settings.json, parent);
    dialog.setWindowTitle("Reset Watched Filter Dialog");
    QPushButton* b = dialog.ui.buttonBox->button(QDialogButtonBox::Cancel);
    if (b)
        b->setDefault(true);
    dialog.ui.ignore_defaults->setText("Set others watched state to \"Yes\"");
    int value = dialog.exec();
    if (value == QDialog::Accepted) {
        if(dialog.ui.ignore_defaults->isChecked())
            this->App->db->resetWatched(this->App->currentDB, -1, "Yes");
        this->App->db->resetWatched(this->App->currentDB,dialog.toJson());
        this->refreshVideosWidget(false,true);
        this->App->db->setMainInfoValue("sv_target_count", "ALL", QString::number(this->calculate_sv_target()));
        this->initListDetails();
    }
}

void MainWindow::applySettings(SettingsDialog* dialog) {
    Config* config = this->App->config;
    if (dialog->ui.volumeSpinBox->value() != dialog->oldVolume) {
        config->set("music_volume", QString::number(dialog->ui.volumeSpinBox->value()));
        if (this->App->musicPlayer)
            this->App->musicPlayer->setVolume(dialog->ui.volumeSpinBox->value());
        dialog->oldVolume = dialog->ui.volumeSpinBox->value();
    }
    if (dialog->ui.musicRandomStart->checkState() == Qt::CheckState::Checked)
        config->set("music_random_start", "True");
    else if (dialog->ui.musicRandomStart->checkState() == Qt::CheckState::Unchecked)
        config->set("music_random_start", "False");
    if (dialog->ui.musicLoopFirst->checkState() == Qt::CheckState::Checked)
        config->set("music_loop_first", "True");
    else if (dialog->ui.musicLoopFirst->checkState() == Qt::CheckState::Unchecked)
        config->set("music_loop_first", "False");
    if (dialog->ui.musicOnOff->checkState() == Qt::CheckState::Checked) {
        config->set("music_on", "True");
        if (this->App->musicPlayer == nullptr) {
            this->App->musicPlayer = new MusicPlayer(this->App);
            this->initMusic();
        }
    }
    else if (dialog->ui.musicOnOff->checkState() == Qt::CheckState::Unchecked) {
        config->set("music_on", "False");
        if (this->App->musicPlayer) {
            this->App->musicPlayer->stop();
            delete this->App->musicPlayer;
            this->App->musicPlayer = nullptr;
        }
    }
    if (dialog->ui.AnimatedIcons->checkState() == Qt::CheckState::Checked) {
        config->set("animated_icon_flag", "True");
        this->animatedIconFlag = true;
        this->animatedIcon->showFirstIcon();
    }
    else if (dialog->ui.AnimatedIcons->checkState() == Qt::CheckState::Unchecked) {
        config->set("animated_icon_flag", "False");
        this->animatedIconFlag = false;
        this->updateIconByWatchingState();
    }
    if (dialog->ui.randomIcon->checkState() == Qt::CheckState::Checked) {
        config->set("random_icon", "True");
        if (this->animatedIcon != nullptr) {
            this->animatedIcon->random_icon = true;
            this->animatedIcon->initIcon();
        }
    }
    else if (dialog->ui.randomIcon->checkState() == Qt::CheckState::Unchecked) {
        config->set("random_icon", "False");
        if (this->animatedIcon != nullptr) {
            this->animatedIcon->random_icon = false;
            this->animatedIcon->initIcon();
        }
    }
    if (dialog->ui.mascotsOnOff->checkState() == Qt::CheckState::Checked) {
        config->set("mascots", "True");
        if (this->ui.leftImg->isHidden()) {
            this->setMascots();
            this->showMascots();
            this->App->MascotsAnimation->start_running();
        }
    }
    else if (dialog->ui.mascotsOnOff->checkState() == Qt::CheckState::Unchecked) {
        config->set("mascots", "False");
        if (this->ui.leftImg->isVisible()) {
            this->hideMascots();
            this->App->MascotsAnimation->stop_running();
        }
        this->resetPalette();
    }
    if (dialog->ui.mascotsAllFilesRandom->checkState() == Qt::CheckState::Checked) {
        config->set("mascots_allfiles_random", "True");
        this->App->MascotsGenerator->allfiles_random = true;
    }
    else if (dialog->ui.mascotsAllFilesRandom->checkState() == Qt::CheckState::Unchecked) {
        config->set("mascots_allfiles_random", "False");
        this->App->MascotsGenerator->allfiles_random = false;
    }
    if (dialog->ui.mascotsMirror->checkState() == Qt::CheckState::Checked) {
        bool mascot_mirror_flag = config->get_bool("mascots_mirror");
        config->set("mascots_mirror", "True");
        if (!mascot_mirror_flag) {
            this->setMascots();
        }
    }
    else if (dialog->ui.mascotsMirror->checkState() == Qt::CheckState::Unchecked) {
        bool mascot_mirror_flag = config->get_bool("mascots_mirror");
        config->set("mascots_mirror", "False");
        if (mascot_mirror_flag) {
            this->setMascots();
        }
    }
    if (dialog->ui.mascotsAnimated->checkState() == Qt::CheckState::Checked) {
        bool mascot_anim_flag = config->get_bool("mascots_animated");
        config->set("mascots_animated", "True");
        if (!mascot_anim_flag) {
            this->App->MascotsAnimation->start_running();
        }
    }
    else if (dialog->ui.mascotsAnimated->checkState() == Qt::CheckState::Unchecked) {
        bool mascot_anim_flag = config->get_bool("mascots_animated");
        config->set("mascots_animated", "False");
        if (mascot_anim_flag) {
            this->App->MascotsAnimation->stop_running();
        }
    }
    if (dialog->ui.mascotsExtractColor->checkState() == Qt::CheckState::Checked) {
        this->App->MascotsExtractColor = true;
        config->set("mascots_color_theme", "True");
    }
    else if (dialog->ui.mascotsExtractColor->checkState() == Qt::CheckState::Unchecked) {
        this->App->MascotsExtractColor = false;
        config->set("mascots_color_theme", "False");
        this->resetPalette();
    }
    if (dialog->ui.minusCatRadioBtn->isChecked())
        config->set("current_db", "MINUS");
    if (dialog->ui.plusCatRadioBtn->isChecked())
        config->set("current_db", "PLUS");
    if (dialog->ui.randomNext->checkState() == Qt::CheckState::Checked)
        config->set("random_next", "True");
    else if (dialog->ui.randomNext->checkState() == Qt::CheckState::Unchecked)
        config->set("random_next", "False");
    if (dialog->ui.singleInstance->checkState() == Qt::CheckState::Checked) {
        if (this->App->instanceServer == nullptr) {
            this->App->startSingleInstanceServer(QString::fromStdString(utils::getAppId(VERSION_TEXT)));
        }
        config->set("single_instance", "True");
    }
    else if (dialog->ui.singleInstance->checkState() == Qt::CheckState::Unchecked) {
        if (this->App->instanceServer) {
            this->App->stopSingleInstanceServer();
        }
        config->set("single_instance", "False");
    }
    if (dialog->ui.numlockOnly->checkState() == Qt::CheckState::Checked) {
        this->App->numlock_only_on = true;
        config->set("numlock_only_on", "True");
    }
    else if (dialog->ui.numlockOnly->checkState() == Qt::CheckState::Unchecked) {
        this->App->numlock_only_on = false;
        config->set("numlock_only_on", "False");
    }
    if (dialog->ui.SVspinBox->value() != dialog->oldSVmax) {
        this->App->db->setMainInfoValue("sv_target_count", "ALL", QString::number(dialog->ui.SVspinBox->value()));
        this->sv_target_count = dialog->ui.SVspinBox->value();
        this->ui.counterLabel->setMinMax(0, this->sv_target_count);
        dialog->oldSVmax = dialog->ui.SVspinBox->value();
    }
    if (dialog->ui.mascotsChanceSpinBox->value() != dialog->old_mascotsChanceSpinBox) {
        config->set("mascots_random_chance", QString::number(dialog->ui.mascotsChanceSpinBox->value()));
        this->App->MascotsAnimation->set_random_chance(dialog->ui.mascotsChanceSpinBox->value() / 100.0);
        dialog->old_mascotsChanceSpinBox = dialog->ui.mascotsChanceSpinBox->value();
    }
    if (dialog->ui.mascotsFreqSpinBox->value() != dialog->old_mascotsFreqSpinBox) {
        config->set("mascots_frequency", QString::number(dialog->ui.mascotsFreqSpinBox->value()));
        this->App->MascotsAnimation->frequency = dialog->ui.mascotsFreqSpinBox->value();
        dialog->old_mascotsFreqSpinBox = dialog->ui.mascotsFreqSpinBox->value();
    }
    if (dialog->ui.mascotsRandomChange->checkState() == Qt::CheckState::Checked) {
        config->set("mascots_random_change", "True");
        this->App->MascotsAnimation->random_change = true;
    }
    else if (dialog->ui.mascotsRandomChange->checkState() == Qt::CheckState::Unchecked) {
        config->set("mascots_random_change", "False");
        this->App->MascotsAnimation->random_change = false;
    }
    if (dialog->ui.soundEffectsOnOff->checkState() == Qt::CheckState::Checked) {
        config->set("sound_effects_on", "True");
        if(!this->App->soundPlayer->running)
            this->App->soundPlayer->loadSoundEffects();
        this->App->soundPlayer->start();
    }
    else if (dialog->ui.soundEffectsOnOff->checkState() == Qt::CheckState::Unchecked) {
        config->set("sound_effects_on", "False");
        this->App->soundPlayer->stop();
    }
    config->set("sound_effects_volume", QString::number(dialog->ui.soundEffectsVolume->value()));
    this->App->soundPlayer->setVolume(dialog->ui.soundEffectsVolume->value());
    if (dialog->ui.soundEffectsExit->checkState() == Qt::CheckState::Checked) {
        config->set("sound_effects_exit", "True");
    }
    else if (dialog->ui.soundEffectsExit->checkState() == Qt::CheckState::Unchecked) {
        config->set("sound_effects_exit", "False");
    }
    if (dialog->ui.soundEffectsStart->checkState() == Qt::CheckState::Checked) {
        config->set("sound_effects_start", "True");
    }
    else if (dialog->ui.soundEffectsStart->checkState() == Qt::CheckState::Unchecked) {
        config->set("sound_effects_start", "False");
    }
    if (dialog->ui.soundEffectsClicks->checkState() == Qt::CheckState::Checked) {
        config->set("sound_effects_clicks", "True");
        this->App->installEventFilter(this->App->genEventFilter);
    }
    else if (dialog->ui.soundEffectsClicks->checkState() == Qt::CheckState::Unchecked) {
        config->set("sound_effects_clicks", "False");
        this->App->removeEventFilter(this->App->genEventFilter);

    }
    if (dialog->ui.soundEffectsPlay->checkState() == Qt::CheckState::Checked) {
        config->set("sound_effects_playpause", "True");
        this->App->soundPlayer->effects_playpause = true;
    }
    else if (dialog->ui.soundEffectsPlay->checkState() == Qt::CheckState::Unchecked) {
        config->set("sound_effects_playpause", "False");
        this->App->soundPlayer->effects_playpause = false;
    }
    config->set("sound_effects_chance_playpause", QString::number(dialog->ui.soundEffectsChance->value()));
    this->App->soundPlayer->effect_chance = dialog->ui.soundEffectsChance->value() / 100.0;
    if (dialog->ui.soundEffectsChainPlay->checkState() == Qt::CheckState::Checked) {
        config->set("sound_effects_chain_playpause", "True");
        this->App->soundPlayer->effects_chain_playpause = true;
    }
    else if (dialog->ui.soundEffectsChainPlay->checkState() == Qt::CheckState::Unchecked) {
        config->set("sound_effects_chain_playpause", "False");
        this->App->soundPlayer->effects_chain_playpause = false;
    }
    config->set("sound_effects_chain_chance", QString::number(dialog->ui.soundEffectsChainChance->value()));
    this->App->soundPlayer->effect_chain_chance = dialog->ui.soundEffectsChainChance->value() / 100.0;
    this->ui.next_button->setText(config->get_bool("random_next") ? "Next (R)" : "Next");

    int new_time_watched_limit = (dialog->ui.MinutesSpinBox->value() * 60) + dialog->ui.SecondsSpinBox->value();
    if (this->time_watched_limit != new_time_watched_limit) {
        this->time_watched_limit = new_time_watched_limit;
        this->ui.watchedTimePB->setMaximum(this->time_watched_limit);
        config->set("time_watched_limit", QString::number(new_time_watched_limit));
        this->App->db->db.transaction();
        this->checktimeWatchedIncrement();
        this->updateWatchedProgressBar();
        this->App->db->db.commit();
    }
    if (dialog->ui.aicon_fps_modifier_spinBox->value() != dialog->old_aicon_fps_modifier) {
        config->set("animated_icon_fps_modifier", QString::number(dialog->ui.aicon_fps_modifier_spinBox->value()));
        this->animatedIcon->fps_modifier = dialog->ui.aicon_fps_modifier_spinBox->value();
        dialog->old_aicon_fps_modifier = dialog->ui.aicon_fps_modifier_spinBox->value();
    }
    if (dialog->ui.debugMode->checkState() == Qt::CheckState::Checked) {
        this->setDebugMode(true);
    }
    else {
        this->setDebugMode(false);
    }
    config->save_config();
}

void MainWindow::setDebugMode(bool debug) {
    if (debug == true) {
        this->setIconWatchingState(true);
        this->updateIconByWatchingState();
        QPushButton* btn1 = this->ui.MenuButtons->findChild<QPushButton*>("resetIconBtn");
        if (!btn1) {
            QPushButton* btn1 = new QPushButton(this->ui.MenuButtons);
            btn1->setObjectName("resetIconBtn");
            btn1->setText("Reset Icon");
            connect(btn1, &QPushButton::clicked, this, [this] {this->animatedIcon->setIcon(this->animatedIcon->currentIconPath, false); });
            this->ui.MenuButtons->layout()->addWidget(btn1);
        }
        QPushButton* btn2 = this->ui.MenuButtons->findChild<QPushButton*>("changeThemeBtn");
        if (!btn2) {
            QCustomButton* btn2 = new QCustomButton(this->ui.centralwidget);
            btn2->setObjectName("changeThemeBtn");
            btn2->setText("Theme");
            connect(btn2, &QCustomButton::clicked, this, [this] {
                QPalette p = this->App->palette();
                p.setColor(QPalette::Highlight, QColor(utils::randint(0, 255), utils::randint(0, 255), utils::randint(0, 255)));
                this->changePalette(p);
            });
            connect(btn2, &QCustomButton::rightClicked, this, [this] {
                QPalette p = this->App->style()->standardPalette();
                this->changePalette(p);
            });
            connect(btn2, &QCustomButton::middleClicked, this, [this] {
                QColorDialog d = QColorDialog();
                connect(&d, &QColorDialog::currentColorChanged, this, [this](const QColor& color) {
                    if (color.isValid()) {
                        QPalette p = this->App->palette();
                        p.setColor(QPalette::Highlight, color);
                        this->changePalette(p);
                    }
                });
                d.exec();
                if (d.currentColor().isValid()) {
                    QPalette p = this->App->palette();
                    p.setColor(QPalette::Highlight, d.currentColor());
                    this->changePalette(p);
                }
            });
            this->ui.MenuButtons->layout()->addWidget(btn2);
        }
    }
    else {
        QPushButton* btn1 = this->ui.MenuButtons->findChild<QPushButton*>("resetIconBtn");
        if (btn1)
            btn1->deleteLater();
        QPushButton* btn2 = this->ui.MenuButtons->findChild<QPushButton*>("changeThemeBtn");
        if (btn2)
            btn2->deleteLater();
    }
    this->App->debug_mode = debug;
}

void MainWindow::settingsDialogButton()
{
    SettingsDialog dialog = SettingsDialog(this);
    connect(dialog.ui.buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this, &dialog] {
        this->applySettings(&dialog); dialog.ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false); 
        dialog.oldVolume = dialog.ui.volumeSpinBox->value();
        dialog.old_mascotsChanceSpinBox = dialog.ui.mascotsChanceSpinBox->value();
        dialog.old_mascotsFreqSpinBox = dialog.ui.mascotsFreqSpinBox->value();
        dialog.old_aicon_fps_modifier = dialog.ui.aicon_fps_modifier_spinBox->value();
    });
    int value = dialog.exec();
    if (value == QDialog::Accepted) {
        this->applySettings(&dialog);
    }
    else if (value == QDialog::Rejected) {
        if (dialog.ui.volumeSpinBox->value() != dialog.oldVolume) {
            if (this->App->musicPlayer)
                this->App->musicPlayer->setVolume(dialog.oldVolume);
        }
        if (dialog.ui.mascotsChanceSpinBox->value() != dialog.old_mascotsChanceSpinBox) {
            this->App->MascotsAnimation->set_random_chance(dialog.old_mascotsChanceSpinBox);
        }
        if (dialog.ui.mascotsFreqSpinBox->value() != dialog.old_mascotsFreqSpinBox) {
            this->App->MascotsAnimation->frequency = dialog.old_mascotsFreqSpinBox;
        }
        if (dialog.ui.aicon_fps_modifier_spinBox->value() != dialog.old_aicon_fps_modifier) {
            this->animatedIcon->fps_modifier = dialog.old_aicon_fps_modifier;
        }
    }
}

bool MainWindow::randomVideo(bool watched_all, QStringList vid_type_include, QStringList vid_type_exclude) {
    QTreeWidgetItem* root = this->ui.videosWidget->invisibleRootItem();
    QTreeWidgetItem* item = nullptr;
    int videos_count = root->childCount();
    if (videos_count > 0) {
        if (watched_all) {
            int index = utils::randint(0, videos_count - 1);
            item = root->child(index);
        }
        else {
            QJsonObject settings = QJsonObject(this->filterSettings.json);
            bool ignore_defaults = utils::text_to_bool(settings.value("ignore_defaults").toString().toStdString());
            if (!ignore_defaults) {
                settings.insert("watched", QJsonArray{ "No"});
                settings.insert("types_include", QJsonArray::fromStringList(vid_type_include.isEmpty() ? QStringList({ "All" }) : vid_type_include));
                if(not vid_type_exclude.isEmpty())
                    settings.insert("types_exclude", QJsonArray::fromStringList(vid_type_exclude));
            }
            QString item_name;
            if (this->App->config->get("get_random_mode") == "Filtered") {
                item_name = this->App->db->getRandomVideo(this->App->currentDB, settings);
            }
            else {
                item_name = this->App->db->getRandomVideo(this->App->currentDB, "No", vid_type_include, vid_type_exclude);
            }
            if (!item_name.isEmpty()) {
                QList<QTreeWidgetItem*> items = this->ui.videosWidget->findItemsCustom(item_name, Qt::MatchExactly, ListColumns["PATH_COLUMN"], 1);
                if (!items.isEmpty())
                    item = items.first();
            }
        }
        if (item != nullptr) {
            this->setCurrent(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), item->text(ListColumns["PATH_COLUMN"]), item->text(ListColumns["NAME_COLUMN"]), item->text(ListColumns["AUTHOR_COLUMN"]));
            this->selectCurrentItem(item);
            return true;
        }
        else {
            this->setCurrent(-1, "", "", "");
            this->selectCurrentItem(nullptr);
        }
    }
    return false;
}

void MainWindow::initListDetails() {
    this->App->db->db.transaction();
    this->ui.currentVideo->setValues(this->App->db->getCurrentVideo(this->App->currentDB));
    this->ui.counterLabel->setText(this->App->db->getMainInfoValue("counterVar", "ALL", "0"));
    this->sv_count = this->App->db->getMainInfoValue("sv_count","ALL", "0").toInt();
    this->sv_target_count = this->App->db->getMainInfoValue("sv_target_count", "ALL", "0").toInt();
    this->ui.counterLabel->setMinMax(0, this->sv_target_count);
    this->ui.counterLabel->setProgress(this->sv_count);
    this->checktimeWatchedIncrement();
    this->updateWatchedProgressBar();
    this->updateProgressBar(this->App->db->getVideoProgress(this->ui.currentVideo->id,"0"), utils::getVideoDuration(this->ui.currentVideo->path));
    this->App->db->db.commit();
}

void MainWindow::updateWatchedProgressBar() {
    int value = round(this->App->db->getMainInfoValue("timeWatchedIncrement", "ALL", "0").toDouble());
    this->ui.watchedTimePB->setValue(value);
    int minutes = value / 60;
    int seconds = value % 60;
    //minutes = minutes % 60;
    int minutes_limit = this->time_watched_limit / 60;
    int seconds_limit = this->time_watched_limit % 60;
    //minutes_limit = minutes_limit % 60;
    this->ui.watchedTimePB->setFormat(QString::fromStdString(std::format("{:02d}:{:02d} / {:02d}:{:02d}",minutes,seconds, minutes_limit, seconds_limit)));
}

void MainWindow::updateProgressBar(double position, double duration) {
    if (position >= 0 && this->position != position)
        this->position = position;
    if (duration >= 0 && this->duration != duration)
        this->duration = duration;

    int percent = (this->duration == 0) ? 0 : (int)((this->position / this->duration) * 100);
    this->ui.progressBar->setValue(percent);
    if (this->App->taskbar != nullptr) {
        this->App->taskbar->setProgress(this->App->hwnd, static_cast<ULONGLONG>(percent) * 100, 10000);
    }
    this->ui.progressBar->setFormat(QString::fromStdString(std::format("{} / {}", utils::formatSeconds(this->position), utils::formatSeconds(this->duration))));
}

void MainWindow::updateProgressBar(double position, double duration, std::shared_ptr<Listener> listener, bool running)
{
    //qDebug() << "current" << this->position << listener->currentPosition << this->duration << listener->change_in_progress;
    if(!listener->change_in_progress)
        this->updateProgressBar(position, duration);
    if (!listener->path.isEmpty() && this->position >= this->duration - 0.5) {
        if (listener && listener->endvideo == false && !listener->change_in_progress) {
            if(listener)
                listener->endvideo = true;
            if (!this->finish_dialog) {
                this->finish_dialog = new finishDialog(this);
                this->finish_dialog->setWindowFlags(Qt::WindowStaysOnTopHint);
                this->finish_dialog->setWindowState((this->finish_dialog->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
                this->finish_dialog->raise();
                this->finish_dialog->activateWindow();
                int result = this->finish_dialog->exec();
                if (result == finishDialog::Accepted) {
                    listener->change_in_progress = true;
                    //qDebug() << "clicked" << this->position << listener->currentPosition;
                    if (listener && listener->currentPosition != -1) {
                        this->App->db->updateVideoProgress(listener->video_id, listener->currentPosition);
                    }
                    listener->currentPosition = -1;
                    this->NextButtonClicked(listener);
                    this->position = 0;
                }
                else if (result == finishDialog::Skip) {
                    //Skip button
                    listener->change_in_progress = true;
                    listener->currentPosition = -1;
                    this->NextButtonClicked(listener, false);
                    this->position = 0;
                }
                delete this->finish_dialog; // using deletelater causes some warning because it is already deleted by qt
                this->finish_dialog = nullptr;
            }
        }
    }
    else {
        if(listener && !listener->change_in_progress)
            listener->endvideo = false;
    }
}

void MainWindow::updateProgressBar(QString position, QString duration) {
    if (!position.isEmpty()) {
        double p = position.toDouble();
        if (p >= 0)
            this->position = p;
    }
    if (!duration.isEmpty()) {
        double d = duration.toDouble();
        if (d >= 0 && this->duration != d)
            this->duration = d;
    }
    else if (duration.isEmpty()) {
        this->duration = 0;
    }

    int percent = (this->duration == 0) ? 0 : (int)((this->position / this->duration) * 100);
    this->ui.progressBar->setValue(percent);
    if (this->App->taskbar != nullptr) {
        this->App->taskbar->setProgress(this->App->hwnd, static_cast<ULONGLONG>(percent) * 100, 10000);
    }
    this->ui.progressBar->setFormat(QString::fromStdString(std::format("{} / {}", utils::formatSeconds(this->position), utils::formatSeconds(this->duration))));
}

void MainWindow::DeleteDialogButton(QList<QTreeWidgetItem*> items) {
    DeleteDialog dialog = DeleteDialog(this);
    int value = dialog.exec();
    if (value == QDialog::Accepted) {
        QTreeWidgetItem* root = this->ui.videosWidget->invisibleRootItem();
        if (!items.isEmpty()) {
            this->App->db->db.transaction();
            for (auto item : items) {
                if (item->parent()) {
                    item->parent()->removeChild(item);
                }
                else if(root) {
                    root->removeChild(item);
                }
                this->App->db->deleteVideo(item->data(ListColumns["PATH_COLUMN"],CustomRoles::id).toInt());
                QString suffix = "_" + QString(QCryptographicHash::hash(item->text(ListColumns["PATH_COLUMN"]).toStdString(), QCryptographicHash::Md5).toHex()) + ".jpg";
                QString cachename = QFileInfo(item->text(ListColumns["PATH_COLUMN"])).completeBaseName() + suffix;
                QString cachepath = QString(THUMBNAILS_CACHE_PATH) + "/" + cachename;
                QFile file(cachepath);
                if (file.exists()) {
                    file.remove();
                }
                if (item == this->ui.videosWidget->last_selected)
                    this->ui.videosWidget->last_selected = nullptr;
                delete item;
            }
            this->App->db->db.commit();
            this->updateTotalListLabel();
        }
    }
}

bool MainWindow::InsertVideoFiles(QStringList files, bool update_state, QString currentdb, QString type) {
    for (QString &video : QStringList(files)) {
        QFileInfo fi(video);
        if (fi.exists()) {
            if (fi.isDir()) {
                QStringList newfiles = utils::getFilesQt(video, true);
                files.append(newfiles);
                files.removeAll(video);
            }
        }
        else {
            files.removeAll(video);
        }
    }

    QMimeDatabase db;
    QList<QPair<QString, bool>> valid_files = QList<QPair<QString, bool>>();
    int new_files_counter = 0;
    for (QString& file : files) {
        QMimeType guess_type = db.mimeTypeForFile(file);
        QString path = QDir::toNativeSeparators(file);
        if (guess_type.isValid() && guess_type.name().startsWith("video")) {
            bool exists_in_db = this->App->db->checkIfVideoInDB(path, this->App->currentDB);
            valid_files.append({ path,exists_in_db });
            if (not exists_in_db)
                new_files_counter++;
        }
    }

    if (valid_files.isEmpty())
        return false;

    QString current_db;
    if (!currentdb.isEmpty())
        current_db = currentdb;
    else
        current_db = this->App->currentDB;

    bool set_first_current = true;
    QTreeWidgetItem* root = this->ui.videosWidget->invisibleRootItem();
    QTreeWidgetItemIterator it(this->ui.videosWidget);
    while (*it) {
        if ((*it)->text(ListColumns["WATCHED_COLUMN"]) == "No") {
            set_first_current = false;
            break;
        }
        ++it;
    }

    QString namedir_detected = "";
    QString author_detected = "";
    QString type_detected = "";
    QString name_ = "";
    QString namedir_ = "";
    QString author_ = "";
    QString type_ = "";

    InsertSettingsDialog dialog(this);

    if (!type.isEmpty()) {
        type_detected = type;
        dialog.ui.typeComboBox->setCurrentText(type_detected);
    }
    dialog.setWindowState((dialog.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    dialog.raise();
    dialog.activateWindow();
    dialog.ui.newfilesLabel->setText(dialog.ui.newfilesLabel->text() + QString::number(new_files_counter));

    QSet<QString> authors_names = QSet<QString>();

    for (QPair<QString,bool>& path_pair : valid_files) {
        QStringList tmp = utils::getDirNames(path_pair.first);
        QSet<QString> authors_names_(tmp.begin(),tmp.end());
        authors_names += authors_names_;
        QTreeWidgetItem* item = new TreeWidgetItem({ path_pair.first,"","",""}, 0, nullptr);
        item->setDisabled(path_pair.second);
        item->setData(0, Qt::UserRole, path_pair.second);
        item->setTextAlignment(3, Qt::AlignHCenter);
        dialog.ui.newFilesTreeWidget->addTopLevelItem(item);
        if (path_pair.second and not dialog.ui.showExistingCheckBox->isChecked())
            item->setHidden(true);
    }
    dialog.init_authors(QStringList(authors_names.begin(), authors_names.end()));
    dialog.init_namedir(QStringList(authors_names.begin(), authors_names.end()));
    dialog.init_types();
    dialog.update_treewidget();

    int result = dialog.exec();
    if (result == QDialog::Accepted)
    {
        namedir_detected = dialog.ui.nameDirComboBox->currentText();
        author_detected = dialog.ui.authorComboBox->currentText();
        type_detected = dialog.ui.typeComboBox->currentText();
        if (author_detected != "Auto")
            author_ = author_detected;
        if (type_detected != "Auto")
            type_ = type_detected;
        if (namedir_detected != "Auto")
            namedir_ = namedir_detected;
    }else if (result == QDialog::Rejected)
    {
        return false;
    }

    int batch_size = 100;
    int count = 0;
    bool transaction = false;
    QStringList files_inserted = QStringList();

    it = QTreeWidgetItemIterator(dialog.ui.newFilesTreeWidget);
    while (*it) {
        if (transaction == false) {
            transaction = true;
            this->App->db->db.transaction();
        }
        if ((*it)->isDisabled()) {
            ++it;
            continue;
        }
        this->App->db->insertVideo((*it)->text(0), current_db, (*it)->text(2), (*it)->text(1), (*it)->text(3));
        // probably should remove, mostly unused
        if(update_state)
            this->App->db->updateWatchedState((*it)->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), "No");
        count++;
        if (count == 100) {
            this->App->db->db.commit();
            transaction = false;
            count = 0;
        }
        files_inserted.append((*it)->text(0));
        this->thumbnailManager.enqueue_work({ (*it)->text(0), false});
        ++it;
    }

    if (count > 0) {
        this->App->db->db.commit();
    }
    if (!files_inserted.isEmpty()) {
        this->refreshVideosWidget(false, false);
        this->updateTotalListLabel();
        QString first = files_inserted.first();
        this->ui.videosWidget->findAndScrollToDelayed(first, false);
        this->selectItemsDelayed(files_inserted);
        this->thumbnailManager.start();
        if (set_first_current) {
            QList<QTreeWidgetItem*> items = this->ui.videosWidget->findItemsCustom(first, Qt::MatchExactly, ListColumns["PATH_COLUMN"], 1);
            if (!items.isEmpty()) {
                QTreeWidgetItem *item = items.first();
                this->setCurrent(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), item->text(ListColumns["PATH_COLUMN"]), item->text(ListColumns["NAME_COLUMN"]), item->text(ListColumns["AUTHOR_COLUMN"]));
                this->selectCurrentItem(item,false);
            }
        }
    }
    return true;
}

void MainWindow::watchCurrent() {
    if (this->App->VW->mainListener == nullptr) {
        std::shared_ptr<Listener> l = this->App->VW->newListener(this->ui.currentVideo->path, this->ui.currentVideo->id);
        this->App->VW->setMainListener(l);
        double seconds = this->position;
        if (seconds < 0.001)
            seconds = 0.001;
        l->process->setProgram(this->App->config->get("player_path"));
        if(this->ui.currentVideo->path.isEmpty())
            l->process->setArguments({"/slave",QString::number((unsigned long long int)l->hwnd) });
        else
            l->process->setArguments({ "/open", this->ui.currentVideo->path, "/start", QString::number(seconds * 1000, 'f',4), "/slave",QString::number((unsigned long long int)l->hwnd)});
        qint64 pid = 0;
        l->process->start();
        l->pid = l->process->processId();
        this->VideoInfoNotification();
        qMainApp->logger->log(QString("Playing Current Video \"%1\" from %2").arg(this->ui.currentVideo->path).arg(utils::formatSecondsQt(seconds)), "Video", this->ui.currentVideo->path);
    }
    else {
        utils::bring_hwnd_to_foreground_uiautomation_method(this->App->uiAutomation, this->App->VW->mainListener->mpchc_hwnd);
    }
}

void MainWindow::watchSelected(int video_id, QString path) {
    std::shared_ptr<Listener> l = this->App->VW->newListener(path, video_id);
    double seconds = this->App->db->getVideoProgress(video_id).toDouble();
    if (seconds < 0.001)
        seconds = 0.001;
    l->process->setProgram(this->App->config->get("player_path"));
    if (path.isEmpty())
        l->process->setArguments({"/slave",QString::number((unsigned long long int)l->hwnd) });
    else
        l->process->setArguments({"/open", path, "/start", QString::number(seconds * 1000, 'f',4), "/slave",QString::number((unsigned long long int)l->hwnd) });
    l->process->start();
    l->pid = l->process->processId();
    qMainApp->logger->log(QString("Playing Video \"%1\" from %2").arg(path).arg(utils::formatSecondsQt(seconds)), "Video", path);
}

void MainWindow::refreshVideosWidget(bool selectcurrent, bool remember_selected) {
    QStringList selected_items = {};
    if (remember_selected) {
        for (auto item_ : this->ui.videosWidget->selectedItems()) {
            selected_items.append(item_->text(ListColumns["PATH_COLUMN"]));
        }
    }
    this->ui.videosWidget->clear();
    this->populateList(selectcurrent);
    if (!selected_items.isEmpty())
        this->selectItemsDelayed(selected_items);
}

void MainWindow::switchCurrentDB(QString db) {
    this->App->VW->clearData(false);
    this->lastScrolls.clear();
    std::shared_ptr<Listener> listener = nullptr;
    if (this->App->VW->mainListener) {
        listener = this->App->VW->mainListener;
        if (listener->currentPosition != -1)
            this->App->db->updateVideoProgress(listener->video_id, listener->currentPosition);
    }
    if (!db.isEmpty()) {
        this->App->currentDB = db;
    }
    else if(this->App->currentDB == "MINUS") {
        this->App->currentDB = "PLUS";
    }
    else if (this->App->currentDB == "PLUS") {
        this->App->currentDB = "MINUS";
    }
    qMainApp->logger->log(QString("Switching Current DB to \"%1\"").arg(this->getCategoryName()), "Database");
    this->UpdateWindowTitle();
    if (this->animatedIconFlag)
        this->animatedIcon->initIcon();
    this->initListDetails();
    this->refreshVideosWidget();
    this->refreshHeadersVisibility();
    if (this->toggleDatesFlag)
        this->toggleDates(false);
    this->App->config->set("current_db", this->App->currentDB);
    this->App->config->save_config();
    if(listener != nullptr){
        this->changeListenerVideo(listener, this->ui.currentVideo->path, this->position);
    }
}

void MainWindow::selectCurrentItem(QTreeWidgetItem* item,bool selectcurrent) {
    QTreeWidgetItem* current_item = nullptr;
    if (item != nullptr) {
        current_item = item;
    }
    else {
        QString item_name = this->ui.currentVideo->path;
        QList<QTreeWidgetItem*> items = this->ui.videosWidget->findItemsCustom(item_name, Qt::MatchExactly, ListColumns["PATH_COLUMN"], 1);
        if (!items.isEmpty()) {
            current_item = items.first();
        }
    }
    if (current_item != nullptr) {
        if (this->ui.videosWidget->last_selected != nullptr) {
            for (int i = 0; i < this->ui.videosWidget->last_selected->columnCount(); i++) {
                this->ui.videosWidget->last_selected->setData(i, Qt::BackgroundRole, QVariant());
                if (i == ListColumns["WATCHED_COLUMN"]) {
                    if (this->ui.videosWidget->last_selected->text(i) == "Yes")
                        this->ui.videosWidget->last_selected->setForeground(i, QBrush(QColor("#00e640")));
                    else if (this->ui.videosWidget->last_selected->text(i) == "No")
                        this->ui.videosWidget->last_selected->setForeground(i, QBrush(QColor("#cf000f")));
                }
                else {
                    this->ui.videosWidget->last_selected->setData(i, Qt::ForegroundRole, QVariant());
                }
            }
        }
        this->ui.videosWidget->last_selected = current_item;
        if (selectcurrent) {
            this->ui.videosWidget->scrollToItemCustom(current_item, true, QAbstractItemView::PositionAtCenter);
        }
        for (int i = 0; i < current_item->columnCount(); i++) {
            current_item->setBackground(i, get_highlight_brush(this->App->palette()));
            current_item->setForeground(i, get_highlighted_text_brush(this->App->palette()));
        }
    }
    else {
        this->ui.videosWidget->last_selected = nullptr;
    }
}

void MainWindow::addWatchedDialogButton() {
    addWatchedDialog dialog = addWatchedDialog(this);
    int value = dialog.exec();
    if (value == QDialog::Accepted) {
        this->App->db->db.transaction();
        this->incrementtimeWatchedIncrement(dialog.ui.HH->value() * 3600 + dialog.ui.MM->value() * 60 + dialog.ui.SS->value());
        this->checktimeWatchedIncrement();
        this->updateWatchedProgressBar();
        this->App->db->db.commit();
    }
    dialog.deleteLater();
}

void MainWindow::incrementtimeWatchedIncrement(double value) {
    if(value > 0)
        qMainApp->logger->log(QString("Increasing time watched by %1").arg(utils::formatSecondsQt(value)), "Stats");
    double time = this->App->db->getMainInfoValue("timeWatchedIncrement", "ALL", "0.0").toDouble();
    time += value;
    this->App->db->setMainInfoValue("timeWatchedIncrement", "ALL", QString::number(time));
}

void MainWindow::checktimeWatchedIncrement() {
    double time = this->App->db->getMainInfoValue("timeWatchedIncrement", "ALL", "0.0").toDouble();
    int count = time / this->time_watched_limit;
    if (count > 0) {
        this->incrementtimeWatchedIncrement(-(count * this->time_watched_limit));
        this->incrementCounterVar(count);
    }
}

void MainWindow::incrementCounterVar(int value) {
    if(value >= 0)
        qMainApp->logger->log(QString("Increasing Counter by %1").arg(value), "Stats");
    else
        qMainApp->logger->log(QString("Decreasing Counter by %1").arg(value), "Stats");
    int oldval = this->ui.counterLabel->text().toInt();
    this->setCounterVar(oldval + value);
}

void MainWindow::setCounterVar(int value) {
    this->App->db->setMainInfoValue("counterVar", "ALL", QString::number(value));
    this->ui.counterLabel->setText(QString::number(value));
}

int MainWindow::getCounterVar() {
    return this->App->db->getMainInfoValue("counterVar", "ALL", "0").toInt();
}

void MainWindow::switchNextButtonMode(QCustomButton* nextbutton) {
    this->App->config->set("random_next", utils::bool_to_text_qt(!this->App->config->get_bool("random_next")));
    if(nextbutton != this->ui.next_button)
        this->ui.next_button->setText(this->App->config->get_bool("random_next") ? "Next (R)" : "Next");
    nextbutton->setText(this->App->config->get_bool("random_next") ? "Next (R)" : "Next");
    this->App->config->save_config();
}

void MainWindow::switchRandomButtonMode(QCustomButton* randombutton) {
    QString mode = this->App->config->get("get_random_mode");
    if (mode == "Normal")
        mode = "Filtered";
    else if (mode == "Filtered")
        mode = "All";
    else
        mode = "Normal";
    this->App->config->set("get_random_mode", mode);
    if (randombutton != this->ui.random_button) {
        if (this->App->config->get("get_random_mode") == "All") {
            this->ui.random_button->setText("Get Random (All)");
        }
        else if (this->App->config->get("get_random_mode") == "Filtered") {
            this->ui.random_button->setText("Get Random (F)");
        }
        else {
            this->ui.random_button->setText("Get Random");
        }
    }
    if (this->App->config->get("get_random_mode") == "All") {
        randombutton->setText("Get Random (All)");
    }
    else if (this->App->config->get("get_random_mode") == "Filtered") {
        randombutton->setText("Get Random (F)");
    }
    else {
        randombutton->setText("Get Random");
    }
    this->App->config->save_config();
}

void MainWindow::updateSortConfig() {
    this->App->config->set("sort_column", ListColumns_reversed[this->ui.videosWidget->header()->sortIndicatorSection()]);
    this->App->config->set("sort_order", sortingDict_reversed[this->ui.videosWidget->header()->sortIndicatorOrder()]);
    this->App->config->save_config();
}

void MainWindow::filterVisibilityItem(QTreeWidgetItem *item, QString watched_option, QStringList &mixed_done, QString search_text) {

    bool hidden = false;
    QList<QTreeWidgetItem*> items = {};

    //watched
    if (watched_option == "Mixed" && item->text(ListColumns["WATCHED_COLUMN"]) == "No") {
        if (mixed_done.contains(item->text(ListColumns["AUTHOR_COLUMN"]))) {
            hidden = false;
        }
        else {
            hidden = false;
            items = this->ui.videosWidget->findItems(item->text(ListColumns["AUTHOR_COLUMN"]), Qt::MatchExactly, ListColumns["AUTHOR_COLUMN"]);
            if (!items.isEmpty()) {
                mixed_done.append(item->text(ListColumns["AUTHOR_COLUMN"]));
            }
        }
    }
    else if (watched_option == "Yes" && item->text(ListColumns["WATCHED_COLUMN"]) == watched_option)
        hidden = false;
    else if (watched_option == "No" && item->text(ListColumns["WATCHED_COLUMN"]) == watched_option)
        hidden = false;
    else if (watched_option == "All")
        hidden = false;
    else
        hidden = true;

    //search
    if(not search_text.isEmpty() and hidden == false) {
        double score = rapidfuzz::fuzz::partial_ratio(search_text.toLower().toStdString(), (item->text(ListColumns["PATH_COLUMN"]) % " " % item->text(ListColumns["AUTHOR_COLUMN"]) % " " % item->text(ListColumns["TYPE_COLUMN"])).toLower().toStdString());
        if (score > 80) {
            hidden = false;
        }
        else {
            hidden = true;
        }
    }

    //actually hidding the item/items
    item->setHidden(hidden);
    if (not items.isEmpty()) {
        for (auto& i : items)
            i->setHidden(hidden);
    }
}

void MainWindow::populateList(bool selectcurrent) {
    auto itemlist = this->App->db->getVideos(this->App->currentDB);
    this->ui.videosWidget->setSortingEnabled(false);
    this->ui.videosWidget->addTopLevelItems(itemlist);

    QStringList mixed_done = {};
    QString settings_str = QString();
    if (this->App->currentDB == "PLUS")
        settings_str = this->App->config->get("headers_plus_visible");
    else if (this->App->currentDB == "MINUS")
        settings_str = this->App->config->get("headers_minus_visible");
    QStringList settings_list = settings_str.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    bool watched_yes = settings_list.contains("watched_yes");
    bool watched_no = settings_list.contains("watched_no");
    bool watched_mixed = settings_list.contains("watched_mixed");
    QString option = this->getWatchedVisibilityOption(watched_yes, watched_no, watched_mixed);

    for (auto &item : itemlist)
    {
        item->setTextAlignment(ListColumns["WATCHED_COLUMN"], Qt::AlignHCenter);
        item->setTextAlignment(ListColumns["VIEWS_COLUMN"], Qt::AlignHCenter);
        item->setTextAlignment(ListColumns["TYPE_COLUMN"], Qt::AlignHCenter);
        item->setTextAlignment(ListColumns["DATE_CREATED_COLUMN"], Qt::AlignHCenter);
        item->setTextAlignment(ListColumns["LAST_WATCHED_COLUMN"], Qt::AlignHCenter);
        if (item->text(ListColumns["WATCHED_COLUMN"]) == "Yes")
            item->setForeground(ListColumns["WATCHED_COLUMN"], QBrush(QColor("#00e640")));
        else if (item->text(ListColumns["WATCHED_COLUMN"]) == "No")
            item->setForeground(ListColumns["WATCHED_COLUMN"], QBrush(QColor("#cf000f")));
        if (this->ui.searchBar->isVisible())
            this->filterVisibilityItem(item,option,mixed_done,this->old_search);
        else
            this->filterVisibilityItem(item, option, mixed_done, "");
    }
    // not sure if this alternative method is needed anymore
    //if (this->ui.searchBar->isVisible())
    //    QTimer::singleShot(1, [this,itemlist] {this->search(this->old_search);});
    this->updateTotalListLabel();
    this->ui.videosWidget->setSortingEnabled(true);
    this->selectCurrentItem(nullptr, selectcurrent);
}

void MainWindow::videosWidgetHeaderContextMenu(QPoint point) {
    QString settings_str = QString();
    if (this->App->currentDB == "PLUS")
        settings_str = this->App->config->get("headers_plus_visible");
    else if (this->App->currentDB == "MINUS")
        settings_str = this->App->config->get("headers_minus_visible");
    QStringList settings_list = settings_str.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

    QMenu menu = QMenu(this);
    menu.setAttribute(Qt::WA_DeleteOnClose);
    QAction* author = new QAction("Author", &menu);
    author->setCheckable(true);
    if (settings_list.contains("author"))
        author->setChecked(true);
    menu.addAction(author);
    QAction* name = new QAction("Name", &menu);
    name->setCheckable(true);
    if (settings_list.contains("name"))
        name->setChecked(true);
    menu.addAction(name);
    QAction *path = new QAction("Path", &menu);
    path->setCheckable(true);
    if (settings_list.contains("path"))
        path->setChecked(true);
    menu.addAction(path);
    QAction* vid_type = new QAction("Type", &menu);
    vid_type->setCheckable(true);
    if (settings_list.contains("type"))
        vid_type->setChecked(true);
    menu.addAction(vid_type);
    QMenu* watched = new QMenu("Watched", &menu);
    QAction* watched_yes = new QAction("Yes", watched);
    watched_yes->setCheckable(true);
    if (settings_list.contains("watched_yes"))
        watched_yes->setChecked(true);
    QAction* watched_no = new QAction("No", watched);
    watched_no->setCheckable(true);
    if (settings_list.contains("watched_no"))
        watched_no->setChecked(true);
    QAction* watched_mixed = new QAction("Mixed", watched);
    watched_mixed->setCheckable(true);
    if (settings_list.contains("watched_mixed"))
        watched_mixed->setChecked(true);
    watched->addAction(watched_yes);
    watched->addAction(watched_no);
    watched->addAction(watched_mixed);
    menu.addMenu(watched);
    QAction* views = new QAction("Views", &menu);
    views->setCheckable(true);
    if (settings_list.contains("views"))
        views->setChecked(true);
    menu.addAction(views);
    QAction* rating = new QAction("Rating", &menu);
    rating->setCheckable(true);
    if (settings_list.contains("rating"))
        rating->setChecked(true);
    menu.addAction(rating);
    QAction* date_created = new QAction("Date Created", &menu);
    date_created->setCheckable(true);
    if (settings_list.contains("date_created"))
        date_created->setChecked(true);
    menu.addAction(date_created);
    QAction* last_watched = new QAction("Last Watched", &menu);
    last_watched->setCheckable(true);
    if (settings_list.contains("last_watched"))
        last_watched->setChecked(true);
    menu.addAction(last_watched);
    QAction *menu_click = menu.exec(this->ui.videosWidget->header()->viewport()->mapToGlobal(point));

    if (menu_click == author) {
        if (utils::hiddenCheck(settings_list) && !author->isChecked())
            settings_list.removeAll("author");
        else
            settings_list.append("author");
    }
    if (menu_click == name) {
        if (utils::hiddenCheck(settings_list) && !name->isChecked())
            settings_list.removeAll("name");
        else
            settings_list.append("name");
    }
    if (menu_click == path) {
        if (utils::hiddenCheck(settings_list) && !path->isChecked())
            settings_list.removeAll("path");
        else
            settings_list.append("path");
    }
    else if (menu_click == vid_type) {
        if (utils::hiddenCheck(settings_list) && !vid_type->isChecked())
            settings_list.removeAll("type");
        else
            settings_list.append("type");
    }
    else if (menu_click == watched_yes) {
        if (utils::hiddenCheck(settings_list) && !watched_yes->isChecked())
            settings_list.removeAll("watched_yes");
        else {
            settings_list.removeAll("watched_mixed");
            settings_list.append("watched_yes");
        }
    }
    else if (menu_click == watched_no) {
        if (utils::hiddenCheck(settings_list) && !watched_no->isChecked())
            settings_list.removeAll("watched_no");
        else {
            settings_list.removeAll("watched_mixed");
            settings_list.append("watched_no");
        }
    }
    else if (menu_click == watched_mixed) {
        if (utils::hiddenCheck(settings_list) && !watched_mixed->isChecked())
            settings_list.removeAll("watched_mixed");
        else {
            settings_list.removeAll("watched_yes");
            settings_list.removeAll("watched_no");
            settings_list.append("watched_mixed");
        }
    }
    else if (menu_click == views) {
        if (utils::hiddenCheck(settings_list) && !views->isChecked())
            settings_list.removeAll("views");
        else
            settings_list.append("views");
    }
    else if (menu_click == rating) {
        if (utils::hiddenCheck(settings_list) && !rating->isChecked())
            settings_list.removeAll("rating");
        else
            settings_list.append("rating");
    }
    else if (menu_click == date_created) {
        if (utils::hiddenCheck(settings_list) && !date_created->isChecked())
            settings_list.removeAll("date_created");
        else
            settings_list.append("date_created");
    }
    else if (menu_click == last_watched) {
        if (utils::hiddenCheck(settings_list) && !last_watched->isChecked())
            settings_list.removeAll("last_watched");
        else
            settings_list.append("last_watched");
    }
    if (menu_click) {
        settings_list.removeDuplicates();
        if (this->App->currentDB == "PLUS")
            this->App->config->set("headers_plus_visible", settings_list.join(" "));
        else if (this->App->currentDB == "MINUS")
            this->App->config->set("headers_minus_visible", settings_list.join(" "));
        this->refreshHeadersVisibility();
        this->refreshVisibility();
        if (this->toggleDatesFlag)
            this->toggleDates(false);
        this->App->config->save_config();
    }
}

void MainWindow::videosWidgetContextMenu(QPoint point) {
    QModelIndex index = this->ui.videosWidget->indexAt(point);
    if (!index.isValid())
        return;
    
    QTreeWidgetItem* item = this->ui.videosWidget->itemAt(point);
    QList<QTreeWidgetItem*> items = this->ui.videosWidget->selectedItems();

    QMenu menu = QMenu(this);
    QAction* play_video = new QAction("Play", &menu);
    QAction* sync_items = new QAction("Sync Items", &menu);
    menu.addAction(play_video);
    if (items.size() > 1) {
        menu.addAction(sync_items);
    }
    QAction* generate_author = new QAction("Generate Author", &menu);
    menu.addAction(generate_author);
    QAction* generate_name = new QAction("Generate Name", &menu);
    menu.addAction(generate_name);
    QMenu* set_menu = new QMenu("Set", &menu);
    QAction* set_current = new QAction("Set as current", set_menu);
    set_menu->addAction(set_current);
    QAction* author_edit = new QAction("Edit Author", set_menu);
    set_menu->addAction(author_edit);
    QAction* name_edit = new QAction("Edit Name", set_menu);
    set_menu->addAction(name_edit);
    QAction* update_path = new QAction("Update Path", set_menu);
    if (items.size() == 1) {
        set_menu->addAction(update_path);
    }
    QMenu* watched_menu = new QMenu("Set Watched as", &menu);
    QAction* watched_yes = new QAction("Yes", watched_menu);
    QAction* watched_no = new QAction("No", watched_menu);
    watched_menu->addActions({ watched_yes, watched_no });
    set_menu->addMenu(watched_menu);
    QMenu* category_menu = new QMenu("Set Type as", &menu);
    for (QString& type : videoTypes) {
        QAction* new_type = new QAction(type, category_menu);
        category_menu->addAction(new_type);
    }
    set_menu->addMenu(category_menu);
    QMenu* views_menu = new QMenu("Views", &menu);
    QAction* views_edit = new QAction("Edit", views_menu);
    QAction* views_plus = new QAction("Increase by 1", views_menu);
    QAction* views_minus = new QAction("Decrease by 1", views_menu);
    views_menu->addActions({views_edit, views_plus, views_minus });
    set_menu->addMenu(views_menu);
    menu.addMenu(set_menu);
    QAction* open_location = new QAction("Open file location", &menu);
    menu.addAction(open_location);
    QAction* delete_video = new QAction("Delete", &menu);
    menu.addAction(delete_video);
    QAction* menu_click = menu.exec(this->ui.videosWidget->viewport()->mapToGlobal(point));

    if (!menu_click)
        return;

    if (menu_click == play_video)
        this->watchSelected(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), item->text(ListColumns["PATH_COLUMN"]));
    else if (menu_click == set_current) {
        this->setCurrent(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), item->text(ListColumns["PATH_COLUMN"]), item->text(ListColumns["NAME_COLUMN"]), item->text(ListColumns["AUTHOR_COLUMN"]));
        this->selectCurrentItem();
        if (this->App->VW->mainListener) {
            this->changeListenerVideo(this->App->VW->mainListener, this->ui.currentVideo->path, this->position);
        }
    }
    else if (menu_click == open_location) {
        utils::openFileExplorer(item->text(ListColumns["PATH_COLUMN"]));
    }
    else if (menu_click == delete_video)
        this->DeleteDialogButton(items);
    else if (menu_click == watched_yes)
        this->setWatched("Yes", items);
    else if (menu_click == watched_no)
        this->setWatched("No", items);
    else if (menu_click == views_edit) {
        bool ok;
        int old_value = item->text(ListColumns["VIEWS_COLUMN"]).toInt();
        int i = QInputDialog::getInt(this, "Edit Views",
            "Views", old_value, 0, 2147483647, 1, &ok);
        if (ok)
            this->setViews(i, items);
    }
    else if (menu_click == views_plus)
        this->incrementViews(1, items);
    else if (menu_click == views_minus)
        this->incrementViews(-1, items);
    else if (menu_click == generate_author) {
        this->updateAuthors(items);
    }
    else if (menu_click == generate_name) {
        this->updateNames(items);
    }
    else if (menu_click == author_edit) {
        bool ok;
        QString old_value = item->text(ListColumns["AUTHOR_COLUMN"]);
        QString a = QInputDialog::getText(this, "Edit Author",
            "Author", QLineEdit::Normal, old_value, &ok);
        if (ok) {
            this->updateAuthors(a, items);
        }
    }
    else if (menu_click == name_edit) {
        bool ok;
        QString old_value = item->text(ListColumns["NAME_COLUMN"]);
        QString a = QInputDialog::getText(this, "Edit Name",
            "Name", QLineEdit::Normal, old_value, &ok);
        if (ok) {
            this->updateNames(a, items);
        }
    }
    else if (menu_click == update_path) {
        QString old_path = item->text(ListColumns["PATH_COLUMN"]);
        QFileInfo fileInfo(old_path);
        QString new_path = QFileDialog::getOpenFileName(this, "Select new path", fileInfo.absolutePath());
        if (!new_path.isEmpty()) {
            new_path = QDir::toNativeSeparators(new_path);
            this->updatePath(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), new_path);
            if (old_path == this->ui.currentVideo->path) {
                auto items = this->ui.videosWidget->findItemsCustom(new_path, Qt::MatchFixedString, ListColumns["PATH_COLUMN"], 1);
                if (not items.isEmpty()) {
                    auto item = items.first();
                    this->setCurrent(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(),item->text(ListColumns["PATH_COLUMN"]), item->text(ListColumns["NAME_COLUMN"]), item->text(ListColumns["AUTHOR_COLUMN"]));
                    if (this->App->VW->mainListener) {
                        this->changeListenerVideo(this->App->VW->mainListener, this->ui.currentVideo->path, 0);
                    }
                }
            }
        }
    }
    else if (menu_click == sync_items) {
        QMessageBox msgBox;
        msgBox.setWindowTitle("Sync Items?");
        msgBox.setText("Sync Items?");
        msgBox.setStandardButtons(QMessageBox::Yes);
        msgBox.addButton(QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);
        if (msgBox.exec() == QMessageBox::Yes) {
            QList<QTreeWidgetItem*> items_ = items;
            items_.removeAll(item);
            this->syncItems(item, items_);
        }
    }
    else if (menu_click->parent() == category_menu)
        this->setType(menu_click->text(),items);
}

void MainWindow::setType(QString type, QList<QTreeWidgetItem*> items) {
    QString last = items.last()->text(ListColumns["PATH_COLUMN"]);
    int lastScroll = this->ui.videosWidget->verticalScrollBar()->value();
    if (!items.isEmpty()) {
        this->App->db->db.transaction();
        for (auto const& item : items) {
            this->App->db->updateType(item->data(ListColumns["PATH_COLUMN"],CustomRoles::id).toInt(), type);
        }
        this->App->db->db.commit();
        this->refreshVideosWidget(false,true);
        this->ui.videosWidget->findAndScrollToDelayed(last, false);
        this->ui.videosWidget->scrollToVerticalPositionDelayed(lastScroll);
    }
}

void MainWindow::setViews(int value, QList<QTreeWidgetItem*> items) {
    QString last = items.last()->text(ListColumns["PATH_COLUMN"]);
    int lastScroll = this->ui.videosWidget->verticalScrollBar()->value();
    if (!items.isEmpty()) {
        this->App->db->db.transaction();
        for (auto const& item : items) {
            this->App->db->setViews(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), value);
        }
        this->App->db->db.commit();
        this->refreshVideosWidget(false,true);
        this->ui.videosWidget->findAndScrollToDelayed(last, false);
        this->ui.videosWidget->scrollToVerticalPositionDelayed(lastScroll);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) {
        e->accept();
    }
    else {
        e->ignore();
    }
}
void MainWindow::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasUrls()) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
    else {
        e->ignore();
    }
}
void MainWindow::dropEvent(QDropEvent* e) {
    if (e->mimeData()->hasUrls()) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
        QStringList pathList;
        QWidget* widget = this->childAt(e->pos());
        foreach(const QUrl & url, e->mimeData()->urls()) {
            QString fileName = QDir::toNativeSeparators(url.toLocalFile());
            pathList.append(fileName);
            if (widget) {
                if (widget->objectName() == "customGraphicsView_viewport") {
                    widget = widget->parentWidget();
                    if (not widget)
                        return;
                }
            }
        }
        if (!pathList.isEmpty()) {
            emit fileDropped(pathList,widget);
        }
    }
    else {
        e->ignore();
    }
}

void MainWindow::incrementViews(int count, QList<QTreeWidgetItem*> items) {
    QString last = items.last()->text(ListColumns["PATH_COLUMN"]);
    int lastScroll = this->ui.videosWidget->verticalScrollBar()->value();
    if (!items.isEmpty()) {
        this->App->db->db.transaction();
        for (auto const& item : items) {
            this->App->db->incrementVideoViews(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), count);
        }
        this->App->db->db.commit();
        this->refreshVideosWidget(false,true);
        this->ui.videosWidget->findAndScrollToDelayed(last, false);
        this->ui.videosWidget->scrollToVerticalPositionDelayed(lastScroll);
    }
}

void MainWindow::setWatched(QString value, QList<QTreeWidgetItem*> items) {
    QString last = items.last()->text(ListColumns["PATH_COLUMN"]);
    int lastScroll = this->ui.videosWidget->verticalScrollBar()->value();
    if (!items.isEmpty()) {
        this->App->db->db.transaction();
        for (auto const& item : items) {
            this->App->db->updateWatchedState(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), value);
        }
        this->App->db->db.commit();
        this->refreshVideosWidget(false,true);
        this->ui.videosWidget->findAndScrollToDelayed(last, false);
        this->ui.videosWidget->scrollToVerticalPositionDelayed(lastScroll);
    }
}

void MainWindow::selectItems(QStringList items, bool clear_selection) {
    if (items.isEmpty())
        return;
    if (clear_selection)
        this->ui.videosWidget->clearSelection();
    QTreeWidgetItemIterator it(this->ui.videosWidget);
    while (*it) {
        if (items.contains((*it)->text(ListColumns["PATH_COLUMN"]))) {
            (*it)->setSelected(true);
        }
        ++it;
    }
}

void MainWindow::selectItemsDelayed(QStringList items, bool clear_selection) {
    if (items.isEmpty())
        return;
    QTimer::singleShot(0, [this, items,clear_selection] {
        this->selectItems(items, clear_selection);
    });
}

void MainWindow::setCurrent(int id, QString path, QString name, QString author) {
    this->ui.currentVideo->setValues(id, path,name,author);
    this->updateProgressBar(this->App->db->getVideoProgress(this->ui.currentVideo->id), utils::getVideoDuration(this->ui.currentVideo->path));
    this->App->db->setMainInfoValue("current", this->App->currentDB, path);
    qMainApp->logger->log(QString("Setting current Video to \"%1\"").arg(path), "Video",path);
}

QString MainWindow::getWatchedVisibilityOption(bool watched_yes, bool watched_no, bool watched_mixed) {
    QString option = "";
    if (watched_mixed == true)
        option = "Mixed";
    else if ((watched_yes == true) && (watched_no == true))
        option = "All";
    else if (watched_yes == true)
        option = "Yes";
    else if (watched_no == true)
        option = "No";
    else
        option = "All";
    return option;
}

void MainWindow::refreshHeadersVisibility() {
    QString settings_str = QString();
    if(this->App->currentDB == "PLUS")
        settings_str = this->App->config->get("headers_plus_visible");
    else if (this->App->currentDB == "MINUS")
        settings_str = this->App->config->get("headers_minus_visible");
    QStringList settings_list = settings_str.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    bool watched_yes = settings_list.contains("watched_yes");
    bool watched_no = settings_list.contains("watched_no");
    bool watched_mixed = settings_list.contains("watched_mixed");

    QHeaderView *header = this->ui.videosWidget->header();
    if (settings_list.contains("author"))
        header->setSectionHidden(ListColumns["AUTHOR_COLUMN"], false);
    else
        header->setSectionHidden(ListColumns["AUTHOR_COLUMN"], true);
    if (settings_list.contains("name"))
        header->setSectionHidden(ListColumns["NAME_COLUMN"], false);
    else
        header->setSectionHidden(ListColumns["NAME_COLUMN"], true);
    if (settings_list.contains("path"))
        header->setSectionHidden(ListColumns["PATH_COLUMN"], false);
    else
        header->setSectionHidden(ListColumns["PATH_COLUMN"], true);
    if (settings_list.contains("type"))
        header->setSectionHidden(ListColumns["TYPE_COLUMN"], false);
    else
        header->setSectionHidden(ListColumns["TYPE_COLUMN"], true);
    if (settings_list.contains("views"))
        header->setSectionHidden(ListColumns["VIEWS_COLUMN"], false);
    else
        header->setSectionHidden(ListColumns["VIEWS_COLUMN"], true);
    if (settings_list.contains("rating"))
        header->setSectionHidden(ListColumns["RATING_COLUMN"], false);
    else
        header->setSectionHidden(ListColumns["RATING_COLUMN"], true);
    if (settings_list.contains("date_created"))
        header->setSectionHidden(ListColumns["DATE_CREATED_COLUMN"], false);
    else
        header->setSectionHidden(ListColumns["DATE_CREATED_COLUMN"], true);
    if (settings_list.contains("last_watched"))
        header->setSectionHidden(ListColumns["LAST_WATCHED_COLUMN"], false);
    else
        header->setSectionHidden(ListColumns["LAST_WATCHED_COLUMN"], true);
    if ((watched_yes == false) && (watched_no == false) && (watched_mixed == false))
        header->setSectionHidden(ListColumns["WATCHED_COLUMN"], true);
    else if (watched_yes == true || watched_no == true || watched_mixed == true)
        header->setSectionHidden(ListColumns["WATCHED_COLUMN"], false);
}

void MainWindow::updateTotalListLabel(bool force_update) {
    QTreeWidgetItem *root = this->ui.videosWidget->invisibleRootItem();
    int child_count = root->childCount();
    QTreeWidgetItemIterator it(this->ui.videosWidget);
    std::map<std::string, int> values = { {"Yes",0 },{"No" , 0}, {"All" , child_count}};
    while (*it) {
        if ((*it)->text(ListColumns["WATCHED_COLUMN"]) == "No")
            values["No"]++;
        ++it;
    }
    values["Yes"] = values["All"] - values["No"];
    this->ui.totalListLabel->setText(QString::number(values["No"]));
    bool updated = false;
    if (this->ui.totalListLabel->progress() != values["No"]) {
        this->ui.totalListLabel->setProgress(values["No"],false);
        updated = true;
    }
    if (this->ui.totalListLabel->maximum() != values["All"]) {
        this->ui.totalListLabel->setMinMax(0, values["All"],false);
        updated = true;
    }
    if (updated)
        this->ui.totalListLabel->update();
}

void MainWindow::changeListenerVideo(std::shared_ptr<Listener> listener, QString path, double position) {
    QFuture<void> change_video = QtConcurrent::run([this, listener,path,position] {listener->change_video(path, position); });
    if (this->finish_dialog) {
        this->finish_dialog->deleteLater();
        this->finish_dialog = nullptr;
    }
    this->VideoInfoNotification();
}

MainWindow::~MainWindow()
{
    this->animatedIcon->running = false;
    this->animatedIcon->quit();
    this->animatedIcon->deleteLater();
    delete this->search_timer;
    delete this->active;
    delete this->inactive;
    delete this->halfactive;
}
