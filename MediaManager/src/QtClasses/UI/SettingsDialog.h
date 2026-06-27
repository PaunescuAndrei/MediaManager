#pragma once
#include <QDialog>
#include "ui_SettingsDialog.h"
#include "WheelStrongFocusEventFilter.h"
#include "stylesQt.h"

class MainWindow;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QSlider;

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
    bool oldTooltipsEnabled = true;
    int oldTooltipDelayMs = 700;

    SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog();
    Ui::SettingsDialog ui;

    // Notification page widgets (built programmatically)
    QCheckBox* notificationVideoInfoEnabled = nullptr;
    QSpinBox* notificationVideoInfoDurationSpinBox = nullptr;
    QCheckBox* notificationGeneralMessageEnabled = nullptr;
    QSpinBox* notificationGeneralMessageDurationSpinBox = nullptr;
    QCheckBox* notificationGoalMetEnabled = nullptr;
    QSpinBox* notificationGoalMetDurationSpinBox = nullptr;
    QDoubleSpinBox* milestoneVideoStepSpinBox = nullptr;
    QSpinBox* milestoneTimeStepSpinBox = nullptr;

private:
    // Per-category setup methods
    void setupGeneralPage(class MainWindow* mw);
    void setupPlaybackPage(class MainWindow* mw);
    void setupAppearancePage(class MainWindow* mw);
    void setupAudioPage(class MainWindow* mw);
    void setupDatabasePage(class MainWindow* mw);
    void setupRandomPage(class MainWindow* mw);
    void setupNotificationsPage(class MainWindow* mw);

    // Helper methods
    void setupCheckBox(QCheckBox* cb, const QString& configKey, class MainWindow* mw);
    void bindSliderToSpinBox(QSlider* slider, QDoubleSpinBox* spinbox);
    void wireApplyEnable();
    void installWheelFilters(WheelStrongFocusEventFilter* filter);

    template<typename T>
    void setupSpinStyle(T* sb, const QString& key) { sb->setStyleSheet(get_stylesheet(key)); }
};
