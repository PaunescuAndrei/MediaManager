#pragma once
#include <QJsonObject>

class FilterSettings
{
public:
	QJsonObject json;
	FilterSettings();
	FilterSettings(QJsonObject json);
	void initJson();
	void setJson(QJsonObject json);
	~FilterSettings();
};

