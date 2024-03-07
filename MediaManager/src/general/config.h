#pragma once
#define MINI_CASE_SENSITIVE
#include "ini.h"
#include <string>
class Config
{
public:
	mINI::INIFile *file;
	mINI::INIStructure ini;
	Config(QString filepath);
	~Config();
	void init_defaults();
	void open_config(QString filepath);
	void save_config();
	QString get(QString item);
	void set(QString key, QString value);
	bool get_bool(QString item);
};

