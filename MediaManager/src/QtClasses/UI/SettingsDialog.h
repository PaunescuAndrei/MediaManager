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

    // The legacy .ui group box is reparented onto this page rather than rebuilt here.
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
    QCheckBox* mascotPanCheckBox = nullptr;
    QCheckBox* mascotBackgroundPanCheckBox = nullptr;
    QSpinBox* mascotScrollSpeedSpinBox = nullptr;
    QComboBox* mascotScrollDirectionCombo = nullptr;
    QCheckBox* mascotScrollPingpongCheckBox = nullptr;
    QSpinBox* mascotStripesLeftSpinBox = nullptr;
    QSpinBox* mascotStripesRightSpinBox = nullptr;
    QComboBox* mascotStripesOrientationCombo = nullptr;
    QCheckBox* mascotStripesAlternateCheckBox = nullptr;
    QCheckBox* mascotBackgroundCheckBox = nullptr;
    QDoubleSpinBox* mascotBackgroundOpacitySpinBox = nullptr;
    // The list's row fill, distinct from the window-wide veil above it.
    QDoubleSpinBox* mascotListAlphaSpinBox = nullptr;
    // Alternate rows' alpha: a colour that differs from the window background, so it is the
    // one that decides readability over the layer.
    QDoubleSpinBox* mascotAltAlphaSpinBox = nullptr;
    // Scrollbar alpha: the groove paints with the Window role, which the row palette never sets.
    QDoubleSpinBox* mascotScrollbarAlphaSpinBox = nullptr;
    // Qt's low-latency intent, which trades buffering for a higher chance of dropped frames.
    QCheckBox* mascotLowLatencyCheckBox = nullptr;
    QComboBox* mascotPairScrollCombo = nullptr;
    // Saved so the disabled state can explain itself, then restore the original tooltip.
    QString mascotPairScrollTip;
    QCheckBox* mascotPairFlipCheckBox = nullptr;
    QSpinBox* mascotBackgroundClipSpinBox = nullptr;
    QCheckBox* mascotBackgroundSwapOnBeatCheckBox = nullptr;
    QCheckBox* mascotBackgroundRandomChangeCheckBox = nullptr;
    QSpinBox* mascotBackgroundChanceSpinBox = nullptr;
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

    // Every mascot key's value at build time. Live apply writes config, so Cancel has to write
    // config back too, and one `old_*` field per control would drift.
    QHash<QString, QString> mascotConfigSnapshot;
    // The one writer for every mascot key; a non-empty `changedKey` scopes the apply to that key,
    // and empty (the Apply/OK/Cancel baseline) writes all of them.
    void syncMascotConfig(const QString& changedKey = QString());
    void restoreMascotConfig();
    // Re-read after an Apply, so Cancel cannot undo what Apply has already kept.
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
    void updateMascotControlStates();
    void wireApplyEnable();
    void installWheelFilters(WheelStrongFocusEventFilter* filter);

    // `mascots` disables these as a unit; the rows inside follow the media and the mode individually.
    QGroupBox* mascotContentGroup = nullptr;
    QGroupBox* mascotPlaybackGroup = nullptr;
    QGroupBox* mascotStripesGroup = nullptr;

    template<typename T>
    void setupSpinStyle(T* sb, const QString& key) { sb->setStyleSheet(get_stylesheet(key)); }
};
