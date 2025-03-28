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
#include "WheelEventFilter.h"

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
	connect(this->ui.cacheThumbsButton, &QPushButton::clicked, this, [mw, this] {mw->thumbnailManager.rebuildThumbnailCache(mw->App->db->db,true); });
	connect(this->ui.cacheThumbsButton, &customQButton::rightClicked, this, [mw, this] {mw->thumbnailManager.rebuildThumbnailCache(mw->App->db->db, false); });
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
	this->ui.SVspinBox->setValue(mw->App->db->getMainInfoValue("sv_target_count", "ALL","0").toInt());
	this->oldSVmax = this->ui.SVspinBox->value();
	connect(this->ui.CalculateSVcountButton, &QPushButton::clicked, this, [mw, this] {
		int val = mw->calculate_sv_target(); 
		this->ui.SVspinBox->setValue(val);
	});
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

	int time_watched_limit = mw->App->config->get("time_watched_limit").toInt();
	int minutes_limit = time_watched_limit / 60;
	int seconds_limit = time_watched_limit % 60;

	this->ui.MinutesSpinBox->setValue(minutes_limit);
	this->ui.SecondsSpinBox->setValue(seconds_limit);

	this->ui.aicon_fps_modifier_spinBox->setValue(mw->App->config->get("animated_icon_fps_modifier").toDouble());
	this->old_aicon_fps_modifier = this->ui.aicon_fps_modifier_spinBox->value();
	connect(this->ui.aicon_fps_modifier_spinBox, &QDoubleSpinBox::valueChanged, this, [mw, this] {mw->animatedIcon->fps_modifier = this->ui.aicon_fps_modifier_spinBox->value(); });

	QList<QSpinBox*> spinboxes = this->findChildren<QSpinBox*>();
	for (auto spinbox : spinboxes) {
		spinbox->installEventFilter(this);
		connect(spinbox, &QSpinBox::valueChanged, this, [this] {this->ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true); });
	}
	QList<QDoubleSpinBox*> doublespinboxes = this->findChildren<QDoubleSpinBox*>();
	for (auto doublespinbox : doublespinboxes) {
		doublespinbox->installEventFilter(this);
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

	if (mw->App->config->get_bool("random_use_seed"))
		this->ui.seedCheckBox->setCheckState(Qt::CheckState::Checked);
	else
		this->ui.seedCheckBox->setCheckState(Qt::CheckState::Unchecked);
	this->ui.seedLineEdit->setText(mw->App->config->get("random_seed"));
	this->ui.weightedRandGroupBox->setChecked(mw->App->config->get_bool("weighted_random"));
	WheelEventFilter* filter = new WheelEventFilter(this);
	this->ui.generalBiasSlider->installEventFilter(filter);
	connect(this->ui.generalBiasSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.generalBiasSpinBox->blockSignals(true);
		this->ui.generalBiasSpinBox->setValue(value / 100.0); 
		this->ui.generalBiasSpinBox->blockSignals(false);
	});
	connect(this->ui.generalBiasSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.generalBiasSlider->blockSignals(true);
		this->ui.generalBiasSlider->setValue(value * 100);
		this->ui.generalBiasSlider->blockSignals(false);
	});
	this->ui.generalBiasSpinBox->setValue(mw->App->config->get("random_general_bias").toDouble());

	this->ui.viewsBiasSlider->installEventFilter(filter);
	connect(this->ui.viewsBiasSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.viewsBiasSpinBox->blockSignals(true);
		this->ui.viewsBiasSpinBox->setValue(value / 100.0);
		this->ui.viewsBiasSpinBox->blockSignals(false);
	});
	connect(this->ui.viewsBiasSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.viewsBiasSlider->blockSignals(true);
		this->ui.viewsBiasSlider->setValue(value * 100);
		this->ui.viewsBiasSlider->blockSignals(false);
	});
	this->ui.viewsBiasSpinBox->setValue(mw->App->config->get("random_views_bias").toDouble());

	this->ui.ratingBiasSlider->installEventFilter(filter);
	connect(this->ui.ratingBiasSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.ratingBiasSpinBox->blockSignals(true);
		this->ui.ratingBiasSpinBox->setValue(value / 100.0);
		this->ui.ratingBiasSpinBox->blockSignals(false);
	});
	connect(this->ui.ratingBiasSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.ratingBiasSlider->blockSignals(true);
		this->ui.ratingBiasSlider->setValue(value * 100);
		this->ui.ratingBiasSlider->blockSignals(false);
	});
	this->ui.ratingBiasSpinBox->setValue(mw->App->config->get("random_rating_bias").toDouble());

	this->ui.tagsBiasSlider->installEventFilter(filter);
	connect(this->ui.tagsBiasSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.tagsBiasSpinBox->blockSignals(true);
		this->ui.tagsBiasSpinBox->setValue(value / 100.0);
		this->ui.tagsBiasSpinBox->blockSignals(false);
	});
	connect(this->ui.tagsBiasSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.tagsBiasSlider->blockSignals(true);
		this->ui.tagsBiasSlider->setValue(value * 100);
		this->ui.tagsBiasSlider->blockSignals(false);
	});
	this->ui.tagsBiasSpinBox->setValue(mw->App->config->get("random_tags_bias").toDouble());

	this->ui.noViewsSlider->installEventFilter(filter);
	connect(this->ui.noViewsSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.noViewsSpinBox->blockSignals(true);
		this->ui.noViewsSpinBox->setValue(value / 100.0);
		this->ui.noViewsSpinBox->blockSignals(false);
	});
	connect(this->ui.noViewsSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.noViewsSlider->blockSignals(true);
		this->ui.noViewsSlider->setValue(value * 100);
		this->ui.noViewsSlider->blockSignals(false);
	});
	this->ui.noViewsSpinBox->setValue(mw->App->config->get("random_no_views_weight").toDouble());

	this->ui.noRatingSlider->installEventFilter(filter);
	connect(this->ui.noRatingSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.noRatingSpinBox->blockSignals(true);
		this->ui.noRatingSpinBox->setValue(value / 100.0);
		this->ui.noRatingSpinBox->blockSignals(false);
	});
	connect(this->ui.noRatingSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.noRatingSlider->blockSignals(true);
		this->ui.noRatingSlider->setValue(value * 100);
		this->ui.noRatingSlider->blockSignals(false);
	});
	this->ui.noRatingSpinBox->setValue(mw->App->config->get("random_no_ratings_weight").toDouble());

	this->ui.noTagsSlider->installEventFilter(filter);
	connect(this->ui.noTagsSlider, &QSlider::valueChanged, this, [this](int value) {
		this->ui.noTagsSpinBox->blockSignals(true);
		this->ui.noTagsSpinBox->setValue(value / 100.0);
		this->ui.noTagsSpinBox->blockSignals(false);
	});
	connect(this->ui.noTagsSpinBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		this->ui.noTagsSlider->blockSignals(true);
		this->ui.noTagsSlider->setValue(value * 100);
		this->ui.noTagsSlider->blockSignals(false);
	});
	this->ui.noTagsSpinBox->setValue(mw->App->config->get("random_no_tags_weight").toDouble());
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

