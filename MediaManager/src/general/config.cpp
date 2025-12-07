#include "stdafx.h"
#include "config.h"
#include <QtGlobal>
#include "utils.h"
#include <QFileInfo>

Config::Config(QString filepath) {
	this->file = new mINI::INIFile();
	this->open_config(filepath);
	this->init_defaults();
	//if (!std::filesystem::exists(filepath))
	if (!QFileInfo::exists(filepath))
		this->save_config();
	//this->ini["tmp"]["tmp"] = "test";

	//for (auto const& it : this->ini)
	//{
	//	auto const& section = it.first;
	//	auto const& collection = it.second;
	//	print_debug(section + "\n");
	//	for (auto const& it2 : collection)
	//	{
	//		auto const& key = it2.first;
	//		auto const& value = it2.second;
	//		print_debug(key + " " + value + "\n");
	//	}
	//}
}

void Config::init_defaults() {
	this->ini["DEFAULT"].set({
		{"single_instance", "False"},
		{"threads","8"},
		{"player_path", ""},
		{"shoko_path", ""},
		{"shoko_user",""},
		{"shoko_password",""},
		{"shoko_import_playlist",""},
		{"shoko_import_random_item","True"},
		{"default_directory", ""},
		{"plus_category_name", "PLUS"},
		{"minus_category_name", "MINUS"},
		{"plus_update_watched", "True"},
		{"minus_update_watched", "True"},
		{"plus_random_next", "0"},  // Sequential by default
		{"minus_random_next", "0"}, // Sequential by default
		{"plus_get_random_mode", "2"},
		{"minus_get_random_mode", "2"},
		{"next_multichoice_enabled", "False"},
		{"next_multichoice_count", "3"},
		{"current_db", "MINUS"},
		{"video_types",	""},
		{"sv_types",""},
		{"skip_progress_enabled", "False"},
		{"sv_mode", "MINUS"},
		{"animated_icon_flag", "False"},
		{"animated_icon_fps_modifier", "1.0"},
		{"random_icon", "False"},
		{"default_icon_not_watching", "True"},
		{"headers_minus_visible", "author name path type watched_yes watched_no views rating"},
		{"headers_plus_visible", "author name path type watched_yes watched_no views rating"},
		{"sort_column", "PATH_COLUMN"},
		{"sort_order", "Ascending"},
		{"music_on", "False"},
		{"music_volume", "4"},
		{"music_random_start", "False"},
		{"music_loop_first", "False"},
		{"music_init_track_path", ""},
		{"music_playlist_path", ""},
		{"mascots", "False"},
		{"mascots_allfiles_random","False"},
		{"mascots_mirror", "False"},
		{"mascots_animated", "False"},
		{"mascots_random_change", "True"},
		{"mascots_random_chance", "10"},
		{"mascots_frequency", "400"},
		{"mascots_color_theme","False"},
		{"sound_effects_on","False"},
		{"sound_effects_volume","3"},
		{"sound_effects_special_on","False"},
		{"sound_effects_special_volume","5"},
		{"sound_effects_exit","True"},
		{"sound_effects_start","True"},
		{"sound_effects_clicks","True"},
		{"sound_effects_playpause","True"},
		{"sound_effects_chance_playpause","50"},
		{"sound_effects_chain_playpause","True"},
		{"sound_effects_chain_chance","100"},
		{"time_watched_limit","600"},
		{"numlock_only_on","True"},
		{"video_autoplay", "True"},
		{"random_use_seed","False"},
		{"random_seed",""},
		{"weighted_random_minus","True"},
		{"random_general_bias_minus","2.0"},
		{"random_views_bias_minus","1.0"},
		{"random_rating_bias_minus","1.0"},
		{"random_tags_bias_minus","1.0"},
		{"random_no_views_weight_minus","1.0"},
		{"random_no_ratings_weight_minus","1.0"},
		{"random_no_tags_weight_minus","0.0"},
		{"weighted_random_plus","True"},
		{"random_general_bias_plus","2.0"},
		{"random_views_bias_plus","1.0"},
		{"random_rating_bias_plus","1.0"},
		{"random_tags_bias_plus","1.0"},
		{"random_no_views_weight_plus","1.0"},
		{"random_no_ratings_weight_plus","1.0"},
		{"random_no_tags_weight_plus","0.0"},
		{"auto_continue", "False"},
		{"auto_continue_delay", "5"},
		{"search_timer_interval", "100"},
		{"notification_duration_ms", "10000"},
		{"preview_volume", "15"},
		{"preview_random_start", "True"},
		{"preview_autoplay_all_mute", "False"},
		{"preview_remember_position", "True"},
		{"preview_random_each_hover", "False"},
		{"preview_seeded_random", "True"},
		{"preview_seek_seconds", "10"},
		{"preview_next_choices_enabled", "True"},
		{"preview_main_history_count", "5"},
		{"preview_main_enabled", "True"},
		{"preview_main_width", "800"},
		{"preview_main_height", "450"},
		{"preview_overlay_scale", "1.0"},
		{"preview_overlay_pad_x", "4"},
		{"preview_overlay_pad_y", "2"},
		{"preview_overlay_margin", "6"},
	});
}

void Config::open_config(QString filepath) {
	this->file->open(filepath.toStdString());
	this->ini.clear();
	this->file->read(this->ini);
}

void Config::save_config() {
	this->file->write(this->ini);
}

QString Config::get(QString item) {
	if (this->ini.has("Settings"))
	{
		// we have section, we can access it safely without creating a new one
		auto& collection = this->ini["Settings"];
		if (collection.has(item.toStdString()))
		{
			// we have key, we can access it safely without creating a new one
			return QString::fromStdString(collection[item.toStdString()]);
		}
	}
	return QString::fromStdString(this->ini["DEFAULT"][item.toStdString()]);
}

void Config::set(QString key, QString value) {
	this->ini["Settings"][key.toStdString()] = value.toStdString();
}

bool Config::get_bool(QString item) {
	if (this->ini.has("Settings"))
	{
		// we have section, we can access it safely without creating a new one
		auto& collection = this->ini["Settings"];
		if (collection.has(item.toStdString()))
		{
			// we have key, we can access it safely without creating a new one
			return utils::text_to_bool(collection[item.toStdString()]);
		}
	}
	return utils::text_to_bool(this->ini["DEFAULT"][item.toStdString()]);
}

Config::~Config() {
	delete this->file;
}
