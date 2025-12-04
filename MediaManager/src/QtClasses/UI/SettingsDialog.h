#pragma once
#include <QDialog>
#include "ui_SettingsDialog.h"
#include "WheelStrongFocusEventFilter.h"

class MainWindow;

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
    int oldSVmax = 0;
    double old_mascotsChanceSpinBox = 0;
    double old_aicon_fps_modifier = 1.0;
    int old_mascotsFreqSpinBox = 400;
    SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog();
    Ui::SettingsDialog ui;
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
};

