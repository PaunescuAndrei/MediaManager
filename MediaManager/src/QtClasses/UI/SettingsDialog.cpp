#include "stdafx.h"
#include "SettingsDialog.h"
#include "MainWindow.h"
#include "MainApp.h"
#include "config.h"
#include <QPushButton>
#include "utils.h"
#include <QTreeWidget>
#include "stylesQt.h"
#include <QMap>
#include <QSpinBox>
#include <QSignalBlocker>
#include "definitions.h"

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
	this->ui.setupUi(this);
	this->setWindowModality(Qt::WindowModal);
	this->ui.SVspinBox->setStyleSheet(get_stylesheet("spinbox"));
	this->ui.mascotsChanceSpinBox->setStyleSheet(get_stylesheet("spinbox"));
	this->ui.volumeSpinBox->setStyleSheet(get_stylesheet("spinbox"));
	this->ui.mascotsFreqSpinBox->setStyleSheet(get_stylesheet("spinbox"));
	this->ui.soundEffectsVolume->setStyleSheet(get_stylesheet("spinbox"));
	this->ui.specialSoundEffectsVolume->setStyleSheet(get_stylesheet("spinbox"));
	this->ui.soundEffectsChance->setStyleSheet(get_stylesheet("spinbox"));
	this->ui.soundEffectsChainChance->setStyleSheet(get_stylesheet("spinbox"));
	this->ui.MinutesSpinBox->setStyleSheet(get_stylesheet("spinbox"));
	this->ui.SecondsSpinBox->setStyleSheet(get_stylesheet("spinbox"));
	this->ui.aicon_fps_modifier_spinBox->setStyleSheet(get_stylesheet("doublespinbox"));
    this->ui.searchTimerInterval->setStyleSheet(get_stylesheet("spinbox"));
	this->ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);

	MainWindow* mw = (MainWindow*)parent;
	this->ui.plusCatRadioBtn->setText(mw->App->config->get("plus_category_name"));
	this->ui.minusCatRadioBtn->setText(mw->App->config->get("minus_category_name"));
	connect(this->ui.resetBtn, &QPushButton::clicked, this, [mw,this] {
		mw->resetDBDialogButton(this); 
		QSignalBlocker blocker(this->ui.SVspinBox);
		this->ui.SVspinBox->setValue(mw->sv_target_count);
		this->oldSVmax = this->ui.SVspinBox->value();
		blocker.unblock();
	});
	connect(this->ui.loadBtn, &QPushButton::clicked, this, [mw,this] {
		mw->loadDB(this);
		QSignalBlocker blocker(this->ui.SVspinBox);
		this->ui.SVspinBox->setValue(mw->sv_target_count);
		this->oldSVmax = this->ui.SVspinBox->value();
		blocker.unblock();
	});
	connect(this->ui.backupBtn, &QPushButton::clicked, this, [mw, this] {mw->backupDB(this); });
	connect(this->ui.resetWatchedButton, &QPushButton::clicked, this, [mw, this] {
		mw->resetWatchedDB(this); 
		QSignalBlocker blocker(this->ui.SVspinBox);
		this->ui.SVspinBox->setValue(mw->sv_target_count);
		this->oldSVmax = this->ui.SVspinBox->value();
		blocker.unblock();
	});
	connect(this->ui.cacheIconButton, &QPushButton::clicked, this, [mw, this] {mw->animatedIcon->rebuildIconCacheNonBlocking(); });
	connect(this->ui.cacheBeatsButton, &QPushButton::clicked, this, [mw, this] {mw->App->MascotsAnimation->rebuildBeatsCacheNonBlocking(); });
	connect(this->ui.cacheThumbsButton, &QPushButton::clicked, this, [mw, this] {mw->thumbnailManager->rebuildThumbnailCache(mw->App->db->db,true); });
	connect(this->ui.cacheThumbsButton, &customQButton::rightClicked, this, [mw, this] {mw->thumbnailManager->rebuildThumbnailCache(mw->App->db->db, false); });
	if (mw->App->config->get_bool("music_on"))
		this->ui.musicOnOff->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.musicOnOff->setCheckState(Qt::CheckState::Unchecked);
	this->ui.volumeSpinBox->setValue(mw->App->config->get("music_volume").toInt());
	this->oldVolume = this->ui.volumeSpinBox->value();
	connect(this->ui.volumeSpinBox, &QSpinBox::valueChanged, [mw,this] { if(mw->App->musicPlayer) mw->App->musicPlayer->setVolume(this->ui.volumeSpinBox->value()); });
	if (mw->App->config->get_bool("music_random_start"))
		this->ui.musicRandomStart->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.musicRandomStart->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("music_loop_first"))
		this->ui.musicLoopFirst->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.musicLoopFirst->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("animated_icon_flag"))
		this->ui.AnimatedIcons->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.AnimatedIcons->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("random_icon"))
		this->ui.randomIcon->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.randomIcon->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("default_icon_not_watching"))
		this->ui.defaultIconNotWatching->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.defaultIconNotWatching->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("mascots"))
		this->ui.mascotsOnOff->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.mascotsOnOff->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("mascots_allfiles_random"))
		this->ui.mascotsAllFilesRandom->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.mascotsAllFilesRandom->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("mascots_mirror"))
		this->ui.mascotsMirror->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.mascotsMirror->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("mascots_animated"))
		this->ui.mascotsAnimated->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.mascotsAnimated->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("mascots_color_theme"))
		this->ui.mascotsExtractColor->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.mascotsExtractColor->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get("current_db") == "MINUS")
		this->ui.minusCatRadioBtn->setChecked(true);
	else if(mw->App->config->get("current_db") == "PLUS")
		this->ui.plusCatRadioBtn->setChecked(true);
	if (mw->App->config->get_bool("single_instance"))
		this->ui.singleInstance->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.singleInstance->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("numlock_only_on"))
		this->ui.numlockOnly->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.numlockOnly->setCheckState(Qt::CheckState::Unchecked);

	if (mw->App->config->get_bool("video_autoplay"))
		this->ui.videoAutoplay->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.videoAutoplay->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("auto_continue"))
		this->ui.autoContinue->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.autoContinue->setCheckState(Qt::CheckState::Unchecked);

	this->ui.autoContinueDelay->setValue(mw->App->config->get("auto_continue_delay").toInt());

	this->ui.SVspinBox->setValue(mw->App->db->getMainInfoValue("sv_target_count", "ALL","0").toInt());
	this->oldSVmax = this->ui.SVspinBox->value();
	connect(this->ui.CalculateSVcountButton, &QPushButton::clicked, this, [mw, this] {
		int val = mw->calculate_sv_target(); 
		this->ui.SVspinBox->setValue(val);
	});
	QString specialMode = mw->App->config->get("sv_mode").toUpper();
	if (specialMode != "PLUS" && specialMode != "MINUS")
		specialMode = "MINUS";
	int specialModeIndex = this->ui.specialSvModeCombo->findText(specialMode, Qt::MatchFixedString);
	if (specialModeIndex < 0)
		specialModeIndex = 0;
	this->ui.specialSvModeCombo->setCurrentIndex(specialModeIndex);
	if (mw->App->config->get_bool("mascots_random_change"))
		this->ui.mascotsRandomChange->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.mascotsRandomChange->setCheckState(Qt::CheckState::Unchecked);
	this->ui.mascotsChanceSpinBox->setValue(mw->App->config->get("mascots_random_chance").toInt());
	this->old_mascotsChanceSpinBox = this->ui.mascotsChanceSpinBox->value() / 100.0;
	connect(this->ui.mascotsChanceSpinBox, &QSpinBox::valueChanged, this, [mw, this] {mw->App->MascotsAnimation->set_random_chance(this->ui.mascotsChanceSpinBox->value()/100.0); });
	this->ui.mascotsFreqSpinBox->setValue(mw->App->config->get("mascots_frequency").toInt());
	this->old_mascotsFreqSpinBox = this->ui.mascotsFreqSpinBox->value();
	connect(this->ui.mascotsFreqSpinBox, &QSpinBox::valueChanged, this, [mw, this] {mw->App->MascotsAnimation->frequency = this->ui.mascotsFreqSpinBox->value(); });
	
	if (mw->App->config->get_bool("sound_effects_on"))
		this->ui.soundEffectsOnOff->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.soundEffectsOnOff->setCheckState(Qt::CheckState::Unchecked);
	this->ui.soundEffectsVolume->setValue(mw->App->config->get("sound_effects_volume").toInt());
	if (mw->App->config->get_bool("sound_effects_special_on"))
		this->ui.specialSoundEffectsOnOff->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.specialSoundEffectsOnOff->setCheckState(Qt::CheckState::Unchecked);
	this->ui.specialSoundEffectsVolume->setValue(mw->App->config->get("sound_effects_special_volume").toInt());
	if (mw->App->config->get_bool("sound_effects_exit"))
		this->ui.soundEffectsExit->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.soundEffectsExit->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("sound_effects_start"))
		this->ui.soundEffectsStart->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.soundEffectsStart->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("sound_effects_clicks"))
		this->ui.soundEffectsClicks->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.soundEffectsClicks->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("sound_effects_playpause"))
		this->ui.soundEffectsPlay->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.soundEffectsPlay->setCheckState(Qt::CheckState::Unchecked);
	this->ui.soundEffectsChance->setValue(mw->App->config->get("sound_effects_chance_playpause").toInt());
	if (mw->App->config->get_bool("sound_effects_chain_playpause"))
		this->ui.soundEffectsChainPlay->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.soundEffectsChainPlay->setCheckState(Qt::CheckState::Unchecked);
	this->ui.soundEffectsChainChance->setValue(mw->App->config->get("sound_effects_chain_chance").toInt());

	if(mw->App->debug_mode == true)
		this->ui.debugMode->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.debugMode->setCheckState(Qt::CheckState::Unchecked);
	if (mw->App->config->get_bool("skip_progress_enabled"))
		this->ui.skipAllowedProgress->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.skipAllowedProgress->setCheckState(Qt::CheckState::Unchecked);

	int time_watched_limit = mw->App->config->get("time_watched_limit").toInt();
	int minutes_limit = time_watched_limit / 60;
	int seconds_limit = time_watched_limit % 60;

	this->ui.MinutesSpinBox->setValue(minutes_limit);
	this->ui.SecondsSpinBox->setValue(seconds_limit);

	this->ui.aicon_fps_modifier_spinBox->setValue(mw->App->config->get("animated_icon_fps_modifier").toDouble());
	this->old_aicon_fps_modifier = this->ui.aicon_fps_modifier_spinBox->value();
	connect(this->ui.aicon_fps_modifier_spinBox, &QDoubleSpinBox::valueChanged, this, [mw, this] {mw->animatedIcon->fps_modifier = this->ui.aicon_fps_modifier_spinBox->value(); });

	if (mw->App->config->get_bool("random_use_seed"))
		this->ui.seedCheckBox->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.seedCheckBox->setCheckState(Qt::CheckState::Unchecked);
	this->ui.seedLineEdit->setText(mw->App->config->get("random_seed"));
    this->ui.searchTimerInterval->setValue(mw->App->config->get("search_timer_interval").toInt());

	this->ui.weightedRandMinusGroupBox->setTitle(this->ui.weightedRandMinusGroupBox->title().replace("MINUS", mw->App->config->get("minus_category_name"), Qt::CaseSensitive));
	this->ui.weightedRandPlusGroupBox->setTitle(this->ui.weightedRandPlusGroupBox->title().replace("PLUS", mw->App->config->get("plus_category_name"), Qt::CaseSensitive));
	WheelStrongFocusEventFilter* filter = new WheelStrongFocusEventFilter(this);

	//MINUS Weighted settings
	this->ui.weightedRandMinusGroupBox->setChecked(mw->App->config->get_bool("weighted_random_minus"));
	this->ui.generalBiasMinusSlider->installEventFilter(filter);
	connect(this->ui.generalBiasMinusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.generalBiasMinusSpinBox->blockSignals(true);
		this->ui.generalBiasMinusSpinBox->setValue(value / 100.0);
		this->ui.generalBiasMinusSpinBox->blockSignals(false);
	});
	connect(this->ui.generalBiasMinusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.generalBiasMinusSlider->blockSignals(true);
		this->ui.generalBiasMinusSlider->setValue(value * 100);
		this->ui.generalBiasMinusSlider->blockSignals(false);
	});
	this->ui.generalBiasMinusSpinBox->setValue(mw->App->config->get("random_general_bias_minus").toDouble());

	this->ui.viewsBiasMinusSlider->installEventFilter(filter);
	connect(this->ui.viewsBiasMinusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.viewsBiasMinusSpinBox->blockSignals(true);
		this->ui.viewsBiasMinusSpinBox->setValue(value / 100.0);
		this->ui.viewsBiasMinusSpinBox->blockSignals(false);
	});
	connect(this->ui.viewsBiasMinusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.viewsBiasMinusSlider->blockSignals(true);
		this->ui.viewsBiasMinusSlider->setValue(value * 100);
		this->ui.viewsBiasMinusSlider->blockSignals(false);
	});
	this->ui.viewsBiasMinusSpinBox->setValue(mw->App->config->get("random_views_bias_minus").toDouble());

	this->ui.ratingBiasMinusSlider->installEventFilter(filter);
	connect(this->ui.ratingBiasMinusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.ratingBiasMinusSpinBox->blockSignals(true);
		this->ui.ratingBiasMinusSpinBox->setValue(value / 100.0);
		this->ui.ratingBiasMinusSpinBox->blockSignals(false);
	});
	connect(this->ui.ratingBiasMinusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.ratingBiasMinusSlider->blockSignals(true);
		this->ui.ratingBiasMinusSlider->setValue(value * 100);
		this->ui.ratingBiasMinusSlider->blockSignals(false);
	});
	this->ui.ratingBiasMinusSpinBox->setValue(mw->App->config->get("random_rating_bias_minus").toDouble());

	this->ui.tagsBiasMinusSlider->installEventFilter(filter);
	connect(this->ui.tagsBiasMinusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.tagsBiasMinusSpinBox->blockSignals(true);
		this->ui.tagsBiasMinusSpinBox->setValue(value / 100.0);
		this->ui.tagsBiasMinusSpinBox->blockSignals(false);
	});
	connect(this->ui.tagsBiasMinusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.tagsBiasMinusSlider->blockSignals(true);
		this->ui.tagsBiasMinusSlider->setValue(value * 100);
		this->ui.tagsBiasMinusSlider->blockSignals(false);
	});
	this->ui.tagsBiasMinusSpinBox->setValue(mw->App->config->get("random_tags_bias_minus").toDouble());

	this->ui.noViewsMinusSlider->installEventFilter(filter);
	connect(this->ui.noViewsMinusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.noViewsMinusSpinBox->blockSignals(true);
		this->ui.noViewsMinusSpinBox->setValue(value / 100.0);
		this->ui.noViewsMinusSpinBox->blockSignals(false);
	});
	connect(this->ui.noViewsMinusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.noViewsMinusSlider->blockSignals(true);
		this->ui.noViewsMinusSlider->setValue(value * 100);
		this->ui.noViewsMinusSlider->blockSignals(false);
	});
	this->ui.noViewsMinusSpinBox->setValue(mw->App->config->get("random_no_views_weight_minus").toDouble());

	this->ui.noRatingMinusSlider->installEventFilter(filter);
	connect(this->ui.noRatingMinusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.noRatingMinusSpinBox->blockSignals(true);
		this->ui.noRatingMinusSpinBox->setValue(value / 100.0);
		this->ui.noRatingMinusSpinBox->blockSignals(false);
	});
	connect(this->ui.noRatingMinusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.noRatingMinusSlider->blockSignals(true);
		this->ui.noRatingMinusSlider->setValue(value * 100);
		this->ui.noRatingMinusSlider->blockSignals(false);
	});
	this->ui.noRatingMinusSpinBox->setValue(mw->App->config->get("random_no_ratings_weight_minus").toDouble());

	this->ui.noTagsMinusSlider->installEventFilter(filter);
	connect(this->ui.noTagsMinusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.noTagsMinusSpinBox->blockSignals(true);
		this->ui.noTagsMinusSpinBox->setValue(value / 100.0);
		this->ui.noTagsMinusSpinBox->blockSignals(false);
	});
	connect(this->ui.noTagsMinusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.noTagsMinusSlider->blockSignals(true);
		this->ui.noTagsMinusSlider->setValue(value * 100);
		this->ui.noTagsMinusSlider->blockSignals(false);
	});
	this->ui.noTagsMinusSpinBox->setValue(mw->App->config->get("random_no_tags_weight_minus").toDouble());

	//PLUS Weighted settings
	this->ui.weightedRandPlusGroupBox->setChecked(mw->App->config->get_bool("weighted_random_plus"));
	this->ui.generalBiasPlusSlider->installEventFilter(filter);
	connect(this->ui.generalBiasPlusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.generalBiasPlusSpinBox->blockSignals(true);
		this->ui.generalBiasPlusSpinBox->setValue(value / 100.0);
		this->ui.generalBiasPlusSpinBox->blockSignals(false);
	});
	connect(this->ui.generalBiasPlusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.generalBiasPlusSlider->blockSignals(true);
		this->ui.generalBiasPlusSlider->setValue(value * 100);
		this->ui.generalBiasPlusSlider->blockSignals(false);
	});
	this->ui.generalBiasPlusSpinBox->setValue(mw->App->config->get("random_general_bias_plus").toDouble());

	this->ui.viewsBiasPlusSlider->installEventFilter(filter);
	connect(this->ui.viewsBiasPlusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.viewsBiasPlusSpinBox->blockSignals(true);
		this->ui.viewsBiasPlusSpinBox->setValue(value / 100.0);
		this->ui.viewsBiasPlusSpinBox->blockSignals(false);
	});
	connect(this->ui.viewsBiasPlusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.viewsBiasPlusSlider->blockSignals(true);
		this->ui.viewsBiasPlusSlider->setValue(value * 100);
		this->ui.viewsBiasPlusSlider->blockSignals(false);
	});
	this->ui.viewsBiasPlusSpinBox->setValue(mw->App->config->get("random_views_bias_plus").toDouble());

	this->ui.ratingBiasPlusSlider->installEventFilter(filter);
	connect(this->ui.ratingBiasPlusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.ratingBiasPlusSpinBox->blockSignals(true);
		this->ui.ratingBiasPlusSpinBox->setValue(value / 100.0);
		this->ui.ratingBiasPlusSpinBox->blockSignals(false);
	});
	connect(this->ui.ratingBiasPlusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.ratingBiasPlusSlider->blockSignals(true);
		this->ui.ratingBiasPlusSlider->setValue(value * 100);
		this->ui.ratingBiasPlusSlider->blockSignals(false);
	});
	this->ui.ratingBiasPlusSpinBox->setValue(mw->App->config->get("random_rating_bias_plus").toDouble());

	this->ui.tagsBiasPlusSlider->installEventFilter(filter);
	connect(this->ui.tagsBiasPlusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.tagsBiasPlusSpinBox->blockSignals(true);
		this->ui.tagsBiasPlusSpinBox->setValue(value / 100.0);
		this->ui.tagsBiasPlusSpinBox->blockSignals(false);
	});
	connect(this->ui.tagsBiasPlusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.tagsBiasPlusSlider->blockSignals(true);
		this->ui.tagsBiasPlusSlider->setValue(value * 100);
		this->ui.tagsBiasPlusSlider->blockSignals(false);
	});
	this->ui.tagsBiasPlusSpinBox->setValue(mw->App->config->get("random_tags_bias_plus").toDouble());

	this->ui.noViewsPlusSlider->installEventFilter(filter);
	connect(this->ui.noViewsPlusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.noViewsPlusSpinBox->blockSignals(true);
		this->ui.noViewsPlusSpinBox->setValue(value / 100.0);
		this->ui.noViewsPlusSpinBox->blockSignals(false);
	});
	connect(this->ui.noViewsPlusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.noViewsPlusSlider->blockSignals(true);
		this->ui.noViewsPlusSlider->setValue(value * 100);
		this->ui.noViewsPlusSlider->blockSignals(false);
	});
	this->ui.noViewsPlusSpinBox->setValue(mw->App->config->get("random_no_views_weight_plus").toDouble());

	this->ui.noRatingPlusSlider->installEventFilter(filter);
	connect(this->ui.noRatingPlusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.noRatingPlusSpinBox->blockSignals(true);
		this->ui.noRatingPlusSpinBox->setValue(value / 100.0);
		this->ui.noRatingPlusSpinBox->blockSignals(false);
	});
	connect(this->ui.noRatingPlusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.noRatingPlusSlider->blockSignals(true);
		this->ui.noRatingPlusSlider->setValue(value * 100);
		this->ui.noRatingPlusSlider->blockSignals(false);
	});
	this->ui.noRatingPlusSpinBox->setValue(mw->App->config->get("random_no_ratings_weight_plus").toDouble());

	this->ui.noTagsPlusSlider->installEventFilter(filter);
	connect(this->ui.noTagsPlusSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.noTagsPlusSpinBox->blockSignals(true);
		this->ui.noTagsPlusSpinBox->setValue(value / 100.0);
		this->ui.noTagsPlusSpinBox->blockSignals(false);
	});
	connect(this->ui.noTagsPlusSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.noTagsPlusSlider->blockSignals(true);
		this->ui.noTagsPlusSlider->setValue(value * 100);
		this->ui.noTagsPlusSlider->blockSignals(false);
	});
	this->ui.noTagsPlusSpinBox->setValue(mw->App->config->get("random_no_tags_weight_plus").toDouble());

	//These need to be at the bottom
	QList<QSpinBox*> spinboxes = this->findChildren<QSpinBox*>();
	for (auto spinbox : spinboxes) {
		spinbox->installEventFilter(filter);
		connect(spinbox, &QSpinBox::valueChanged, this, [this] {this->ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true); });
	}
	QList<QDoubleSpinBox*> doublespinboxes = this->findChildren<QDoubleSpinBox*>();
	for (auto doublespinbox : doublespinboxes) {
		doublespinbox->installEventFilter(filter);
		connect(doublespinbox, &QDoubleSpinBox::valueChanged, this, [this] {this->ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true); });
	}
	QList<QCheckBox*> checkboxes = this->findChildren<QCheckBox*>();
	for (auto checkbox : checkboxes) {
		connect(checkbox, &QCheckBox::checkStateChanged, this, [this] {this->ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true); });
	}
	QList<QRadioButton*> radiobuttons = this->findChildren<QRadioButton*>();
	for (auto radiobutton : radiobuttons) {
		connect(radiobutton, &QRadioButton::toggled, this, [this] {this->ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true); });
	}
	QList<QGroupBox*> groupboxes = this->findChildren<QGroupBox*>();
	for (auto groupbox : groupboxes) {
		connect(groupbox, &QGroupBox::toggled, this, [this] {this->ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true); });
	}
	QList<QSlider*> sliders = this->findChildren<QSlider*>();
	for (auto slider : sliders) {
		connect(slider, &QSlider::valueChanged, this, [this] {this->ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true); });
	}
	QList<QLineEdit*> lineedits = this->findChildren<QLineEdit*>();
	for (auto lineedit : lineedits) {
		connect(lineedit, &QLineEdit::textChanged, this, [this] {this->ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true); });
	}
}

bool SettingsDialog::eventFilter(QObject* o, QEvent* e)
{
	const QWidget* widget = static_cast<QWidget*>(o);
	if (e->type() == QEvent::Wheel && widget && !widget->hasFocus())  
	{
		e->ignore();
		return true;
	}

	return QObject::eventFilter(o, e);
}

SettingsDialog::~SettingsDialog()
{
}

