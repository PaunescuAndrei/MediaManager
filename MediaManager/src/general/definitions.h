#pragma once
#include <QString>
#include <QStringList>
#include <QMetaType>
#include <QList>
#include <QTreeWidgetItem>

extern std::map<QString, int> ListColumns;
extern std::map<int, QString> ListColumns_reversed;
extern std::map<QString, int> sortingDict;
extern std::map<int, QString> sortingDict_reversed;
extern QStringList videoTypes;
extern QStringList svTypes;

struct VideoWeightedData {
	int id;
	QString path;
	double views;
	double rating;
	double tagsWeight;
};

struct NextVideoChoice {
	int id = -1;
	QString path;
	QString name;
	QString author;
	QString tags;
	bool resetProgress = true;
};

struct AuthorVideoData {
	QString author;
	QList<VideoWeightedData> videos;
	QTreeWidgetItem* firstItem;
};

struct WeightedBiasSettings {
	bool weighted_random_enabled = false;
	double bias_general = 0;
	double bias_views = 0;
	double bias_rating = 0;
	double bias_tags = 0;
	double no_views_weight = 0;
	double no_rating_weight = 0;
	double no_tags_weight = 0;
};

namespace RandomModes {
	enum Mode {
		All = 0,
		Normal = 1,
		Filtered = 2,
	};
}

namespace NextVideoModes {
	enum Mode {
		Sequential = 0,
		Random = 1,
		SeriesRandom = 2,
	};
}

struct NextVideoSettings {
	RandomModes::Mode random_mode = RandomModes::Filtered;
	bool ignore_filters_and_defaults = false;
	QStringList vid_type_include = {};
	QStringList vid_type_exclude = {};
	bool next_video_is_sv = false;
	bool sv_is_available = false;
};

struct Tag {
	int id;
	QString name;
	int display_priority = 100;
	auto operator<=>(const Tag&) const = default;
};
Q_DECLARE_METATYPE(Tag);

uint qHash(const Tag& key);

struct TagRelation {
	int id;
	int video_id;
	int tag_id;
	auto operator<=>(const TagRelation&) const = default;
};

namespace CustomRoles {
	enum roles {
		id = Qt::UserRole,
		rating = Qt::UserRole+1,
	};
}

//Database Paths
constexpr auto DATABASE_PATH = "res/database.db";
constexpr auto DATABASE_BACKUPS_PATH = "res/backups";

//Icons Paths
constexpr auto ICONS_PATH = "res/icons";
constexpr auto ANIMATED_ICONS_PATH = "res/icons/animated_icons";
constexpr auto NORMAL_ICONS_PATH = "res/icons/normal_icons";

//Cache Paths
constexpr auto ANIMATED_ICONS_CACHE_PATH = "res/cache/aicons_cache";
constexpr auto BEATS_CACHE_PATH = "res/cache/beats_cache";
constexpr auto THUMBNAILS_CACHE_PATH = "res/cache/thumbs_cache";

//Tools Paths
constexpr auto UTILS_PATH = "res/utils";
constexpr auto THUMBNAILS_PROGRAM_PATH = "res/utils/mtn-win64/bin/mtn.exe";

//Mascots Paths
constexpr auto MASCOTS_PATH = "res/mascots";

//Sound Paths
constexpr auto SOUND_EFFECTS_PATH = "res/sound/effects";
constexpr auto SOUND_EFFECTS_SPECIAL_PATH = "res/sound/special";
constexpr auto SOUND_EFFECTS_INTRO_PATH = "res/sound/intro";
constexpr auto SOUND_EFFECTS_END_PATH = "res/sound/end";
