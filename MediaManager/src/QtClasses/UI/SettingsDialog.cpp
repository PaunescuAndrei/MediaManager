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
// Mascot keys, as one list
// ---------------------------------------------------------------------------

// (M5 review fix I4: the Cancel snapshot needs a universe that is the *writer's*, not "whatever the ini
// happens to contain". Four layer keys have no `init_defaults` entry on purpose -- absent means "follow the
// columns" -- so a dialog opened before anything wrote them snapshotted nothing for them, and a live change
// that wrote them survived Cancel. An absent key answers empty here, and restoring empty is precisely what
// "follow the columns" means, so the snapshot and the writer now cover the same keys.
// The second half is the legacy rows, whose writes live in MainWindow::applySettings(); they were in the old
// ini walk and have to stay in the snapshot or Cancel would stop reverting them.)
const QStringList& mascotConfigKeys()
{
    static const QStringList keys = {
        // Written by SettingsDialog::syncMascotConfig().
        QStringLiteral("mascots_media"),
        QStringLiteral("mascots_mode"),
        QStringLiteral("mascots_fill"),
        QStringLiteral("mascots_weighted_random"),
        QStringLiteral("mascots_clip_seconds"),
        QStringLiteral("mascots_crossfade_ms"),
        QStringLiteral("mascots_swap_on_beat"),
        QStringLiteral("mascots_random_start"),
        QStringLiteral("mascots_preload"),
        QStringLiteral("mascots_pause_unfocused"),
        QStringLiteral("mascots_low_latency"),
        QStringLiteral("mascots_pair_scroll"),
        QStringLiteral("mascots_pair_flip"),
        QStringLiteral("mascots_mirror"),
        QStringLiteral("mascots_max_decoders"),
        QStringLiteral("mascots_scroll_speed"),
        QStringLiteral("mascots_scroll_direction"),
        QStringLiteral("mascots_scroll_pingpong"),
        QStringLiteral("mascots_stripes_left"),
        QStringLiteral("mascots_stripes_right"),
        QStringLiteral("mascots_stripes_orientation"),
        QStringLiteral("mascots_stripes_alternate"),
        QStringLiteral("mascots_background"),
        QStringLiteral("mascots_background_opacity"),
        QStringLiteral("mascots_columns_z"),
        QStringLiteral("mascots_background_list_alpha"),
        QStringLiteral("mascots_background_alternate_alpha"),
        QStringLiteral("mascots_background_scrollbar_alpha"),
        QStringLiteral("mascots_background_mode"),
        QStringLiteral("mascots_background_fill"),
        QStringLiteral("mascots_background_stripes"),
        QStringLiteral("mascots_background_stripes_orientation"),
        QStringLiteral("mascots_background_stripes_alternate"),
        QStringLiteral("mascots_background_scroll_speed"),
        QStringLiteral("mascots_background_scroll_direction"),
        QStringLiteral("mascots_background_clip_seconds"),
        QStringLiteral("mascots_background_swap_on_beat"),
        QStringLiteral("mascots_background_random_change"),
        QStringLiteral("mascots_background_random_chance"),
        QStringLiteral("mascots_background_scroll_pingpong"),
        QStringLiteral("mascots_image_cache_mb"),
        QStringLiteral("mascots_image_cache_max_items"),
        // Written by MainWindow::applySettings() from the legacy rows on this page.
        QStringLiteral("mascots"),
        QStringLiteral("mascots_allfiles_random"),
        QStringLiteral("mascots_animated"),
        QStringLiteral("mascots_color_theme"),
        QStringLiteral("mascots_center_content"),
        QStringLiteral("mascots_use_seed"),
        QStringLiteral("mascots_frequency"),
        QStringLiteral("mascots_width_percent"),
        QStringLiteral("mascots_random_change"),
        QStringLiteral("mascots_random_chance"),
    };
    return keys;
}

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
        // (M5 fix: combos were missing from this walk, so choosing a media type, a layout mode or a
        // fill mode left Apply greyed out. Nothing in the dialog noticed, because every other control
        // type was covered and the Mascots page is the first one whose main controls are combos.)
        else if (auto* co = qobject_cast<QComboBox*>(w))
            connect(co, &QComboBox::currentIndexChanged, this, [applyBtn] { applyBtn->setEnabled(true); });
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
    setupCheckBox(this->ui.previewLazyLoading, "preview_lazy_loading", mw);
    this->oldPreviewLazyLoading = this->ui.previewLazyLoading->isChecked();

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
    // (M5 Task 2: the group box these controls live in now sits on the Mascots category, not
    // Appearance. The wiring below is untouched; only the box's parent changed.)
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

void SettingsDialog::setupMascotsPage(MainWindow* mw)
{
    Config* config = mw->App->config;

    // (M5 review fix I4: the walk that used to be here collected "whatever mascot keys the ini happens to
    // contain". The four layer keys have no default entry on purpose, so a dialog opened before anything
    // wrote them snapshotted nothing for them and a live write survived Cancel. The snapshot now covers
    // exactly the keys the writers own -- see mascotConfigKeys() -- and an absent key is captured empty,
    // which is the same "follow the columns" the readers see.)
    for (const QString& key : mascotConfigKeys())
        mascotConfigSnapshot.insert(key, config->get(key));

    // Build the page programmatically and insert it after Appearance. Both the sidebar and the
    // stack are given the same index, the way setupStreaksPage() keeps the two in sync.
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);

    QWidget* pageContent = new QWidget();
    QVBoxLayout* pageLayout = new QVBoxLayout(pageContent);
    pageLayout->setContentsMargins(4, 4, 4, 4);
    pageLayout->setSpacing(8);

    // M5 Task 5: the one paragraph that says how the pieces fit together; every control's tooltip
    // carries the rest, so the feature needs no separate manual.
    // (M5 review fix: the first draft wrote the keys as `mascots` and `Scroll` in backticks, which
    // a QLabel prints literally -- it is plain text, not markup. The sentence below names the
    // controls the user can see instead of the keys they cannot.)
    // (M5 Task 9: the paragraph is gone, on the user's request. Every control's tooltip already carries
    // its own half of it and the group box titles name the pieces, so it was a wall of text above the
    // first control with nothing in it to click. The two comments above are kept as the record of why it
    // was written.)

    // The existing group box is moved, not copied: adding it to this page's layout reparents it
    // off Appearance, so its controls and their wiring stay exactly as setupAppearancePage() left
    // them. Only the title changes, because its contents are no longer just "mascot settings".
    this->ui.mascotsGroupBox->setTitle("Sources && Animation");
    pageLayout->addWidget(this->ui.mascotsGroupBox);

    // Rows are all the same shape -- label, stretch, control -- so the three builders below keep
    // every group's code to one line per key.
    auto makeSpinBox = [this](int minimum, int maximum, int step, int value) {
        QSpinBox* box = new QSpinBox();
        box->setRange(minimum, maximum);
        box->setSingleStep(step);
        box->setValue(value);
        setupSpinStyle(box, "spinbox");
        return box;
    };
    auto makeDoubleSpinBox = [this](double minimum, double maximum, double step, double value) {
        QDoubleSpinBox* box = new QDoubleSpinBox();
        box->setRange(minimum, maximum);
        box->setSingleStep(step);
        box->setDecimals(2);
        box->setValue(value);
        setupSpinStyle(box, "doublespinbox");
        return box;
    };
    auto makeComboBox = [](const QStringList& items, const QString& value) {
        QComboBox* combo = new QComboBox();
        combo->addItems(items);
        // A value none of the items carries leaves the first one selected: setCurrentText() is a
        // no-op for an unknown string on a non-editable combo, and the first item is the
        // documented fallback in every one of these lists.
        combo->setCurrentText(value);
        return combo;
    };
    auto addRow = [](QVBoxLayout* layout, const QString& text, const QString& tooltip, QWidget* control) {
        QHBoxLayout* row = new QHBoxLayout();
        QLabel* label = new QLabel(text);
        label->setToolTip(tooltip);
        row->addWidget(label);
        row->addStretch();
        control->setToolTip(tooltip);
        row->addWidget(control);
        layout->addLayout(row);
    };

    // --- Content & Layout (the columns' content) ---
    QGroupBox* contentGroup = new QGroupBox("Content && Layout");
    QVBoxLayout* contentLayout = new QVBoxLayout(contentGroup);
    contentLayout->setSpacing(4);
    mascotContentGroup = contentGroup;

    mascotMediaCombo = makeComboBox({ QStringLiteral("Images"), QStringLiteral("Videos"), QStringLiteral("Mixed") },
        config->get(QStringLiteral("mascots_media")));
    addRow(contentLayout, "Media:",
        "What the columns show. Images and Mixed use decoded images; Videos and Mixed need a player "
        "from the decoder ceiling. The background layer is separate and always plays videos.",
        mascotMediaCombo);

    mascotModeCombo = makeComboBox({ QStringLiteral("Static"), QStringLiteral("Scroll"), QStringLiteral("Stripes") },
        config->get(QStringLiteral("mascots_mode")));
    addRow(contentLayout, "Layout mode:",
        "How the columns arrange their content: one still panel, one continuously scrolling band, "
        "or several bands scrolling at once.", mascotModeCombo);

    mascotFillCombo = makeComboBox({ QStringLiteral("Cover"), QStringLiteral("Crop"), QStringLiteral("Fit"),
                                     QStringLiteral("Stretch"), QStringLiteral("HeightFit"), QStringLiteral("WidthFit") },
        config->get(QStringLiteral("mascots_fill")));
    addRow(contentLayout, "Fill mode:",
        "How a picture is fitted into its panel: Cover always fills the panel completely, scaling up or "
        "down as the window changes and cutting off the overflow; Crop fills and cuts off but lets the "
        "centring slide; Fit shows all of it with bars, Stretch distorts, HeightFit and WidthFit scale on "
        "one axis only. Cover is honoured by the one-still-panel mode; a panning mode needs one axis "
        "normalised, so it uses its own fit.", mascotFillCombo);

    mascotWeightedCheckBox = new QCheckBox("Weight picks by rating");
    mascotWeightedCheckBox->setChecked(config->get_bool(QStringLiteral("mascots_weighted_random")));
    mascotWeightedCheckBox->setToolTip(
        "Weight the next mascot pick by the video's rating instead of choosing uniformly: "
        "higher-rated videos come up more often.");
    contentLayout->addWidget(mascotWeightedCheckBox);

    pageLayout->addWidget(contentGroup);

    // --- Columns Playback (the columns' video-only keys) ---
    QGroupBox* playbackGroup = new QGroupBox("Columns Playback");
    QVBoxLayout* playbackLayout = new QVBoxLayout(playbackGroup);
    playbackLayout->setSpacing(4);
    mascotPlaybackGroup = playbackGroup;

    mascotClipSecondsSpinBox = makeSpinBox(0, 3600, 5,
        config->get(QStringLiteral("mascots_clip_seconds")).toInt());
    addRow(playbackLayout, "Clip length (s):",
        "Seconds a video stays on screen before the next swap. 0 swaps on the beat only. Read only "
        "while the columns show videos.", mascotClipSecondsSpinBox);

    mascotCrossfadeSpinBox = makeSpinBox(0, 5000, 100,
        config->get(QStringLiteral("mascots_crossfade_ms")).toInt());
    addRow(playbackLayout, "Crossfade (ms):",
        "Milliseconds a video swap takes to fade in. 0 is a hard cut. Read only while the columns "
        "show videos.", mascotCrossfadeSpinBox);

    mascotSwapOnBeatCheckBox = new QCheckBox("Swap on the music beat");
    mascotSwapOnBeatCheckBox->setChecked(config->get_bool(QStringLiteral("mascots_swap_on_beat")));
    mascotSwapOnBeatCheckBox->setToolTip(
        "Let a beat advance a video before its clip ends, as often as the beat and the random chance "
        "allow. Read only while the columns show videos.");
    playbackLayout->addWidget(mascotSwapOnBeatCheckBox);

    mascotRandomStartCheckBox = new QCheckBox("Start videos at a random position");
    mascotRandomStartCheckBox->setChecked(config->get_bool(QStringLiteral("mascots_random_start")));
    mascotRandomStartCheckBox->setToolTip(
        "Start each video at a random position instead of at its beginning. Read only while the "
        "columns show videos.");
    playbackLayout->addWidget(mascotRandomStartCheckBox);

    mascotPreloadCheckBox = new QCheckBox("Preload the next video");
    mascotPreloadCheckBox->setChecked(config->get_bool(QStringLiteral("mascots_preload")));
    mascotPreloadCheckBox->setToolTip(
        "Open the next clip in the idle player ahead of the swap, so a swap costs a visibility "
        "change instead of a load. Read only while the columns show videos.");
    playbackLayout->addWidget(mascotPreloadCheckBox);
    // (M5: Qt low-latency playback intent. On is what the mascots have always used; off asks for the
    // backend normal buffering, which Qt documents as smoother at the cost of a slower first frame. The
    // intent is fixed for the lifetime of a player, so a change reaches the next player a panel builds.)
    mascotLowLatencyCheckBox = new QCheckBox("Low-latency video playback");
    mascotLowLatencyCheckBox->setChecked(config->get_bool(QStringLiteral("mascots_low_latency")));
    mascotLowLatencyCheckBox->setToolTip(
        "On, the player buffers as little as possible so a mascot clip appears sooner. Qt notes that this "
        "comes with a higher likelihood of lost frames or other glitches during playback; turn it off for "
        "smooth playback at a slower first frame. Applies to the next player a panel builds.");
    playbackLayout->addWidget(mascotLowLatencyCheckBox);

    // (M5 item 1: one control, one clear label. Mirror mode already puts the same media in both columns and
    // flips one against the other; this decides whether they also travel against each other.)
    mascotPairScrollCombo = makeComboBox({ QStringLiteral("Same"), QStringLiteral("Opposite") },
        config->get(QStringLiteral("mascots_pair_scroll")));
    addRow(playbackLayout, "Mirror pair scroll:",
        "In mirror mode, whether the two columns scroll the same way or against each other. Same is what "
        "they have always done; Opposite makes the mirrored pair travel outwards or inwards together.",
        mascotPairScrollCombo);

    // (M5 item 2: the parity switch, and the only one. Mirror mode already puts the same image or clip in
    // both columns; this says whether the right column is that media mirrored or a plain repeat of it. The
    // folder rule keeps deciding which way the pair faces, so this reads the same for images and videos.)
    mascotPairFlipCheckBox = new QCheckBox("Flip the mirrored column");
    // Absent means on: the columns that predate the key were showing the flipped look, and the key is only
    // written when the page is applied. Same answer the controller gives for a config with no line for it.
    const QString pairFlipValue = config->get(QStringLiteral("mascots_pair_flip"));
    mascotPairFlipCheckBox->setChecked(pairFlipValue.isEmpty()
        ? true
        : utils::text_to_bool(pairFlipValue.toStdString()));
    mascotPairFlipCheckBox->setToolTip(
        "On, the right column is the left one mirrored, so the pair reads as two sides of the same picture. "
        "Off, both columns show the identical image or clip. Either way both columns show the same media, and "
        "the left and right folders still decide which way that media faces. Only used while Mirror mascots "
        "is on.");
    playbackLayout->addWidget(mascotPairFlipCheckBox);

    // (M5 Task 9: the label follows the rule the fix gave it. It said "not focused", which is exactly what
    // it must not do any more.)
    mascotPauseUnfocusedCheckBox = new QCheckBox("Pause when the window cannot be seen");
    mascotPauseUnfocusedCheckBox->setChecked(config->get_bool(QStringLiteral("mascots_pause_unfocused")));
    mascotPauseUnfocusedCheckBox->setToolTip(
        "Pause every mascot player while the main window is hidden, minimized, or covered by another "
        "window. Focus does not count: a window that is on screen but not in front keeps its mascots "
        "running. Off keeps them decoding while you are elsewhere.");
    playbackLayout->addWidget(mascotPauseUnfocusedCheckBox);

    mascotMaxDecodersSpinBox = makeSpinBox(0, 64, 1,
        config->get(QStringLiteral("mascots_max_decoders")).toInt());
    addRow(playbackLayout, "Decoder ceiling:",
        "The hard ceiling on open video players. The moving modes spend one player per band (cells "
        "in Scroll), Static spends two per panel for the crossfade, and whatever is left is the "
        "background layer's. 0 means no players at all: images only.", mascotMaxDecodersSpinBox);

    pageLayout->addWidget(playbackGroup);

    // --- Scroll & Stripes (the columns' moving modes) ---
    QGroupBox* stripesGroup = new QGroupBox("Scroll && Stripes");
    QVBoxLayout* stripesLayout = new QVBoxLayout(stripesGroup);
    stripesLayout->setSpacing(4);
    mascotStripesGroup = stripesGroup;

    mascotScrollSpeedSpinBox = makeSpinBox(0, 2000, 5,
        config->get(QStringLiteral("mascots_scroll_speed")).toInt());
    addRow(stripesLayout, "Scroll speed (px/s):",
        "Pixels per second a panning band travels, in Scroll and Stripes alike. 0 stands still.",
        mascotScrollSpeedSpinBox);

    mascotScrollDirectionCombo = makeComboBox({ QStringLiteral("LeftToRight"), QStringLiteral("RightToLeft"),
                                               QStringLiteral("TopToBottom"), QStringLiteral("BottomToTop") },
        config->get(QStringLiteral("mascots_scroll_direction")));
    addRow(stripesLayout, "Direction:",
        "The direction a scrolling band travels. If the axis disagrees with the mode's pan axis, "
        "the band takes the mode's axis and keeps the sense you picked.", mascotScrollDirectionCombo);

    mascotScrollPingpongCheckBox = new QCheckBox("Reverse at the end instead of wrapping");
    mascotScrollPingpongCheckBox->setChecked(config->get_bool(QStringLiteral("mascots_scroll_pingpong")));
    mascotScrollPingpongCheckBox->setToolTip(
        "Ping-pong: reverse at the end of the band instead of wrapping around to its start.");
    stripesLayout->addWidget(mascotScrollPingpongCheckBox);

    mascotStripesLeftSpinBox = makeSpinBox(1, 8, 1,
        config->get(QStringLiteral("mascots_stripes_left")).toInt());
    addRow(stripesLayout, "Stripes, left column:",
        "How many bands the left column stacks in Stripes. The decoder ceiling still decides how "
        "many of them get a player.", mascotStripesLeftSpinBox);

    mascotStripesRightSpinBox = makeSpinBox(1, 8, 1,
        config->get(QStringLiteral("mascots_stripes_right")).toInt());
    addRow(stripesLayout, "Stripes, right column:",
        "How many bands the right column stacks in Stripes. The decoder ceiling still decides how "
        "many of them get a player.", mascotStripesRightSpinBox);

    mascotStripesOrientationCombo = makeComboBox({ QStringLiteral("Horizontal"), QStringLiteral("Vertical") },
        config->get(QStringLiteral("mascots_stripes_orientation")));
    addRow(stripesLayout, "Orientation:",
        "How Stripes are stacked: Horizontal bands are rows, Vertical bands are columns.",
        mascotStripesOrientationCombo);

    mascotStripesAlternateCheckBox = new QCheckBox("Alternate the direction of each stripe");
    mascotStripesAlternateCheckBox->setChecked(config->get_bool(QStringLiteral("mascots_stripes_alternate")));
    mascotStripesAlternateCheckBox->setToolTip(
        "Reverse every other stripe so neighbouring bands travel in opposite directions.");
    stripesLayout->addWidget(mascotStripesAlternateCheckBox);

    pageLayout->addWidget(stripesGroup);

    // --- Background Layer ---
    QGroupBox* backgroundGroup = new QGroupBox("Background Layer");
    QVBoxLayout* backgroundLayout = new QVBoxLayout(backgroundGroup);
    backgroundLayout->setSpacing(4);

    mascotBackgroundCheckBox = new QCheckBox("Enable the background layer");
    mascotBackgroundCheckBox->setChecked(config->get_bool(QStringLiteral("mascots_background")));
    mascotBackgroundCheckBox->setToolTip(
        "Draw a media layer behind the list. It is independent of `mascots`: it runs whether or "
        "not the columns are on, and it always plays videos.");
    backgroundLayout->addWidget(mascotBackgroundCheckBox);

    mascotBackgroundOpacitySpinBox = makeDoubleSpinBox(0.00, 1.00, 0.05,
        config->get_double(QStringLiteral("mascots_background_opacity"), 0.0));
    addRow(backgroundLayout, "Veil opacity:",
        "How much of the window's own background colour is painted over the layer. The layer itself "
        "always runs at full strength; this is one translucent rectangle in front of it, so 1.00 is "
        "the plain window and 0.00 is the media at full strength. It covers the margins and the whole "
        "centre pane; the centre pane then has its own opacity below, which sits on top of this one.",
        mascotBackgroundOpacitySpinBox);

    mascotListAlphaSpinBox = makeDoubleSpinBox(0.00, 1.00, 0.05,
        config->get_double(QStringLiteral("mascots_background_list_alpha"), 0.85));
    addRow(backgroundLayout, "List & strip opacity:",
        "Opacity of the centre pane over the veil: the list's even rows and the strip above them, one "
        "alpha for both, so the top matches the list. 1.00 is an opaque centre pane, 0.00 lets whatever "
        "the veil leaves reach the text at full strength. The alternate rows have their own alpha "
        "below; the veil above is the whole window.",
        mascotListAlphaSpinBox);

    // Falls back to the list alpha when the key is absent, which is what a config from before this
    // control existed gets: the list looks exactly as it did.
    mascotAltAlphaSpinBox = makeDoubleSpinBox(0.00, 1.00, 0.05,
        config->get_double(QStringLiteral("mascots_background_alternate_alpha"),
            config->get_double(QStringLiteral("mascots_background_list_alpha"), 0.85)));
    addRow(backgroundLayout, "Alternate row opacity:",
        "Opacity of the alternate rows only -- the row colour that does not match the window's "
        "background. Lower it to see more of the layer through every other row, raise it for more "
        "contrast behind the text. Equal to the line above is an even list.",
        mascotAltAlphaSpinBox);

    // Opaque unless it is asked for: a scrollbar that fades over a bright frame is harder to grab, so
    // the default is the look it has always had.
    mascotScrollbarAlphaSpinBox = makeDoubleSpinBox(0.00, 1.00, 0.05,
        config->get_double(QStringLiteral("mascots_background_scrollbar_alpha"), 1.0));
    addRow(backgroundLayout, "Scrollbar opacity:",
        "Opacity of the list's scrollbars over the layer. 1.00 keeps them solid, which is what they "
        "have always been; lower it if the solid bar draws the eye. The arrows and the handle stay "
        "readable at any value.",
        mascotScrollbarAlphaSpinBox);

    // (M5 fix, user report: the row said "Columns:", which reads as if it were about layout columns rather
    // than about the mascots' relation to the veil. The values carry the meaning now, and both the old
    // spellings (Above/Below) and the new ones are accepted, so an existing config keeps its setting.)
    const QString storedZ = config->get(QStringLiteral("mascots_columns_z"));
    const bool storedBelow = storedZ.compare(QStringLiteral("Below"), Qt::CaseInsensitive) == 0
        || storedZ.compare(QStringLiteral("Below the veil"), Qt::CaseInsensitive) == 0;
    mascotColumnsZCombo = makeComboBox(
        { QStringLiteral("Above the veil"), QStringLiteral("Below the veil") },
        storedBelow ? QStringLiteral("Below the veil") : QStringLiteral("Above the veil"));
    addRow(backgroundLayout, "Mascots layer:",
        "Whether the two mascot columns are drawn above the veil -- full brightness, the default -- or "
        "behind it, so the veil covers them exactly as it covers the layer: at the layer opacity they "
        "are dimmed to the window colour by that amount, and at 1.00 they are hidden with everything "
        "else behind the list.",
        mascotColumnsZCombo);

    mascotBackgroundModeCombo = makeComboBox({ QStringLiteral("Static"), QStringLiteral("Scroll"),
                                              QStringLiteral("Stripes") },
        config->get(QStringLiteral("mascots_background_mode")));
    addRow(backgroundLayout, "Layer mode:",
        "How the layer moves: Static is one full-window picture, Scroll one marquee, Stripes "
        "several moving bands. The same three shapes the columns have.",
        mascotBackgroundModeCombo);

    mascotBackgroundFillCombo = makeComboBox({ QStringLiteral("Cover"), QStringLiteral("Crop"), QStringLiteral("Fit"),
                                              QStringLiteral("Stretch"), QStringLiteral("HeightFit"), QStringLiteral("WidthFit") },
        config->get(QStringLiteral("mascots_background_fill")));
    addRow(backgroundLayout, "Layer fill:",
        "How a layer video is fitted into the window: Cover always fills the window completely, scaling "
        "a clip up or down to the window's size and cutting off the overflow; Crop fills and cuts off "
        "but lets the centring slide; Fit shows all of it with bars, Stretch distorts, HeightFit and "
        "WidthFit scale on one axis. Stripes bands are normalised to their stacking axis instead.",
        mascotBackgroundFillCombo);

    mascotBackgroundStripesSpinBox = makeSpinBox(1, 8, 1,
        config->get(QStringLiteral("mascots_background_stripes")).toInt());
    addRow(backgroundLayout, "Layer stripes:",
        "Bands in Stripes -- and the marquee's cell count in Scroll: one cell is the wrap buffer, so "
        "Scroll shows stripes minus one cells. Keep it enabled in both moving modes.",
        mascotBackgroundStripesSpinBox);

    mascotBackgroundOrientationCombo = makeComboBox({ QStringLiteral("Horizontal"), QStringLiteral("Vertical") },
        config->get(QStringLiteral("mascots_background_stripes_orientation")));
    addRow(backgroundLayout, "Layer orientation:",
        "How the layer's Stripes are stacked: Horizontal rows or Vertical columns.",
        mascotBackgroundOrientationCombo);

    mascotBackgroundAlternateCheckBox = new QCheckBox("Alternate the direction of each layer stripe");
    mascotBackgroundAlternateCheckBox->setChecked(
        config->get_bool(QStringLiteral("mascots_background_stripes_alternate")));
    mascotBackgroundAlternateCheckBox->setToolTip(
        "Reverse every other layer stripe so neighbouring bands travel in opposite directions.");
    backgroundLayout->addWidget(mascotBackgroundAlternateCheckBox);

    mascotBackgroundScrollSpeedSpinBox = makeSpinBox(0, 2000, 5,
        config->get(QStringLiteral("mascots_background_scroll_speed")).toInt());
    addRow(backgroundLayout, "Layer scroll speed (px/s):",
        "Pixels per second the layer's bands travel in Scroll. 0 stands still.",
        mascotBackgroundScrollSpeedSpinBox);

    mascotBackgroundScrollDirectionCombo = makeComboBox({ QStringLiteral("LeftToRight"),
                                                         QStringLiteral("RightToLeft"),
                                                         QStringLiteral("TopToBottom"),
                                                         QStringLiteral("BottomToTop") },
        config->get(QStringLiteral("mascots_background_scroll_direction")));
    addRow(backgroundLayout, "Layer direction:",
        "The direction the layer's bands travel. If the axis disagrees with the mode's pan axis, "
        "the band takes the mode's axis and keeps the sense you picked.",
        mascotBackgroundScrollDirectionCombo);

    mascotBackgroundScrollPingpongCheckBox = new QCheckBox("Reverse the layer at the end instead of wrapping");
    mascotBackgroundScrollPingpongCheckBox->setChecked(
        config->get_bool(QStringLiteral("mascots_background_scroll_pingpong")));
    mascotBackgroundScrollPingpongCheckBox->setToolTip(
        "Ping-pong: reverse at the end of a layer band instead of wrapping around to its start.");
    backgroundLayout->addWidget(mascotBackgroundScrollPingpongCheckBox);

    // (M5 Task 10: the layer's own cadence. Until now the layer borrowed `mascots_clip_seconds`,
    // `mascots_swap_on_beat` and the image dice from the columns; these four rows are its own, and each
    // one starts from the columns' value so a config that never touches them behaves as before.)
    // (M5 review fix: "starts from the columns' value" is over -- on the user's call the four keys are
    // ordinary settings with defaults in config.cpp, so each row starts from its own key and falls back to
    // that documented default (the columns' own default) when the file predates the key.)
    const auto mascotIntDefault = [config](const QString& key, int fallback) {
        bool ok = false;
        const int parsed = config->get(key).toInt(&ok);
        return ok ? parsed : fallback;
    };
    const auto mascotBoolDefault = [config](const QString& key, bool fallback) {
        const QString value = config->get(key);
        return value.isEmpty() ? fallback : utils::text_to_bool(value.toStdString());
    };
    mascotBackgroundClipSpinBox = makeSpinBox(0, 3600, 5,
        mascotIntDefault(QStringLiteral("mascots_background_clip_seconds"), 30));
    addRow(backgroundLayout, "Layer clip length (s):",
        "How long one layer clip stays before the layer's own timer advances it -- independent of the "
        "columns' Clip length. 0 disables it, so only a swap beat moves the layer.",
        mascotBackgroundClipSpinBox);

    mascotBackgroundSwapOnBeatCheckBox = new QCheckBox("Let a music beat swap the layer");
    mascotBackgroundSwapOnBeatCheckBox->setChecked(
        mascotBoolDefault(QStringLiteral("mascots_background_swap_on_beat"), true));
    mascotBackgroundSwapOnBeatCheckBox->setToolTip(
        "Whether the layer may advance on a beat at all. Independent of the columns' Swap on the music "
        "beat: the layer can swap on beats the columns flip on.");
    backgroundLayout->addWidget(mascotBackgroundSwapOnBeatCheckBox);

    mascotBackgroundRandomChangeCheckBox = new QCheckBox("Random layer change (Music Beat)");
    mascotBackgroundRandomChangeCheckBox->setChecked(
        mascotBoolDefault(QStringLiteral("mascots_background_random_change"), true));
    mascotBackgroundRandomChangeCheckBox->setToolTip(
        "Whether the layer rolls for a change at all. Off means the beat never moves it and only the "
        "layer clip length does. 100% means every beat.");
    mascotBackgroundChanceSpinBox = makeSpinBox(0, 100, 1,
        mascotIntDefault(QStringLiteral("mascots_background_random_chance"), 10));
    // (M5 fix: one row, the way the Mascots page shows the same pair -- the checkbox on the left, its
    // chance right-aligned on the same line. It was a checkbox followed by its own labelled row, which read
    // as two unrelated controls.)
    QHBoxLayout* layerDiceRow = new QHBoxLayout();
    layerDiceRow->addWidget(mascotBackgroundRandomChangeCheckBox);
    layerDiceRow->addStretch();
    QLabel* layerChanceLabel = new QLabel("Chance:");
    layerChanceLabel->setToolTip(
        "The layer's own dice: the chance per beat that it changes, rolled separately from the columns' "
        "chance so one can be quiet while the other moves. 100% means every beat.");
    mascotBackgroundChanceSpinBox->setToolTip(layerChanceLabel->toolTip());
    layerDiceRow->addWidget(layerChanceLabel);
    layerDiceRow->addWidget(mascotBackgroundChanceSpinBox);
    backgroundLayout->addLayout(layerDiceRow);

    // (M5 Task 9: the layer group is appearance, not behaviour -- it decides how the window looks and
    // nothing about what runs -- so it is built here and added to the Appearance page, above that page's
    // trailing spacer. One tab for "make it look different", one for "make it behave differently": the
    // group is built here because its controls are wired into this page's one writer and one matrix, and
    // reparenting the built box is the smaller change than moving twelve control definitions.)
    // (M5 review amendment: "nothing about what runs" stopped being true when the layer got its own cadence.
    // Clip length, beat swap, random change and chance are behaviour, and they live in this group because
    // the layer does -- keeping the layer's controls together matters more to a user looking for them than
    // the appearance/behaviour split does. The sentence above is kept because it is why the group is here at
    // all; the four cadence rows are the deliberate exception.)
    this->ui.appearancePageLayout->insertWidget(
        this->ui.appearancePageLayout->count() - 1, backgroundGroup);

    // --- Image Cache ---
    QGroupBox* cacheGroup = new QGroupBox("Image Cache");
    QVBoxLayout* cacheLayout = new QVBoxLayout(cacheGroup);
    cacheLayout->setSpacing(4);

    mascotCacheMbSpinBox = makeSpinBox(8, 4096, 1,
        config->get(QStringLiteral("mascots_image_cache_mb")).toInt());
    addRow(cacheLayout, "Cache budget (MB):",
        "Byte budget for decoded mascot images. The cache evicts on whichever bound trips first, "
        "bytes or items.", mascotCacheMbSpinBox);

    mascotCacheItemsSpinBox = makeSpinBox(1, 256, 1,
        config->get(QStringLiteral("mascots_image_cache_max_items")).toInt());
    addRow(cacheLayout, "Cache entries:",
        "Maximum number of decoded mascot images kept. The cache evicts on whichever bound trips "
        "first, bytes or items.", mascotCacheItemsSpinBox);

    pageLayout->addWidget(cacheGroup);

    pageLayout->addStretch();
    scrollArea->setWidget(pageContent);

    // Index 3 is the slot right after Appearance, the page that used to own the group box above.
    this->ui.settingsStack->insertWidget(3, scrollArea);
    this->ui.categoryList->insertItem(3, "Mascots");

    // M5 Task 3: one writer and one applier. Every control on the page feeds the same writer, so a
    // change cannot write its key and skip the apply, and the matrix is recomputed from the same
    // widget state the apply reads.
    // (M5 review fix I7: each control also names the key it owns, so the *apply* can be scoped to what
    // changed. The writer still writes every key -- that is a few string assignments and it keeps OK/Apply
    // and Cancel's baseline simple -- but a change to "Cache entries" no longer repaints the window and
    // restarts every clip timer, and a change to the veil no longer re-presents three panels.)
    const auto live = [this](QWidget* widget, const QString& key) {
        const QString owned = key;
        if (auto* combo = qobject_cast<QComboBox*>(widget))
            connect(combo, &QComboBox::currentIndexChanged, this,
                [this, owned] { this->syncMascotConfig(owned); });
        else if (auto* checkBox = qobject_cast<QCheckBox*>(widget))
            connect(checkBox, &QCheckBox::toggled, this,
                [this, owned] { this->syncMascotConfig(owned); });
        else if (auto* spinBox = qobject_cast<QSpinBox*>(widget))
            connect(spinBox, &QSpinBox::valueChanged, this,
                [this, owned] { this->syncMascotConfig(owned); });
        else if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget))
            connect(doubleSpinBox, &QDoubleSpinBox::valueChanged, this,
                [this, owned] { this->syncMascotConfig(owned); });
    };
    live(mascotMediaCombo, QStringLiteral("mascots_media"));
    live(mascotModeCombo, QStringLiteral("mascots_mode"));
    live(mascotFillCombo, QStringLiteral("mascots_fill"));
    live(mascotWeightedCheckBox, QStringLiteral("mascots_weighted_random"));
    live(mascotClipSecondsSpinBox, QStringLiteral("mascots_clip_seconds"));
    live(mascotCrossfadeSpinBox, QStringLiteral("mascots_crossfade_ms"));
    live(mascotSwapOnBeatCheckBox, QStringLiteral("mascots_swap_on_beat"));
    live(mascotRandomStartCheckBox, QStringLiteral("mascots_random_start"));
    live(mascotPreloadCheckBox, QStringLiteral("mascots_preload"));
    live(mascotPauseUnfocusedCheckBox, QStringLiteral("mascots_pause_unfocused"));
    live(mascotLowLatencyCheckBox, QStringLiteral("mascots_low_latency"));
    live(mascotPairScrollCombo, QStringLiteral("mascots_pair_scroll"));
    live(mascotPairFlipCheckBox, QStringLiteral("mascots_pair_flip"));
    live(mascotMaxDecodersSpinBox, QStringLiteral("mascots_max_decoders"));
    live(mascotScrollSpeedSpinBox, QStringLiteral("mascots_scroll_speed"));
    live(mascotScrollDirectionCombo, QStringLiteral("mascots_scroll_direction"));
    live(mascotScrollPingpongCheckBox, QStringLiteral("mascots_scroll_pingpong"));
    live(mascotStripesLeftSpinBox, QStringLiteral("mascots_stripes_left"));
    live(mascotStripesRightSpinBox, QStringLiteral("mascots_stripes_right"));
    live(mascotStripesOrientationCombo, QStringLiteral("mascots_stripes_orientation"));
    live(mascotStripesAlternateCheckBox, QStringLiteral("mascots_stripes_alternate"));
    live(mascotBackgroundCheckBox, QStringLiteral("mascots_background"));
    live(mascotBackgroundOpacitySpinBox, QStringLiteral("mascots_background_opacity"));
    live(mascotColumnsZCombo, QStringLiteral("mascots_columns_z"));
    live(mascotListAlphaSpinBox, QStringLiteral("mascots_background_list_alpha"));
    live(mascotAltAlphaSpinBox, QStringLiteral("mascots_background_alternate_alpha"));
    live(mascotScrollbarAlphaSpinBox, QStringLiteral("mascots_background_scrollbar_alpha"));
    live(mascotBackgroundModeCombo, QStringLiteral("mascots_background_mode"));
    live(mascotBackgroundFillCombo, QStringLiteral("mascots_background_fill"));
    live(mascotBackgroundStripesSpinBox, QStringLiteral("mascots_background_stripes"));
    live(mascotBackgroundOrientationCombo, QStringLiteral("mascots_background_stripes_orientation"));
    live(mascotBackgroundAlternateCheckBox, QStringLiteral("mascots_background_stripes_alternate"));
    live(mascotBackgroundScrollSpeedSpinBox, QStringLiteral("mascots_background_scroll_speed"));
    live(mascotBackgroundScrollDirectionCombo, QStringLiteral("mascots_background_scroll_direction"));
    live(mascotBackgroundClipSpinBox, QStringLiteral("mascots_background_clip_seconds"));
    live(mascotBackgroundSwapOnBeatCheckBox, QStringLiteral("mascots_background_swap_on_beat"));
    live(mascotBackgroundRandomChangeCheckBox, QStringLiteral("mascots_background_random_change"));
    live(mascotBackgroundChanceSpinBox, QStringLiteral("mascots_background_random_chance"));
    live(mascotBackgroundScrollPingpongCheckBox, QStringLiteral("mascots_background_scroll_pingpong"));
    live(mascotCacheMbSpinBox, QStringLiteral("mascots_image_cache_mb"));
    live(mascotCacheItemsSpinBox, QStringLiteral("mascots_image_cache_max_items"));
    // (M5 review fix: the legacy rows, in the same live path, each with its own key. Their runtime side
    // effects are MainWindow::applyLiveMascotRows()'s; the config writes are at the top of
    // syncMascotConfig().)
    live(this->ui.mascotsOnOff, QStringLiteral("mascots"));
    live(this->ui.mascotsAnimated, QStringLiteral("mascots_animated"));
    live(this->ui.mascotsRandomChange, QStringLiteral("mascots_random_change"));
    live(this->ui.mascotsChanceSpinBox, QStringLiteral("mascots_random_chance"));
    live(this->ui.mascotsFreqSpinBox, QStringLiteral("mascots_frequency"));
    live(this->ui.mascotsWidthSpinBox, QStringLiteral("mascots_width_percent"));
    live(this->ui.mascotsExtractColor, QStringLiteral("mascots_color_theme"));
    live(this->ui.mascotsCenterContent, QStringLiteral("mascots_center_content"));
    live(this->ui.mascotsMirror, QStringLiteral("mascots_mirror"));
    // The master switch is not one of the page's own keys, but row 1 of the matrix is about it: the
    // groups have to follow the checkbox at once, not on the next Apply.
    connect(this->ui.mascotsOnOff, &QCheckBox::toggled, this, [this] { updateMascotControlStates(); });

    updateMascotControlStates();
}

void SettingsDialog::syncMascotConfig(const QString& changedKey)
{
    MainWindow* mw = static_cast<MainWindow*>(this->parent());
    Config* config = mw->App->config;

    // One write per key, and the combos write their exact item text: the controller compares those
    // strings case-insensitively and warns about anything else, so no other spelling may leave here.
    config->set("mascots_media", mascotMediaCombo->currentText());
    config->set("mascots_mode", mascotModeCombo->currentText());
    config->set("mascots_fill", mascotFillCombo->currentText());
    config->set("mascots_weighted_random", mascotWeightedCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_clip_seconds", QString::number(mascotClipSecondsSpinBox->value()));
    config->set("mascots_crossfade_ms", QString::number(mascotCrossfadeSpinBox->value()));
    config->set("mascots_swap_on_beat", mascotSwapOnBeatCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_random_start", mascotRandomStartCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_preload", mascotPreloadCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_pause_unfocused", mascotPauseUnfocusedCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_low_latency", mascotLowLatencyCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_pair_scroll", mascotPairScrollCombo->currentText());
    config->set("mascots_pair_flip", mascotPairFlipCheckBox->isChecked() ? "True" : "False");
    // (M5 review fix C1: Mirror is written here now, and the checkbox is part of the live wiring below.
    // It was one of the legacy rows that only took effect on Apply, which made the two pair controls that
    // depend on it live while the switch itself was not -- and it also meant the controller could not see
    // the 0->1 transition it needs in order to re-pair the columns.)
    config->set("mascots_mirror", this->ui.mascotsMirror->isChecked() ? "True" : "False");
    // (M5 review fix: the legacy rows on this page -- master switch, Animated, the dice, frequency, width,
    // colour theme, centre -- are written here too, because they are on the same page as the controls that
    // were already live and the user asked for one rule: a control that decides something on screen takes
    // effect when it is clicked. Their runtime half is MainWindow::applyLiveMascotRows(), called at the end
    // of this function; the two that are *not* here -- All files random and Use seed -- change which pool is
    // built, not a live property, so they stay with applySettings() on Apply.)
    config->set("mascots", this->ui.mascotsOnOff->isChecked() ? "True" : "False");
    config->set("mascots_animated", this->ui.mascotsAnimated->isChecked() ? "True" : "False");
    config->set("mascots_random_change", this->ui.mascotsRandomChange->isChecked() ? "True" : "False");
    config->set("mascots_random_chance", QString::number(this->ui.mascotsChanceSpinBox->value()));
    config->set("mascots_frequency", QString::number(this->ui.mascotsFreqSpinBox->value()));
    config->set("mascots_width_percent", QString::number(this->ui.mascotsWidthSpinBox->value()));
    config->set("mascots_color_theme", this->ui.mascotsExtractColor->isChecked() ? "True" : "False");
    config->set("mascots_center_content", this->ui.mascotsCenterContent->isChecked() ? "True" : "False");
    config->set("mascots_max_decoders", QString::number(mascotMaxDecodersSpinBox->value()));
    config->set("mascots_scroll_speed", QString::number(mascotScrollSpeedSpinBox->value()));
    config->set("mascots_scroll_direction", mascotScrollDirectionCombo->currentText());
    config->set("mascots_scroll_pingpong", mascotScrollPingpongCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_stripes_left", QString::number(mascotStripesLeftSpinBox->value()));
    config->set("mascots_stripes_right", QString::number(mascotStripesRightSpinBox->value()));
    config->set("mascots_stripes_orientation", mascotStripesOrientationCombo->currentText());
    config->set("mascots_stripes_alternate", mascotStripesAlternateCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_background", mascotBackgroundCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_background_opacity", QString::number(mascotBackgroundOpacitySpinBox->value(), 'f', 2));
    config->set("mascots_columns_z", mascotColumnsZCombo->currentText());
    config->set("mascots_background_list_alpha", QString::number(mascotListAlphaSpinBox->value(), 'f', 2));
    config->set("mascots_background_alternate_alpha", QString::number(mascotAltAlphaSpinBox->value(), 'f', 2));
    config->set("mascots_background_scrollbar_alpha", QString::number(mascotScrollbarAlphaSpinBox->value(), 'f', 2));
    config->set("mascots_background_mode", mascotBackgroundModeCombo->currentText());
    config->set("mascots_background_fill", mascotBackgroundFillCombo->currentText());
    config->set("mascots_background_stripes", QString::number(mascotBackgroundStripesSpinBox->value()));
    config->set("mascots_background_stripes_orientation", mascotBackgroundOrientationCombo->currentText());
    config->set("mascots_background_stripes_alternate",
        mascotBackgroundAlternateCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_background_scroll_speed", QString::number(mascotBackgroundScrollSpeedSpinBox->value()));
    config->set("mascots_background_scroll_direction", mascotBackgroundScrollDirectionCombo->currentText());
    config->set("mascots_background_clip_seconds", QString::number(mascotBackgroundClipSpinBox->value()));
    config->set("mascots_background_swap_on_beat", mascotBackgroundSwapOnBeatCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_background_random_change", mascotBackgroundRandomChangeCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_background_random_chance", QString::number(mascotBackgroundChanceSpinBox->value()));
    config->set("mascots_background_scroll_pingpong",
        mascotBackgroundScrollPingpongCheckBox->isChecked() ? "True" : "False");
    config->set("mascots_image_cache_mb", QString::number(mascotCacheMbSpinBox->value()));
    config->set("mascots_image_cache_max_items", QString::number(mascotCacheItemsSpinBox->value()));

    updateMascotControlStates();
    // (M5 review fix I7: a live change applies only what it can have affected -- `applyMascotKey()` reads
    // the key. The empty-key form is the Apply/OK/Cancel baseline and still runs the whole path, because
    // those are the moments everything is expected to be consistent. The legacy rows' runtime half is part
    // of both: `applyMascotKey()` reaches it through applyLiveMascotRows() for the rows that need it.)
    if (changedKey.isEmpty())
        mw->applyLiveMascotRows();
    else
        mw->applyMascotKey(changedKey);
}

void SettingsDialog::restoreMascotConfig()
{
    MainWindow* mw = static_cast<MainWindow*>(this->parent());
    Config* config = mw->App->config;
    for (auto it = mascotConfigSnapshot.constBegin(); it != mascotConfigSnapshot.constEnd(); ++it)
        config->set(it.key(), it.value());
    // (M5 review fix: Cancel restores the legacy rows' keys too, so their runtime half has to follow the
    // restore -- an engine started by a live Animated tick would otherwise keep beating after Cancel put
    // the key back to False.)
    mw->applyLiveMascotRows();
    updateMascotControlStates();
}

void SettingsDialog::rebaseMascotConfigSnapshot()
{
    // (M5 review fix: Apply is a new baseline. Without this, Apply followed by Cancel wrote the
    // dialog-opening values back over the ones Apply had just kept -- Apply would have been the only
    // button in this dialog whose effect a later Cancel undid. The legacy controls do not have the
    // problem because applySettings() refreshes their `old_*` members as it writes.)
    Config* config = static_cast<MainWindow*>(this->parent())->App->config;
    for (auto it = mascotConfigSnapshot.begin(); it != mascotConfigSnapshot.end(); ++it)
        it.value() = config->get(it.key());
}

void SettingsDialog::updateMascotControlStates()
{
    const bool columnsOn = this->ui.mascotsOnOff->isChecked();
    // Row 1: the columns' three groups are one switch; the legacy group box, the layer and the
    // cache describe sources or presentation and stay usable while the columns are off.
    mascotContentGroup->setEnabled(columnsOn);
    mascotPlaybackGroup->setEnabled(columnsOn);
    mascotStripesGroup->setEnabled(columnsOn);

    // Row 2: Images never opens a player, so the five video-only keys are dead until Videos/Mixed;
    // the ceiling and the pause keep their meaning for images and the background.
    const bool videoColumns = mascotMediaCombo->currentText() != QLatin1String("Images");
    mascotClipSecondsSpinBox->setEnabled(videoColumns);
    mascotCrossfadeSpinBox->setEnabled(videoColumns);
    mascotSwapOnBeatCheckBox->setEnabled(videoColumns);
    mascotRandomStartCheckBox->setEnabled(videoColumns);
    mascotPreloadCheckBox->setEnabled(videoColumns);
    mascotMaxDecodersSpinBox->setEnabled(true);
    mascotPauseUnfocusedCheckBox->setEnabled(true);
    mascotLowLatencyCheckBox->setEnabled(true);
    mascotPairScrollCombo->setEnabled(true);
    // (M5 item 2: the parity switch keeps its meaning for images and videos alike -- it is about the pair,
    // not about a player -- so it never goes dark.)
    mascotPairFlipCheckBox->setEnabled(true);


    // Rows 3-5: Scroll and Stripes share this group, so the rows go dark individually instead of
    // the whole box: each mode owns its half and neither owns the other's.
    const QString mode = mascotModeCombo->currentText();
    const bool scrollColumns = mode == QLatin1String("Scroll");
    const bool stripesColumns = mode == QLatin1String("Stripes");
    // Speed, direction and ping-pong are the pan's own rows and both panning modes read them (the controller's
    // scroll tick takes them from the panel's layout whatever the mode); only the rows that name one mode's shape --
    // marquee cells here, bands there -- belong to that mode alone.
    const bool panningColumns = scrollColumns || stripesColumns;
    mascotScrollSpeedSpinBox->setEnabled(panningColumns);
    mascotScrollDirectionCombo->setEnabled(panningColumns);
    mascotScrollPingpongCheckBox->setEnabled(panningColumns);
    mascotStripesLeftSpinBox->setEnabled(stripesColumns);
    mascotStripesRightSpinBox->setEnabled(stripesColumns);
    mascotStripesOrientationCombo->setEnabled(stripesColumns);
    mascotStripesAlternateCheckBox->setEnabled(stripesColumns);

    // Row 6: with the layer off nothing in its box has an effect -- except the switch itself.
    const bool backgroundOn = mascotBackgroundCheckBox->isChecked();
    mascotBackgroundOpacitySpinBox->setEnabled(backgroundOn);
    mascotListAlphaSpinBox->setEnabled(backgroundOn);
    mascotAltAlphaSpinBox->setEnabled(backgroundOn);
    mascotScrollbarAlphaSpinBox->setEnabled(backgroundOn);
    mascotColumnsZCombo->setEnabled(backgroundOn);
    mascotBackgroundModeCombo->setEnabled(backgroundOn);
    mascotBackgroundFillCombo->setEnabled(backgroundOn);

    // Rows 7-9: the layer's Static has neither a pan nor stripes, Scroll owns the scroll rows and
    // the marquee's cell count, Stripes owns the stripes rows -- the same per-row split as above.
    const QString backgroundMode = mascotBackgroundModeCombo->currentText();
    const bool scrollLayer = backgroundMode == QLatin1String("Scroll");
    const bool stripesLayer = backgroundMode == QLatin1String("Stripes");
    mascotBackgroundStripesSpinBox->setEnabled(backgroundOn && (scrollLayer || stripesLayer));
    mascotBackgroundOrientationCombo->setEnabled(backgroundOn && stripesLayer);
    mascotBackgroundAlternateCheckBox->setEnabled(backgroundOn && stripesLayer);
    mascotBackgroundScrollSpeedSpinBox->setEnabled(backgroundOn && scrollLayer);
    mascotBackgroundScrollDirectionCombo->setEnabled(backgroundOn && scrollLayer);
    mascotBackgroundScrollPingpongCheckBox->setEnabled(backgroundOn && scrollLayer);
    mascotBackgroundClipSpinBox->setEnabled(backgroundOn);
    mascotBackgroundSwapOnBeatCheckBox->setEnabled(backgroundOn);
    mascotBackgroundRandomChangeCheckBox->setEnabled(backgroundOn);
    mascotBackgroundChanceSpinBox->setEnabled(backgroundOn);
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
    // M5 Task 2: after Appearance, because the page takes over its mascots group box, and before
    // wireApplyEnable(), so the page's controls are picked up by the generic wiring.
    setupMascotsPage(mw);
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
