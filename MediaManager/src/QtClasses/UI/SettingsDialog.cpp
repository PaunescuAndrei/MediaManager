#include "stdafx.h"
#include "SettingsDialog.h"
#include "MainWindow.h"
#include "MainApp.h"
#include "config.h"
#include <QPushButton>
#include <QColorDialog>
#include "utils.h"
#include "stylesQt.h"
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include "flowlayout.h"
#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QMessageBox>
#include <QDir>
#include <QUrl>
#include <shellapi.h>

// ---------------------------------------------------------------------------
// Shared style constants
// ---------------------------------------------------------------------------

static const char* COLOR_BTN_STYLE =
    "background-color: %1; color: %2; border: 1px solid #555; border-radius: 3px;";

// ---------------------------------------------------------------------------
// Helper methods
// ---------------------------------------------------------------------------

void SettingsDialog::setupCheckBox(QCheckBox* cb, const QString& configKey, MainWindow* mw)
{
    if (mw->App->config->get_bool(configKey))
        cb->setCheckState(Qt::CheckState::Checked);
    else
        cb->setCheckState(Qt::CheckState::Unchecked);
}

void SettingsDialog::bindSliderToSpinBox(QSlider* slider, QDoubleSpinBox* spinbox)
{
    connect(slider, &QSlider::valueChanged, this, [spinbox](int value) {
        spinbox->blockSignals(true);
        spinbox->setValue(value / 100.0);
        spinbox->blockSignals(false);
    });
    connect(spinbox, &QDoubleSpinBox::valueChanged, this, [slider](double value) {
        slider->blockSignals(true);
        slider->setValue(static_cast<int>(value * 100));
        slider->blockSignals(false);
    });
}

static void applyColorBtnStyle(QPushButton* btn, const QColor& c)
{
    btn->setStyleSheet(QString(COLOR_BTN_STYLE)
        .arg(c.name())
        .arg(c.lightnessF() > 0.5 ? "#000" : "#fff"));
    btn->setText(c.name().toUpper());
}

void SettingsDialog::wireApplyEnable()
{
    QPushButton* applyBtn = this->ui.buttonBox->button(QDialogButtonBox::Apply);

    // Single recursive tree walk — dispatch by concrete type
    const QList<QWidget*> widgets = this->findChildren<QWidget*>();
    for (QWidget* w : widgets) {
        if (auto* sb = qobject_cast<QSpinBox*>(w))
            connect(sb, &QSpinBox::valueChanged, this, [applyBtn] { applyBtn->setEnabled(true); });
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w))
            connect(dsb, &QDoubleSpinBox::valueChanged, this, [applyBtn] { applyBtn->setEnabled(true); });
        else if (auto* cb = qobject_cast<QCheckBox*>(w))
            connect(cb, &QCheckBox::checkStateChanged, this, [applyBtn] { applyBtn->setEnabled(true); });
        else if (auto* rb = qobject_cast<QRadioButton*>(w))
            connect(rb, &QRadioButton::toggled, this, [applyBtn] { applyBtn->setEnabled(true); });
        else if (auto* gb = qobject_cast<QGroupBox*>(w))
            connect(gb, &QGroupBox::toggled, this, [applyBtn] { applyBtn->setEnabled(true); });
        else if (auto* sl = qobject_cast<QSlider*>(w))
            connect(sl, &QSlider::valueChanged, this, [applyBtn] { applyBtn->setEnabled(true); });
        else if (auto* le = qobject_cast<QLineEdit*>(w))
            connect(le, &QLineEdit::textChanged, this, [applyBtn] { applyBtn->setEnabled(true); });
    }
}

void SettingsDialog::installWheelFilters(WheelStrongFocusEventFilter* filter)
{
    const QList<QWidget*> widgets = this->findChildren<QWidget*>();
    for (QWidget* w : widgets) {
        if (qobject_cast<QSpinBox*>(w) || qobject_cast<QDoubleSpinBox*>(w)
            || qobject_cast<QSlider*>(w)) {
            w->setFocusPolicy(Qt::FocusPolicy::StrongFocus);
            w->installEventFilter(filter);
        }
    }
}

// ---------------------------------------------------------------------------
// Per-category page setup
// ---------------------------------------------------------------------------

void SettingsDialog::setupGeneralPage(MainWindow* mw)
{
    Config* config = mw->App->config;

    // --- SV Target Count ---
    setupSpinStyle(this->ui.SVspinBox, "spinbox");
    this->ui.SVspinBox->setValue(mw->App->db->getMainInfoValue("sv_target_count", "ALL", "0").toInt());
    this->oldSVmax = this->ui.SVspinBox->value();
    connect(this->ui.CalculateSVcountButton, &QPushButton::clicked, this, [mw, this] {
        int val = mw->calculate_sv_target();
        this->ui.SVspinBox->setValue(val);
    });

    // --- Special Video Mode ---
    QString specialMode = config->get("sv_mode").toUpper();
    if (specialMode != "PLUS" && specialMode != "MINUS")
        specialMode = "MINUS";
    int idx = this->ui.specialSvModeCombo->findText(specialMode, Qt::MatchFixedString);
    if (idx < 0) idx = 0;
    this->ui.specialSvModeCombo->setCurrentIndex(idx);

    setupCheckBox(this->ui.svTrackInPlus, "sv_track_in_plus", mw);

    // --- Time Watched Limit ---
    setupSpinStyle(this->ui.MinutesSpinBox, "spinbox");
    setupSpinStyle(this->ui.SecondsSpinBox, "spinbox");
    int timeWatchedLimit = config->get("time_watched_limit").toInt();
    this->ui.MinutesSpinBox->setValue(timeWatchedLimit / 60);
    this->ui.SecondsSpinBox->setValue(timeWatchedLimit % 60);

    // --- Search Timer ---
    setupSpinStyle(this->ui.searchTimerInterval, "spinbox");
    this->ui.searchTimerInterval->setValue(config->get("search_timer_interval").toInt());

    // --- Random Seed ---
    setupCheckBox(this->ui.seedCheckBox, "random_use_seed", mw);
    this->ui.seedLineEdit->setText(config->get("random_seed"));

    // --- Auto Continue ---
    setupCheckBox(this->ui.autoContinue, "auto_continue", mw);
    setupSpinStyle(this->ui.autoContinueDelay, "spinbox");
    this->ui.autoContinueDelay->setValue(config->get("auto_continue_delay").toInt());

    // --- Simple checkboxes ---
    setupCheckBox(this->ui.videoAutoplay, "video_autoplay", mw);
    setupCheckBox(this->ui.skipAllowedProgress, "skip_progress_enabled", mw);
    setupCheckBox(this->ui.counterUseActualWatchTime, "counter_use_actual_watch_time", mw);
    setupCheckBox(this->ui.singleInstance, "single_instance", mw);
    setupCheckBox(this->ui.numlockOnly, "numlock_only_on", mw);

    // --- Session save interval ---
    setupSpinStyle(this->ui.sessionSaveIntervalSpinBox, "spinbox");
    int sessInterval = qBound(0, config->get("session_save_interval_seconds").toInt(),
                              this->ui.sessionSaveIntervalSpinBox->maximum());
    this->ui.sessionSaveIntervalSpinBox->setValue(sessInterval);

    setupCheckBox(this->ui.emptyPlayerTracking, "empty_player_tracking", mw);

    // --- Debug Mode ---
    if (mw->App->debug_mode)
        this->ui.debugMode->setCheckState(Qt::CheckState::Checked);
    else
        this->ui.debugMode->setCheckState(Qt::CheckState::Unchecked);

    // --- Daily Goals ---
    setupSpinStyle(this->ui.dailyVideoGoalSpinBox, "spinbox");
    setupSpinStyle(this->ui.dailyTimeGoalSpinBox, "spinbox");
    double dailyVid = qBound(0.0, config->get("daily_video_goal").toDouble(),
                             this->ui.dailyVideoGoalSpinBox->maximum());
    int dailyTime = qBound(0, config->get("daily_time_goal_minutes").toInt(),
                           this->ui.dailyTimeGoalSpinBox->maximum());
    this->ui.dailyVideoGoalSpinBox->setValue(dailyVid);
    this->ui.dailyTimeGoalSpinBox->setValue(dailyTime);

    // --- Goal-Reached Counter Bonus ---
    {
        QHBoxLayout* row = new QHBoxLayout();
        goalReachedCounterEnabled = new QCheckBox("Counter bonus on daily goal reached");
        goalReachedCounterEnabled->setToolTip("When a daily goal is met, add a bonus to the main counter");
        setupCheckBox(goalReachedCounterEnabled, "goal_reached_counter_enabled", mw);
        row->addWidget(goalReachedCounterEnabled);
        row->addStretch();
        goalReachedCounterAmountSpinBox = new QSpinBox();
        goalReachedCounterAmountSpinBox->setMinimum(1);
        goalReachedCounterAmountSpinBox->setMaximum(999);
        goalReachedCounterAmountSpinBox->setToolTip("Amount to add to the counter when a daily goal is reached");
        goalReachedCounterAmountSpinBox->setValue(
            qBound(1, config->get("goal_reached_counter_amount").toInt(), 999));
        goalReachedCounterAmountSpinBox->setEnabled(goalReachedCounterEnabled->isChecked());
        setupSpinStyle(goalReachedCounterAmountSpinBox, "spinbox");
        connect(goalReachedCounterEnabled, &QCheckBox::toggled,
                goalReachedCounterAmountSpinBox, &QSpinBox::setEnabled);
        row->addWidget(goalReachedCounterAmountSpinBox);
        qobject_cast<QVBoxLayout*>(this->ui.statsGroupBox->layout())->addLayout(row);
    }

    // --- Tooltip Settings ---
    setupCheckBox(this->ui.tooltipsEnabled, "tooltips_enabled", mw);
    setupSpinStyle(this->ui.tooltipDelaySpinBox, "spinbox");
    this->ui.tooltipDelaySpinBox->setValue(config->get("tooltip_delay_ms").toInt());
    this->oldTooltipsEnabled = this->ui.tooltipsEnabled->isChecked();
    this->oldTooltipDelayMs = this->ui.tooltipDelaySpinBox->value();

    // --- Stats Heatmap ---
    int months = qBound(1, config->get("stats_heatmap_months").toInt(), 24);
    this->ui.statsHeatmapMonthsCombo->setCurrentIndex(heatmapMonthsToComboIndex(months));

    // --- Daily Progress Check ---
    setupSpinStyle(this->ui.dailyProgressIntervalSpinBox, "spinbox");
    this->ui.dailyProgressIntervalSpinBox->setValue(config->get("daily_progress_check_interval_seconds").toInt());
}

void SettingsDialog::setupStreaksPage(MainWindow* mw)
{
    Config* config = mw->App->config;

    // Build the page programmatically and place it before Notifications.
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);

    QWidget* pageContent = new QWidget();
    QVBoxLayout* pageLayout = new QVBoxLayout(pageContent);
    pageLayout->setContentsMargins(4, 4, 4, 4);
    pageLayout->setSpacing(8);

    // --- Streak Target ---
    QGroupBox* targetGroup = new QGroupBox("Streak Target");
    QVBoxLayout* targetLayout = new QVBoxLayout(targetGroup);
    targetLayout->setSpacing(4);

    QHBoxLayout* targetRow = new QHBoxLayout();
    QLabel* targetLabel = new QLabel("Daily target (min):");
    targetLabel->setToolTip(
        "Minutes of watch time needed per day to keep your streak alive (0 = count completed videos instead)");
    targetRow->addWidget(targetLabel);
    targetRow->addStretch();
    streakDailyTimeTargetSpinBox = new QSpinBox();
    streakDailyTimeTargetSpinBox->setMinimum(0);
    streakDailyTimeTargetSpinBox->setMaximum(480);
    streakDailyTimeTargetSpinBox->setSingleStep(1);
    streakDailyTimeTargetSpinBox->setToolTip(
        "Minutes of watch time needed per day to keep your streak alive. Set to 0 to count completed videos instead.");
    streakDailyTimeTargetSpinBox->setValue(config->get("streak_daily_time_target_minutes").toInt());
    setupSpinStyle(streakDailyTimeTargetSpinBox, "spinbox");
    targetRow->addWidget(streakDailyTimeTargetSpinBox);
    targetLayout->addLayout(targetRow);

    QLabel* targetHint = new QLabel(
        "The auto-save multiplier applies to whichever unit is active: minutes, or the 1-video base in completed-video mode.");
    targetHint->setWordWrap(true);
    targetHint->setStyleSheet("color: #AAAAAA;");
    targetLayout->addWidget(targetHint);

    pageLayout->addWidget(targetGroup);

    // --- Streak Saves ---
    QGroupBox* streakSavesGroup = new QGroupBox("Streak Saves");
    QVBoxLayout* savesLayout = new QVBoxLayout(streakSavesGroup);
    savesLayout->setSpacing(4);

    streakSaveEnabled = new QCheckBox("Enable streak saves");
    streakSaveEnabled->setToolTip("Master switch for auto saves and purchasable freezes");
    setupCheckBox(streakSaveEnabled, "streak_save_enabled", mw);
    savesLayout->addWidget(streakSaveEnabled);

    streakAutoSaveEnabled = new QCheckBox("Auto save a missed day (adds a debt to the next day)");
    streakAutoSaveEnabled->setToolTip(
        "A missed day is saved automatically, but the next day's target is multiplied. "
        "Time mode multiplies minutes; completed-video mode multiplies the 1-video base.");
    setupCheckBox(streakAutoSaveEnabled, "streak_auto_save_enabled", mw);
    savesLayout->addWidget(streakAutoSaveEnabled);

    auto addDoubleRow = [&](const QString& label, const QString& tooltip, double minV, double maxV,
                            double step, double value, QDoubleSpinBox*& target) {
        QHBoxLayout* row = new QHBoxLayout();
        QLabel* lbl = new QLabel(label);
        lbl->setToolTip(tooltip);
        row->addWidget(lbl);
        row->addStretch();
        target = new QDoubleSpinBox();
        target->setMinimum(minV);
        target->setMaximum(maxV);
        target->setSingleStep(step);
        target->setDecimals(1);
        target->setToolTip(tooltip);
        target->setValue(qBound(minV, value, maxV));
        setupSpinStyle(target, "spinbox");
        row->addWidget(target);
        savesLayout->addLayout(row);
    };

    auto addIntRow = [&](const QString& label, const QString& tooltip, int minV, int maxV,
                         int step, int value, QSpinBox*& target) {
        QHBoxLayout* row = new QHBoxLayout();
        QLabel* lbl = new QLabel(label);
        lbl->setToolTip(tooltip);
        row->addWidget(lbl);
        row->addStretch();
        target = new QSpinBox();
        target->setMinimum(minV);
        target->setMaximum(maxV);
        target->setSingleStep(step);
        target->setToolTip(tooltip);
        target->setValue(qBound(minV, value, maxV));
        setupSpinStyle(target, "spinbox");
        row->addWidget(target);
        savesLayout->addLayout(row);
    };

    addDoubleRow("Target multiplier:",
        "Multiplier for the first auto save. Time mode multiplies minutes; completed-video mode multiplies the 1-video base.",
        1.0, 20.0, 0.5,
        config->get_double("streak_auto_save_target_multiplier", 2.0),
        streakAutoSaveMultiplierSpinBox);
    addDoubleRow("Multiplier increment:",
        "Added to the multiplier for each consecutive auto save.",
        0.0, 20.0, 0.5,
        config->get_double("streak_auto_save_target_multiplier_increment", 1.0),
        streakAutoSaveMultiplierIncrementSpinBox);
    addIntRow("Max consecutive auto saves:",
        "0 = unlimited. After this many consecutive auto saves, the next miss breaks the streak.",
        0, 99, 1,
        config->get("streak_auto_save_max_consecutive").toInt(),
        streakAutoSaveMaxConsecutiveSpinBox);

    streakFreezeEnabled = new QCheckBox("Enable purchasable freezes");
    streakFreezeEnabled->setToolTip("Buy freezes with counter points; a freeze covers a missed day with no debt.");
    setupCheckBox(streakFreezeEnabled, "streak_freeze_enabled", mw);
    savesLayout->addWidget(streakFreezeEnabled);

    addIntRow("Freeze base price (points):", "Points for the first freeze.", 0, 9999, 1,
              config->get("streak_freeze_base_price").toInt(), streakFreezeBasePriceSpinBox);
    addIntRow("Freeze price increment:", "Added for each consecutive purchase.", 0, 9999, 1,
              config->get("streak_freeze_price_increment").toInt(), streakFreezePriceIncrementSpinBox);
    addIntRow("Freeze max stock:", "Maximum freezes you can hold (0 = disabled).", 0, 99, 1,
              config->get("streak_freeze_max_stock").toInt(), streakFreezeMaxStockSpinBox);
    addIntRow("Price reset (days):",
              "Consecutive purchase pricing resets after this many days without a purchase (0 = never).",
              0, 365, 1,
              config->get("streak_freeze_price_reset_days").toInt(),
              streakFreezePriceResetDaysSpinBox);

    streakFreezeConsumeFirstCheckBox = new QCheckBox("Use a freeze before auto debt");
    streakFreezeConsumeFirstCheckBox->setToolTip(
        "If off, auto debt is tried first and a freeze is only used when the debt path is unavailable.");
    setupCheckBox(streakFreezeConsumeFirstCheckBox, "streak_freeze_consume_first", mw);
    savesLayout->addWidget(streakFreezeConsumeFirstCheckBox);

    auto refreshStreakSaveEnableState = [this]() {
        const bool master = streakSaveEnabled->isChecked();
        streakAutoSaveEnabled->setEnabled(master);
        streakFreezeEnabled->setEnabled(master);
        const bool autoOn = master && streakAutoSaveEnabled->isChecked();
        streakAutoSaveMultiplierSpinBox->setEnabled(autoOn);
        streakAutoSaveMultiplierIncrementSpinBox->setEnabled(autoOn);
        streakAutoSaveMaxConsecutiveSpinBox->setEnabled(autoOn);
        const bool freezeOn = master && streakFreezeEnabled->isChecked();
        streakFreezeBasePriceSpinBox->setEnabled(freezeOn);
        streakFreezePriceIncrementSpinBox->setEnabled(freezeOn);
        streakFreezeMaxStockSpinBox->setEnabled(freezeOn);
        streakFreezePriceResetDaysSpinBox->setEnabled(freezeOn);
        streakFreezeConsumeFirstCheckBox->setEnabled(freezeOn);
    };
    connect(streakSaveEnabled, &QCheckBox::toggled, this,
            [refreshStreakSaveEnableState](bool) { refreshStreakSaveEnableState(); });
    connect(streakAutoSaveEnabled, &QCheckBox::toggled, this,
            [refreshStreakSaveEnableState](bool) { refreshStreakSaveEnableState(); });
    connect(streakFreezeEnabled, &QCheckBox::toggled, this,
            [refreshStreakSaveEnableState](bool) { refreshStreakSaveEnableState(); });
    refreshStreakSaveEnableState();

    pageLayout->addWidget(streakSavesGroup);
    pageLayout->addStretch();

    scrollArea->setWidget(pageContent);

    // Insert before Notifications in both the sidebar and the stack so the two stay in sync.
    int streakIndex = this->ui.categoryList->count() - 1;
    for (int i = 0; i < this->ui.categoryList->count(); ++i) {
        if (this->ui.categoryList->item(i)->text() == QStringLiteral("Notifications")) {
            streakIndex = i;
            break;
        }
    }
    this->ui.settingsStack->insertWidget(streakIndex, scrollArea);
    this->ui.categoryList->insertItem(streakIndex, "Streaks");
}

void SettingsDialog::setupPlaybackPage(MainWindow* mw)
{
    Config* config = mw->App->config;

    // --- Next Choice ---
    setupCheckBox(this->ui.nextMultiChoiceEnabled, "next_multichoice_enabled", mw);
    setupSpinStyle(this->ui.nextMultiChoiceCount, "spinbox");
    int nextChoices = config->get("next_multichoice_count").toInt();
    if (nextChoices < this->ui.nextMultiChoiceCount->minimum())
        nextChoices = this->ui.nextMultiChoiceCount->minimum();
    this->ui.nextMultiChoiceCount->setValue(nextChoices);
    this->ui.nextMultiChoiceCount->setEnabled(this->ui.nextMultiChoiceEnabled->isChecked());
    connect(this->ui.nextMultiChoiceEnabled, &QCheckBox::toggled, this, [this](bool checked) {
        this->ui.nextMultiChoiceCount->setEnabled(checked);
    });

    setupCheckBox(this->ui.nextMultiChoiceAppendOnRefresh, "next_multichoice_append_on_refresh", mw);

    // --- Rarity Tiers ---
    setupCheckBox(this->ui.rarityEnabled, "rarity_enabled", mw);
    setupSpinStyle(this->ui.raritySsrPct, "spinbox");
    setupSpinStyle(this->ui.raritySrPct, "spinbox");
    setupSpinStyle(this->ui.rarityRPct, "spinbox");
    this->ui.raritySsrPct->setValue(config->get("rarity_ssr_pct").toInt());
    this->ui.raritySrPct->setValue(config->get("rarity_sr_pct").toInt());
    this->ui.rarityRPct->setValue(config->get("rarity_r_pct").toInt());

    // Keep SSR ≤ SR ≤ R at all times. Equality is allowed (merges adjacent
    // tiers — the lower one becomes unreachable, which is how you "disable"
    // a single tier without touching all three). When the user moves a
    // spinbox, its neighbors adjust in the same direction so the value the
    // user set is always preserved.

    // User changed SSR — push higher tiers up if they're now below SSR.
    connect(this->ui.raritySsrPct, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this] {
        const QSignalBlocker bSr(this->ui.raritySrPct);
        const QSignalBlocker bR(this->ui.rarityRPct);

        int ssr = this->ui.raritySsrPct->value();
        int sr  = this->ui.raritySrPct->value();
        int r   = this->ui.rarityRPct->value();

        if (sr < ssr) { sr = ssr; this->ui.raritySrPct->setValue(sr); }
        if (r  < sr)  { r  = sr;  this->ui.rarityRPct->setValue(r);  }
    });

    // User changed SR — pull SSR down or push R up to stay ordered.
    connect(this->ui.raritySrPct, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this] {
        const QSignalBlocker bSsr(this->ui.raritySsrPct);
        const QSignalBlocker bR(this->ui.rarityRPct);

        int ssr = this->ui.raritySsrPct->value();
        int sr  = this->ui.raritySrPct->value();
        int r   = this->ui.rarityRPct->value();

        if (sr < ssr) { ssr = sr; this->ui.raritySsrPct->setValue(ssr); }
        if (r  < sr)  { r   = sr; this->ui.rarityRPct->setValue(r);   }
    });

    // User changed R — pull lower tiers down if R fell below them.
    connect(this->ui.rarityRPct, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this] {
        const QSignalBlocker bSsr(this->ui.raritySsrPct);
        const QSignalBlocker bSr(this->ui.raritySrPct);

        int ssr = this->ui.raritySsrPct->value();
        int sr  = this->ui.raritySrPct->value();
        int r   = this->ui.rarityRPct->value();

        if (r  < sr)  { sr  = r; this->ui.raritySrPct->setValue(sr);  }
        if (sr < ssr) { ssr = sr; this->ui.raritySsrPct->setValue(ssr); }
    });

    auto setupColorButton = [mw](QPushButton* btn, const QString& configKey) {
        applyColorBtnStyle(btn, QColor(mw->App->config->get(configKey)));
    };
    setupColorButton(this->ui.raritySsrColorBtn, "rarity_ssr_color");
    setupColorButton(this->ui.raritySrColorBtn, "rarity_sr_color");
    setupColorButton(this->ui.rarityRColorBtn, "rarity_r_color");

    auto setRarityWidgetsEnabled = [this](bool enabled) {
        this->ui.raritySsrPct->setEnabled(enabled);
        this->ui.raritySrPct->setEnabled(enabled);
        this->ui.rarityRPct->setEnabled(enabled);
        this->ui.raritySsrColorBtn->setEnabled(enabled);
        this->ui.raritySrColorBtn->setEnabled(enabled);
        this->ui.rarityRColorBtn->setEnabled(enabled);
    };
    setRarityWidgetsEnabled(this->ui.rarityEnabled->isChecked());
    connect(this->ui.rarityEnabled, &QCheckBox::toggled, this, setRarityWidgetsEnabled);

    auto connectColorButton = [this](QPushButton* btn) {
        connect(btn, &QPushButton::clicked, this, [this, btn] {
            QColor current(btn->text());
            QColor chosen = QColorDialog::getColor(current, this, "Choose Color");
            if (chosen.isValid())
                applyColorBtnStyle(btn, chosen);
        });
    };
    connectColorButton(this->ui.raritySsrColorBtn);
    connectColorButton(this->ui.raritySrColorBtn);
    connectColorButton(this->ui.rarityRColorBtn);

    // --- Video Preview: General ---
    setupSpinStyle(this->ui.previewVolumeSpinBox, "spinbox");
    int previewVol = qBound(0, config->get("preview_volume").toInt(), 100);
    this->ui.previewVolumeSpinBox->setValue(previewVol);
    this->oldPreviewVolume = previewVol;

    setupSpinStyle(this->ui.previewSeekSeconds, "doublespinbox");
    this->ui.previewSeekSeconds->setValue(config->get("preview_seek_seconds").toDouble());

    setupSpinStyle(this->ui.previewOverlayScale, "doublespinbox");
    setupSpinStyle(this->ui.previewOverlayPadX, "spinbox");
    setupSpinStyle(this->ui.previewOverlayPadY, "spinbox");
    setupSpinStyle(this->ui.previewOverlayMargin, "spinbox");
    double overlayScale = qBound(0.1, config->get("preview_overlay_scale").toDouble(), 5.0);
    int padX = qBound(0, config->get("preview_overlay_pad_x").toInt(), this->ui.previewOverlayPadX->maximum());
    int padY = qBound(0, config->get("preview_overlay_pad_y").toInt(), this->ui.previewOverlayPadY->maximum());
    int margin = qBound(0, config->get("preview_overlay_margin").toInt(), this->ui.previewOverlayMargin->maximum());
    this->ui.previewOverlayScale->setValue(overlayScale);
    this->ui.previewOverlayPadX->setValue(padX);
    this->ui.previewOverlayPadY->setValue(padY);
    this->ui.previewOverlayMargin->setValue(margin);
    this->oldPreviewOverlayScale = overlayScale;
    this->oldPreviewOverlayPadX = padX;
    this->oldPreviewOverlayPadY = padY;
    this->oldPreviewOverlayMargin = margin;

    setupCheckBox(this->ui.previewRandomStart, "preview_random_start", mw);
    this->oldPreviewRandomStart = this->ui.previewRandomStart->isChecked();
    setupCheckBox(this->ui.previewSeededRandom, "preview_seeded_random", mw);
    this->oldPreviewSeededRandom = this->ui.previewSeededRandom->isChecked();
    setupCheckBox(this->ui.previewRememberPosition, "preview_remember_position", mw);
    this->oldPreviewRememberPosition = this->ui.previewRememberPosition->isChecked();

    // --- Video Preview: Main list ---
    bool previewMain = config->get_bool("preview_main_enabled");
    this->ui.previewMainEnabled->setCheckState(previewMain ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);
    this->oldPreviewMainEnabled = previewMain;

    setupSpinStyle(this->ui.previewPopupWidth, "spinbox");
    setupSpinStyle(this->ui.previewPopupHeight, "spinbox");
    int popupW = qBound(this->ui.previewPopupWidth->minimum(), config->get("preview_main_width").toInt(),
                        this->ui.previewPopupWidth->maximum());
    int popupH = qBound(this->ui.previewPopupHeight->minimum(), config->get("preview_main_height").toInt(),
                        this->ui.previewPopupHeight->maximum());
    this->ui.previewPopupWidth->setValue(popupW);
    this->ui.previewPopupHeight->setValue(popupH);
    this->oldPreviewPopupWidth = popupW;
    this->oldPreviewPopupHeight = popupH;

    setupSpinStyle(this->ui.previewMainHistoryCount, "spinbox");
    int histCount = qBound(this->ui.previewMainHistoryCount->minimum(),
                           config->get("preview_main_history_count").toInt(),
                           this->ui.previewMainHistoryCount->maximum());
    this->ui.previewMainHistoryCount->setValue(histCount);
    this->oldPreviewMainHistoryCount = histCount;

    // --- Video Preview: Next choices dialog ---
    bool previewsEnabled = config->get_bool("preview_next_choices_enabled");
    this->ui.previewEnabled->setCheckState(previewsEnabled ? Qt::CheckState::Checked : Qt::CheckState::Unchecked);
    this->oldPreviewNextChoicesEnabled = previewsEnabled;

    setupCheckBox(this->ui.previewAutoplayAllMute, "preview_autoplay_all_mute", mw);
    this->oldPreviewAutoplayAllMute = this->ui.previewAutoplayAllMute->isChecked();
    setupCheckBox(this->ui.previewRandomEachHover, "preview_random_each_hover", mw);
    this->oldPreviewRandomEachHover = this->ui.previewRandomEachHover->isChecked();

    // --- Next Volume Guard ---
    QGroupBox* volumeGuardGroup = new QGroupBox(QStringLiteral("Next Volume Guard"));
    QVBoxLayout* volumeGuardLayout = new QVBoxLayout(volumeGuardGroup);
    volumeGuardLayout->setSpacing(4);

    this->nextVolumeGuardEnabled = new QCheckBox(QStringLiteral("Warn before switching when the player volume is high"));
    this->nextVolumeGuardEnabled->setToolTip(
        QStringLiteral("When Next, Skip, or Random changes the video, check the player's volume first."));
    setupCheckBox(this->nextVolumeGuardEnabled, "next_volume_guard_enabled", mw);
    volumeGuardLayout->addWidget(this->nextVolumeGuardEnabled);

    QHBoxLayout* thresholdRow = new QHBoxLayout();
    QLabel* thresholdLabel = new QLabel(QStringLiteral("Trigger at or above:"));
    thresholdLabel->setToolTip(QStringLiteral("Show the guard when the player volume is at or above this value."));
    thresholdRow->addWidget(thresholdLabel);
    thresholdRow->addStretch();
    this->nextVolumeGuardThreshold = new QSpinBox();
    this->nextVolumeGuardThreshold->setMinimum(1);
    this->nextVolumeGuardThreshold->setMaximum(100);
    this->nextVolumeGuardThreshold->setSuffix(QStringLiteral("%"));
    this->nextVolumeGuardThreshold->setToolTip(QStringLiteral("Show the guard when the player volume is at or above this value."));
    setupSpinStyle(this->nextVolumeGuardThreshold, "spinbox");
    bool thresholdOk = false;
    int threshold = config->get("next_volume_guard_threshold").toInt(&thresholdOk);
    if (!thresholdOk)
        threshold = 50;
    this->nextVolumeGuardThreshold->setValue(qBound(1, threshold, 100));
    thresholdRow->addWidget(this->nextVolumeGuardThreshold);
    volumeGuardLayout->addLayout(thresholdRow);

    QHBoxLayout* targetRow = new QHBoxLayout();
    QLabel* targetLabel = new QLabel(QStringLiteral("Lower volume to:"));
    targetLabel->setToolTip(QStringLiteral("Volume applied when you choose to lower it. Keep this below the trigger value."));
    targetRow->addWidget(targetLabel);
    targetRow->addStretch();
    this->nextVolumeGuardTarget = new QSpinBox();
    this->nextVolumeGuardTarget->setMinimum(0);
    this->nextVolumeGuardTarget->setMaximum(100);
    this->nextVolumeGuardTarget->setSuffix(QStringLiteral("%"));
    this->nextVolumeGuardTarget->setToolTip(QStringLiteral("Volume applied when you choose to lower it. Keep this below the trigger value."));
    setupSpinStyle(this->nextVolumeGuardTarget, "spinbox");
    bool targetOk = false;
    int target = config->get("next_volume_guard_target").toInt(&targetOk);
    if (!targetOk)
        target = 20;
    this->nextVolumeGuardTarget->setValue(qBound(0, target, 100));
    targetRow->addWidget(this->nextVolumeGuardTarget);
    volumeGuardLayout->addLayout(targetRow);

    QLabel* volumeGuardHint = new QLabel(QStringLiteral(
        "On the next video switch you can ignore the warning, lower the volume first, or cancel. "
        "A lowered volume is confirmed with the player before the video changes."));
    volumeGuardHint->setWordWrap(true);
    volumeGuardHint->setStyleSheet("color: #AAAAAA;");
    volumeGuardLayout->addWidget(volumeGuardHint);

    this->nextVolumeGuardThreshold->setEnabled(this->nextVolumeGuardEnabled->isChecked());
    this->nextVolumeGuardTarget->setEnabled(this->nextVolumeGuardEnabled->isChecked());
    connect(this->nextVolumeGuardEnabled, &QCheckBox::toggled, this, [this](bool checked) {
        this->nextVolumeGuardThreshold->setEnabled(checked);
        this->nextVolumeGuardTarget->setEnabled(checked);
    });

    int previewIndex = this->ui.playbackPageLayout->indexOf(this->ui.previewGroupBox);
    if (previewIndex < 0)
        previewIndex = this->ui.playbackPageLayout->count() - 1;
    this->ui.playbackPageLayout->insertWidget(previewIndex, volumeGuardGroup);
}

void SettingsDialog::setupAppearancePage(MainWindow* mw)
{
    Config* config = mw->App->config;

    // --- Icon Settings ---
    setupCheckBox(this->ui.AnimatedIcons, "animated_icon_flag", mw);
    setupCheckBox(this->ui.randomIcon, "random_icon", mw);
    setupCheckBox(this->ui.showDefaultIconWhenIdle, "show_default_icon_when_idle", mw);

    setupSpinStyle(this->ui.aicon_fps_modifier_spinBox, "doublespinbox");
    this->ui.aicon_fps_modifier_spinBox->setValue(config->get("animated_icon_fps_modifier").toDouble());
    this->old_aicon_fps_modifier = this->ui.aicon_fps_modifier_spinBox->value();
    connect(this->ui.aicon_fps_modifier_spinBox, &QDoubleSpinBox::valueChanged, this, [mw, this] {
        mw->animatedIcon->fps_modifier = this->ui.aicon_fps_modifier_spinBox->value();
    });

    // --- Mascots Settings ---
    setupCheckBox(this->ui.mascotsOnOff, "mascots", mw);
    setupCheckBox(this->ui.mascotsAllFilesRandom, "mascots_allfiles_random", mw);
    setupCheckBox(this->ui.mascotsMirror, "mascots_mirror", mw);
    setupCheckBox(this->ui.mascotsAnimated, "mascots_animated", mw);
    setupCheckBox(this->ui.mascotsExtractColor, "mascots_color_theme", mw);
    setupCheckBox(this->ui.mascotsCenterContent, "mascots_center_content", mw);
    setupCheckBox(this->ui.mascotsUseSeed, "mascots_use_seed", mw);

    setupSpinStyle(this->ui.mascotsFreqSpinBox, "spinbox");
    this->ui.mascotsFreqSpinBox->setValue(config->get("mascots_frequency").toInt());
    this->old_mascotsFreqSpinBox = this->ui.mascotsFreqSpinBox->value();
    connect(this->ui.mascotsFreqSpinBox, &QSpinBox::valueChanged, this, [mw, this] {
        mw->App->MascotsAnimation->frequency = this->ui.mascotsFreqSpinBox->value();
    });

    setupSpinStyle(this->ui.mascotsWidthSpinBox, "spinbox");
    this->ui.mascotsWidthSpinBox->setValue(static_cast<int>(config->get_double("mascots_width_percent", 15.0)));
    this->old_mascotsWidthSpinBox = this->ui.mascotsWidthSpinBox->value();
    connect(this->ui.mascotsWidthSpinBox, &QSpinBox::valueChanged, this, [mw, this] {
        mw->applyMascotWidth(mw->width(), this->ui.mascotsWidthSpinBox->value());
    });

    setupCheckBox(this->ui.mascotsRandomChange, "mascots_random_change", mw);
    setupSpinStyle(this->ui.mascotsChanceSpinBox, "spinbox");
    this->ui.mascotsChanceSpinBox->setValue(config->get("mascots_random_chance").toInt());
    this->old_mascotsChanceSpinBox = this->ui.mascotsChanceSpinBox->value() / 100.0;
    connect(this->ui.mascotsChanceSpinBox, &QSpinBox::valueChanged, this, [mw, this] {
        mw->App->MascotsAnimation->set_random_chance(this->ui.mascotsChanceSpinBox->value() / 100.0);
    });
}

void SettingsDialog::setupAudioPage(MainWindow* mw)
{
    Config* config = mw->App->config;

    // --- Music Settings ---
    setupCheckBox(this->ui.musicOnOff, "music_on", mw);
    setupSpinStyle(this->ui.volumeSpinBox, "spinbox");
    this->ui.volumeSpinBox->setValue(config->get("music_volume").toInt());
    this->oldVolume = this->ui.volumeSpinBox->value();
    connect(this->ui.volumeSpinBox, &QSpinBox::valueChanged, this, [mw, this] {
        if (mw->App->musicPlayer)
            mw->App->musicPlayer->setVolume(this->ui.volumeSpinBox->value());
    });

    setupCheckBox(this->ui.musicRandomStart, "music_random_start", mw);
    setupCheckBox(this->ui.musicLoopFirst, "music_loop_first", mw);

    // --- Sound Effects Settings ---
    setupCheckBox(this->ui.soundEffectsOnOff, "sound_effects_on", mw);
    setupSpinStyle(this->ui.soundEffectsVolume, "spinbox");
    this->ui.soundEffectsVolume->setValue(config->get("sound_effects_volume").toInt());

    setupCheckBox(this->ui.specialSoundEffectsOnOff, "sound_effects_special_on", mw);
    setupSpinStyle(this->ui.specialSoundEffectsVolume, "spinbox");
    this->ui.specialSoundEffectsVolume->setValue(config->get("sound_effects_special_volume").toInt());

    setupCheckBox(this->ui.soundEffectsStart, "sound_effects_start", mw);
    setupCheckBox(this->ui.soundEffectsExit, "sound_effects_exit", mw);
    setupCheckBox(this->ui.soundEffectsClicks, "sound_effects_clicks", mw);

    setupCheckBox(this->ui.soundEffectsPlay, "sound_effects_playpause", mw);
    setupSpinStyle(this->ui.soundEffectsChance, "spinbox");
    this->ui.soundEffectsChance->setValue(config->get("sound_effects_chance_playpause").toInt());

    setupCheckBox(this->ui.soundEffectsChainPlay, "sound_effects_chain_playpause", mw);
    setupSpinStyle(this->ui.soundEffectsChainChance, "spinbox");
    this->ui.soundEffectsChainChance->setValue(config->get("sound_effects_chain_chance").toInt());
}

void SettingsDialog::setupDatabasePage(MainWindow* mw)
{
    Config* config = mw->App->config;

    // --- Starting DB Radio Buttons ---
    this->ui.plusCatRadioBtn->setText(config->get("plus_category_name"));
    this->ui.minusCatRadioBtn->setText(config->get("minus_category_name"));
    if (config->get("current_db") == "MINUS")
        this->ui.minusCatRadioBtn->setChecked(true);
    else if (config->get("current_db") == "PLUS")
        this->ui.plusCatRadioBtn->setChecked(true);

    // --- DB Action Buttons ---
    connect(this->ui.resetBtn, &QPushButton::clicked, this, [mw, this] {
        mw->resetDBDialogButton(this);
        QSignalBlocker blocker(this->ui.SVspinBox);
        this->ui.SVspinBox->setValue(mw->sv_target_count);
        this->oldSVmax = this->ui.SVspinBox->value();
    });
    connect(this->ui.loadBtn, &QPushButton::clicked, this, [mw, this] {
        mw->loadDB(this);
        QSignalBlocker blocker(this->ui.SVspinBox);
        this->ui.SVspinBox->setValue(mw->sv_target_count);
        this->oldSVmax = this->ui.SVspinBox->value();
    });
    connect(this->ui.backupBtn, &QPushButton::clicked, this, [mw, this] { mw->backupDB(this); });
    connect(this->ui.resetWatchedButton, &QPushButton::clicked, this, [mw, this] {
        mw->resetWatchedDB(this);
        QSignalBlocker blocker(this->ui.SVspinBox);
        this->ui.SVspinBox->setValue(mw->sv_target_count);
        this->oldSVmax = this->ui.SVspinBox->value();
    });
    connect(this->ui.cleanMissingBtn, &QPushButton::clicked, this,
            [mw, this] { mw->cleanMissingFilesDialog(this); });
    connect(this->ui.vacuumBtn, &QPushButton::clicked, this, [mw, this] {
        qint64 freed = mw->App->db->vacuumDB();
        QString msg;
        if (freed == -1) {
            msg = tr("Vacuum failed. Check the log for details.");
        } else if (freed == -2) {
            msg = tr("Vacuum complete.\nCould not estimate freed space (measurement query failed). Check the log for details.");
        } else if (freed == 0) {
            msg = tr("Vacuum complete.\nNo unused space was found.");
        } else {
            // Format freed bytes as KB or MB
            QString sizeStr;
            if (freed >= 1024 * 1024)
                sizeStr = QString::number(freed / (1024.0 * 1024.0), 'f', 2) + " MB";
            else
                sizeStr = QString::number(freed / 1024.0, 'f', 1) + " KB";
            msg = tr("Vacuum complete.\nSpace reclaimed: %1").arg(sizeStr);
        }
        QMessageBox::information(this, tr("Vacuum DB"), msg);
    });

    // --- Cache Buttons ---
    connect(this->ui.cacheIconButton, &QPushButton::clicked, this,
            [mw] { mw->animatedIcon->rebuildIconCacheNonBlocking(); });
    connect(this->ui.cacheBeatsButton, &QPushButton::clicked, this,
            [mw] { mw->App->MascotsAnimation->rebuildBeatsCacheNonBlocking(); });
    connect(this->ui.cacheThumbsButton, &QPushButton::clicked, this,
            [mw] { mw->thumbnailManager->rebuildThumbnailCache(mw->App->db->db, true); });
    connect(this->ui.cacheThumbsButton, &customQPushButton::rightClicked, this,
            [mw] { mw->thumbnailManager->rebuildThumbnailCache(mw->App->db->db, false); });

    // --- BPM Settings ---
    setupSpinStyle(this->ui.bpmThreadsSpinBox, "spinbox");
    this->ui.bpmThreadsSpinBox->setValue(config->get("bpm_threads").toInt());

    // BPM Types FlowLayout (populated programmatically)
    FlowLayout* bpmFlow = new FlowLayout;
    bpmFlow->setContentsMargins(0, 0, 0, 0);
    QStringList configTypes = config->get("bpm_calculation_types").split(',', Qt::SkipEmptyParts);
    QStringList allVideoTypes = config->get("video_types").split(',', Qt::SkipEmptyParts);
    for (const QString& type : allVideoTypes) {
        if (type.isEmpty()) continue;
        QCheckBox* checkbox = new QCheckBox(type, this);
        checkbox->setProperty("type_name", type);
        if (configTypes.contains(type))
            checkbox->setChecked(true);
        connect(checkbox, &QCheckBox::checkStateChanged, this, [this] {
            this->ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
        });
        bpmFlow->addWidget(checkbox);
    }
    this->ui.bpmTypesWidget->setLayout(bpmFlow);
}

void SettingsDialog::setupRandomPage(MainWindow* mw)
{
    Config* config = mw->App->config;

    // --- Dynamic titles ---
    this->ui.weightedRandMinusGroupBox->setTitle(
        this->ui.weightedRandMinusGroupBox->title().replace("MINUS",
            config->get("minus_category_name"), Qt::CaseSensitive));
    this->ui.weightedRandPlusGroupBox->setTitle(
        this->ui.weightedRandPlusGroupBox->title().replace("PLUS",
            config->get("plus_category_name"), Qt::CaseSensitive));

    // --- Weighted Random MINUS ---
    this->ui.weightedRandMinusGroupBox->setChecked(config->get_bool("weighted_random_minus"));

    bindSliderToSpinBox(this->ui.generalBiasMinusSlider, this->ui.generalBiasMinusSpinBox);
    this->ui.generalBiasMinusSpinBox->setValue(config->get("random_general_bias_minus").toDouble());

    bindSliderToSpinBox(this->ui.viewsBiasMinusSlider, this->ui.viewsBiasMinusSpinBox);
    this->ui.viewsBiasMinusSpinBox->setValue(config->get("random_views_bias_minus").toDouble());

    bindSliderToSpinBox(this->ui.ratingBiasMinusSlider, this->ui.ratingBiasMinusSpinBox);
    this->ui.ratingBiasMinusSpinBox->setValue(config->get("random_rating_bias_minus").toDouble());

    bindSliderToSpinBox(this->ui.tagsBiasMinusSlider, this->ui.tagsBiasMinusSpinBox);
    this->ui.tagsBiasMinusSpinBox->setValue(config->get("random_tags_bias_minus").toDouble());

    bindSliderToSpinBox(this->ui.bpmBiasMinusSlider, this->ui.bpmBiasMinusSpinBox);
    this->ui.bpmBiasMinusSpinBox->setValue(config->get("random_bpm_bias_minus").toDouble());

    bindSliderToSpinBox(this->ui.noViewsMinusSlider, this->ui.noViewsMinusSpinBox);
    this->ui.noViewsMinusSpinBox->setValue(config->get("random_no_views_weight_minus").toDouble());

    bindSliderToSpinBox(this->ui.noRatingMinusSlider, this->ui.noRatingMinusSpinBox);
    this->ui.noRatingMinusSpinBox->setValue(config->get("random_no_ratings_weight_minus").toDouble());

    bindSliderToSpinBox(this->ui.noTagsMinusSlider, this->ui.noTagsMinusSpinBox);
    this->ui.noTagsMinusSpinBox->setValue(config->get("random_no_tags_weight_minus").toDouble());

    // --- Weighted Random PLUS ---
    this->ui.weightedRandPlusGroupBox->setChecked(config->get_bool("weighted_random_plus"));

    bindSliderToSpinBox(this->ui.generalBiasPlusSlider, this->ui.generalBiasPlusSpinBox);
    this->ui.generalBiasPlusSpinBox->setValue(config->get("random_general_bias_plus").toDouble());

    bindSliderToSpinBox(this->ui.viewsBiasPlusSlider, this->ui.viewsBiasPlusSpinBox);
    this->ui.viewsBiasPlusSpinBox->setValue(config->get("random_views_bias_plus").toDouble());

    bindSliderToSpinBox(this->ui.ratingBiasPlusSlider, this->ui.ratingBiasPlusSpinBox);
    this->ui.ratingBiasPlusSpinBox->setValue(config->get("random_rating_bias_plus").toDouble());

    bindSliderToSpinBox(this->ui.tagsBiasPlusSlider, this->ui.tagsBiasPlusSpinBox);
    this->ui.tagsBiasPlusSpinBox->setValue(config->get("random_tags_bias_plus").toDouble());

    bindSliderToSpinBox(this->ui.bpmBiasPlusSlider, this->ui.bpmBiasPlusSpinBox);
    this->ui.bpmBiasPlusSpinBox->setValue(config->get("random_bpm_bias_plus").toDouble());

    bindSliderToSpinBox(this->ui.noViewsPlusSlider, this->ui.noViewsPlusSpinBox);
    this->ui.noViewsPlusSpinBox->setValue(config->get("random_no_views_weight_plus").toDouble());

    bindSliderToSpinBox(this->ui.noRatingPlusSlider, this->ui.noRatingPlusSpinBox);
    this->ui.noRatingPlusSpinBox->setValue(config->get("random_no_ratings_weight_plus").toDouble());

    bindSliderToSpinBox(this->ui.noTagsPlusSlider, this->ui.noTagsPlusSpinBox);
    this->ui.noTagsPlusSpinBox->setValue(config->get("random_no_tags_weight_plus").toDouble());
}

void SettingsDialog::setupNotificationsPage(MainWindow* mw)
{
    Config* config = mw->App->config;

    // Build the page programmatically
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);

    QWidget* pageContent = new QWidget();
    QVBoxLayout* pageLayout = new QVBoxLayout(pageContent);
    pageLayout->setContentsMargins(4, 4, 4, 4);
    pageLayout->setSpacing(8);

    // --- Presets Group ---
    QGroupBox* presetsGroup = new QGroupBox("Notification Presets");
    QVBoxLayout* presetsLayout = new QVBoxLayout(presetsGroup);
    presetsLayout->setSpacing(4);

    // Video Info row
    QHBoxLayout* videoInfoRow = new QHBoxLayout();
    notificationVideoInfoEnabled = new QCheckBox("Video Info");
    notificationVideoInfoEnabled->setToolTip("Show notification popup when video changes, watching starts, etc.");
    setupCheckBox(notificationVideoInfoEnabled, "notification_video_info_enabled", mw);
    videoInfoRow->addWidget(notificationVideoInfoEnabled);
    videoInfoRow->addStretch();
    notificationVideoInfoDurationSpinBox = new QSpinBox();
    notificationVideoInfoDurationSpinBox->setMinimum(1000);
    notificationVideoInfoDurationSpinBox->setMaximum(60000);
    notificationVideoInfoDurationSpinBox->setSingleStep(500);
    notificationVideoInfoDurationSpinBox->setToolTip("Duration in milliseconds");
    notificationVideoInfoDurationSpinBox->setValue(config->get("notification_video_info_duration_ms").toInt());
    setupSpinStyle(notificationVideoInfoDurationSpinBox, "spinbox");
    videoInfoRow->addWidget(notificationVideoInfoDurationSpinBox);
    presetsLayout->addLayout(videoInfoRow);

    // Goal Reached row
    QHBoxLayout* goalRow = new QHBoxLayout();
    notificationGoalMetEnabled = new QCheckBox("Goal Reached");
    notificationGoalMetEnabled->setToolTip("Show notification popup when daily watch goals are reached");
    setupCheckBox(notificationGoalMetEnabled, "notification_goal_met_enabled", mw);
    goalRow->addWidget(notificationGoalMetEnabled);
    goalRow->addStretch();
    notificationGoalMetDurationSpinBox = new QSpinBox();
    notificationGoalMetDurationSpinBox->setMinimum(1000);
    notificationGoalMetDurationSpinBox->setMaximum(60000);
    notificationGoalMetDurationSpinBox->setSingleStep(500);
    notificationGoalMetDurationSpinBox->setToolTip("Duration in milliseconds");
    notificationGoalMetDurationSpinBox->setValue(config->get("notification_goal_met_duration_ms").toInt());
    setupSpinStyle(notificationGoalMetDurationSpinBox, "spinbox");
    goalRow->addWidget(notificationGoalMetDurationSpinBox);
    presetsLayout->addLayout(goalRow);

    pageLayout->addWidget(presetsGroup);

    // --- Daily Milestones Group ---
    QGroupBox* milestonesGroup = new QGroupBox("Daily Milestones");
    milestonesGroup->setToolTip("Show progress notifications at regular intervals throughout the day, independent of the final goal");
    QVBoxLayout* milestonesLayout = new QVBoxLayout(milestonesGroup);
    milestonesLayout->setSpacing(4);

    // Milestone Notifications enable + duration
    QHBoxLayout* milestoneEnableRow = new QHBoxLayout();
    notificationMilestoneEnabled = new QCheckBox("Milestone Notifications");
    notificationMilestoneEnabled->setToolTip("Show milestone notification popups at regular progress intervals throughout the day");
    setupCheckBox(notificationMilestoneEnabled, "notification_milestone_enabled", mw);
    milestoneEnableRow->addWidget(notificationMilestoneEnabled);
    milestoneEnableRow->addStretch();
    notificationMilestoneDurationSpinBox = new QSpinBox();
    notificationMilestoneDurationSpinBox->setMinimum(1000);
    notificationMilestoneDurationSpinBox->setMaximum(60000);
    notificationMilestoneDurationSpinBox->setSingleStep(500);
    notificationMilestoneDurationSpinBox->setToolTip("Duration in milliseconds");
    notificationMilestoneDurationSpinBox->setValue(config->get("notification_milestone_duration_ms").toInt());
    setupSpinStyle(notificationMilestoneDurationSpinBox, "spinbox");
    milestoneEnableRow->addWidget(notificationMilestoneDurationSpinBox);
    milestonesLayout->addLayout(milestoneEnableRow);

    // Video milestone step
    QHBoxLayout* videoStepRow = new QHBoxLayout();
    QLabel* videoStepLabel = new QLabel("Video Milestone Step:");
    videoStepLabel->setToolTip("Show a milestone notification every N videos watched today (0 = disabled)");
    videoStepRow->addWidget(videoStepLabel);
    videoStepRow->addStretch();
    milestoneVideoStepSpinBox = new QDoubleSpinBox();
    milestoneVideoStepSpinBox->setDecimals(2);
    milestoneVideoStepSpinBox->setMinimum(0.0);
    milestoneVideoStepSpinBox->setMaximum(999.0);
    milestoneVideoStepSpinBox->setSingleStep(0.5);
    milestoneVideoStepSpinBox->setToolTip("0 = disabled. Example: 2.5 means notify at 3, 5, 8, 10... videos today.");
    milestoneVideoStepSpinBox->setValue(config->get("daily_milestone_video_step").toDouble());
    setupSpinStyle(milestoneVideoStepSpinBox, "spinbox");
    videoStepRow->addWidget(milestoneVideoStepSpinBox);
    milestonesLayout->addLayout(videoStepRow);

    // Time milestone step
    QHBoxLayout* timeStepRow = new QHBoxLayout();
    QLabel* timeStepLabel = new QLabel("Time Milestone Step (min):");
    timeStepLabel->setToolTip("Show a milestone notification every N minutes of watch time today (0 = disabled)");
    timeStepRow->addWidget(timeStepLabel);
    timeStepRow->addStretch();
    milestoneTimeStepSpinBox = new QSpinBox();
    milestoneTimeStepSpinBox->setMinimum(0);
    milestoneTimeStepSpinBox->setMaximum(9999);
    milestoneTimeStepSpinBox->setToolTip("0 = disabled. Example: 30 means notify at 30, 60, 90... minutes today.");
    milestoneTimeStepSpinBox->setValue(config->get("daily_milestone_time_step_minutes").toInt());
    setupSpinStyle(milestoneTimeStepSpinBox, "spinbox");
    timeStepRow->addWidget(milestoneTimeStepSpinBox);
    milestonesLayout->addLayout(timeStepRow);

    pageLayout->addWidget(milestonesGroup);

    // ── Streak Notifications ────────────────────────────────────────
    QGroupBox* streakRiskGroup = new QGroupBox("Streak Notifications");
    QVBoxLayout* streakRiskLayout = new QVBoxLayout(streakRiskGroup);
    streakRiskLayout->setSpacing(4);

    // Enable + duration row
    QHBoxLayout* streakEnableRow = new QHBoxLayout();
    notificationStreakAtRiskEnabled = new QCheckBox("Enable streak at risk notifications");
    notificationStreakAtRiskEnabled->setToolTip("Show notification popup when your watch streak is at risk of ending");
    notificationStreakAtRiskEnabled->setChecked(config->get_bool("notification_streak_at_risk_enabled"));
    streakEnableRow->addWidget(notificationStreakAtRiskEnabled);
    streakEnableRow->addStretch();
    notificationStreakAtRiskDurationSpinBox = new QSpinBox();
    notificationStreakAtRiskDurationSpinBox->setMinimum(1000);
    notificationStreakAtRiskDurationSpinBox->setMaximum(60000);
    notificationStreakAtRiskDurationSpinBox->setSingleStep(500);
    notificationStreakAtRiskDurationSpinBox->setToolTip("Duration in milliseconds");
    notificationStreakAtRiskDurationSpinBox->setValue(config->get("notification_streak_at_risk_duration_ms").toInt());
    setupSpinStyle(notificationStreakAtRiskDurationSpinBox, "spinbox");
    connect(notificationStreakAtRiskEnabled, &QCheckBox::toggled, notificationStreakAtRiskDurationSpinBox, &QSpinBox::setEnabled);
    notificationStreakAtRiskDurationSpinBox->setEnabled(notificationStreakAtRiskEnabled->isChecked());
    streakEnableRow->addWidget(notificationStreakAtRiskDurationSpinBox);
    streakRiskLayout->addLayout(streakEnableRow);

    // Min streak days
    QHBoxLayout* streakMinDaysRow = new QHBoxLayout();
    QLabel* streakMinDaysLabel = new QLabel("Min streak days:");
    streakMinDaysLabel->setToolTip("Only warn if the current streak is at least this long");
    streakMinDaysRow->addWidget(streakMinDaysLabel);
    streakMinDaysRow->addStretch();
    streakAtRiskMinDaysSpinBox = new QSpinBox();
    streakAtRiskMinDaysSpinBox->setMinimum(1);
    streakAtRiskMinDaysSpinBox->setMaximum(365);
    streakAtRiskMinDaysSpinBox->setToolTip("Only warn if the current streak is at least this long");
    streakAtRiskMinDaysSpinBox->setValue(config->get("streak_at_risk_min_days").toInt());
    setupSpinStyle(streakAtRiskMinDaysSpinBox, "spinbox");
    streakMinDaysRow->addWidget(streakAtRiskMinDaysSpinBox);
    streakRiskLayout->addLayout(streakMinDaysRow);

    // Cutoff hour
    QHBoxLayout* streakCutoffRow = new QHBoxLayout();
    QLabel* streakCutoffLabel = new QLabel("Cutoff hour (0-23):");
    streakCutoffLabel->setToolTip("Warn if no watch by this hour (e.g. 20 = 8 PM)");
    streakCutoffRow->addWidget(streakCutoffLabel);
    streakCutoffRow->addStretch();
    streakAtRiskCutoffHourSpinBox = new QSpinBox();
    streakAtRiskCutoffHourSpinBox->setMinimum(0);
    streakAtRiskCutoffHourSpinBox->setMaximum(23);
    streakAtRiskCutoffHourSpinBox->setToolTip("Warn if no watch by this hour (e.g. 20 = 8 PM)");
    streakAtRiskCutoffHourSpinBox->setValue(config->get("streak_at_risk_cutoff_hour").toInt());
    setupSpinStyle(streakAtRiskCutoffHourSpinBox, "spinbox");
    streakCutoffRow->addWidget(streakAtRiskCutoffHourSpinBox);
    streakRiskLayout->addLayout(streakCutoffRow);

    // Re-fire interval
    QHBoxLayout* streakRefireRow = new QHBoxLayout();
    QLabel* streakRefireLabel = new QLabel("Re-fire interval (min):");
    streakRefireLabel->setToolTip("Re-fire notification every N minutes after cutoff (0 = notify once per day)");
    streakRefireRow->addWidget(streakRefireLabel);
    streakRefireRow->addStretch();
    streakAtRiskRefireSpinBox = new QSpinBox();
    streakAtRiskRefireSpinBox->setMinimum(0);
    streakAtRiskRefireSpinBox->setMaximum(480);
    streakAtRiskRefireSpinBox->setSingleStep(15);
    streakAtRiskRefireSpinBox->setValue(config->get("streak_at_risk_refire_minutes").toInt());
    setupSpinStyle(streakAtRiskRefireSpinBox, "spinbox");
    streakRefireRow->addWidget(streakAtRiskRefireSpinBox);
    streakRiskLayout->addLayout(streakRefireRow);

    // Streak saved / debt failed notification
    QHBoxLayout* streakSavedRow = new QHBoxLayout();
    notificationStreakSavedEnabled = new QCheckBox("Notify when a streak save is used");
    notificationStreakSavedEnabled->setToolTip(
        "Show a popup when a freeze or auto save covers a missed day, or when an auto-save debt fails");
    setupCheckBox(notificationStreakSavedEnabled, "notification_streak_saved_enabled", mw);
    streakSavedRow->addWidget(notificationStreakSavedEnabled);
    streakSavedRow->addStretch();
    notificationStreakSavedDurationSpinBox = new QSpinBox();
    notificationStreakSavedDurationSpinBox->setMinimum(1000);
    notificationStreakSavedDurationSpinBox->setMaximum(60000);
    notificationStreakSavedDurationSpinBox->setSingleStep(500);
    notificationStreakSavedDurationSpinBox->setToolTip("Duration in milliseconds");
    notificationStreakSavedDurationSpinBox->setValue(config->get("notification_streak_saved_duration_ms").toInt());
    setupSpinStyle(notificationStreakSavedDurationSpinBox, "spinbox");
    connect(notificationStreakSavedEnabled, &QCheckBox::toggled,
            notificationStreakSavedDurationSpinBox, &QSpinBox::setEnabled);
    notificationStreakSavedDurationSpinBox->setEnabled(notificationStreakSavedEnabled->isChecked());
    streakSavedRow->addWidget(notificationStreakSavedDurationSpinBox);
    streakRiskLayout->addLayout(streakSavedRow);

    // Debt-day re-fire interval
    QHBoxLayout* streakDebtRefireRow = new QHBoxLayout();
    QLabel* streakDebtRefireLabel = new QLabel("Debt re-fire interval (min):");
    streakDebtRefireLabel->setToolTip(
        "While an auto-save debt is pending, re-fire the urgent notification every N minutes (0 = use the normal re-fire)");
    streakDebtRefireRow->addWidget(streakDebtRefireLabel);
    streakDebtRefireRow->addStretch();
    streakDebtRefireSpinBox = new QSpinBox();
    streakDebtRefireSpinBox->setMinimum(0);
    streakDebtRefireSpinBox->setMaximum(480);
    streakDebtRefireSpinBox->setSingleStep(15);
    streakDebtRefireSpinBox->setToolTip(
        "While an auto-save debt is pending, re-fire the urgent notification every N minutes (0 = use the normal re-fire)");
    streakDebtRefireSpinBox->setValue(config->get("streak_debt_refire_minutes").toInt());
    setupSpinStyle(streakDebtRefireSpinBox, "spinbox");
    streakDebtRefireRow->addWidget(streakDebtRefireSpinBox);
    streakRiskLayout->addLayout(streakDebtRefireRow);

    // Debt final-hour escalation
    QHBoxLayout* streakDebtFinalHourRow = new QHBoxLayout();
    QLabel* streakDebtFinalHourLabel = new QLabel("Debt final hour (0-23):");
    streakDebtFinalHourLabel->setToolTip(
        "After this hour, a pending debt notification becomes LAST CHANCE (clamped to at least the at-risk cutoff)");
    streakDebtFinalHourRow->addWidget(streakDebtFinalHourLabel);
    streakDebtFinalHourRow->addStretch();
    streakDebtFinalHourSpinBox = new QSpinBox();
    streakDebtFinalHourSpinBox->setMinimum(0);
    streakDebtFinalHourSpinBox->setMaximum(23);
    streakDebtFinalHourSpinBox->setToolTip(
        "After this hour, a pending debt notification becomes LAST CHANCE (clamped to at least the at-risk cutoff)");
    streakDebtFinalHourSpinBox->setValue(config->get("streak_debt_final_hour").toInt());
    setupSpinStyle(streakDebtFinalHourSpinBox, "spinbox");
    streakDebtFinalHourRow->addWidget(streakDebtFinalHourSpinBox);
    streakRiskLayout->addLayout(streakDebtFinalHourRow);

    pageLayout->addWidget(streakRiskGroup);

    // ── Personal Best ───────────────────────────────────────────────
    QGroupBox* personalBestGroup = new QGroupBox("Personal Best");
    QVBoxLayout* personalBestLayout = new QVBoxLayout(personalBestGroup);
    personalBestLayout->setSpacing(4);

    // Enable + duration row
    QHBoxLayout* pbEnableRow = new QHBoxLayout();
    notificationPersonalBestEnabled = new QCheckBox("Enable personal best notifications");
    notificationPersonalBestEnabled->setToolTip("Show notification popups when you break an all-time daily record");
    notificationPersonalBestEnabled->setChecked(config->get_bool("notification_personal_best_enabled"));
    pbEnableRow->addWidget(notificationPersonalBestEnabled);
    pbEnableRow->addStretch();
    notificationPersonalBestDurationSpinBox = new QSpinBox();
    notificationPersonalBestDurationSpinBox->setMinimum(1000);
    notificationPersonalBestDurationSpinBox->setMaximum(60000);
    notificationPersonalBestDurationSpinBox->setSingleStep(500);
    notificationPersonalBestDurationSpinBox->setToolTip("Duration in milliseconds");
    notificationPersonalBestDurationSpinBox->setValue(config->get("notification_personal_best_duration_ms").toInt());
    setupSpinStyle(notificationPersonalBestDurationSpinBox, "spinbox");
    connect(notificationPersonalBestEnabled, &QCheckBox::toggled, notificationPersonalBestDurationSpinBox, &QSpinBox::setEnabled);
    notificationPersonalBestDurationSpinBox->setEnabled(notificationPersonalBestEnabled->isChecked());
    pbEnableRow->addWidget(notificationPersonalBestDurationSpinBox);
    personalBestLayout->addLayout(pbEnableRow);

    // Videos step
    QHBoxLayout* pbVideoStepRow = new QHBoxLayout();
    QLabel* pbVideoStepLabel = new QLabel("Videos step:");
    pbVideoStepLabel->setToolTip("Notify every N videos above your all-time daily record (1 = every video)");
    pbVideoStepRow->addWidget(pbVideoStepLabel);
    pbVideoStepRow->addStretch();
    pbVideosStepSpinBox = new QSpinBox();
    pbVideosStepSpinBox->setMinimum(1);
    pbVideosStepSpinBox->setMaximum(999);
    pbVideosStepSpinBox->setToolTip("Notify every N videos above your all-time daily record (1 = every video)");
    pbVideosStepSpinBox->setValue(config->get("pb_videos_step").toInt());
    setupSpinStyle(pbVideosStepSpinBox, "spinbox");
    pbVideoStepRow->addWidget(pbVideosStepSpinBox);
    personalBestLayout->addLayout(pbVideoStepRow);

    // Videos min threshold
    QHBoxLayout* pbVideoThreshRow = new QHBoxLayout();
    QLabel* pbVideoThreshLabel = new QLabel("Videos min. threshold:");
    pbVideoThreshLabel->setToolTip("Only notify if your old daily record was at least this many videos");
    pbVideoThreshRow->addWidget(pbVideoThreshLabel);
    pbVideoThreshRow->addStretch();
    pbVideosMinThresholdSpinBox = new QSpinBox();
    pbVideosMinThresholdSpinBox->setMinimum(1);
    pbVideosMinThresholdSpinBox->setMaximum(999);
    pbVideosMinThresholdSpinBox->setToolTip("Only notify if your old daily record was at least this many videos");
    pbVideosMinThresholdSpinBox->setValue(config->get("pb_videos_min_threshold").toInt());
    setupSpinStyle(pbVideosMinThresholdSpinBox, "spinbox");
    pbVideoThreshRow->addWidget(pbVideosMinThresholdSpinBox);
    personalBestLayout->addLayout(pbVideoThreshRow);

    // Watch time step
    QHBoxLayout* pbTimeStepRow = new QHBoxLayout();
    QLabel* pbTimeStepLabel = new QLabel("Watch time step (min):");
    pbTimeStepLabel->setToolTip("Notify every N minutes above your all-time daily record (5 = every 5 minutes)");
    pbTimeStepRow->addWidget(pbTimeStepLabel);
    pbTimeStepRow->addStretch();
    pbTimeStepSpinBox = new QSpinBox();
    pbTimeStepSpinBox->setMinimum(1);
    pbTimeStepSpinBox->setMaximum(9999);
    pbTimeStepSpinBox->setSingleStep(5);
    pbTimeStepSpinBox->setToolTip("Notify every N minutes above your all-time daily record (5 = every 5 minutes)");
    pbTimeStepSpinBox->setValue(config->get("pb_time_step_minutes").toInt());
    setupSpinStyle(pbTimeStepSpinBox, "spinbox");
    pbTimeStepRow->addWidget(pbTimeStepSpinBox);
    personalBestLayout->addLayout(pbTimeStepRow);

    // Watch time min threshold
    QHBoxLayout* pbTimeThreshRow = new QHBoxLayout();
    QLabel* pbTimeThreshLabel = new QLabel("Watch time min. threshold (min):");
    pbTimeThreshLabel->setToolTip("Only notify if your old daily record was at least this many minutes");
    pbTimeThreshRow->addWidget(pbTimeThreshLabel);
    pbTimeThreshRow->addStretch();
    pbTimeMinThresholdSpinBox = new QSpinBox();
    pbTimeMinThresholdSpinBox->setMinimum(1);
    pbTimeMinThresholdSpinBox->setMaximum(9999);
    pbTimeMinThresholdSpinBox->setSingleStep(5);
    pbTimeMinThresholdSpinBox->setToolTip("Only notify if your old daily record was at least this many minutes");
    pbTimeMinThresholdSpinBox->setValue(config->get("pb_time_min_threshold_minutes").toInt());
    setupSpinStyle(pbTimeMinThresholdSpinBox, "spinbox");
    pbTimeThreshRow->addWidget(pbTimeMinThresholdSpinBox);
    personalBestLayout->addLayout(pbTimeThreshRow);

    pageLayout->addWidget(personalBestGroup);

    // --- Session Summary Group ---
    QGroupBox* sessionSummaryGroup = new QGroupBox("Session Summary");
    sessionSummaryGroup->setToolTip("Show a summary notification when a watching session ends (MPC-HC closes)");
    QVBoxLayout* sessionSummaryLayout = new QVBoxLayout(sessionSummaryGroup);
    sessionSummaryLayout->setSpacing(4);

    // Enable + duration row
    QHBoxLayout* ssEnableRow = new QHBoxLayout();
    notificationSessionSummaryEnabled = new QCheckBox("Enable session summary notifications");
    notificationSessionSummaryEnabled->setToolTip("Show a summary notification popup when you close the player after watching");
    notificationSessionSummaryEnabled->setChecked(config->get_bool("notification_session_summary_enabled"));
    ssEnableRow->addWidget(notificationSessionSummaryEnabled);
    ssEnableRow->addStretch();
    notificationSessionSummaryDurationSpinBox = new QSpinBox();
    notificationSessionSummaryDurationSpinBox->setMinimum(1000);
    notificationSessionSummaryDurationSpinBox->setMaximum(60000);
    notificationSessionSummaryDurationSpinBox->setSingleStep(500);
    notificationSessionSummaryDurationSpinBox->setToolTip("Duration in milliseconds");
    notificationSessionSummaryDurationSpinBox->setValue(config->get("notification_session_summary_duration_ms").toInt());
    setupSpinStyle(notificationSessionSummaryDurationSpinBox, "spinbox");
    connect(notificationSessionSummaryEnabled, &QCheckBox::toggled, notificationSessionSummaryDurationSpinBox, &QSpinBox::setEnabled);
    notificationSessionSummaryDurationSpinBox->setEnabled(notificationSessionSummaryEnabled->isChecked());
    ssEnableRow->addWidget(notificationSessionSummaryDurationSpinBox);
    sessionSummaryLayout->addLayout(ssEnableRow);

    // Min session time row
    QHBoxLayout* ssMinSessionRow = new QHBoxLayout();
    QLabel* ssMinSessionLabel = new QLabel("Minimum session time (seconds):");
    ssMinSessionLabel->setToolTip("Only show the summary if the session lasted at least this long");
    ssMinSessionRow->addWidget(ssMinSessionLabel);
    ssMinSessionRow->addStretch();
    notificationSessionSummaryMinSessionSpinBox = new QSpinBox();
    notificationSessionSummaryMinSessionSpinBox->setMinimum(1);
    notificationSessionSummaryMinSessionSpinBox->setMaximum(3600);
    notificationSessionSummaryMinSessionSpinBox->setToolTip("Only show the summary if the session lasted at least this many seconds");
    notificationSessionSummaryMinSessionSpinBox->setValue(config->get("notification_session_summary_min_session_seconds").toInt());
    setupSpinStyle(notificationSessionSummaryMinSessionSpinBox, "spinbox");
    ssMinSessionRow->addWidget(notificationSessionSummaryMinSessionSpinBox);
    connect(notificationSessionSummaryEnabled, &QCheckBox::toggled, notificationSessionSummaryMinSessionSpinBox, &QSpinBox::setEnabled);
    notificationSessionSummaryMinSessionSpinBox->setEnabled(notificationSessionSummaryEnabled->isChecked());
    sessionSummaryLayout->addLayout(ssMinSessionRow);

    // Min video watch time row
    QHBoxLayout* ssMinVideoWatchRow = new QHBoxLayout();
    QLabel* ssMinVideoWatchLabel = new QLabel("Minimum video watch time (seconds):");
    ssMinVideoWatchLabel->setToolTip("Videos watched for less than this time won't count toward the session summary");
    ssMinVideoWatchRow->addWidget(ssMinVideoWatchLabel);
    ssMinVideoWatchRow->addStretch();
    notificationSessionSummaryMinVideoWatchSpinBox = new QSpinBox();
    notificationSessionSummaryMinVideoWatchSpinBox->setMinimum(1);
    notificationSessionSummaryMinVideoWatchSpinBox->setMaximum(300);
    notificationSessionSummaryMinVideoWatchSpinBox->setToolTip("Videos watched for less than this many seconds won't count toward the session summary");
    notificationSessionSummaryMinVideoWatchSpinBox->setValue(config->get("notification_session_summary_min_video_watch_seconds").toInt());
    setupSpinStyle(notificationSessionSummaryMinVideoWatchSpinBox, "spinbox");
    ssMinVideoWatchRow->addWidget(notificationSessionSummaryMinVideoWatchSpinBox);
    connect(notificationSessionSummaryEnabled, &QCheckBox::toggled, notificationSessionSummaryMinVideoWatchSpinBox, &QSpinBox::setEnabled);
    notificationSessionSummaryMinVideoWatchSpinBox->setEnabled(notificationSessionSummaryEnabled->isChecked());
    sessionSummaryLayout->addLayout(ssMinVideoWatchRow);

    pageLayout->addWidget(sessionSummaryGroup);

    // --- Appearance Group ---
    QGroupBox* notifAppearanceGroup = new QGroupBox("Appearance");
    QVBoxLayout* notifAppearanceLayout = new QVBoxLayout(notifAppearanceGroup);
    notifAppearanceLayout->setSpacing(4);

    auto addOpacityRow = [&](const QString& labelText, const QString& tooltip, const QString& configKey) -> QDoubleSpinBox* {
        QHBoxLayout* row = new QHBoxLayout();
        QLabel* label = new QLabel(labelText);
        label->setToolTip(tooltip);
        row->addWidget(label);
        row->addStretch();
        QDoubleSpinBox* spinBox = new QDoubleSpinBox();
        spinBox->setMinimum(0.00);
        spinBox->setMaximum(1.00);
        spinBox->setSingleStep(0.05);
        spinBox->setDecimals(2);
        spinBox->setToolTip(tooltip);
        spinBox->setValue(config->get_double(configKey, 1.0));
        setupSpinStyle(spinBox, "doublespinbox");
        row->addWidget(spinBox);
        notifAppearanceLayout->addLayout(row);
        return spinBox;
    };

    notificationBgOpacitySpinBox = addOpacityRow("Background Opacity:",
        "Transparency of the notification background (0.00 = fully transparent, 1.00 = fully opaque)",
        "notification_bg_opacity");
    notificationContentOpacitySpinBox = addOpacityRow("Content Opacity:",
        "Transparency of text, stars, and progress bar inside notifications (0.00 = fully transparent, 1.00 = fully opaque)",
        "notification_content_opacity");
    notificationTimerBarOpacitySpinBox = addOpacityRow("Timer Bar Opacity:",
        "Transparency of the duration progress bar (0.00 = fully transparent, 1.00 = fully opaque)",
        "notification_timerbar_opacity");
    notificationCounterOpacitySpinBox = addOpacityRow("Counter Opacity:",
        "Transparency of the counter label backgrounds — text on them is controlled by Content Opacity (0.00 = fully transparent, 1.00 = fully opaque)",
        "notification_counter_opacity");

    pageLayout->addWidget(notifAppearanceGroup);

    // Spacer at bottom
    pageLayout->addStretch();

    scrollArea->setWidget(pageContent);
    this->ui.settingsStack->addWidget(scrollArea);
}

// ---------------------------------------------------------------------------
// Crash dumps page — Windows Error Reporting LocalDumps for this executable
// ---------------------------------------------------------------------------

namespace {

// WER reads per-application dump settings from this machine-wide key. Reading it does
// not need elevation; writing it does, so we go through an elevated reg.exe.
QString localDumpsKeyPath()
{
    return QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\")
        + QFileInfo(QCoreApplication::applicationFilePath()).fileName();
}

QString defaultCrashDumpFolder()
{
    // GenericDataLocation is %LOCALAPPDATA% on Windows, which is also WER's default.
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/CrashDumps");
}

QString werReportArchivePath()
{
    QString programData = qEnvironmentVariable("ProgramData");
    if (programData.isEmpty())
        programData = QStringLiteral("C:/ProgramData");
    return programData + QStringLiteral("/Microsoft/Windows/WER/ReportArchive");
}

QString regExePath()
{
    QString systemRoot = qEnvironmentVariable("SystemRoot");
    if (systemRoot.isEmpty())
        systemRoot = QStringLiteral("C:/Windows");
    return systemRoot + QStringLiteral("/System32/reg.exe");
}

// Run a helper process elevated: exactly one UAC prompt per call. Returns false without
// touching exitCode if the prompt is cancelled or cannot be shown; otherwise exitCode is
// the helper's exit code (0 on success).
bool runElevated(const QString& file, const QString& arguments, DWORD& exitCode)
{
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas";
    sei.nShow = SW_HIDE;

    const std::wstring fileW = QDir::toNativeSeparators(file).toStdWString();
    const std::wstring argsW = arguments.toStdWString();
    sei.lpFile = fileW.c_str();
    sei.lpParameters = argsW.c_str();

    if (!ShellExecuteExW(&sei)) {
        exitCode = GetLastError();
        return false;
    }
    WaitForSingleObject(sei.hProcess, 60000);
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    return true;
}

QString cmdExePath()
{
    QString systemRoot = qEnvironmentVariable("SystemRoot");
    if (systemRoot.isEmpty())
        systemRoot = QStringLiteral("C:/Windows");
    return systemRoot + QStringLiteral("/System32/cmd.exe");
}

// Write the per-application LocalDumps values in one elevated call. The values travel as
// arguments to the elevated process instead of through a temporary file: a .reg in the
// user's %TEMP% could be replaced by any same-user process during the UAC prompt and then
// be imported with administrator rights.
bool writeLocalDumpsKey(const QString& dumpFolder, int dumpType, int dumpCount, DWORD& exitCode)
{
    // The folder normally comes from the registry. Reject characters cmd.exe would treat
    // specially even inside quotes (% expands variables); otherwise use the default.
    QString safeFolder = dumpFolder;
    if (safeFolder.contains(QLatin1Char('"')) || safeFolder.contains(QLatin1Char('%'))
        || safeFolder.contains(QLatin1Char('\n')) || safeFolder.contains(QLatin1Char('\r'))) {
        safeFolder = defaultCrashDumpFolder();
    }

    const QString reg = QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(regExePath()));
    const QString key = QStringLiteral("\"%1\"").arg(localDumpsKeyPath());
    const QString folder = QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(safeFolder));

    QStringList commands;
    commands << QStringLiteral("%1 add %2 /v DumpFolder /t REG_EXPAND_SZ /d %3 /f").arg(reg, key, folder);
    commands << QStringLiteral("%1 add %2 /v DumpType /t REG_DWORD /d %3 /f").arg(reg, key, QString::number(dumpType));
    commands << QStringLiteral("%1 add %2 /v DumpCount /t REG_DWORD /d %3 /f").arg(reg, key, QString::number(dumpCount));

    return runElevated(cmdExePath(),
        QStringLiteral("/c \"%1\"").arg(commands.join(QStringLiteral(" && "))), exitCode);
}

bool removeLocalDumpsKey(DWORD& exitCode)
{
    return runElevated(regExePath(),
        QStringLiteral("delete \"%1\" /f").arg(localDumpsKeyPath()), exitCode);
}

// Read back what WER would use right now, filling in the defaults when the key is absent.
void readLocalDumpsState(bool& enabled, int& dumpType, int& dumpCount, QString& folder)
{
    QSettings settings(localDumpsKeyPath(), QSettings::NativeFormat);
    enabled = settings.contains(QStringLiteral("DumpType"))
        || settings.contains(QStringLiteral("DumpFolder"));
    dumpType = settings.value(QStringLiteral("DumpType"), 1).toInt();
    dumpCount = settings.value(QStringLiteral("DumpCount"), 10).toInt();
    folder = settings.value(QStringLiteral("DumpFolder"), defaultCrashDumpFolder()).toString();
    if (folder.isEmpty())
        folder = defaultCrashDumpFolder();
}

bool localDumpsConfigured()
{
    QSettings settings(localDumpsKeyPath(), QSettings::NativeFormat);
    return settings.contains(QStringLiteral("DumpType"))
        || settings.contains(QStringLiteral("DumpFolder"));
}

} // namespace

void SettingsDialog::setupCrashDumpsPage(MainWindow* mw)
{
    Q_UNUSED(mw);

    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);

    QWidget* pageContent = new QWidget();
    QVBoxLayout* pageLayout = new QVBoxLayout(pageContent);
    pageLayout->setContentsMargins(4, 4, 4, 4);
    pageLayout->setSpacing(8);

    // --- Dump settings ---
    QGroupBox* dumpGroup = new QGroupBox("Windows Error Reporting dumps");
    QVBoxLayout* dumpLayout = new QVBoxLayout(dumpGroup);
    dumpLayout->setSpacing(6);

    QLabel* intro = new QLabel("When Media Manager crashes, Windows can write a dump file that can be analyzed later. "
        "These settings apply to MediaManager.exe only and are stored machine-wide, so applying them asks for "
        "administrator rights (UAC).");
    intro->setWordWrap(true);
    dumpLayout->addWidget(intro);

    QCheckBox* enableCheck = new QCheckBox("Collect a dump when Media Manager crashes");
    enableCheck->setToolTip("Creates the per-application LocalDumps registry key. Uncheck and apply to remove it again.");
    dumpLayout->addWidget(enableCheck);

    QHBoxLayout* levelRow = new QHBoxLayout();
    levelRow->addWidget(new QLabel("Dump level:"));
    QComboBox* levelCombo = new QComboBox();
    levelCombo->addItem("Mini — stack and thread info", 1);
    levelCombo->addItem("Full — entire process memory", 2);
    levelCombo->setItemData(0, "Small dump with the crashing thread's call stack; enough to see where it crashed.", Qt::ToolTipRole);
    levelCombo->setItemData(1, "Full process memory. Much larger, but lets a debugger inspect heap data such as the SQLite connection state.", Qt::ToolTipRole);
    levelCombo->setToolTip("Windows DumpType: 1 = mini, 2 = full.");
    levelRow->addWidget(levelCombo, 1);
    dumpLayout->addLayout(levelRow);

    QHBoxLayout* countRow = new QHBoxLayout();
    countRow->addWidget(new QLabel("Keep at most:"));
    QSpinBox* countSpin = new QSpinBox();
    countSpin->setRange(1, 100);
    countSpin->setValue(10);
    countSpin->setToolTip("Older dumps beyond this count are deleted by Windows Error Reporting.");
    setupSpinStyle(countSpin, "spinbox");
    countRow->addWidget(countSpin);
    countRow->addWidget(new QLabel("dumps"));
    countRow->addStretch();
    dumpLayout->addLayout(countRow);

    QLabel* statusLabel = new QLabel();
    statusLabel->setWordWrap(true);
    dumpLayout->addWidget(statusLabel);

    QHBoxLayout* actionRow = new QHBoxLayout();
    QPushButton* applyButton = new QPushButton("Apply dump settings");
    QPushButton* refreshButton = new QPushButton("Refresh");
    actionRow->addWidget(applyButton);
    actionRow->addWidget(refreshButton);
    actionRow->addStretch();
    dumpLayout->addLayout(actionRow);

    pageLayout->addWidget(dumpGroup);

    // --- Folders ---
    QGroupBox* foldersGroup = new QGroupBox("Dump folders");
    QVBoxLayout* foldersLayout = new QVBoxLayout(foldersGroup);
    foldersLayout->setSpacing(6);

    QLineEdit* dumpFolderEdit = new QLineEdit();
    dumpFolderEdit->setReadOnly(true);
    QPushButton* openDumpsButton = new QPushButton("Open");

    QLineEdit* werFolderEdit = new QLineEdit(QDir::toNativeSeparators(werReportArchivePath()));
    werFolderEdit->setReadOnly(true);
    QPushButton* openWerButton = new QPushButton("Open");

    QHBoxLayout* dumpsFolderRow = new QHBoxLayout();
    dumpsFolderRow->addWidget(new QLabel("Crash dumps:"));
    dumpsFolderRow->addWidget(dumpFolderEdit, 1);
    dumpsFolderRow->addWidget(openDumpsButton);
    foldersLayout->addLayout(dumpsFolderRow);

    QHBoxLayout* werFolderRow = new QHBoxLayout();
    werFolderRow->addWidget(new QLabel("WER reports:"));
    werFolderRow->addWidget(werFolderEdit, 1);
    werFolderRow->addWidget(openWerButton);
    foldersLayout->addLayout(werFolderRow);

    pageLayout->addWidget(foldersGroup);
    pageLayout->addStretch();

    scrollArea->setWidget(pageContent);
    this->ui.settingsStack->addWidget(scrollArea);
    this->ui.categoryList->addItem("Crash Dumps");

    // Helpers ------------------------------------------------------------

    auto refresh = [enableCheck, levelCombo, countSpin, dumpFolderEdit, statusLabel]() {
        bool enabled = false;
        int dumpType = 1;
        int dumpCount = 5;
        QString folder;
        readLocalDumpsState(enabled, dumpType, dumpCount, folder);

        {
            QSignalBlocker blockEnable(enableCheck);
            QSignalBlocker blockLevel(levelCombo);
            QSignalBlocker blockCount(countSpin);
            enableCheck->setChecked(enabled);
            levelCombo->setCurrentIndex(levelCombo->findData(dumpType == 2 ? 2 : 1));
            countSpin->setValue(dumpCount >= 1 ? dumpCount : 10);
        }
        dumpFolderEdit->setText(QDir::toNativeSeparators(folder));
        statusLabel->setText(enabled
            ? QStringLiteral("Enabled: %1 dumps, at most %2 kept, written to %3.")
                .arg(dumpType == 2 ? QStringLiteral("full") : QStringLiteral("mini"))
                .arg(dumpCount)
                .arg(QDir::toNativeSeparators(folder))
            : QStringLiteral("Not configured — Media Manager will not write a crash dump."));
    };

    // Connections --------------------------------------------------------

    connect(applyButton, &QPushButton::clicked, this,
        [this, enableCheck, levelCombo, countSpin, dumpFolderEdit, refresh]() {
            if (!enableCheck->isChecked() && !localDumpsConfigured()) {
                refresh();
                return;
            }

            DWORD result = 0;
            bool launched = false;
            if (enableCheck->isChecked()) {
                launched = writeLocalDumpsKey(dumpFolderEdit->text(),
                    levelCombo->currentData().toInt(), countSpin->value(), result);
            } else {
                launched = removeLocalDumpsKey(result);
            }

            if (!launched) {
                if (result == ERROR_CANCELLED)
                    QMessageBox::information(this, "Crash dumps",
                        "The change was cancelled. Administrator rights are required to update the dump settings.");
                else
                    QMessageBox::warning(this, "Crash dumps",
                        QStringLiteral("Could not start the elevated registry update (error %1).").arg(result));
                return;
            }
            if (result != 0) {
                QMessageBox::warning(this, "Crash dumps",
                    QStringLiteral("The registry update failed (reg.exe exit code %1).").arg(result));
                return;
            }
            refresh();
            QMessageBox::information(this, "Crash dumps",
                "Dump settings updated. They take effect the next time Media Manager crashes.");
        });

    connect(refreshButton, &QPushButton::clicked, this, refresh);
    connect(openDumpsButton, &QPushButton::clicked, this, [this, dumpFolderEdit]() {
        const QString folder = dumpFolderEdit->text();
        const QString native = QDir::toNativeSeparators(folder);
        if (!QDir(folder).exists()) {
            QMessageBox::information(this, "Crash dumps", QStringLiteral("The folder does not exist yet:\n%1").arg(native));
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
            QMessageBox::information(this, "Crash dumps", QStringLiteral("Could not open the folder:\n%1").arg(native));
    });
    connect(openWerButton, &QPushButton::clicked, this, [this]() {
        const QString folder = werReportArchivePath();
        const QString native = QDir::toNativeSeparators(folder);
        if (!QDir(folder).exists()) {
            QMessageBox::information(this, "WER reports", QStringLiteral("The folder does not exist yet:\n%1").arg(native));
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
            QMessageBox::information(this, "WER reports", QStringLiteral("Could not open the folder:\n%1").arg(native));
    });

    refresh();
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
    this->ui.setupUi(this);
    this->setWindowModality(Qt::WindowModal);
    MainWindow* mw = static_cast<MainWindow*>(parent);

    // Sidebar navigation
    connect(this->ui.categoryList, &QListWidget::currentRowChanged,
            this->ui.settingsStack, &QStackedWidget::setCurrentIndex);
    this->ui.categoryList->setCurrentRow(0);

    // Setup all category pages
    setupGeneralPage(mw);
    setupPlaybackPage(mw);
    setupAppearancePage(mw);
    setupAudioPage(mw);
    setupDatabasePage(mw);
    setupRandomPage(mw);
    setupStreaksPage(mw);
    setupNotificationsPage(mw);

    //These need to be at the bottom
    // Wire Apply button for all changeable widgets + wheel filters
    wireApplyEnable();
    // Added after wireApplyEnable() on purpose: the dump controls only feed the page's own
    // "Apply dump settings" button and must not enable the dialog-wide Apply button.
    setupCrashDumpsPage(mw);
    WheelStrongFocusEventFilter* wheelFilter = new WheelStrongFocusEventFilter(this);
    installWheelFilters(wheelFilter);
    this->ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
}

SettingsDialog::~SettingsDialog()
{
}
