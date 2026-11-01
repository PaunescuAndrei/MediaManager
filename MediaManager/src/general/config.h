#pragma once
// ini.h must be parsed without MINI_CASE_SENSITIVE: its key and section lookup then folds case,
// which is the only reason the legacy [default] / [settings] spellings in existing files resolve.
#include "ini.h"
#include <string>
#include <unordered_map>
class Config
{
public:
	mINI::INIFile file;
	mINI::INIStructure ini;
	Config(QString filepath);
	void init_defaults();
	void open_config(QString filepath);
	void save_config();
	QString get(QString item);
	QString get_default(QString item);
	void set(QString key, QString value);
	bool get_bool(QString item);
	double get_double(QString item);
	double get_double(QString item, double defaultValue);

private:
	// A flat [settings] block is still read on load and folded into its sections, so a file written
	// before the sections existed keeps working without the user touching it. Returns true when
	// something moved, so the caller can rewrite the file right away.
	bool migrate_flat_settings();
	std::string sectionFor(QString const& key) const;
	QString defaultFor(QString const& item) const;

	std::filesystem::path path;
	std::unordered_map<std::string, std::string> defaults;    // key -> default value
	std::unordered_map<std::string, std::string> keySection;  // key -> section in config.ini
};
