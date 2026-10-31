#pragma once
#include <QDialog>
#include <QHash>
#include "ui_SettingsDialog.h"
#include "WheelStrongFocusEventFilter.h"
#include "stylesQt.h"

class MainWindow;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QSlider;
class QComboBox;
class QGroupBox;

class SettingsDialog :
    public QDialog
{
    Q_OBJECT
public:
    int oldVolume = 0;
    int oldPreviewVolume = 0;
    bool oldPreviewRandomStart = false;
    bool oldPreviewAutoplayAllMute = false;
    bool oldPreviewRememberPosition = false;
    bool oldPreviewRandomEachHover = false;
    bool oldPreviewLazyLoading = true;
    bool oldPreviewNextChoicesEnabled = true;
    bool oldPreviewSeededRandom = false;
    int oldPreviewMainHistoryCount = 0;
    bool oldPreviewMainEnabled = true;
    int oldPreviewPopupWidth = 420;
    int oldPreviewPopupHeight = 240;
    double oldPreviewOverlayScale = 1.0;
    int oldPreviewOverlayPadX = 4;
    int oldPreviewOverlayPadY = 2;
    int oldPreviewOverlayMargin = 6;
    int oldSVmax = 0;
    double old_mascotsChanceSpinBox = 0;
    double old_aicon_fps_modifier = 1.0;
    int old_mascotsFreqSpinBox = 400;
    int old_mascotsWidthSpinBox = 15;
    bool oldTooltipsEnabled = true;
    int oldTooltipDelayMs = 700;

    // Playback page — next-video volume guard
    QCheckBox* nextVolumeGuardEnabled = nullptr;
    QSpinBox* nextVolumeGuardThreshold = nullptr;
    QSpinBox* nextVolumeGuardTarget = nullptr;

    SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog();
    Ui::SettingsDialog ui;

    // Notification page widgets (built programmatically)
    QCheckBox* notificationVideoInfoEnabled = nullptr;
    QSpinBox* notificationVideoInfoDurationSpinBox = nullptr;
    QCheckBox* notificationMilestoneEnabled = nullptr;
    QSpinBox* notificationMilestoneDurationSpinBox = nullptr;
    QCheckBox* notificationGoalMetEnabled = nullptr;
    QSpinBox* notificationGoalMetDurationSpinBox = nullptr;
    QDoubleSpinBox* milestoneVideoStepSpinBox = nullptr;
    QSpinBox* milestoneTimeStepSpinBox = nullptr;
    QCheckBox* notificationStreakAtRiskEnabled = nullptr;
    QSpinBox* notificationStreakAtRiskDurationSpinBox = nullptr;
    QSpinBox* streakAtRiskMinDaysSpinBox = nullptr;
    QSpinBox* streakAtRiskCutoffHourSpinBox = nullptr;
    QSpinBox* streakAtRiskRefireSpinBox = nullptr;
    QSpinBox* streakDailyTimeTargetSpinBox = nullptr;
    QCheckBox* notificationStreakSavedEnabled = nullptr;
    QSpinBox* notificationStreakSavedDurationSpinBox = nullptr;
    QSpinBox* streakDebtRefireSpinBox = nullptr;
    QSpinBox* streakDebtFinalHourSpinBox = nullptr;

    // General page — streak saves (auto debt + bought freezes)
    QCheckBox* streakSaveEnabled = nullptr;
    QCheckBox* streakAutoSaveEnabled = nullptr;
    QDoubleSpinBox* streakAutoSaveMultiplierSpinBox = nullptr;
    QDoubleSpinBox* streakAutoSaveMultiplierIncrementSpinBox = nullptr;
    QSpinBox* streakAutoSaveMaxConsecutiveSpinBox = nullptr;
    QCheckBox* streakFreezeEnabled = nullptr;
    QSpinBox* streakFreezeBasePriceSpinBox = nullptr;
    QSpinBox* streakFreezePriceIncrementSpinBox = nullptr;
    QSpinBox* streakFreezeMaxStockSpinBox = nullptr;
    QSpinBox* streakFreezePriceResetDaysSpinBox = nullptr;
    QCheckBox* streakFreezeConsumeFirstCheckBox = nullptr;
    QCheckBox* notificationPersonalBestEnabled = nullptr;
    QSpinBox* notificationPersonalBestDurationSpinBox = nullptr;
    QCheckBox* notificationSessionSummaryEnabled = nullptr;
    QSpinBox* notificationSessionSummaryDurationSpinBox = nullptr;
    QSpinBox* notificationSessionSummaryMinSessionSpinBox = nullptr;
    QSpinBox* notificationSessionSummaryMinVideoWatchSpinBox = nullptr;
    QSpinBox* pbVideosStepSpinBox = nullptr;
    QSpinBox* pbTimeStepSpinBox = nullptr;
    QSpinBox* pbVideosMinThresholdSpinBox = nullptr;
    QSpinBox* pbTimeMinThresholdSpinBox = nullptr;
    QDoubleSpinBox* notificationBgOpacitySpinBox = nullptr;
    QDoubleSpinBox* notificationContentOpacitySpinBox = nullptr;
    QDoubleSpinBox* notificationTimerBarOpacitySpinBox = nullptr;
    QDoubleSpinBox* notificationCounterOpacitySpinBox = nullptr;

    // General page — goal-reached counter bonus
    QCheckBox* goalReachedCounterEnabled = nullptr;
    QSpinBox* goalReachedCounterAmountSpinBox = nullptr;

    // Mascots page — built programmatically. The legacy mascots group box itself is not repeated
    // here: `ui.mascotsGroupBox` is reparented onto the page when it is built.
    QComboBox* mascotMediaCombo = nullptr;
    QComboBox* mascotModeCombo = nullptr;
    QComboBox* mascotFillCombo = nullptr;
    QCheckBox* mascotWeightedCheckBox = nullptr;
    QSpinBox* mascotClipSecondsSpinBox = nullptr;
    QSpinBox* mascotCrossfadeSpinBox = nullptr;
    QCheckBox* mascotSwapOnBeatCheckBox = nullptr;
    QCheckBox* mascotRandomStartCheckBox = nullptr;
    QCheckBox* mascotPreloadCheckBox = nullptr;
    QCheckBox* mascotPauseUnfocusedCheckBox = nullptr;
    QSpinBox* mascotMaxDecodersSpinBox = nullptr;
    QSpinBox* mascotScrollSpeedSpinBox = nullptr;
    QComboBox* mascotScrollDirectionCombo = nullptr;
    QCheckBox* mascotScrollPingpongCheckBox = nullptr;
    QSpinBox* mascotStripesLeftSpinBox = nullptr;
    QSpinBox* mascotStripesRightSpinBox = nullptr;
    QComboBox* mascotStripesOrientationCombo = nullptr;
    QCheckBox* mascotStripesAlternateCheckBox = nullptr;
    QCheckBox* mascotBackgroundCheckBox = nullptr;
    QDoubleSpinBox* mascotBackgroundOpacitySpinBox = nullptr;
    // (M5 Task 8: the list's own opacity, back after a commit that removed it. The veil above is the
    // window; this is the list's row fill, which is what makes the rows alternate.)
    QDoubleSpinBox* mascotListAlphaSpinBox = nullptr;
    // (M5 Task 8d: the alternate rows' own alpha -- the row colour that does not match the window's
    // background, so it is the one that decides readability over the layer.)
    QDoubleSpinBox* mascotAltAlphaSpinBox = nullptr;
    QComboBox* mascotColumnsZCombo = nullptr;
    QComboBox* mascotBackgroundModeCombo = nullptr;
    QComboBox* mascotBackgroundFillCombo = nullptr;
    QSpinBox* mascotBackgroundStripesSpinBox = nullptr;
    QComboBox* mascotBackgroundOrientationCombo = nullptr;
    QCheckBox* mascotBackgroundAlternateCheckBox = nullptr;
    QSpinBox* mascotBackgroundScrollSpeedSpinBox = nullptr;
    QComboBox* mascotBackgroundScrollDirectionCombo = nullptr;
    QCheckBox* mascotBackgroundScrollPingpongCheckBox = nullptr;
    QSpinBox* mascotCacheMbSpinBox = nullptr;
    QSpinBox* mascotCacheItemsSpinBox = nullptr;

    // M5 Task 3: every mascot key's value when the page was built (the legacy keys included),
    // captured here so Cancel can write the same values back. Live apply writes config, so the
    // revert has to be a config write too -- a snapshot cannot drift the way one `old_*` field per
    // control could.
    QHash<QString, QString> mascotConfigSnapshot;
    // Writes every control on the Mascots page to config and applies live; shared by the page's
    // change signals and by MainWindow::applySettings(), so a key has one writer.
    void syncMascotConfig();
    // Puts the snapshot back and re-applies it; called from MainWindow's reject branch.
    void restoreMascotConfig();
    // (M5 review fix: re-reads the snapshot from config after an Apply, so Cancel stops being able
    // to undo settings Apply has already kept.)
    void rebaseMascotConfigSnapshot();

private:
    // Per-category setup methods
    void setupGeneralPage(class MainWindow* mw);
    void setupPlaybackPage(class MainWindow* mw);
    void setupAppearancePage(class MainWindow* mw);
    void setupMascotsPage(class MainWindow* mw);
    void setupAudioPage(class MainWindow* mw);
    void setupDatabasePage(class MainWindow* mw);
    void setupRandomPage(class MainWindow* mw);
    void setupStreaksPage(class MainWindow* mw);
    void setupNotificationsPage(class MainWindow* mw);
    void setupCrashDumpsPage(class MainWindow* mw);

    // Helper methods
    void setupCheckBox(QCheckBox* cb, const QString& configKey, class MainWindow* mw);
    void bindSliderToSpinBox(QSlider* slider, QDoubleSpinBox* spinbox);
    // The Mascots page's enable/disable matrix: which rows are live in the current media/mode.
    void updateMascotControlStates();
    void wireApplyEnable();
    void installWheelFilters(WheelStrongFocusEventFilter* filter);

    // The columns' three groups: `mascots` disables them as a unit, while the rows inside them
    // follow the media and the layout mode individually.
    QGroupBox* mascotContentGroup = nullptr;
    QGroupBox* mascotPlaybackGroup = nullptr;
    QGroupBox* mascotStripesGroup = nullptr;

    template<typename T>
    void setupSpinStyle(T* sb, const QString& key) { sb->setStyleSheet(get_stylesheet(key)); }
};
