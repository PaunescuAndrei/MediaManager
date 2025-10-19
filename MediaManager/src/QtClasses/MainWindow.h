#pragma once
#include <QMainWindow>
#include "ui_MainWindow.h"
#include <QPoint>
#include <QTreeWidgetItem>
#include "IconChanger.h"
#include <QQueue>
#include "generateThumbnailManager.h"
#include "generateThumbnailRunnable.h"
#include <QSystemTrayIcon>
#include "SettingsDialog.h"
#include "FilterSettings.h"
#include "NotificationDialog.h"
#include "finishDialog.h"
#include "SafeQueue.h"
#include "VideosTagsDialog.h"
#include <QMediaPlayer>
#include "BasePlayer.h"
#include "MpcPlayer.h"
#include "rapidfuzz_all.hpp"
#include <QCompleter>
#include <QThreadPool>

class MainApp;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    QCompleter* search_completer = nullptr;
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
    bool intro_played = true;
    QList<QPair<QString,int>> lastScrolls = QList<QPair<QString, int>>();
    QString old_search = "";
    QString last_backup = "";
    generateThumbnailManager *thumbnailManager = nullptr;
    QIcon* active = new QIcon();
    QIcon* inactive = new QIcon();
    QIcon* halfactive = new QIcon();
    NotificationDialog* notification_dialog = nullptr;
    finishDialog* finish_dialog = nullptr;
    QList<QStringList> IconsStage = QList<QStringList>({ QStringList(),QStringList(),QStringList() });
    QMediaPlayer* special_effects_player = nullptr;
    QThreadPool* searchThreadPool;

    MainWindow(QWidget *parent = nullptr,MainApp *App = nullptr);
    //void resizeEvent(QResizeEvent* event) override;
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
    void switchNextButtonMode(customQButton* nextbutton);
    QString getRandomButtonConfigKey();
    void initRandomButtonMode(customQButton* randombutton);
    void switchRandomButtonMode(customQButton* randombutton);
    void updateSortConfig();
    void addWatchedDialogButton();
    void switchCurrentDB(QString db = "");
    void initUpdateWatchedToggleButton();
    void setCheckedUpdateWatchedToggleButton(bool checked);
    QString getUpdateWatchedToggleButtonConfigKey();
    bool getCheckedUpdateWatchedToggleButton();
    void refreshVideosWidget(bool selectcurrent = true, bool remember_selected = false);
    void watchSelected(int video_id, QString path);
    void watchCurrent();
    void initListDetails();
    void updateWatchedProgressBar();
    void updateProgressBar(double position, double duration);
    void updateProgressBar(double position, double duration, QSharedPointer<BasePlayer> player, bool running = false);
    void showEndOfVideoDialog();
    void updateProgressBar(QString position, QString duration);
    void updateTotalListLabel(bool force_update = false);
    void changePlayerVideo(QSharedPointer<BasePlayer> player, QString path, int video_id, double position);
    void updateVideoListRandomProbabilities();
    void updateVideoListRandomProbabilitiesIfVisible();
    void updateSearchCompleter();
    static bool determineVisibility(QTreeWidgetItem* item, const QString& watched_option, const QString& search_text, const rapidfuzz::fuzz::CachedPartialRatio<char>* cached_search_text_ratio, const QSet<QString>& authorsWithUnwatched);
    void refreshVisibility(QString search_text);
    void refreshVisibility();
    void refreshHeadersVisibility();
    void videosWidgetHeaderContextMenu(QPoint point);
    void updateHeaderSettings(QStringList settings);
    void videosWidgetContextMenu(QPoint point);
    VideosTagsDialog* editTags(QList<QTreeWidgetItem*> items, QWidget* parent = nullptr);
    void setCurrent(int id, QString path, QString name, QString author, QString tags, bool reset_progress = false);
    QString getWatchedVisibilityOption(bool watched_yes, bool watched_no, bool watched_mixed, bool search_bar_visible, bool visible_only_checkbox);
    void DeleteDialogButton(QList<QTreeWidgetItem*> items);
    void setWatched(QString value, QList<QTreeWidgetItem*> items);
    void selectItems(QStringList items, bool clear_selection = true);
    void selectItemsDelayed(QStringList items, bool clear_selection = true);
    void incrementViews(int count, QList<QTreeWidgetItem*> items);
    void setType(QString type, QList<QTreeWidgetItem*> items);
    bool InsertVideoFiles(QStringList files,bool update_state = true, QString currentdb = "", QString type = "");
    void openEmptyVideoPlayer();
    void incrementtimeWatchedIncrement(double value);
    void checktimeWatchedIncrement();
    void incrementCounterVar(int value = 1);
    int getCounterVar();
    void initNextButtonMode(customQButton* nextbutton);
    bool isNextButtonRandom();
    QString getNextButtonConfigKey();
    void setCounterVar(int value);
    QString getRandomVideo(QString seed, WeightedBiasSettings weighted_settings, QJsonObject settings);
    QJsonObject getRandomSettings(RandomModes::Mode random_mode, bool ignore_filters_and_defaults = false, QStringList vid_type_include = {}, QStringList vid_type_exclude = {});
    bool randomVideo(RandomModes::Mode random_mode, bool ignore_filters_and_defaults = false, QStringList vid_type_include = {}, QStringList vid_type_exclude = {}, bool reset_progress = true);
    void refreshCurrentVideo();
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
    void handleMascotClickEvents(customGraphicsView* mascot, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    void applySettings(SettingsDialog* dialog);
    void setDebugMode(bool debug);
    void settingsDialogButton();
    QString saltSeed(QString seed);
    WeightedBiasSettings getWeightedBiasSettings();
    void quit();
    void NextButtonClicked(bool increment, bool update_watched_state);
    void NextButtonClicked(QSharedPointer<BasePlayer> player, bool increment, bool update_watched_state);
    NextVideoSettings getNextVideoSettings();
    bool NextVideo(bool random, bool increment, bool update_watched_state);
    bool setNextVideo(QTreeWidgetItem* item);
    int calculate_sv_target();
    void setCounterDialogButton();
    void loadIcons(QString path);
    QIcon getIconByStage(int stage);
    void setIconWatchingState(bool watching);
    void updateIconByWatchingState();
    void setIcon(QIcon icon);
    void closeEvent(QCloseEvent* event) override;
    void initMascotAnimation();
    void setMascotDebug(QString path, customGraphicsView* sender);
    void setLeftMascot(QString path, QPixmap pixmap);
    void setRightMascot(QString path, QPixmap pixmap);
    void setLeftMascot(QString path);
    void setRightMascot(QString path);
    void setLeftMascot(ImageData data);
    void setRightMascot(ImageData data);
    void updateMascot(customGraphicsView* mascot, bool use_cache, bool update_highlight_color);
    void updateMascots(bool use_cache = true, bool forced_mirror = false);
    void updateThemeHighlightColorFromMascot(customGraphicsView* mascot, bool weighted = true, bool regen_colors_if_missing = true);
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
    bool event(QEvent* e) override;
    void playSpecialSoundEffect(bool force_play = false);
    ~MainWindow();
signals:
    void fileDropped(QStringList files, QWidget* widget = nullptr);
public slots:
    void iconActivated(QSystemTrayIcon::ActivationReason reason);
};
