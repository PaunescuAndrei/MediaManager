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
	std::unordered_map<std::string, std::string> defaults;
	QString defaultFor(QString const& item) const;
};
