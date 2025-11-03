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
#include <QDesktopServices>
#include "generateThumbnailRunnable.h"
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
#include "customQButton.h"
#include "scrollAreaEventFilter.h"
#include "TreeWidgetItem.h"
#include "ProgressBarQLabel.h"
#include <QColorDialog>
#include "FilterDialog.h"
#include "noRightClickEventFilter.h"
#include "DateItemDelegate.h"
#include "colorPaletteExtractor.h"
#include "loadBackupDialog.h"
#include "TagsDialog.h"
#include "UpdatePathsDialog.h"

#pragma warning(push ,3)
#include "rapidfuzz_all.hpp"
#pragma warning(pop)

#include "stardelegate.h"

MainWindow::MainWindow(QWidget *parent,MainApp *App)
    : QMainWindow(parent)
{
    this->App = App;
    this->thumbnailManager = new generateThumbnailManager(this->App->config->get("threads").toInt());
    connect(this->thumbnailManager, &generateThumbnailManager::openFile, this, [](QString path) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
    this->initRatingIcons();
    ui.setupUi(this);

    // scale main window
    // Define variables for aspect ratio and screen size fraction
    float aspectRatioWidth = 16.0f;
    float aspectRatioHeight = 9.0f;
    float screenSizeFraction = 3.0f / 4.0f;

    // Get screen geometry (size)
    QScreen* screen = this->App->primaryScreen();
    int screenWidth = screen->geometry().width();
    int screenHeight = screen->geometry().height();

    // Calculate desired width and height based on aspect ratio and screen size fraction
    int newWidth = screenWidth * screenSizeFraction;  // Fraction of screen width
    int newHeight = newWidth * aspectRatioHeight / aspectRatioWidth;  // Maintain aspect ratio

    // Ensure the height doesn't exceed the screen's height (in case of small screens)
    if (newHeight > screenHeight * screenSizeFraction) {
        newHeight = screenHeight * screenSizeFraction;
        newWidth = newHeight * aspectRatioWidth / aspectRatioHeight;  // Adjust width to maintain aspect ratio
    }

    // Set the window geometry (position and size)
    this->setGeometry(
        (screenWidth / 2) - (newWidth / 2), // Center the window horizontally
        (screenHeight / 2) - (newHeight / 2), // Center the window vertically
        newWidth, // Set the new width
        newHeight  // Set the new height
    );
    //scale mascots
    //this->ui.leftImg->setMinimumWidth(newWidth / 6.6);
    //this->ui.rightImg->setMinimumWidth(newWidth / 6.6);

    this->ui.searchWidget->hide();
    new QShortcut(QKeySequence("F1"), this, [this] {this->switchCurrentDB(); });
    new QShortcut(QKeySequence("F2"), this, [this] {this->toggleSearchBar(); });
    new QShortcut(QKeySequence("F3"), this, [this] {this->toggleDates(); });
    new QShortcut(QKeySequence("F4"), this, [this] {
        QString settings_str = QString();
        if (this->App->currentDB == "PLUS")
            settings_str = this->App->config->get("headers_plus_visible");
        else if (this->App->currentDB == "MINUS")
            settings_str = this->App->config->get("headers_minus_visible");
        QStringList settings_list = settings_str.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (settings_list.contains("random%"))
            settings_list.removeAll("random%");
        else
            settings_list.append("random%");
        this->updateHeaderSettings(settings_list);
    });
    QShortcut *shortcutLogs = new QShortcut(QKeySequence("F5"), this, [this] {this->App->toggleLogWindow(); });

    QShortcut* shortcutSSE = new QShortcut(QKeySequence("F8"), this, [this] {
        this->playSpecialSoundEffect(true);
    });
    shortcutSSE->setContext(Qt::ApplicationShortcut);
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

    this->intro_played = !this->App->config->get_bool("sound_effects_start");
    this->special_effects_player = this->App->soundPlayer->get_player();
    this->special_effects_player->audioOutput()->setVolume(utils::volume_convert(this->App->config->get("sound_effects_special_volume").toInt()));

    this->initStyleSheets();
    this->ui.currentVideoScrollArea->installEventFilter(new scrollAreaEventFilter(this->ui.currentVideoScrollArea));

    this->ui.watchedTimeFrame->setMainWindow(this);
    this->initNextButtonMode(this->ui.next_button);
    this->initRandomButtonMode(this->ui.random_button);
    connect(this->ui.update_watched_button, &QToolButton::toggled, this, &MainWindow::setCheckedUpdateWatchedToggleButton);
    this->initUpdateWatchedToggleButton();
    this->ui.shoko_button->setMainWindow(this);

    this->searchThreadPool = new QThreadPool(this);
    this->searchThreadPool->setMaxThreadCount(QThread::idealThreadCount());
    // Warm up the QtConcurrent thread pool
    QList<int> dummy_list = { 1 };
    QFuture<void> dummy_future = QtConcurrent::map(this->searchThreadPool, dummy_list, [](int value) {
        (void)value; // Suppress unused variable warning
    });

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
    connect(this->ui.videosWidget, &videosTreeWidget::itemMiddleClicked, this, [this](QTreeWidgetItem* item) { this->openThumbnails(item->text(ListColumns["PATH_COLUMN"])); });
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
    header->setSectionResizeMode(ListColumns["TAGS_COLUMN"], QHeaderView::Fixed);
    header->resizeSection(ListColumns["TAGS_COLUMN"], 150);
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
                    this->App->soundPlayer->play(files[0], true, true);
                }
                return;
            }
            if (guess_type.name().startsWith("audio/", Qt::CaseInsensitive)) {
                if (this->App->soundPlayer) {
                    this->App->soundPlayer->play(files[0], true, true);
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
        NextVideoSettings settings = this->getNextVideoSettings();
        bool ignore_filters_and_defaults = settings.random_mode == RandomModes::All;
        bool video_changed = this->randomVideo(settings.random_mode, ignore_filters_and_defaults, settings.vid_type_include, settings.vid_type_exclude, false);
        if (this->App->VW->mainPlayer and video_changed) {
            this->changePlayerVideo(this->App->VW->mainPlayer, this->ui.currentVideo->path, this->ui.currentVideo->id, this->position);
        }
    });
    connect(this->ui.random_button, &customQButton::middleClicked, this, [this] {
        FilterDialog dialog = FilterDialog(this,this->filterSettings.json,this);
        int value = dialog.exec();
        if (value == QDialog::Accepted) {
            QJsonObject json = dialog.toJson();
            this->filterSettings.setJson(json);
            this->App->db->setFilterSettings(QJsonDocument(dialog.toJson()).toJson(QJsonDocument::Compact), this->App->currentDB);
            this->updateVideoListRandomProbabilitiesIfVisible();
        }
    });
    connect(this->ui.random_button, &customQButton::rightClicked, this, [this] {
        customQButton* buttonSender = qobject_cast<customQButton*>(sender());
        this->switchRandomButtonMode(buttonSender);
     });
    connect(this->ui.watch_button, &QPushButton::clicked, this, [this] { 
        this->watchCurrent(); 
    });
    connect(this->ui.watch_button, &customQButton::rightClicked, this, [this] {
        this->openEmptyVideoPlayer();
    });
    connect(this->ui.insert_button, &QPushButton::clicked, this, [this] { 
        this->insertDialogButton(); 
    });
    connect(this->ui.tags_button, &QPushButton::clicked, this, [this] {
        this->TagsDialogButton();
    });
    connect(this->ui.next_button, &QPushButton::clicked, this, [this] {
        this->NextButtonClicked(true, this->getCheckedUpdateWatchedToggleButton());
    });
    connect(this->ui.next_button, &customQButton::middleClicked, this, [this] {
        this->NextButtonClicked(false, this->getCheckedUpdateWatchedToggleButton());
    });
    connect(this->ui.next_button, &customQButton::rightClicked, this, [this] {
        customQButton* buttonSender = qobject_cast<customQButton*>(sender());
        this->switchNextButtonMode(buttonSender);
     });
    connect(this->ui.settings_button, &QPushButton::clicked, this, [this] {
        this->settingsDialogButton(); 
    });
    connect(this->ui.settings_button, &customQButton::rightClicked, this, [this] { 
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
        QMediaPlayer *intro_player = this->App->soundPlayer->play(track, true, true);
        QObject::connect(intro_player, &QMediaPlayer::mediaStatusChanged, [this](QMediaPlayer::MediaStatus status) {if (status == QMediaPlayer::EndOfMedia) { this->intro_played = true; } });
        QObject::connect(intro_player, &QMediaPlayer::errorOccurred, [this](QMediaPlayer::Error error, const QString& errorString) {qDebug() << errorString; this->intro_played = true; });
    }
    
    this->initMascotAnimation();
    if (this->App->config->get_bool("mascots"))
        this->updateMascots();
    else
        this->hideMascots();
    connect(this->ui.leftImg, &customGraphicsView::mouseClicked, this, [this](Qt::MouseButton button, Qt::KeyboardModifiers modifiers) { this->handleMascotClickEvents(this->ui.leftImg, button, modifiers); });
    connect(this->ui.rightImg, &customGraphicsView::mouseClicked, this, [this](Qt::MouseButton button, Qt::KeyboardModifiers modifiers) { this->handleMascotClickEvents(this->ui.rightImg, button, modifiers); });

    connect(&this->update_title_timer, &QTimer::timeout, this, [this] {
        this->UpdateWindowTitle();
    });
    this->update_title_timer.start(1000);

    connect(this->ui.searchButton, &QPushButton::clicked, this, [this] {this->refreshVisibility(this->ui.searchBar->text()); });
    QList<QAction*> actionList = this->ui.searchBar->findChildren<QAction*>();
    connect(this->ui.searchBar, &QLineEdit::returnPressed, this, [this] {this->refreshVisibility(this->ui.searchBar->text()); });
    if (!actionList.isEmpty()) {
        connect(actionList.first(), &QAction::triggered, this, [this]() {qDebug() << "test"; this->ui.searchBar->setText(""); });
    }
    connect(this->ui.visibleOnlyCheckBox, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) {
        this->refreshVisibility(this->ui.searchBar->text());
    });

    this->search_completer = new QCompleter(this);
    this->search_completer->setModel(new QStringListModel(this));
    this->updateSearchCompleter();
    this->search_completer->setCaseSensitivity(Qt::CaseInsensitive);
    this->search_completer->setFilterMode(Qt::MatchContains);
	this->search_completer->setCompletionMode(QCompleter::PopupCompletion);
	this->search_completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    this->ui.searchBar->setCompleter(this->search_completer);

    //connect(this->ui.searchBar, &QLineEdit::textChanged, this, [this] {this->search(this->ui.searchBar->text()); });

    this->search_timer = new QTimer(this);
    connect(this->search_timer, &QTimer::timeout, this, [this] {
        if(this->ui.searchBar->text() != this->old_search)
            this->refreshVisibility(this->ui.searchBar->text());
    });

    this->ui.videosWidget->setItemDelegate(new AutoToolTipDelegate(this->ui.videosWidget));
    
    this->animatedIcon = new IconChanger(this->App, this->App->config->get_bool("random_icon"));
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
    QString session_time;
    QString watched_time;
    QString thumb_work_count;
    QString main_title = QString("Media Manager %1 %2").arg(this->getCategoryName()).arg(VERSION_TEXT);
    if (this->App->VW and this->App->VW->mainPlayer) {
        int sessionSeconds = this->App->VW->mainPlayer->getSessionTime();
        int watchedSeconds = this->App->VW->mainPlayer->getTotalWatchedTime();
        if (sessionSeconds > 0) {
            session_time = " [Session: " % QString::fromStdString(utils::formatSeconds(sessionSeconds)) % "]";
        }
        if (watchedSeconds > 0) {
            watched_time = " [Watched: " % QString::fromStdString(utils::formatSeconds(watchedSeconds)) % "]";
        }
    }
    if (this->thumbnailManager->work_count > 0) {
        thumb_work_count = " (" % QString::number(this->thumbnailManager->work_count) % ")";
    }
    this->setWindowTitle(main_title % thumb_work_count % session_time % watched_time);
}

void MainWindow::VideoInfoNotification() {
    QScreen* screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->geometry();
    if (this->notification_dialog) {
        this->notification_dialog->closeNotification();
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
    if (this->ui.currentVideo->tags.isEmpty())
        this->notification_dialog->ui.tags->hide();
    else
        this->notification_dialog->ui.tags->setText(this->ui.currentVideo->tags);
    StarRating starRating = StarRating(this->active, this->halfactive, this->inactive, 0, 5.0);
    if (this->ui.videosWidget->last_selected) {
        bool ok = true;
        double stars = this->ui.videosWidget->last_selected->data(ListColumns["RATING_COLUMN"], CustomRoles::rating).toDouble(&ok);
        if (not ok)
            stars = 0;
        starRating.setStarCount(stars);
        this->notification_dialog->ui.starsLabel->setText(QString(" %1 ").arg(stars));
        this->notification_dialog->ui.viewsLabel->setText(QString(" %1 Views ").arg(this->ui.videosWidget->last_selected->text(ListColumns["VIEWS_COLUMN"])));
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
        qMainApp->logger->log(QString("Updating video with id \"%1\" to %2").arg(video_id).arg(new_path), "UpdatePath", new_path);
        this->App->db->db.transaction();
        this->App->db->updateItem(video_id, new_path);
        QString author = InsertSettingsDialog::get_author(new_path);
        QString name = InsertSettingsDialog::get_name(new_path);
        this->App->db->updateAuthor(video_id, author);
        this->App->db->updateName(video_id, name);
        this->App->db->db.commit();
        if (video_id == this->ui.currentVideo->id) {
            this->setCurrent(video_id, new_path, name, author, this->ui.currentVideo->tags, false);
        }
        this->thumbnailManager->enqueue_work({ new_path, false, false });
        this->thumbnailManager->start();
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
            this->search_timer->start(this->App->config->get("search_timer_interval").toInt());
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

bool MainWindow::determineVisibility(QTreeWidgetItem* item, const QString& watched_option, const QString& search_text, const rapidfuzz::fuzz::CachedPartialRatio<char>* cached_search_text_ratio, const QSet<QString>& authorsWithUnwatched)
{
    bool hidden = false;

    //watched
    if (watched_option == "Mixed") {
        if (authorsWithUnwatched.contains(item->text(ListColumns["AUTHOR_COLUMN"]))) {
            hidden = false;
        }
        else {
            hidden = true;
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
    if (not search_text.isEmpty() and hidden == false) {
        double score = 0;
        std::string item_combined_text = (item->text(ListColumns["PATH_COLUMN"]) % " " %
            item->text(ListColumns["AUTHOR_COLUMN"]) % " " %
            item->text(ListColumns["TAGS_COLUMN"]) % " " %
            item->text(ListColumns["TYPE_COLUMN"])).toLower().toStdString();

        if (cached_search_text_ratio) {
            score = cached_search_text_ratio->similarity(item_combined_text);

        }
        else {
            score = rapidfuzz::fuzz::partial_ratio(search_text.toLower().toStdString(), item_combined_text);

        }
        if (score > 80) {
            hidden = false;

        }
        else {
            hidden = true;
        }
    }

    return hidden;
}

void MainWindow::refreshVisibility(QString search_text)
{
	QList<QTreeWidgetItem*> items_to_filter = QList<QTreeWidgetItem*>();
	bool watched_yes = true, watched_no = true, watched_mixed = false;
	QString settings_str = QString();
	if (this->App->currentDB == "PLUS")
		settings_str = this->App->config->get("headers_plus_visible");
	else if (this->App->currentDB == "MINUS")
		settings_str = this->App->config->get("headers_minus_visible");
	QStringList settings_list = settings_str.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
	watched_yes = settings_list.contains("watched_yes");
	watched_no = settings_list.contains("watched_no");
	watched_mixed = settings_list.contains("watched_mixed");
	QString option = this->getWatchedVisibilityOption(watched_yes, watched_no, watched_mixed, this->ui.searchBar->isVisible(), this->ui.visibleOnlyCheckBox->isChecked());

	std::shared_ptr<rapidfuzz::fuzz::CachedPartialRatio<char>> cached_ratio_ptr_shared;
	if (!search_text.isEmpty()) {
		cached_ratio_ptr_shared = std::make_shared<rapidfuzz::fuzz::CachedPartialRatio<char>>(
			search_text.toLower().toStdString()
		);
	}

	QSet<QString> authorsWithUnwatched;
	QTreeWidgetItemIterator it(this->ui.videosWidget);
	while (*it) {
		QTreeWidgetItem* item = *it;
		items_to_filter.append(item);
		if (option == "Mixed" && item->text(ListColumns["WATCHED_COLUMN"]) == "No") {
			authorsWithUnwatched.insert(item->text(ListColumns["AUTHOR_COLUMN"]));
		}
		++it;
	}

	QList<bool> hidden_flags = QtConcurrent::blockingMapped(this->searchThreadPool, items_to_filter, [=](QTreeWidgetItem* item) {
		return determineVisibility(item, option, search_text, cached_ratio_ptr_shared.get(), authorsWithUnwatched);
		});

	for (int i = 0; i < items_to_filter.count(); ++i)
	{
		if (i < hidden_flags.count())
		{
			items_to_filter[i]->setHidden(hidden_flags[i]);
		}
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
    if (this->animatedIconFlag && this->animatedIcon && this->App->config->get_bool("default_icon_not_watching")) {
        this->animatedIcon->initIcon(false);
    }
    else if (this->animatedIconFlag && this->animatedIcon && not this->App->config->get_bool("default_icon_not_watching")) {
        this->animatedIcon->initIcon(true);
    }
    else {
        this->setIcon(this->getIconByStage(0));
    }
}

void MainWindow::openStats() {
    StatsDialog *dialog = new StatsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    
    // Setup all statistics sections
    dialog->setupTimeStats(this->App);
    dialog->setupVideoStats(this->App);
    dialog->setupRatingStats(this->App);
    
    // Play sound effects
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
    NextVideoModes::Mode mode = this->getNextVideoMode();
    QString mode_text;
    switch (mode) {
        case NextVideoModes::Sequential:
            mode_text = "Next";
            break;
        case NextVideoModes::Random:
            mode_text = "Next (R)";
            break;
        case NextVideoModes::SeriesRandom:
            mode_text = "Next (SR)";
            break;
        default:
            mode_text = "Next"; // Default to Sequential
            break;
    }
    traycontextmenu->addAction(mode_text, [this] {
        this->NextButtonClicked(true, this->getCheckedUpdateWatchedToggleButton());
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
        else {
            this->animatedIcon->animatedIconEvent.clear();
            if (this->App->config->get_bool("default_icon_not_watching")) {
                this->setIcon(this->getIconByStage(2));
            }
        }
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

void MainWindow::setMascotDebug(QString path, customGraphicsView* sender) {
    if (this->App->debug_mode) {
        sender->setImage(path);
        if (this->App->config->get_bool("mascots_mirror")) {
            if (sender == this->ui.leftImg)
                this->ui.rightImg->setImage(path, true);
            else
                this->ui.leftImg->setImage(path, true);
        }
        if (this->App->MascotsExtractColor) {
            this->updateThemeHighlightColorFromMascot(sender);
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

void MainWindow::setLeftMascot(ImageData data) {
    if (utils::getParentDirectoryName(data.path, 1) == "right")
        this->ui.leftImg->setImage(data, true);
    else
        this->ui.leftImg->setImage(data);
}

void MainWindow::setRightMascot(ImageData data) {
    if (utils::getParentDirectoryName(data.path, 1) == "left")
        this->ui.rightImg->setImage(data, true);
    else
        this->ui.rightImg->setImage(data);
}

void MainWindow::updateMascot(customGraphicsView* mascot, bool use_cache, bool update_highlight_color) {
    if (this->App->MascotsGenerator->mascots_paths.isEmpty()) {
        this->App->MascotsGenerator->initMascotsPaths(MASCOTS_PATH);
    }
    ImageData img_data;
    if (use_cache)
        img_data = this->App->MascotsGenerator->getImage();
    else
        img_data = this->App->MascotsGenerator->getRandomImagePath();

    if (!img_data.pixmap.isNull()) {
        if(mascot == this->ui.leftImg)
            this->setLeftMascot(img_data);
        if (mascot == this->ui.rightImg)
            this->setRightMascot(img_data);
    }
    else if (!img_data.path.isEmpty()) {
        if (mascot == this->ui.leftImg)
            this->setLeftMascot(img_data.path);
        if (mascot == this->ui.rightImg)
            this->setRightMascot(img_data.path);
    }
    if (update_highlight_color and this->App->MascotsExtractColor) {
        this->updateThemeHighlightColorFromMascot(mascot);
    }
}

void MainWindow::updateMascots(bool use_cache, bool forced_mirror)
{
    try {
        this->updateMascot(this->ui.leftImg, use_cache, false);
        if (this->App->config->get_bool("mascots_mirror") or forced_mirror) {
            ImageData img_data = {this->ui.leftImg->imgpath, this->ui.leftImg->original_pixmap, this->ui.leftImg->accepted_colors, this->ui.leftImg->rejected_colors };
            this->setRightMascot(img_data);
        }
        else {
            this->updateMascot(this->ui.rightImg, use_cache, false);
        }
        if (this->App->MascotsExtractColor) {
            this->updateThemeHighlightColorFromMascot(this->ui.leftImg);
        }
    }
    catch (...) {
        this->hideMascots();
        qDebug("Set Mascots exception.");
    }
}

void MainWindow::updateThemeHighlightColorFromMascot(customGraphicsView* mascot, bool weighted, bool regen_colors_if_missing) {
    if (mascot->accepted_colors.isEmpty() and mascot->rejected_colors.isEmpty() and regen_colors_if_missing) {
        auto colors_pair = mascotsGeneratorThread::extractColors(mascot->original_pixmap);
        mascot->accepted_colors = colors_pair.first;
        mascot->rejected_colors = colors_pair.second;
    }
    color_area color = mascot->getColor(weighted);
    if (color.color.isValid())
        this->setThemeHighlightColor(color.color);
}

void MainWindow::setThemeHighlightColor(QColor color) {
    QPalette p = this->App->palette();
    p.setColor(QPalette::Highlight, color);
    p.setColor(QPalette::HighlightedText, utils::complementary_color(color));
    this->changePalette(p);
}

void MainWindow::openThumbnails(QString path) {
    QString cachename = generateThumbnailRunnable::getThumbnailFilename(path);
    QString cachepath = QString(THUMBNAILS_CACHE_PATH) + "/" + cachename;
    QFileInfo file(cachepath);
    if (file.exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(file.absoluteFilePath()));
    }
    else {
        this->thumbnailManager->enqueue_work_front({ path, false, true });
        this->thumbnailManager->start();
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

void MainWindow::NextButtonClicked(bool increment, bool update_watched_state) {
    this->NextButtonClicked(this->App->VW->mainPlayer, increment, update_watched_state);
}

void MainWindow::NextButtonClicked(QSharedPointer<BasePlayer> player, bool increment, bool update_watched_state) {
    NextVideoModes::Mode mode = this->getNextVideoMode();
    bool video_changed = this->NextVideo(mode, increment, update_watched_state);
    if (player) {
        this->changePlayerVideo(player, this->ui.currentVideo->path, this->ui.currentVideo->id, 0);
    }
    if (video_changed) {
        if (this->animatedIconFlag)
            this->animatedIcon->setRandomIcon(not this->App->config->get_bool("default_icon_not_watching"));
        if (this->App->config->get_bool("mascots"))
            this->updateMascots();
    }
    this->updateVideoListRandomProbabilitiesIfVisible();
}

NextVideoSettings MainWindow::getNextVideoSettings() {
    NextVideoSettings settings;
    settings.random_mode = static_cast<RandomModes::Mode>(this->App->config->get(this->getRandomButtonConfigKey()).toInt());
    if (this->App->currentDB == "MINUS") {
        if (this->sv_count >= this->sv_target_count) {
            settings.vid_type_include = svTypes;
            settings.vid_type_exclude = {};
            QJsonObject random_settings = getRandomSettings(settings.random_mode, settings.ignore_filters_and_defaults, settings.vid_type_include, settings.vid_type_exclude);
            QString seed = this->App->config->get_bool("random_use_seed") ? this->App->config->get("random_seed") : "";
            settings.sv_is_available = not this->getRandomVideo(seed, this->getWeightedBiasSettings(), random_settings).isEmpty();
            settings.next_video_is_sv = true;
            if (!settings.sv_is_available) {
                settings.vid_type_include = {};
            }
        }
        else {
            settings.next_video_is_sv = false;
            settings.vid_type_include = {};
            settings.vid_type_exclude = svTypes;
        }
    }
    else {
        //default values are good
    }
    return settings;
}

bool MainWindow::NextVideo(bool random, bool increment, bool update_watched_state) {
    // Legacy function - convert boolean to enum and call new function
    NextVideoModes::Mode mode = random ? NextVideoModes::Random : NextVideoModes::Sequential;
    return this->NextVideo(mode, increment, update_watched_state);
}

bool MainWindow::NextVideo(NextVideoModes::Mode mode, bool increment, bool update_watched_state) {
    bool video_changed = false;
    bool sv_count_reset = false;
    QList<QTreeWidgetItem*> items = this->ui.videosWidget->findItemsCustom(this->ui.currentVideo->path, Qt::MatchFixedString, ListColumns["PATH_COLUMN"], 1);
    QTreeWidgetItem* item = (!items.isEmpty()) ? items.first() : nullptr;
    this->App->db->db.transaction();
    if (item != nullptr) {
        this->App->db->setMainInfoValue("current", this->App->currentDB, "");
        QString watched = (update_watched_state) ? "Yes" : items.first()->text(ListColumns["WATCHED_COLUMN"]);
        if (increment)
            this->App->db->updateWatchedState(items.first()->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), this->position, watched, true, true);
        else
            this->App->db->updateWatchedState(items.first()->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), watched, false, false);
    }
    
    switch (mode) {
        case NextVideoModes::Sequential:
            video_changed = this->setNextVideo(item);
            break;
        case NextVideoModes::Random:
            {
                NextVideoSettings settings = this->getNextVideoSettings();
                if (this->App->currentDB == "MINUS") {
                    if (this->sv_count >= this->sv_target_count) {
                        this->sv_count = 0;
                        sv_count_reset = true;
                        this->ui.counterLabel->setProgress(this->sv_count);
                        this->App->db->setMainInfoValue("sv_count", "ALL", QString::number(this->sv_count));
                    }
                }
                video_changed = this->randomVideo(settings.random_mode,settings.ignore_filters_and_defaults,settings.vid_type_include,settings.vid_type_exclude);
            }
            break;
        case NextVideoModes::SeriesRandom:
            video_changed = this->seriesRandomVideo(item);
            break;
    }
    
    if (item != nullptr and sv_count_reset == false) {
        this->sv_count++;
        this->ui.counterLabel->setProgress(this->sv_count);
        this->App->db->setMainInfoValue("sv_count", "ALL", QString::number(this->sv_count));
    }
    this->App->db->db.commit();

    if (item != nullptr) {
        QDateTime currenttime = QDateTime::currentDateTime();
        item->setData(ListColumns["LAST_WATCHED_COLUMN"], Qt::DisplayRole, currenttime);
        if (increment)
            item->setText(ListColumns["VIEWS_COLUMN"], QString::number(item->text(ListColumns["VIEWS_COLUMN"]).toInt() + 1));
        if (update_watched_state) {
            item->setText(ListColumns["WATCHED_COLUMN"], "Yes");
            item->setForeground(ListColumns["WATCHED_COLUMN"], QBrush(QColor("#00e640")));
        }
        this->refreshVisibility();

        this->App->db->db.transaction();
        if (increment) {
            this->App->db->incrementVideosWatchedToday(this->App->currentDB);
        }
        if (this->App->currentDB == "MINUS") {
            this->incrementCounterVar(-1);
            if (svTypes.contains(item->text(ListColumns["TYPE_COLUMN"]))) {
                int val = this->calculate_sv_target();
                this->App->db->setMainInfoValue("sv_target_count", "ALL", QString::number(val));
                this->sv_target_count = val;
                this->ui.counterLabel->setMinMax(0, val);
            }
        }
        else if (this->App->currentDB == "PLUS" && increment)
            this->incrementtimeWatchedIncrement(this->App->db->getVideoProgress(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), "0").toDouble());
        this->updateTotalListLabel();
        this->checktimeWatchedIncrement();
        this->updateWatchedProgressBar();
        this->App->db->db.commit();
    }
    return video_changed;
}

bool MainWindow::setNextVideo(QTreeWidgetItem* item) {
    QTreeWidgetItem* item_below = this->ui.videosWidget->itemBelow(item);
    bool looped_once = false;
    while (true) {
        if (item_below == nullptr and looped_once == false) {
            item_below = this->ui.videosWidget->topLevelItem(0);
            looped_once = true;
        }
        if (item == item_below or item_below == nullptr) {
            this->setCurrent(-1, "", "", "", "");
            this->selectCurrentItem(nullptr);
            return false;
            break;
        }        
        if (item_below->text(ListColumns["WATCHED_COLUMN"]) == "No") {
            this->setCurrent(item_below->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), item_below->text(ListColumns["PATH_COLUMN"]), item_below->text(ListColumns["NAME_COLUMN"]), item_below->text(ListColumns["AUTHOR_COLUMN"]), item_below->text(ListColumns["TAGS_COLUMN"]), true);
            this->selectCurrentItem(item_below);
            return true;
            break;
        }
        item_below = this->ui.videosWidget->itemBelow(item_below);
    }
}

QMap<QString, AuthorVideoData> MainWindow::getAuthorVideoMap(const QString& exclude_author, const QList<VideoWeightedData>& all_videos) {
    QMap<QString, AuthorVideoData> author_map;

    // Single pass through all videos
    for (const VideoWeightedData& video : all_videos) {
        QList<QTreeWidgetItem*> items = this->ui.videosWidget->findItemsCustom(video.path, Qt::MatchExactly, ListColumns["PATH_COLUMN"], 1);
        if (items.isEmpty() || items.first()->text(ListColumns["WATCHED_COLUMN"]) != "No") {
            continue;
        }

        QTreeWidgetItem* item = items.first();
        QString author = item->text(ListColumns["AUTHOR_COLUMN"]);
        if (author.isEmpty()) {
            author = video.path;
        }

        // Skip excluded author if specified
        if (!exclude_author.isEmpty() && author == exclude_author) {
            continue;
        }

        // Add video to author's list
        author_map[author].videos.append(video);
        author_map[author].author = author;
    }

    return author_map;
}

// Optimized helper to find first item for each author in a single tree pass
void MainWindow::findFirstItemsForAuthors(QMap<QString, AuthorVideoData>& author_map) {
    QTreeWidgetItemIterator it(this->ui.videosWidget);
    QSet<QString> found_authors;

    while (*it && found_authors.size() < author_map.size()) {
        QTreeWidgetItem* item = *it;
        if (item->text(ListColumns["WATCHED_COLUMN"]) == "No") {
            QString author = item->text(ListColumns["AUTHOR_COLUMN"]);
            if (author.isEmpty()) {
                author = item->text(ListColumns["PATH_COLUMN"]);
            }

            if (author_map.contains(author) && !found_authors.contains(author)) {
                author_map[author].firstItem = item;
                found_authors.insert(author);
            }
        }
        ++it;
    }
}

// Optimized helper to calculate author weighted data
QList<VideoWeightedData> MainWindow::calculateAuthorWeights(const QMap<QString, AuthorVideoData>& author_map) {
    QList<VideoWeightedData> author_weighted_data;
    author_weighted_data.reserve(author_map.size());

    for (auto it = author_map.constBegin(); it != author_map.constEnd(); ++it) {
        const AuthorVideoData& data = it.value();
        if (data.videos.isEmpty() || !data.firstItem) {
            continue;
        }

        // Calculate average weights
        double total_views = 0, total_rating = 0, total_tags = 0;
        for (const VideoWeightedData& v : data.videos) {
            total_views += v.views;
            total_rating += v.rating;
            total_tags += v.tagsWeight;
        }

        int count = data.videos.size();
        VideoWeightedData author_data;
        author_data.id = data.firstItem->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt();
        author_data.path = data.firstItem->text(ListColumns["PATH_COLUMN"]);
        author_data.views = total_views / count;
        author_data.rating = total_rating / count;
        author_data.tagsWeight = total_tags / count;

        author_weighted_data.append(author_data);
    }

    return author_weighted_data;
}


bool MainWindow::seriesRandomVideo(QTreeWidgetItem* current_item) {
    NextVideoSettings settings = this->getNextVideoSettings();
    QJsonObject json_settings = this->getRandomSettings(settings.random_mode, settings.ignore_filters_and_defaults,
        settings.vid_type_include, settings.vid_type_exclude);
    QString seed = this->App->config->get_bool("random_use_seed") ? this->App->config->get("random_seed") : "";

    if (current_item == nullptr) {
        // Get all videos once
        QList<VideoWeightedData> all_videos = this->App->db->getVideos(this->App->currentDB, json_settings);

        // Build author map in single pass
        QMap<QString, AuthorVideoData> author_map = this->getAuthorVideoMap("", all_videos);

        if (author_map.isEmpty()) {
            this->setCurrent(-1, "", "", "", "");
            this->selectCurrentItem(nullptr);
            return false;
        }

        // Find first items for all authors in single tree pass
        this->findFirstItemsForAuthors(author_map);

        // Calculate author weights
        QList<VideoWeightedData> author_weighted_data = this->calculateAuthorWeights(author_map);

        if (author_weighted_data.isEmpty()) {
            this->setCurrent(-1, "", "", "", "");
            this->selectCurrentItem(nullptr);
            return false;
        }

        // Use weighted random selection to choose an author
        QRandomGenerator generator;
        if (seed.isEmpty()) {
            generator.seed(QRandomGenerator::global()->generate());
        }
        else {
            generator.seed(utils::stringToSeed(this->saltSeed(seed)));
        }

        WeightedBiasSettings weighted_settings = this->getWeightedBiasSettings();
        if (!weighted_settings.weighted_random_enabled) {
            weighted_settings.bias_general = 0;
        }

        QString selected_path = utils::weightedRandomChoice(author_weighted_data, generator,
            weighted_settings.bias_views,
            weighted_settings.bias_rating,
            weighted_settings.bias_tags,
            weighted_settings.bias_general,
            weighted_settings.no_views_weight,
            weighted_settings.no_rating_weight,
            weighted_settings.no_tags_weight);

        // Find the selected author's first item by matching the path
        if (!selected_path.isEmpty()) {
            for (auto it = author_map.constBegin(); it != author_map.constEnd(); ++it) {
                if (it.value().firstItem && it.value().firstItem->text(ListColumns["PATH_COLUMN"]) == selected_path) {
                    QTreeWidgetItem* target_item = it.value().firstItem;
                    this->setCurrent(target_item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(),
                        target_item->text(ListColumns["PATH_COLUMN"]),
                        target_item->text(ListColumns["NAME_COLUMN"]),
                        target_item->text(ListColumns["AUTHOR_COLUMN"]),
                        target_item->text(ListColumns["TAGS_COLUMN"]), true);
                    this->selectCurrentItem(target_item);
                    return true;
                }
            }
        }

        this->setCurrent(-1, "", "", "", "");
        this->selectCurrentItem(nullptr);
        return false;
    }

    // Get current author
    QString current_author = current_item->text(ListColumns["AUTHOR_COLUMN"]);
    if (current_author.isEmpty()) {
        current_author = current_item->text(ListColumns["PATH_COLUMN"]);
    }

    // Get all unwatched videos from same author in single pass
    QList<QTreeWidgetItem*> author_videos_unwatched;
    QTreeWidgetItemIterator it(this->ui.videosWidget);
    while (*it) {
        QTreeWidgetItem* item = *it;
        if (item->text(ListColumns["WATCHED_COLUMN"]) == "No") {
            QString item_author = item->text(ListColumns["AUTHOR_COLUMN"]);
            if (item_author.isEmpty()) {
                item_author = item->text(ListColumns["PATH_COLUMN"]);
            }
            if (item_author == current_author) {
                author_videos_unwatched.append(item);
            }
        }
        ++it;
    }

    // Find current position
    int current_pos = author_videos_unwatched.indexOf(current_item);

    QTreeWidgetItem* next_item = nullptr;

    // Determine next item
    if (current_pos != -1 && current_pos + 1 < author_videos_unwatched.size()) {
        next_item = author_videos_unwatched[current_pos + 1];
    }
    else if (current_pos != -1 && current_pos == author_videos_unwatched.size() - 1 && author_videos_unwatched.size() > 1) {
        next_item = author_videos_unwatched[0];
    }

    if (next_item != nullptr) {
        this->setCurrent(next_item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(),
            next_item->text(ListColumns["PATH_COLUMN"]),
            next_item->text(ListColumns["NAME_COLUMN"]),
            next_item->text(ListColumns["AUTHOR_COLUMN"]),
            next_item->text(ListColumns["TAGS_COLUMN"]), true);
        this->selectCurrentItem(next_item);
        return true;
    }

    // Need to switch to new author - get all videos once
    QList<VideoWeightedData> all_videos = this->App->db->getVideos(this->App->currentDB, json_settings);

    // Build author map excluding current author
    QMap<QString, AuthorVideoData> author_map = this->getAuthorVideoMap(current_author, all_videos);

    if (author_map.isEmpty()) {
        this->setCurrent(-1, "", "", "", "");
        this->selectCurrentItem(nullptr);
        return false;
    }

    // Find first items for all authors in single tree pass
    this->findFirstItemsForAuthors(author_map);

    // Calculate author weights
    QList<VideoWeightedData> author_weighted_data = this->calculateAuthorWeights(author_map);

    if (author_weighted_data.isEmpty()) {
        this->setCurrent(-1, "", "", "", "");
        this->selectCurrentItem(nullptr);
        return false;
    }

    // Use weighted random selection to choose an author
    QRandomGenerator generator;
    if (seed.isEmpty()) {
        generator.seed(QRandomGenerator::global()->generate());
    }
    else {
        generator.seed(utils::stringToSeed(this->saltSeed(seed)));
    }

    WeightedBiasSettings weighted_settings = this->getWeightedBiasSettings();
    if (!weighted_settings.weighted_random_enabled) {
        weighted_settings.bias_general = 0;
    }

    QString selected_path = utils::weightedRandomChoice(author_weighted_data, generator,
        weighted_settings.bias_views,
        weighted_settings.bias_rating,
        weighted_settings.bias_tags,
        weighted_settings.bias_general,
        weighted_settings.no_views_weight,
        weighted_settings.no_rating_weight,
        weighted_settings.no_tags_weight);

    // Find the selected author's first item by matching the path
    if (!selected_path.isEmpty()) {
        for (auto it = author_map.constBegin(); it != author_map.constEnd(); ++it) {
            if (it.value().firstItem && it.value().firstItem->text(ListColumns["PATH_COLUMN"]) == selected_path) {
                QTreeWidgetItem* target_item = it.value().firstItem;
                this->setCurrent(target_item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(),
                    target_item->text(ListColumns["PATH_COLUMN"]),
                    target_item->text(ListColumns["NAME_COLUMN"]),
                    target_item->text(ListColumns["AUTHOR_COLUMN"]),
                    target_item->text(ListColumns["TAGS_COLUMN"]), true);
                this->selectCurrentItem(target_item);
                return true;
            }
        }
    }

    this->setCurrent(-1, "", "", "", "");
    this->selectCurrentItem(nullptr);
    return false;
}

int MainWindow::calculate_sv_target() {
    if (this->App->currentDB == "MINUS") {
        QTreeWidgetItem* root = this->ui.videosWidget->invisibleRootItem();
        int val = 0;
        int child_count = root->childCount();
        QStringList sv_types = svTypes;
        QMap<QString, int> values = { {"SPECIAL_TYPES",0},{"OTHERS",0} };
        for (int i = 0; i < child_count; i++) {
            QTreeWidgetItem* item = root->child(i);
            if (item->text(ListColumns["WATCHED_COLUMN"]) == "No" && sv_types.contains(item->text(ListColumns["TYPE_COLUMN"])))
                values["SPECIAL_TYPES"]++;
            else if (item->text(ListColumns["WATCHED_COLUMN"]) == "No")
                values["OTHERS"]++;
        }
        if (values["SPECIAL_TYPES"] > 0) {
            val = floor((double)values["OTHERS"] / (double)values["SPECIAL_TYPES"]);
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

bool MainWindow::TagsDialogButton() {
    TagsDialog dialog = TagsDialog(this->App->db->db,this);
    dialog.model->database().transaction();
    int value = dialog.exec();
    if (value == QDialog::Accepted) {
        if (dialog.model->submitAll()) {
            dialog.model->database().commit();
            this->refreshVideosWidget(false, true);
            this->refreshCurrentVideo();
            return true;
        }
        else {
            dialog.model->database().rollback();
            if (qApp)
                qMainApp->showErrorMessage("Database error: " + dialog.model->lastError().text());
        }
    }
    else {
        dialog.model->revertAll();
        dialog.model->database().rollback();
    }
    return false;
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
            this->refreshHeadersVisibility();
            this->updateTotalListLabel();
            this->init_icons();
            this->filterSettings = FilterSettings(this->App->db->getFilterSettings(this->App->currentDB));
            if (this->App->VW->mainPlayer) {
                this->changePlayerVideo(this->App->VW->mainPlayer, this->ui.currentVideo->path, this->ui.currentVideo->id, this->position);
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

void MainWindow::handleMascotClickEvents(customGraphicsView* mascot, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
    if (modifiers.testFlag(Qt::ControlModifier)) {
        switch (button)
        {
        case Qt::LeftButton:
            this->updateThemeHighlightColorFromMascot(mascot, true);
            break;
        case Qt::RightButton:
            this->updateThemeHighlightColorFromMascot(mascot, false);
            break;
        case Qt::MiddleButton:
            if (this->App->config->get_bool("mascots_mirror")) {
                if (modifiers.testFlag(Qt::ShiftModifier))
                    this->updateMascot(mascot, true, true);
                else 
                    this->updateMascots(true, false);
            }
            else {
                if (modifiers.testFlag(Qt::ShiftModifier))
                    this->updateMascots(true, true);
                else
                    this->updateMascot(mascot, true, true);
            }
            break;
        default:
            break;
        }
    }else if (this->App->debug_mode or modifiers.testFlag(Qt::ShiftModifier)) {
        if (button == Qt::LeftButton)
            mascot->flipPixmap();
        else if (button == Qt::RightButton)
            this->flipMascots();
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
    if (dialog->ui.defaultIconNotWatching->checkState() == Qt::CheckState::Checked) {
        config->set("default_icon_not_watching", "True");
        this->updateIconByWatchingState();
    }
    else if (dialog->ui.defaultIconNotWatching->checkState() == Qt::CheckState::Unchecked) {
        config->set("default_icon_not_watching", "False");
        this->updateIconByWatchingState();
    }
    if (dialog->ui.randomIcon->checkState() == Qt::CheckState::Checked) {
        config->set("random_icon", "True");
        if (this->animatedIcon != nullptr) {
            this->animatedIcon->random_icon = true;
            this->init_icons();
        }
    }
    else if (dialog->ui.randomIcon->checkState() == Qt::CheckState::Unchecked) {
        config->set("random_icon", "False");
        if (this->animatedIcon != nullptr) {
            this->animatedIcon->random_icon = false;
            this->init_icons();
        }
    }
    if (dialog->ui.mascotsOnOff->checkState() == Qt::CheckState::Checked) {
        config->set("mascots", "True");
        if (this->ui.leftImg->isHidden()) {
            this->updateMascots();
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
            this->updateMascots();
        }
    }
    else if (dialog->ui.mascotsMirror->checkState() == Qt::CheckState::Unchecked) {
        bool mascot_mirror_flag = config->get_bool("mascots_mirror");
        config->set("mascots_mirror", "False");
        if (mascot_mirror_flag) {
            this->updateMascots();
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
    if (dialog->ui.videoAutoplay->checkState() == Qt::CheckState::Checked)
        config->set("video_autoplay", "True");
    else if (dialog->ui.videoAutoplay->checkState() == Qt::CheckState::Unchecked)
        config->set("video_autoplay", "False");
    if (dialog->ui.autoContinue->checkState() == Qt::CheckState::Checked)
        config->set("auto_continue", "True");
    else if (dialog->ui.autoContinue->checkState() == Qt::CheckState::Unchecked)
        config->set("auto_continue", "False");
    config->set("auto_continue_delay", QString::number(dialog->ui.autoContinueDelay->value()));
    config->set("search_timer_interval", QString::number(dialog->ui.searchTimerInterval->value()));
    if (this->search_timer->isActive()) {
        this->search_timer->stop();
        this->search_timer->start(this->App->config->get("search_timer_interval").toInt());
    }
    
    if (dialog->ui.minusCatRadioBtn->isChecked())
        config->set("current_db", "MINUS");
    if (dialog->ui.plusCatRadioBtn->isChecked())
        config->set("current_db", "PLUS");
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
    if (dialog->ui.specialSoundEffectsOnOff->checkState() == Qt::CheckState::Checked) {
        config->set("sound_effects_special_on", "True");
    }
    else if (dialog->ui.specialSoundEffectsOnOff->checkState() == Qt::CheckState::Unchecked) {
        config->set("sound_effects_special_on", "False");
        this->special_effects_player->stop();
    }
    config->set("sound_effects_special_volume", QString::number(dialog->ui.specialSoundEffectsVolume->value()));
    if(this->special_effects_player and this->special_effects_player->audioOutput()){
        this->special_effects_player->audioOutput()->setVolume(utils::volume_convert(dialog->ui.specialSoundEffectsVolume->value()));
    }
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

    config->set("random_seed", dialog->ui.seedLineEdit->text());
    if (dialog->ui.seedCheckBox->checkState() == Qt::CheckState::Checked) {
        config->set("random_use_seed", "True");
    }
    else if (dialog->ui.seedCheckBox->checkState() == Qt::CheckState::Unchecked) {
        config->set("random_use_seed", "False");
    }

    //MINUS Weighted settings
    if (dialog->ui.weightedRandMinusGroupBox->isChecked()) {
        config->set("weighted_random_minus", "True");
    }
    else {
        config->set("weighted_random_minus", "False");
    }
    config->set("random_general_bias_minus", QString::number(dialog->ui.generalBiasMinusSpinBox->value()));
    config->set("random_views_bias_minus", QString::number(dialog->ui.viewsBiasMinusSpinBox->value()));
    config->set("random_rating_bias_minus", QString::number(dialog->ui.ratingBiasMinusSpinBox->value()));
    config->set("random_tags_bias_minus", QString::number(dialog->ui.tagsBiasMinusSpinBox->value()));
    config->set("random_no_views_weight_minus", QString::number(dialog->ui.noViewsMinusSpinBox->value()));
    config->set("random_no_ratings_weight_minus", QString::number(dialog->ui.noRatingMinusSpinBox->value()));
    config->set("random_no_tags_weight_minus", QString::number(dialog->ui.noTagsMinusSpinBox->value()));

    //PLUS Weighted settings
    if (dialog->ui.weightedRandPlusGroupBox->isChecked()) {
        config->set("weighted_random_plus", "True");
    }
    else {
        config->set("weighted_random_plus", "False");
    }
    config->set("random_general_bias_plus", QString::number(dialog->ui.generalBiasPlusSpinBox->value()));
    config->set("random_views_bias_plus", QString::number(dialog->ui.viewsBiasPlusSpinBox->value()));
    config->set("random_rating_bias_plus", QString::number(dialog->ui.ratingBiasPlusSpinBox->value()));
    config->set("random_tags_bias_plus", QString::number(dialog->ui.tagsBiasPlusSpinBox->value()));
    config->set("random_no_views_weight_plus", QString::number(dialog->ui.noViewsPlusSpinBox->value()));
    config->set("random_no_ratings_weight_plus", QString::number(dialog->ui.noRatingPlusSpinBox->value()));
    config->set("random_no_tags_weight_plus", QString::number(dialog->ui.noTagsPlusSpinBox->value()));

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
            customQButton* btn2 = new customQButton(this->ui.centralwidget);
            btn2->setObjectName("changeThemeBtn");
            btn2->setText("Theme");
            connect(btn2, &customQButton::clicked, this, [this] {
                QPalette p = this->App->palette();
                p.setColor(QPalette::Highlight, QColor(utils::randint(0, 255), utils::randint(0, 255), utils::randint(0, 255)));
                this->changePalette(p);
            });
            connect(btn2, &customQButton::rightClicked, this, [this] {
                QPalette p = this->App->style()->standardPalette();
                this->changePalette(p);
            });
            connect(btn2, &customQButton::middleClicked, this, [this] {
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
        this->updateVideoListRandomProbabilitiesIfVisible();
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

QString MainWindow::saltSeed(QString seed) {
    seed = seed % this->ui.currentVideo->path % this->ui.totalListLabel->text() % this->ui.counterLabel->text();
    if (this->ui.videosWidget->last_selected) {
        seed = seed % this->ui.videosWidget->last_selected->text(ListColumns["VIEWS_COLUMN"]) % this->ui.videosWidget->last_selected->data(ListColumns["RATING_COLUMN"], CustomRoles::rating).toString();
    }
    return seed;
}

WeightedBiasSettings MainWindow::getWeightedBiasSettings() {
    WeightedBiasSettings settings;
    if (this->App->currentDB == "PLUS") {
        settings.weighted_random_enabled = this->App->config->get_bool("weighted_random_plus");
        settings.bias_general = this->App->config->get("random_general_bias_plus").toDouble();
        settings.bias_views = this->App->config->get("random_views_bias_plus").toDouble();
        settings.bias_rating = this->App->config->get("random_rating_bias_plus").toDouble();
        settings.bias_tags = this->App->config->get("random_tags_bias_plus").toDouble();
        settings.no_views_weight = this->App->config->get("random_no_views_weight_plus").toDouble();
        settings.no_rating_weight = this->App->config->get("random_no_ratings_weight_plus").toDouble();
        settings.no_tags_weight = this->App->config->get("random_no_tags_weight_plus").toDouble();
    }
    else if (this->App->currentDB == "MINUS") {
        settings.weighted_random_enabled = this->App->config->get_bool("weighted_random_minus");
        settings.bias_general = this->App->config->get("random_general_bias_minus").toDouble();
        settings.bias_views = this->App->config->get("random_views_bias_minus").toDouble();
        settings.bias_rating = this->App->config->get("random_rating_bias_minus").toDouble();
        settings.bias_tags = this->App->config->get("random_tags_bias_minus").toDouble();
        settings.no_views_weight = this->App->config->get("random_no_views_weight_minus").toDouble();
        settings.no_rating_weight = this->App->config->get("random_no_ratings_weight_minus").toDouble();
        settings.no_tags_weight = this->App->config->get("random_no_tags_weight_minus").toDouble();
    }
    return settings;
}

QString MainWindow::getRandomVideo(QString seed, WeightedBiasSettings weighted_settings, QJsonObject settings) {
    QList<VideoWeightedData> videos = this->App->db->getVideos(this->App->currentDB, settings);
    if (!videos.isEmpty()) {
        QRandomGenerator generator;
        if (seed.isEmpty()) {
            generator.seed(QRandomGenerator::global()->generate());
        }
        else {
            generator.seed(utils::stringToSeed(this->saltSeed(seed)));
        }
        if (not weighted_settings.weighted_random_enabled) {
            weighted_settings.bias_general = 0;
        }
        return utils::weightedRandomChoice(videos, generator, weighted_settings.bias_views, weighted_settings.bias_rating, weighted_settings.bias_tags, weighted_settings.bias_general, weighted_settings.no_views_weight, weighted_settings.no_rating_weight, weighted_settings.no_tags_weight);
    }
    return "";
}

QJsonObject MainWindow::getRandomSettings(RandomModes::Mode random_mode, bool ignore_filters_and_defaults, QStringList vid_type_include, QStringList vid_type_exclude) {
    QJsonObject settings = FilterSettings().json; //default settings
    if (ignore_filters_and_defaults) {
        settings.insert("ignore_defaults", "True");
    }
    else if (random_mode == RandomModes::Filtered) {
        settings = QJsonObject(this->filterSettings.json); //filter settings
    }
    else if (random_mode == RandomModes::Normal) {
        //default behaviour for next when not filtered
        settings.insert("ignore_defaults", "False");
    }
    else if (random_mode == RandomModes::All) {
        settings.insert("ignore_defaults", "True");
    }
    else {
        //still default settings but we shouldnt be here
    }
    bool ignore_defaults = utils::text_to_bool(settings.value("ignore_defaults").toString().toStdString());
    if (not ignore_defaults) {
        settings.insert("watched", QJsonArray{ "No" });
        //these will not work as intented if you want to ignore default and use include/exclude from function call but its not used for now so whatever
        settings.insert("types_include", QJsonArray::fromStringList(vid_type_include));
        settings.insert("types_exclude", QJsonArray::fromStringList(vid_type_exclude));
    }
    return settings;
}

bool MainWindow::randomVideo(RandomModes::Mode random_mode, bool ignore_filters_and_defaults, QStringList vid_type_include, QStringList vid_type_exclude, bool reset_progress) {
    // Early exit if no videos
    if (this->ui.videosWidget->invisibleRootItem()->childCount() == 0) {
        return false;
    }

    // Get random settings
    QJsonObject settings = this->getRandomSettings(random_mode, ignore_filters_and_defaults, vid_type_include, vid_type_exclude);
    QString seed = this->App->config->get_bool("random_use_seed") ? this->App->config->get("random_seed") : "";

    // Get random video path
    QString item_path = this->getRandomVideo(seed, this->getWeightedBiasSettings(), settings);

    // Early exit if no video selected
    if (item_path.isEmpty()) {
        this->setCurrent(-1, "", "", "", "");
        this->selectCurrentItem(nullptr);
        return false;
    }

    // Find the item in the tree
    QList<QTreeWidgetItem*> items = this->ui.videosWidget->findItemsCustom(item_path, Qt::MatchExactly, ListColumns["PATH_COLUMN"], 1);

    if (items.isEmpty()) {
        this->setCurrent(-1, "", "", "", "");
        this->selectCurrentItem(nullptr);
        return false;
    }

    // Set current video
    QTreeWidgetItem* item = items.first();
    this->setCurrent(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(),
        item->text(ListColumns["PATH_COLUMN"]),
        item->text(ListColumns["NAME_COLUMN"]),
        item->text(ListColumns["AUTHOR_COLUMN"]),
        item->text(ListColumns["TAGS_COLUMN"]),
        reset_progress);
    this->selectCurrentItem(item);
    return true;
}

void MainWindow::refreshCurrentVideo() {
    this->ui.currentVideo->setValues(this->App->db->getCurrentVideo(this->App->currentDB));
}

void MainWindow::initListDetails() {
    this->App->db->db.transaction();
    this->refreshCurrentVideo();
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

void MainWindow::updateProgressBar(double position, double duration, QSharedPointer<BasePlayer> player, bool running)
{
    if(not player->change_in_progress)
        this->updateProgressBar(position, duration);
}

void MainWindow::showEndOfVideoDialog() {
    if (this->App->VW->mainPlayer and not this->App->VW->mainPlayer->video_path.isEmpty() and this->App->VW->mainPlayer->end_of_video == true and this->App->VW->mainPlayer->change_in_progress == false) {
        if (not this->finish_dialog) {
            this->finish_dialog = new finishDialog(this);
            this->finish_dialog->setAttribute(Qt::WA_DeleteOnClose);
            this->finish_dialog->setWindowFlag(Qt::WindowStaysOnTopHint, true);
            this->finish_dialog->setWindowState((this->finish_dialog->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
            this->finish_dialog->raise();
            this->finish_dialog->activateWindow();
            this->finish_dialog->open();
            connect(this->finish_dialog, &finishDialog::finished, this, [this](int result) {
                if (result == finishDialog::Accepted) {
                    this->App->VW->mainPlayer->change_in_progress = true;
                    if (this->App->VW->mainPlayer && this->App->VW->mainPlayer->position != -1) {
                        this->App->db->updateVideoProgress(this->App->VW->mainPlayer->video_id, this->App->VW->mainPlayer->position);
                    }
                    this->App->VW->mainPlayer->position = -1;
                    this->NextButtonClicked(this->App->VW->mainPlayer, true, this->getCheckedUpdateWatchedToggleButton());
                    this->position = 0;
                }
                else if (result == finishDialog::Replay) {
                    //Replay button
                    this->App->db->db.transaction();
                    if (this->App->currentDB == "MINUS") {
                        this->incrementCounterVar(-1);
                        this->sv_count++;
                        this->ui.counterLabel->setProgress(sv_count);
                        this->App->db->setMainInfoValue("sv_count", "ALL", QString::number(this->sv_count));
                    }
                    else if (this->App->currentDB == "PLUS") {
                        this->incrementtimeWatchedIncrement(std::max(0.0, this->App->VW->mainPlayer->position));
                        this->checktimeWatchedIncrement();
                        this->updateWatchedProgressBar();
                    }
                    if (this->ui.videosWidget->last_selected and this->ui.videosWidget->last_selected->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt() == this->App->VW->mainPlayer->video_id) {
                        this->ui.videosWidget->last_selected->setText(ListColumns["VIEWS_COLUMN"], QString::number(this->ui.videosWidget->last_selected->text(ListColumns["VIEWS_COLUMN"]).toInt() + 1));
                        this->ui.videosWidget->last_selected->setData(ListColumns["LAST_WATCHED_COLUMN"], Qt::DisplayRole, QDateTime::currentDateTime());
                    }
                    this->App->db->incrementVideoViews(this->App->VW->mainPlayer->video_id, 1, true);
                    this->App->db->incrementVideosWatchedToday(this->App->currentDB);
                    this->App->db->db.commit();
                    this->App->VW->mainPlayer->queue.enqueue(std::make_shared<MpcDirectCommand>(CMD_SETPOSITION, "0"));
                    this->position = 0;
                }
                else if (result == finishDialog::Skip) {
                    //Skip button
                    this->App->VW->mainPlayer->change_in_progress = true;
                    this->App->VW->mainPlayer->position = -1;
                    this->NextButtonClicked(this->App->VW->mainPlayer, false, this->getCheckedUpdateWatchedToggleButton());
                    this->position = 0;
                }
                this->finish_dialog = nullptr;
            });
        }
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
                generateThumbnailRunnable::deleteThumbnail(item->text(ListColumns["PATH_COLUMN"]));
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
    QList<QPair<QString, int>> valid_files = QList<QPair<QString, int>>();
    int new_files_counter = 0;
    for (QString& file : files) {
        QMimeType guess_type = db.mimeTypeForFile(file);
        QString path = QDir::toNativeSeparators(file);
        if (guess_type.isValid() && guess_type.name().startsWith("video")) {
            int video_id_if_exists = this->App->db->getVideoId(path, this->App->currentDB);
            valid_files.append({ path,video_id_if_exists });
            if (video_id_if_exists == -1)
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
    dialog.ui.ResetWatchedCheckBox->setChecked(update_state);

    if (!type.isEmpty()) {
        type_detected = type;
        dialog.ui.typeComboBox->setCurrentText(type_detected);
    }
    dialog.setWindowState((dialog.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    dialog.raise();
    dialog.activateWindow();
    dialog.ui.newfilesLabel->setText(dialog.ui.newfilesLabel->text() + QString::number(new_files_counter));

    QSet<QString> authors_names = QSet<QString>();

    for (QPair<QString,int>& path_pair : valid_files) {
        QStringList tmp = utils::getDirNames(path_pair.first);
        QSet<QString> authors_names_(tmp.begin(),tmp.end());
        authors_names += authors_names_;
        QTreeWidgetItem* item = new TreeWidgetItem({ path_pair.first,"","",""}, 0, nullptr);
        item->setDisabled(path_pair.second != -1);
        item->setData(0, CustomRoles::id, path_pair.second);
        item->setTextAlignment(3, Qt::AlignHCenter);
        dialog.ui.newFilesTreeWidget->addTopLevelItem(item);
        if (path_pair.second != -1 and not dialog.ui.showExistingCheckBox->isChecked())
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
    bool updated = false;
    QStringList files_inserted = QStringList();

    it = QTreeWidgetItemIterator(dialog.ui.newFilesTreeWidget);
    while (*it) {
        if (transaction == false) {
            transaction = true;
            this->App->db->db.transaction();
        }
        if ((*it)->isDisabled()) {
            if (dialog.ui.ResetWatchedCheckBox->isChecked()) {
                this->App->db->updateWatchedState((*it)->data(0, CustomRoles::id).toInt(), "No", false, false);
                updated = true;
            }
            ++it;
            continue;
        }
        this->App->db->insertVideo((*it)->text(0), current_db, (*it)->text(2), (*it)->text(1), (*it)->text(3));
        count++;
        if (count == 100) {
            this->App->db->db.commit();
            transaction = false;
            count = 0;
        }
        files_inserted.append((*it)->text(0));
        this->thumbnailManager->enqueue_work({ (*it)->text(0), false, false});
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
        this->thumbnailManager->start();
        if (set_first_current) {
            QList<QTreeWidgetItem*> items = this->ui.videosWidget->findItemsCustom(first, Qt::MatchExactly, ListColumns["PATH_COLUMN"], 1);
            if (!items.isEmpty()) {
                QTreeWidgetItem *item = items.first();
                this->setCurrent(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), item->text(ListColumns["PATH_COLUMN"]), item->text(ListColumns["NAME_COLUMN"]), item->text(ListColumns["AUTHOR_COLUMN"]), item->text(ListColumns["TAGS_COLUMN"]));
                this->selectCurrentItem(item,false);
            }
        }
    }
    else if (updated) {
        this->refreshVideosWidget(false, true);
    }
    return true;
}

void MainWindow::openEmptyVideoPlayer() {
    if (this->App->VW->mainPlayer == nullptr) {
        QSharedPointer<BasePlayer> l = this->App->VW->newPlayer("",-1);
        this->App->VW->setMainPlayer(l);
        l->openPlayer(l->video_path, l->position);
        this->VideoInfoNotification();
        qMainApp->logger->log(QString("Opening empty Video Player."),"Video");
    }
    else {
        utils::bring_hwnd_to_foreground_uiautomation_method(this->App->VW->mainPlayer->player_hwnd, this->App->uiAutomation);
    }
}

void MainWindow::watchCurrent() {
    if (this->App->VW->mainPlayer == nullptr) {
        QSharedPointer<BasePlayer> l = this->App->VW->newPlayer(this->ui.currentVideo->path, this->ui.currentVideo->id);
        this->App->VW->setMainPlayer(l);
        double seconds = this->position;
        if (seconds < 0.001)
            seconds = 0.001;
        else if (seconds >= this->duration - 0.1) {
			seconds = this->duration - 0.1; // mpc-hc bugs out a bit if we try to play from the end of the video, so we set it to a bit before the end
        }
        l->openPlayer(this->ui.currentVideo->path, seconds);
        connect(l.data(), &BasePlayer::endOfVideoSignal, this, &MainWindow::showEndOfVideoDialog);
        this->VideoInfoNotification();
        qMainApp->logger->log(QString("Playing Current Video \"%1\" from %2").arg(this->ui.currentVideo->path).arg(utils::formatSecondsQt(seconds)), "Video", this->ui.currentVideo->path);
    }
    else {
        utils::bring_hwnd_to_foreground_uiautomation_method(this->App->VW->mainPlayer->player_hwnd, this->App->uiAutomation);
    }
}

void MainWindow::watchSelected(int video_id, QString path) {
    QSharedPointer<BasePlayer> l = this->App->VW->newPlayer(path, video_id);
    double seconds = this->App->db->getVideoProgress(video_id).toDouble();
    if (seconds < 0.001)
        seconds = 0.001;
    l->openPlayer(path, seconds);
    qMainApp->logger->log(QString("Playing Video \"%1\" from %2").arg(path).arg(utils::formatSecondsQt(seconds)), "Video", path);
}

void MainWindow::updateSearchCompleter() {
    QStringList wordList;
    wordList << this->App->db->getAuthors(this->App->currentDB);
    wordList << this->App->db->getUniqueNames(this->App->currentDB);
    wordList << this->App->db->getUniqueTags(this->App->currentDB);
    wordList.removeDuplicates();
    static_cast<QStringListModel*>(this->search_completer->model())->setStringList(wordList);
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
    this->updateSearchCompleter();
}

void MainWindow::switchCurrentDB(QString db) {
    this->App->VW->clearData(false);
    this->lastScrolls.clear();
    QSharedPointer<BasePlayer> player = nullptr;
    if (this->App->VW->mainPlayer) {
        player = this->App->VW->mainPlayer;
        if (player->position != -1)
            this->App->db->updateVideoProgress(player->video_id, player->position);
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
    this->init_icons();
    this->initListDetails();
    this->initNextButtonMode(this->ui.next_button);
    this->initRandomButtonMode(this->ui.random_button);
    connect(this->ui.update_watched_button, &QToolButton::toggled, this, &MainWindow::setCheckedUpdateWatchedToggleButton);
    this->initUpdateWatchedToggleButton();
    this->refreshVideosWidget();
    this->refreshHeadersVisibility();
    if (this->toggleDatesFlag)
        this->toggleDates(false);
    this->App->config->set("current_db", this->App->currentDB);
    this->App->config->save_config();
    this->filterSettings = FilterSettings(this->App->db->getFilterSettings(this->App->currentDB));
    if(player != nullptr){
        this->changePlayerVideo(player, this->ui.currentVideo->path, this->ui.currentVideo->id, this->position);
    }
    this->updateSearchCompleter();
}

void MainWindow::initUpdateWatchedToggleButton() {
    QString config_key = this->getUpdateWatchedToggleButtonConfigKey();
    this->ui.update_watched_button->setChecked(this->App->config->get_bool(config_key));
}

void MainWindow::setCheckedUpdateWatchedToggleButton(bool checked) {
    QString config_key = this->getUpdateWatchedToggleButtonConfigKey();
    this->App->config->set(config_key, utils::bool_to_text_qt(checked));
    this->ui.update_watched_button->setChecked(checked);
    this->App->config->save_config();
}

QString MainWindow::getUpdateWatchedToggleButtonConfigKey() {
    if (this->App->currentDB == "PLUS") {
        return "plus_update_watched";
    }
    else if (this->App->currentDB == "MINUS") {
        return "minus_update_watched";
    }
    return "";
}

bool MainWindow::getCheckedUpdateWatchedToggleButton() {
    QString config_key = this->getUpdateWatchedToggleButtonConfigKey();
    return this->App->config->get_bool(config_key);
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
    if (value > 0)
        qMainApp->logger->log(QString("Increasing time watched by %1").arg(utils::formatSecondsQt(value)), "Stats");
    double time = this->App->db->getMainInfoValue("timeWatchedIncrement", "ALL", "0").toDouble();
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
    if (value >= 0)
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

void MainWindow::initNextButtonMode(customQButton* nextbutton) {
    NextVideoModes::Mode mode = this->getNextVideoMode();
    switch (mode) {
        case NextVideoModes::Sequential:
            nextbutton->setText("Next");
            break;
        case NextVideoModes::Random:
            nextbutton->setText("Next (R)");
            break;
        case NextVideoModes::SeriesRandom:
            nextbutton->setText("Next (SR)");
            break;
        default:
            // Default to Sequential if an unexpected value is encountered
            nextbutton->setText("Next");
            break;
    }
}

bool MainWindow::isNextButtonRandom() {
    if (this->App->currentDB == "PLUS") {
        return this->App->config->get_bool("plus_random_next");
    }
    else if (this->App->currentDB == "MINUS") {
        return this->App->config->get_bool("minus_random_next");
    }
    return true;
}

NextVideoModes::Mode MainWindow::getNextVideoMode() {
    // Get the raw string value from config
    QString config_value = this->App->config->get(this->getNextButtonConfigKey());
    
    // Try to convert to integer first to check if it's already using the new system
    bool ok;
    int mode_value = config_value.toInt(&ok);
    
    if (ok && mode_value >= NextVideoModes::Sequential && mode_value <= NextVideoModes::SeriesRandom) {
        // It's already using the new system
        return static_cast<NextVideoModes::Mode>(mode_value);
    } else {
        // It's still using the old boolean system, convert it
        bool is_random = utils::text_to_bool(config_value.toStdString());
        if (is_random) {
            return NextVideoModes::Random;
        } else {
            return NextVideoModes::Sequential;
        }
    }
}

QString MainWindow::getNextButtonConfigKey() {
    if (this->App->currentDB == "PLUS") {
        return "plus_random_next";
    }
    else if (this->App->currentDB == "MINUS") {
        return "minus_random_next";
    }
    return "";
}

void MainWindow::switchNextButtonMode(customQButton* nextbutton) {
    QString config_key = this->getNextButtonConfigKey();
    NextVideoModes::Mode current_mode = this->getNextVideoMode();
    
    // Cycle through the modes: Sequential -> Random -> SeriesRandom -> Sequential -> ...
    NextVideoModes::Mode next_mode;
    switch (current_mode) {
        case NextVideoModes::Sequential:
            next_mode = NextVideoModes::Random;
            break;
        case NextVideoModes::Random:
            next_mode = NextVideoModes::SeriesRandom;
            break;
        case NextVideoModes::SeriesRandom:
            next_mode = NextVideoModes::Sequential;
            break;
        default:
            // In case of any unexpected value, default to Sequential
            next_mode = NextVideoModes::Sequential;
            break;
    }
    
    this->App->config->set(config_key, QString::number(static_cast<int>(next_mode)));
    if (nextbutton != this->ui.next_button)
        this->initNextButtonMode(this->ui.next_button);
    this->initNextButtonMode(nextbutton);
    this->App->config->save_config();
}

QString MainWindow::getRandomButtonConfigKey() {
    // All - doesnt care about default settings
    // Normal - ignores filter settings but cares about default settings
    // Filtered - uses filter settings
    if (this->App->currentDB == "PLUS") {
        return "plus_get_random_mode";
    }
    else if (this->App->currentDB == "MINUS") {
        return "minus_get_random_mode";
    }
    return "";
}

void MainWindow::initRandomButtonMode(customQButton* randombutton) {
    QString config_key = this->getRandomButtonConfigKey();
    if (this->App->config->get(config_key).toInt() == RandomModes::All) {
        randombutton->setText("Get Random (All)");
    }
    else if (this->App->config->get(config_key).toInt() == RandomModes::Filtered) {
        randombutton->setText("Get Random (F)");
    }
    else {
        randombutton->setText("Get Random");
    }
}

void MainWindow::switchRandomButtonMode(customQButton* randombutton) {
    QString config_key = this->getRandomButtonConfigKey();
    int mode = this->App->config->get(config_key).toInt();
    if (mode == RandomModes::Normal)
        mode = RandomModes::Filtered;
    else if (mode == RandomModes::Filtered)
        mode = RandomModes::All;
    else
        mode = RandomModes::Normal;
    this->App->config->set(config_key, QString::number(mode));
    if (randombutton != this->ui.random_button) {
        this->initRandomButtonMode(this->ui.random_button);
    }
    this->initRandomButtonMode(randombutton);
    this->App->config->save_config();
}

void MainWindow::updateSortConfig() {
    this->App->config->set("sort_column", ListColumns_reversed[this->ui.videosWidget->header()->sortIndicatorSection()]);
    this->App->config->set("sort_order", sortingDict_reversed[this->ui.videosWidget->header()->sortIndicatorOrder()]);
    this->App->config->save_config();
}



void MainWindow::populateList(bool selectcurrent) {
	auto itemlist = this->App->db->getVideos(this->App->currentDB);
	this->ui.videosWidget->setSortingEnabled(false);
	this->ui.videosWidget->addTopLevelItems(itemlist);

	QString settings_str = QString();
	if (this->App->currentDB == "PLUS")
		settings_str = this->App->config->get("headers_plus_visible");
	else if (this->App->currentDB == "MINUS")
		settings_str = this->App->config->get("headers_minus_visible");
	QStringList settings_list = settings_str.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
	bool watched_yes = settings_list.contains("watched_yes");
	bool watched_no = settings_list.contains("watched_no");
	bool watched_mixed = settings_list.contains("watched_mixed");
	QString option = this->getWatchedVisibilityOption(watched_yes, watched_no, watched_mixed, this->ui.searchBar->isVisible(), this->ui.visibleOnlyCheckBox->isChecked());

	QSet<QString> authorsWithUnwatched;
	if (option == "Mixed") {
		for (QTreeWidgetItem* item : itemlist) {
			if (item->text(ListColumns["WATCHED_COLUMN"]) == "No") {
				authorsWithUnwatched.insert(item->text(ListColumns["AUTHOR_COLUMN"]));
			}
		}
	}

	QString search_text = this->ui.searchBar->isVisible() ? this->old_search : "";
	std::shared_ptr<rapidfuzz::fuzz::CachedPartialRatio<char>> cached_ratio_ptr_shared;
	if (!search_text.isEmpty()) {
		cached_ratio_ptr_shared = std::make_shared<rapidfuzz::fuzz::CachedPartialRatio<char>>(
			search_text.toLower().toStdString()
		);
	}

	for (auto& item : itemlist)
	{
		item->setTextAlignment(ListColumns["WATCHED_COLUMN"], Qt::AlignHCenter);
		item->setTextAlignment(ListColumns["VIEWS_COLUMN"], Qt::AlignHCenter);
		item->setTextAlignment(ListColumns["TAGS_COLUMN"], Qt::AlignHCenter);
		item->setTextAlignment(ListColumns["TYPE_COLUMN"], Qt::AlignHCenter);
		item->setTextAlignment(ListColumns["DATE_CREATED_COLUMN"], Qt::AlignHCenter);
		item->setTextAlignment(ListColumns["LAST_WATCHED_COLUMN"], Qt::AlignHCenter);
		if (item->text(ListColumns["WATCHED_COLUMN"]) == "Yes")
			item->setForeground(ListColumns["WATCHED_COLUMN"], QBrush(QColor("#00e640")));
		else if (item->text(ListColumns["WATCHED_COLUMN"]) == "No")
			item->setForeground(ListColumns["WATCHED_COLUMN"], QBrush(QColor("#cf000f")));

		bool hidden = determineVisibility(item, option, search_text, cached_ratio_ptr_shared.get(), authorsWithUnwatched);
		item->setHidden(hidden);
	}
	// not sure if this alternative method is needed anymore
	//if (this->ui.searchBar->isVisible())
	//    QTimer::singleShot(1, [this,itemlist] {this->search(this->old_search);});
	this->updateTotalListLabel();
	this->updateVideoListRandomProbabilitiesIfVisible();
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
    QAction* tags = new QAction("Tags", &menu);
    tags->setCheckable(true);
    if (settings_list.contains("tags"))
        tags->setChecked(true);
    menu.addAction(tags);
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
    QAction* random_chance = new QAction("Random %", &menu);
    random_chance->setCheckable(true);
    if (settings_list.contains("random%"))
        random_chance->setChecked(true);
    menu.addAction(random_chance);
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
    else if (menu_click == tags) {
        if (utils::hiddenCheck(settings_list) && !tags->isChecked())
            settings_list.removeAll("tags");
        else
            settings_list.append("tags");
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
        if (!watched_mixed->isChecked()) {
            settings_list.removeAll("watched_mixed");
            settings_list.append("watched_yes");
            settings_list.append("watched_no");
        }
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
    else if (menu_click == random_chance) {
        if (utils::hiddenCheck(settings_list) && !random_chance->isChecked())
            settings_list.removeAll("random%");
        else
            settings_list.append("random%");
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
        this->updateHeaderSettings(settings_list);
    }
}

void MainWindow::updateHeaderSettings(QStringList settings) {
    settings.removeDuplicates();
    if (this->App->currentDB == "PLUS")
        this->App->config->set("headers_plus_visible", settings.join(" "));
    else if (this->App->currentDB == "MINUS")
        this->App->config->set("headers_minus_visible", settings.join(" "));
    this->refreshHeadersVisibility();
    this->refreshVisibility();
    if (this->toggleDatesFlag)
        this->toggleDates(false);
    this->App->config->save_config();
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
    QAction* generate_thumbnails = new QAction("Generate Thumbnails", &menu);
    menu.addAction(generate_thumbnails);
    QMenu* set_menu = new QMenu("Set", &menu);
    QAction* set_current = new QAction("Set as current", set_menu);
    set_menu->addAction(set_current);
    QAction* author_edit = new QAction("Edit Author", set_menu);
    set_menu->addAction(author_edit);
    QAction* name_edit = new QAction("Edit Name", set_menu);
    set_menu->addAction(name_edit);
    QAction* update_path = new QAction("Update Path", set_menu);
    set_menu->addAction(update_path);
    QAction* tags_edit = new QAction("Edit Tags", set_menu);
    set_menu->addAction(tags_edit);
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
        this->setCurrent(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), item->text(ListColumns["PATH_COLUMN"]), item->text(ListColumns["NAME_COLUMN"]), item->text(ListColumns["AUTHOR_COLUMN"]), item->text(ListColumns["TAGS_COLUMN"]));
        this->selectCurrentItem();
        if (this->App->VW->mainPlayer) {
            this->changePlayerVideo(this->App->VW->mainPlayer, this->ui.currentVideo->path, this->ui.currentVideo->id, this->position);
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
    else if (menu_click == generate_thumbnails) {
        for (auto const& item : items) {
            this->thumbnailManager->enqueue_work({ item->text(ListColumns["PATH_COLUMN"]), true, false });
            this->thumbnailManager->start();
        }
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
        if (not items.isEmpty()) {
            UpdatePathsDialog* dialog = new UpdatePathsDialog(this, this);
            dialog->addItems(items);
            dialog->exec();
            dialog->deleteLater();
        }
    }
    else if (menu_click == tags_edit) {
        this->editTags(items);
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

VideosTagsDialog* MainWindow::editTags(QList<QTreeWidgetItem*> items, QWidget* parent) {
    if (items.isEmpty())
        return nullptr;
    VideosTagsDialog *dialog = new VideosTagsDialog(items, this, parent);
    dialog->open();
    connect(dialog, &VideosTagsDialog::finished, this, [this, dialog](int result) {
        if (result == QDialog::Accepted) {
            this->App->db->db.transaction();
            for (auto& video : dialog->items) {
                int video_id = video.first;
                QSet<int> tags_id = QSet<int>();
                for (int i = 0; i < dialog->ui.listWidgetAdded->count(); ++i) {
                    QListWidgetItem* item = dialog->ui.listWidgetAdded->item(i);
                    tags_id.insert(item->data(Qt::UserRole).value<Tag>().id);
                    this->App->db->insertTag(video_id, item->data(Qt::UserRole).value<Tag>().id);
                }
                this->App->db->deleteExtraTags(video_id, tags_id);
            }
            this->App->db->db.commit();
            this->refreshVideosWidget(false, true);
            this->refreshCurrentVideo();
        }
        dialog->deleteLater();
    });
    return dialog;
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

bool MainWindow::event(QEvent* e)
{
    if (e->type() == QEvent::WindowActivate && this->isActiveWindow()) {
        if (this->intro_played) {
            this->playSpecialSoundEffect();
        }
    }
    if (e->type() == QEvent::Resize) {
        //scale mascots
        QResizeEvent* resizeEvent = static_cast<QResizeEvent*>(e);
        int windowWidth = resizeEvent->size().width();
        int windowHeight = resizeEvent->size().height();
        this->ui.leftImg->setMinimumWidth(windowWidth / 6.6);
        this->ui.rightImg->setMinimumWidth(windowWidth / 6.6);
    }
    return QMainWindow::event(e);
}

void MainWindow::playSpecialSoundEffect(bool force_play) {
    if (this->App->config->get_bool("sound_effects_special_on")) {
        if(force_play or not this->special_effects_player->isPlaying())
            this->App->soundPlayer->playSpecialSoundEffect(this->special_effects_player);
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
        bool update_date = (value == "Yes") ? true : false;
        for (auto const& item : items) {
            this->App->db->updateWatchedState(item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(), value, false, update_date);
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

void MainWindow::setCurrent(int id, QString path, QString name, QString author, QString tags, bool reset_progress) {
    this->ui.currentVideo->setValues(id, path, name, author, tags);
    if (reset_progress)
        this->updateProgressBar("0", utils::getVideoDuration(this->ui.currentVideo->path));
    else
        this->updateProgressBar(this->App->db->getVideoProgress(this->ui.currentVideo->id), utils::getVideoDuration(this->ui.currentVideo->path));
    this->App->db->setMainInfoValue("current", this->App->currentDB, path);
    qMainApp->logger->log(QString("Setting current Video to \"%1\"").arg(path), "Video",path);
}

QString MainWindow::getWatchedVisibilityOption(bool watched_yes, bool watched_no, bool watched_mixed, bool search_bar_visible, bool visible_only_checkbox) {
    QString option = "";
    if (search_bar_visible and not visible_only_checkbox)
        option = "All";
    else if (watched_mixed == true)
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
    if (settings_list.contains("tags"))
        header->setSectionHidden(ListColumns["TAGS_COLUMN"], false);
    else
        header->setSectionHidden(ListColumns["TAGS_COLUMN"], true);
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
    if (settings_list.contains("random%")) {
        header->setSectionHidden(ListColumns["RANDOM%_COLUMN"], false);
        this->updateVideoListRandomProbabilitiesIfVisible();
    }
    else
        header->setSectionHidden(ListColumns["RANDOM%_COLUMN"], true);
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

void MainWindow::changePlayerVideo(QSharedPointer<BasePlayer> player, QString path, int video_id, double position) {
    player->changeVideo(path, video_id, position);
    if (this->finish_dialog) {
        this->finish_dialog->close();
        this->finish_dialog = nullptr;
    }
    this->VideoInfoNotification();
    qMainApp->logger->log(QString("Changing Video to \"%1\"").arg(path), "Video", path);
}

void MainWindow::updateVideoListRandomProbabilities() {
    NextVideoSettings next_video_settings = this->getNextVideoSettings();
    NextVideoModes::Mode current_mode = this->getNextVideoMode();

    QJsonObject random_settings;
    QList<VideoWeightedData> items;
    QList<QTreeWidgetItem*> relevant_items;

    if (current_mode == NextVideoModes::SeriesRandom) {
        // Find current video item once
        QTreeWidgetItem* current_video_item = nullptr;
        if (!this->ui.currentVideo->path.isEmpty()) {
            QList<QTreeWidgetItem*> current_items = this->ui.videosWidget->findItemsCustom(this->ui.currentVideo->path, Qt::MatchExactly, ListColumns["PATH_COLUMN"], 1);
            if (!current_items.isEmpty()) {
                current_video_item = current_items.first();
            }
        }

        if (current_video_item != nullptr) {
            QString current_author = current_video_item->text(ListColumns["AUTHOR_COLUMN"]);
            if (current_author.isEmpty()) {
                current_author = current_video_item->text(ListColumns["PATH_COLUMN"]);
            }

            // Single pass to get author's unwatched videos
            QList<QTreeWidgetItem*> author_unwatched_videos;
            QTreeWidgetItemIterator iter(this->ui.videosWidget);
            while (*iter) {
                QTreeWidgetItem* item = *iter;
                if (item->text(ListColumns["WATCHED_COLUMN"]) == "No") {
                    QString item_author = item->text(ListColumns["AUTHOR_COLUMN"]);
                    if (item_author.isEmpty()) {
                        item_author = item->text(ListColumns["PATH_COLUMN"]);
                    }
                    if (item_author == current_author) {
                        author_unwatched_videos.append(item);
                    }
                }
                ++iter;
            }

            // Find current position
            int current_pos = author_unwatched_videos.indexOf(current_video_item);

            // Determine next item
            if (current_pos != -1 && current_pos + 1 < author_unwatched_videos.size()) {
                relevant_items.append(author_unwatched_videos[current_pos + 1]);
            }
            else if (current_pos != -1 && current_pos == author_unwatched_videos.size() - 1 && author_unwatched_videos.size() > 1) {
                relevant_items.append(author_unwatched_videos[0]);
            }
            else {
                // Need to switch authors - use optimized helper
                random_settings = this->getRandomSettings(next_video_settings.random_mode, next_video_settings.ignore_filters_and_defaults,
                    next_video_settings.vid_type_include, next_video_settings.vid_type_exclude);
                QList<VideoWeightedData> all_videos = this->App->db->getVideos(this->App->currentDB, random_settings);

                QMap<QString, AuthorVideoData> author_map = this->getAuthorVideoMap(current_author, all_videos);
                this->findFirstItemsForAuthors(author_map);
                items = this->calculateAuthorWeights(author_map);

                // Collect relevant items
                for (const VideoWeightedData& author_data : items) {
                    for (auto it = author_map.constBegin(); it != author_map.constEnd(); ++it) {
                        if (it.value().firstItem &&
                            author_data.path == it.value().firstItem->text(ListColumns["PATH_COLUMN"])) {
                            relevant_items.append(it.value().firstItem);
                            break;
                        }
                    }
                }
            }
        }
        else {
            // No current video - use optimized helper
            random_settings = this->getRandomSettings(next_video_settings.random_mode, next_video_settings.ignore_filters_and_defaults,
                next_video_settings.vid_type_include, next_video_settings.vid_type_exclude);
            QList<VideoWeightedData> all_videos = this->App->db->getVideos(this->App->currentDB, random_settings);

            QMap<QString, AuthorVideoData> author_map = this->getAuthorVideoMap("", all_videos);
            this->findFirstItemsForAuthors(author_map);
            items = this->calculateAuthorWeights(author_map);

            // Collect relevant items
            for (const VideoWeightedData& author_data : items) {
                for (auto it = author_map.constBegin(); it != author_map.constEnd(); ++it) {
                    if (it.value().firstItem &&
                        author_data.path == it.value().firstItem->text(ListColumns["PATH_COLUMN"])) {
                        relevant_items.append(it.value().firstItem);
                        break;
                    }
                }
            }
        }

        // If items is empty (staying with current author), get actual video data
        if (items.isEmpty() && !relevant_items.isEmpty()) {
            random_settings = this->getRandomSettings(next_video_settings.random_mode, next_video_settings.ignore_filters_and_defaults,
                next_video_settings.vid_type_include, next_video_settings.vid_type_exclude);
            QList<VideoWeightedData> all_items = this->App->db->getVideos(this->App->currentDB, random_settings);

            QString target_path = relevant_items.first()->text(ListColumns["PATH_COLUMN"]);
            for (const VideoWeightedData& video : all_items) {
                if (video.path == target_path) {
                    items.append(video);
                    break;
                }
            }
        }
    }
    else {
        // For other modes, use original behavior
        random_settings = this->getRandomSettings(next_video_settings.random_mode, next_video_settings.ignore_filters_and_defaults,
            next_video_settings.vid_type_include, next_video_settings.vid_type_exclude);
        items = this->App->db->getVideos(this->App->currentDB, random_settings);
    }

    WeightedBiasSettings bias_settings = this->getWeightedBiasSettings();
    if (!bias_settings.weighted_random_enabled) {
        bias_settings.bias_general = 0;
    }

    // Calculate probabilities
    QMap<int, long double> probabilities;

    if (current_mode == NextVideoModes::SeriesRandom && !relevant_items.isEmpty()) {
        if (relevant_items.size() == 1) {
            // Single deterministic next video - 100%
            probabilities[items.first().id] = 100.0;
        }
        else {
            // Multiple possible next videos
            probabilities = utils::calculateProbabilities(items, bias_settings.bias_views, bias_settings.bias_rating,
                bias_settings.bias_tags, bias_settings.bias_general,
                bias_settings.no_views_weight, bias_settings.no_rating_weight,
                bias_settings.no_tags_weight);
        }
    }
    else {
        probabilities = utils::calculateProbabilities(items, bias_settings.bias_views, bias_settings.bias_rating,
            bias_settings.bias_tags, bias_settings.bias_general,
            bias_settings.no_views_weight, bias_settings.no_rating_weight,
            bias_settings.no_tags_weight);
    }

    // Update UI - single pass
    QTreeWidgetItemIterator it2(this->ui.videosWidget);
    bool sorting = this->ui.videosWidget->isSortingEnabled();
    this->ui.videosWidget->setSortingEnabled(false);

    while (*it2) {
        bool hidden = (*it2)->isHidden();
        (*it2)->setHidden(true);
        int id = (*it2)->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt();
        (*it2)->setText(ListColumns["RANDOM%_COLUMN"], QString::number(probabilities.value(id, 0), 'f', 2) % "%");
        (*it2)->setTextAlignment(ListColumns["RANDOM%_COLUMN"], Qt::AlignCenter);
        (*it2)->setHidden(hidden);
        ++it2;
    }

    this->ui.videosWidget->setSortingEnabled(sorting);
}

void MainWindow::updateVideoListRandomProbabilitiesIfVisible() {
    if (not this->ui.videosWidget->header()->isSectionHidden(ListColumns["RANDOM%_COLUMN"]))
        this->updateVideoListRandomProbabilities();
}

MainWindow::~MainWindow()
{
    this->animatedIcon->running = false;
    this->animatedIcon->quit();
    this->animatedIcon->deleteLater();
    this->search_completer->deleteLater();
    this->thumbnailManager->deleteLater();
    delete this->active;
    delete this->inactive;
    delete this->halfactive;
}
