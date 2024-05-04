#pragma once
#include <QMainWindow>
#include "ui_MainWindow.h"
#include <QPoint>
#include <QTreeWidgetItem>
#include "IconChanger.h"
#include "Listener.h"
#include <QQueue>
#include "generateThumbnailManager.h"
#include "generateThumbnailThread.h"
#include <QSystemTrayIcon>
#include "SettingsDialog.h"
#include "FilterSettings.h"
#include "NotificationDialog.h"
#include "finishDialog.h"
#include "SafeQueue.h"
#include "VideosTagsDialog.h"

class MainApp;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    Ui::MainWindow ui;
    MainApp *App = nullptr;
    FilterSettings filterSettings;
    IconChanger* animatedIcon = nullptr;
    QSystemTrayIcon* trayIcon;
    QTimer* search_timer = nullptr;
    QTimer update_title_timer = QTimer();
    double position = 0;
    double duration = 0;
    int sv_count = 0;
    int sv_target_count = 0;
    int time_watched_limit = -1;
    bool iconWatchingState = false;
    bool animatedIconFlag = false;
    bool toggleDatesFlag = false;
    QList<QPair<QString,int>> lastScrolls = QList<QPair<QString, int>>();
    QString old_search = "";
    QString last_backup = "";
    generateThumbnailManager thumbnailManager = generateThumbnailManager(4);
    QIcon* active = new QIcon();
    QIcon* inactive = new QIcon();
    QIcon* halfactive = new QIcon();
    NotificationDialog* notification_dialog = nullptr;
    finishDialog* finish_dialog = nullptr;
    QList<QStringList> IconsStage = QList<QStringList>({ QStringList(),QStringList(),QStringList() });
    MainWindow(QWidget *parent = nullptr,MainApp *App = nullptr);
    QString getCategoryName(QString currentdb);
    QString getCategoryName();
    void UpdateWindowTitle();
    void VideoInfoNotification();
    void resetPalette();
    void changePalette(QPalette palette);
    void initStyleSheets();
    void initRatingIcons();
    void updateRating(QPersistentModelIndex index, double old_value, double new_value);
    void updateAuthors(QList<QTreeWidgetItem*> items);
    void updateAuthors(QString value, QList<QTreeWidgetItem*> items);
    void updateNames(QList<QTreeWidgetItem*> items);
    void updateNames(QString value, QList<QTreeWidgetItem*> items);
    void updatePath(int video_id, QString new_path);
    void syncItems(QTreeWidgetItem* main_item, QList<QTreeWidgetItem*> items);
    void toggleSearchBar();
    void toggleDates(bool scroll = true);
    void init_icons();
    QMenu* trayIconContextMenu(QWidget* parent = nullptr);
    void openStats();
    void populateList(bool selectcurrent = true);
    void switchNextButtonMode(QCustomButton* nextbutton);
    void switchRandomButtonMode(QCustomButton* randombutton);
    void updateSortConfig();
    void filterVisibilityItem(QTreeWidgetItem* item, QString watched_option, QStringList& mixed_done, QString search_text);
    void addWatchedDialogButton();
    void switchCurrentDB(QString db = "");
    void refreshVideosWidget(bool selectcurrent = true, bool remember_selected = false);
    void watchSelected(int video_id, QString path);
    void watchCurrent();
    void initListDetails();
    void updateWatchedProgressBar();
    void updateProgressBar(double position, double duration);
    void updateProgressBar(double position, double duration, std::shared_ptr<Listener> listener, bool running = false);
    void updateProgressBar(QString position, QString duration);
    void updateTotalListLabel(bool force_update = false);
    void changeListenerVideo(std::shared_ptr<Listener> listener, QString path, int video_id, double position);
    void refreshVisibility(QString search_text);
    void refreshVisibility();
    void refreshHeadersVisibility();
    void videosWidgetHeaderContextMenu(QPoint point);
    void videosWidgetContextMenu(QPoint point);
    VideosTagsDialog* editTags(QList<QTreeWidgetItem*> items, QWidget* parent = nullptr);
    void setCurrent(int id, QString path, QString name, QString author);
    QString getWatchedVisibilityOption(bool watched_yes, bool watched_no, bool watched_mixed);
    void DeleteDialogButton(QList<QTreeWidgetItem*> items);
    void setWatched(QString value, QList<QTreeWidgetItem*> items);
    void selectItems(QStringList items, bool clear_selection = true);
    void selectItemsDelayed(QStringList items, bool clear_selection = true);
    void incrementViews(int count, QList<QTreeWidgetItem*> items);
    void setType(QString type, QList<QTreeWidgetItem*> items);
    bool InsertVideoFiles(QStringList files,bool update_state = true, QString currentdb = "", QString type = "");
    void incrementtimeWatchedIncrement(double value);
    void checktimeWatchedIncrement();
    void incrementCounterVar(int value = 1);
    int getCounterVar();
    void setCounterVar(int value);
    bool randomVideo(bool watched_all = true, QStringList vid_type_include = {}, QStringList vid_type_exclude = {});
    void selectCurrentItem(QTreeWidgetItem* item = nullptr, bool selectcurrent = true);
    void insertDialogButton();
    bool TagsDialogButton();
    void resetDB(QString directory);
    void resetDBDialogButton(QWidget* parent = nullptr);
    bool loadDB(QString path, QWidget * parent = nullptr);
    bool loadDB(QWidget* parent = nullptr);
    bool backupDB(QString path, QWidget * parent = nullptr);
    bool backupDB(QWidget* parent = nullptr);
    void resetWatchedDB(QWidget* parent = nullptr);
    void applySettings(SettingsDialog* dialog);
    void setDebugMode(bool debug);
    void settingsDialogButton();
    void quit();
    void NextButtonClicked(bool increment = true);
    void NextButtonClicked(std::shared_ptr<Listener> listener, bool increment = true);
    bool NextVideo(bool random = true, bool increment = true);
    bool setNextVideo(QTreeWidgetItem* item);
    int calculate_sv_target();
    void setCounterDialogButton();
    // to do implement random to normal icons and dont hard-code it
    void loadIcons(QString path);
    QIcon getIconByStage(int stage);
    void setIconWatchingState(bool watching);
    void updateIconByWatchingState();
    void setIcon(QIcon icon);
    void closeEvent(QCloseEvent* event) override;
    void initMascotAnimation();
    void flipMascotDebug(customGraphicsView* sender, Qt::MouseButton button);
    void setMascotDebug(QString path, customGraphicsView* sender);
    void setLeftMascot(QString path, QPixmap pixmap);
    void setRightMascot(QString path, QPixmap pixmap);
    void setLeftMascot(QString path);
    void setRightMascot(QString path);
    void setMascots(bool cache = true);
    void setThemeHighlightColor(QColor color);
    void openThumbnails(QString path);
    void flipMascots();
    void showMascots();
    void hideMascots();
    bool initMusic();
    void bringWindowToFront();
    void center();
    void resize_center(int w, int h);
    void setViews(int value, QList<QTreeWidgetItem*> items);
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    ~MainWindow();
signals:
    void fileDropped(QStringList files, QWidget* widget = nullptr);
public slots:
    void iconActivated(QSystemTrayIcon::ActivationReason reason);
};
