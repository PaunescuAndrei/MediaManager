#pragma once
#include <QString>
#include <QStringList>

extern std::map<QString, int> ListColumns;
extern std::map<int, QString> ListColumns_reversed;
extern std::map<QString, int> sortingDict;
extern std::map<int, QString> sortingDict_reversed;
extern QStringList videoTypes;

namespace CustomRoles {
	enum roles {rating = Qt::UserRole};
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
constexpr auto SOUND_EFFECTS_INTRO_PATH = "res/sound/intro";
constexpr auto SOUND_EFFECTS_END_PATH = "res/sound/end";
