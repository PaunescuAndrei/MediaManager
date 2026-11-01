#include "stdafx.h"
#include "config.h"
#include <QtGlobal>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "utils.h"

namespace {
	constexpr char const* kLegacySection = "settings";

	// mINI opens std::filesystem::path natively; on Windows the narrow form is the ANSI code page,
	// which drops non-ASCII characters from a user profile path.
	std::filesystem::path configPath(QString const& filepath) {
#ifdef Q_OS_WIN
		return std::filesystem::path(filepath.toStdWString());
#else
		return std::filesystem::path(filepath.toStdString());
#endif
	}
}

Config::Config(QString filepath) : file(configPath(filepath)) {
	this->init_defaults();
	this->open_config(filepath);
}

void Config::init_defaults() {
	// The one place a default lives, and the key-to-section map: each group below is a section of
	// config.ini, so a key's default and the section it is saved in cannot drift apart. Reads
	// answer from this table when the section has no key, so a key read at runtime but missing
	// here silently reads as empty, false or zero. It is memory-only: save_config() writes ini
	// alone, so no default is ever persisted into config.ini.
	this->defaults.clear();
	this->keySection.clear();
	auto group = [this](char const* section, std::initializer_list<std::pair<char const*, char const*>> rows) {
		for (auto const& row : rows) {
			this->defaults[row.first] = row.second;
			this->keySection[row.first] = section;
		}
	};

	group("general", {
		{"single_instance", "False"},
		{"threads", "8"},
		{"current_db", "MINUS"},
		{"default_directory", ""},
		{"video_types", ""},
		{"sv_types", ""},
		{"numlock_only_on", "True"},
		{"search_timer_interval", "100"},
		{"session_save_interval_seconds", "10"},
	});

	group("playback", {
		{"player_path", ""},
		{"video_autoplay", "True"},
		{"skip_progress_enabled", "False"},
		{"time_watched_limit", "600"},
		{"counter_use_actual_watch_time", "False"},
		{"empty_player_tracking", "True"},
		{"auto_continue", "False"},
		{"auto_continue_delay", "5"},
	});

	group("ui", {
		{"headers_minus_visible", "author name path type watched_yes watched_no views rating"},
		{"headers_plus_visible", "author name path type watched_yes watched_no views rating"},
		{"sort_column", "PATH_COLUMN"},
		{"sort_order", "Ascending"},
		{"tooltips_enabled", "True"},
		{"tooltip_delay_ms", "700"},
		{"show_default_icon_when_idle", "True"},
		{"animated_icon_flag", "False"},
		{"animated_icon_fps_modifier", "1.0"},
		{"random_icon", "False"},
	});

	group("categories", {
		{"plus_category_name", "PLUS"},
		{"minus_category_name", "MINUS"},
		{"plus_update_watched", "True"},
		{"minus_update_watched", "True"},
		{"plus_random_next", "0"},  // Sequential by default
		{"minus_random_next", "0"}, // Sequential by default
		{"plus_get_random_mode", "2"},
		{"minus_get_random_mode", "2"},
		{"sv_mode", "MINUS"},
		{"sv_track_in_plus", "False"},
	});

	group("mascots", {
		{"mascots", "False"},
		{"mascots_allfiles_random", "False"},
		{"mascots_mirror", "False"},
		{"mascots_animated", "False"},
		{"mascots_random_change", "True"},
		{"mascots_random_chance", "10"},
		{"mascots_frequency", "400"},
		{"mascots_color_theme", "False"},
		{"mascots_center_content", "True"},
		{"mascots_use_seed", "True"},
		{"mascots_width_percent", "15"},
		{"mascots_mode", "Static"},
		{"mascots_fill", "HeightFit"},
		{"mascots_media", "Images"},
		{"mascots_image_cache_mb", "128"},
		{"mascots_image_cache_max_items", "12"},
		{"mascots_clip_seconds", "30"},
		// Qt's low-latency playback intent. False asks for the backend's normal buffering instead --
		// smoother playback, a slower first frame.
		{"mascots_low_latency", "True"},
		// Opposite makes a mirrored pair travel outwards or inwards together.
		{"mascots_pair_scroll", "Same"},
		{"mascots_pair_flip", "True"},
		{"mascots_swap_on_beat", "True"},
		{"mascots_random_start", "True"},
		{"mascots_crossfade_ms", "600"},
		{"mascots_max_decoders", "24"},
		{"mascots_scroll_speed", "40"},
		{"mascots_pan", "False"},
		{"mascots_scroll_direction", "LeftToRight"},
		{"mascots_scroll_pingpong", "False"},
		{"mascots_stripes_left", "2"},
		{"mascots_stripes_right", "2"},
		{"mascots_stripes_orientation", "Horizontal"},
		{"mascots_stripes_alternate", "True"},
		{"mascots_pause_unfocused", "True"},
		{"mascots_weighted_random", "True"},
		{"mascots_preload", "True"},
		{"mascots_swap_timeout_ms", "5000"},
		{"mascots_retry_interval_ms", "150"},
		{"mascots_retry_attempts", "5"},
		{"mascots_busy_defer_ms", "250"},
	});

	group("mascots_background", {
		{"mascots_background", "False"},
		{"mascots_background_mode", "Static"},
		{"mascots_background_fill", "Crop"},
		{"mascots_background_stripes", "3"},
		{"mascots_background_stripes_orientation", "Horizontal"},
		{"mascots_background_stripes_alternate", "True"},
		{"mascots_background_pan", "False"},
		{"mascots_background_scroll_speed", "50"},
		{"mascots_background_scroll_direction", "LeftToRight"},
		{"mascots_background_scroll_pingpong", "False"},
		{"mascots_background_clip_seconds", "30"},
		{"mascots_background_swap_on_beat", "True"},
		{"mascots_background_random_change", "True"},
		{"mascots_background_random_chance", "10"},
		{"mascots_background_alternate_alpha", "0.85"},
		{"mascots_background_scrollbar_alpha", "1.00"},
		// The veil's alpha over the background layer: 1.0 hides the layer and 0.0 shows it at full
		// strength. The default is 0 because 0.85 composed with the rows' own 0.85 fill left only
		// ~2% of the frame visible under the list.
		{"mascots_background_opacity", "0"},
		// The list's rows (and the strip above them) over the layer: 1.0 hides the layer behind
		// the list, 0.0 puts body text straight on the video. 0.85 keeps white text at 9.8:1 on
		// the Base row and 7.5:1 on the alternate row even over a white frame, and the layer still
		// reads as a picture rather than a wash. The veil is a separate whole-window surface, so
		// this row fill is the only control over the list's own opacity.
		{"mascots_background_list_alpha", "0.85"},
		// Where the two columns sit relative to the veil: Above draws them after it at full
		// brightness, Below puts the veil between them and the rest of the UI so they dim with it.
		{"mascots_columns_z", "Above the veil"},
	});

	group("preview", {
		{"preview_volume", "15"},
		{"preview_random_start", "True"},
		{"preview_autoplay_all_mute", "False"},
		{"preview_remember_position", "True"},
		{"preview_random_each_hover", "False"},
		{"preview_seeded_random", "True"},
		{"preview_seek_seconds", "10"},
		{"preview_next_choices_enabled", "True"},
		{"preview_lazy_loading", "True"},
		{"preview_main_history_count", "5"},
		{"preview_main_enabled", "True"},
		{"preview_main_width", "800"},
		{"preview_main_height", "450"},
		{"preview_overlay_scale", "1.0"},
		{"preview_overlay_pad_x", "4"},
		{"preview_overlay_pad_y", "2"},
		{"preview_overlay_margin", "6"},
	});

	group("random", {
		{"random_use_seed", "False"},
		{"random_seed", ""},
		{"weighted_random_minus", "True"},
		{"random_general_bias_minus", "2.0"},
		{"random_views_bias_minus", "1.0"},
		{"random_rating_bias_minus", "1.0"},
		{"random_tags_bias_minus", "1.0"},
		{"random_bpm_bias_minus", "1.0"},
		{"random_no_views_weight_minus", "1.0"},
		{"random_no_ratings_weight_minus", "1.0"},
		{"random_no_tags_weight_minus", "0.0"},
		{"weighted_random_plus", "True"},
		{"random_general_bias_plus", "2.0"},
		{"random_views_bias_plus", "1.0"},
		{"random_rating_bias_plus", "1.0"},
		{"random_tags_bias_plus", "1.0"},
		{"random_bpm_bias_plus", "1.0"},
		{"random_no_views_weight_plus", "1.0"},
		{"random_no_ratings_weight_plus", "1.0"},
		{"random_no_tags_weight_plus", "0.0"},
	});

	group("notifications", {
		{"notification_video_info_enabled", "True"},
		{"notification_video_info_duration_ms", "10000"},
		{"notification_milestone_enabled", "True"},
		{"notification_milestone_duration_ms", "8000"},
		{"notification_goal_met_enabled", "True"},
		{"notification_goal_met_duration_ms", "12000"},
		{"notification_streak_saved_enabled", "True"},
		{"notification_streak_saved_duration_ms", "8000"},
		{"notification_streak_at_risk_enabled", "True"},
		{"notification_streak_at_risk_duration_ms", "8000"},
		{"notification_personal_best_enabled", "True"},
		{"notification_personal_best_duration_ms", "10000"},
		{"notification_session_summary_enabled", "True"},
		{"notification_session_summary_duration_ms", "10000"},
		{"notification_session_summary_min_session_seconds", "10"},
		{"notification_session_summary_min_video_watch_seconds", "10"},
		{"notification_bg_opacity", "1.00"},
		{"notification_content_opacity", "1.00"},
		{"notification_timerbar_opacity", "1.00"},
		{"notification_counter_opacity", "1.00"},
	});

	group("streak", {
		{"streak_daily_time_target_minutes", "5"},
		{"streak_save_enabled", "True"},
		{"streak_auto_save_enabled", "True"},
		{"streak_auto_save_target_multiplier", "2.0"},
		{"streak_auto_save_target_multiplier_increment", "1.0"},
		{"streak_auto_save_max_consecutive", "0"},
		{"streak_freeze_enabled", "True"},
		{"streak_freeze_base_price", "10"},
		{"streak_freeze_price_increment", "10"},
		{"streak_freeze_max_stock", "2"},
		{"streak_freeze_price_reset_days", "30"},
		{"streak_freeze_consume_first", "True"},
		{"streak_debt_refire_minutes", "30"},
		{"streak_debt_final_hour", "23"},
		{"streak_at_risk_min_days", "3"},
		{"streak_at_risk_cutoff_hour", "20"},
		{"streak_at_risk_refire_minutes", "0"},
	});

	group("goals", {
		{"daily_video_goal", "5.0"},
		{"daily_time_goal_minutes", "60"},
		{"daily_milestone_video_step", "0.0"},
		{"daily_milestone_time_step_minutes", "0"},
		{"daily_progress_check_interval_seconds", "10"},
		{"pb_videos_step", "1"},
		{"pb_time_step_minutes", "5"},
		{"pb_videos_min_threshold", "1"},
		{"pb_time_min_threshold_minutes", "5"},
		{"goal_reached_counter_enabled", "False"},
		{"goal_reached_counter_amount", "1"},
	});

	group("sound", {
		{"sound_effects_on", "False"},
		{"sound_effects_volume", "3"},
		{"sound_effects_special_on", "False"},
		{"sound_effects_special_volume", "5"},
		{"sound_effects_exit", "True"},
		{"sound_effects_start", "True"},
		{"sound_effects_clicks", "True"},
		{"sound_effects_playpause", "True"},
		{"sound_effects_chance_playpause", "50"},
		{"sound_effects_chain_playpause", "True"},
		{"sound_effects_chain_chance", "100"},
	});

	group("music", {
		{"music_on", "False"},
		{"music_volume", "4"},
		{"music_random_start", "False"},
		{"music_loop_first", "False"},
		{"music_init_track_path", ""},
		{"music_playlist_path", ""},
	});

	group("rarity", {
		{"rarity_enabled", "False"},
		{"rarity_ssr_pct", "10"},
		{"rarity_sr_pct", "30"},
		{"rarity_r_pct", "65"},
		{"rarity_ssr_color", "#ffb833"},
		{"rarity_sr_color", "#5599cc"},
		{"rarity_r_color", "#55cc55"},
	});

	group("shoko", {
		{"shoko_path", ""},
		{"shoko_user", ""},
		{"shoko_password", ""},
		{"shoko_import_playlist", ""},
		{"shoko_import_random_item", "True"},
	});

	group("next", {
		{"next_multichoice_enabled", "False"},
		{"next_multichoice_count", "3"},
		{"next_multichoice_append_on_refresh", "False"},
		{"next_volume_guard_enabled", "False"},
		{"next_volume_guard_threshold", "50"},
		{"next_volume_guard_target", "20"},
	});

	group("bpm", {
		{"bpm_threads", "2"},
		{"bpm_calculation_types", ""},
	});

	group("stats", {
		{"stats_heatmap_months", "6"},
	});
}

void Config::open_config(QString filepath) {
	this->path = configPath(filepath);
	this->file = mINI::INIFile(this->path);
	this->ini.clear();
	this->file.read(this->ini);
	// Drop any [default] block read from an older file: defaults are code-owned, and a stale copy
	// left in ini would be rewritten by every save as if this build still read it.
	this->ini.remove("default");
	this->migrate_flat_settings();
	// No migration from `mascots_background_list_alpha` to `mascots_background_opacity`: they are
	// two surfaces (the rows and the whole-window veil), and moving the old value would rewrite one
	// the user still owns on every load.
}

void Config::migrate_flat_settings() {
	if (!this->ini.has(kLegacySection)) {
		return;
	}
	struct Move {
		std::string section;
		std::string key;
		std::string value;
	};
	std::vector<Move> moves;
	{
		auto& legacy = this->ini[kLegacySection];
		for (auto const& entry : legacy) {
			auto const mapped = this->keySection.find(entry.first);
			if (mapped == this->keySection.end() || mapped->second == kLegacySection) {
				continue;
			}
			moves.push_back(Move{ mapped->second, entry.first, entry.second });
		}
	}
	// Applied after collecting because inserting a section can reallocate the outer container and
	// invalidate the reference the loop above holds.
	for (auto const& move : moves) {
		this->ini[move.section].set(move.key, move.value);
		this->ini[kLegacySection].remove(move.key);
	}
	if (this->ini[kLegacySection].size() == 0) {
		this->ini.remove(kLegacySection);
	}
}

void Config::save_config() {
	this->file.write(this->ini);
	// mINI terminates every line but the last, so without this every save would leave config.ini
	// without a final newline and a text editor would join the last key to whatever follows.
	std::ifstream probe(this->path, std::ios::binary);
	if (!probe.is_open()) {
		return;
	}
	probe.seekg(0, std::ios::end);
	auto const size = static_cast<std::streamoff>(probe.tellg());
	if (size <= 0) {
		return;
	}
	probe.seekg(size - 1);
	char last = '\0';
	probe.read(&last, 1);
	probe.close();
	if (last == '\n') {
		return;
	}
	std::ofstream terminator(this->path, std::ios::binary | std::ios::app);
	terminator << mINI::INIStringUtil::endl;
}

QString Config::get(QString item) {
	std::string const section = this->sectionFor(item);
	if (this->ini.has(section)) {
		// we have section, we can access it safely without creating a new one
		auto& collection = this->ini[section];
		if (collection.has(item.toStdString())) {
			// we have key, we can access it safely without creating a new one
			return QString::fromStdString(collection[item.toStdString()]);
		}
	}
	// A key missing from its own section may still sit in a flat [settings] block: either one
	// written before the sections existed or one a user typed by hand.
	if (section != kLegacySection && this->ini.has(kLegacySection)) {
		auto& collection = this->ini[kLegacySection];
		if (collection.has(item.toStdString())) {
			return QString::fromStdString(collection[item.toStdString()]);
		}
	}
	// Last resort: the key sits in some other section, which happens when a user moves it by hand
	// or when a later build regroups keys.
	for (auto const& collection : this->ini) {
		if (collection.second.has(item.toStdString())) {
			return QString::fromStdString(collection.second.get(item.toStdString()));
		}
	}
	return this->defaultFor(item);
}

QString Config::get_default(QString item) {
	return this->defaultFor(item);
}

void Config::set(QString key, QString value) {
	this->ini[this->sectionFor(key)][key.toStdString()] = value.toStdString();
}

double Config::get_double(QString item) {
	bool ok = false;
	const double value = this->get(item).toDouble(&ok);
	if (ok)
		return value;
	bool fallbackOk = false;
	const double fallback = this->get_default(item).toDouble(&fallbackOk);
	return fallbackOk ? fallback : 0.0;
}

double Config::get_double(QString item, double defaultValue) {
	QString val = this->get(item);
	if (val.isEmpty()) return defaultValue;
	bool ok = false;
	double d = val.toDouble(&ok);
	return ok ? d : defaultValue;
}

bool Config::get_bool(QString item) {
	return utils::text_to_bool(this->get(item).toStdString());
}

QString Config::defaultFor(QString const& item) const {
	auto const it = this->defaults.find(item.trimmed().toLower().toStdString());
	return it != this->defaults.end() ? QString::fromStdString(it->second) : QString();
}

std::string Config::sectionFor(QString const& key) const {
	auto const it = this->keySection.find(key.trimmed().toLower().toStdString());
	return it != this->keySection.end() ? it->second : std::string(kLegacySection);
}
