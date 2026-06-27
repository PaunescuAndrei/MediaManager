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
#include <QSignalBlocker>
#include "flowlayout.h"
#include <QCheckBox>
#include <QListWidget>

// ---------------------------------------------------------------------------
// Shared style constants
// ---------------------------------------------------------------------------

static const char* kColorBtnStyle =
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
    btn->setStyleSheet(QString(kColorBtnStyle)
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

    // --- Notification Duration ---
    setupSpinStyle(this->ui.notificationDurationSpinBox, "spinbox");
    this->ui.notificationDurationSpinBox->setValue(config->get("notification_duration_ms").toInt());

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
    int dailyVid = qBound(0, config->get("daily_video_goal").toInt(),
                          this->ui.dailyVideoGoalSpinBox->maximum());
    int dailyTime = qBound(0, config->get("daily_time_goal_minutes").toInt(),
                           this->ui.dailyTimeGoalSpinBox->maximum());
    this->ui.dailyVideoGoalSpinBox->setValue(dailyVid);
    this->ui.dailyTimeGoalSpinBox->setValue(dailyTime);

    // --- Tooltip Settings ---
    setupCheckBox(this->ui.tooltipsEnabled, "tooltips_enabled", mw);
    setupSpinStyle(this->ui.tooltipDelaySpinBox, "spinbox");
    this->ui.tooltipDelaySpinBox->setValue(config->get("tooltip_delay_ms").toInt());
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
}

void SettingsDialog::setupAppearancePage(MainWindow* mw)
{
    Config* config = mw->App->config;

    // --- Icon Settings ---
    setupCheckBox(this->ui.AnimatedIcons, "animated_icon_flag", mw);
    setupCheckBox(this->ui.randomIcon, "random_icon", mw);
    setupCheckBox(this->ui.defaultIconNotWatching, "default_icon_not_watching", mw);

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

    // Wire Apply button for all changeable widgets + wheel filters
    wireApplyEnable();
    WheelStrongFocusEventFilter* wheelFilter = new WheelStrongFocusEventFilter(this);
    installWheelFilters(wheelFilter);
    this->ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
}

SettingsDialog::~SettingsDialog()
{
}
