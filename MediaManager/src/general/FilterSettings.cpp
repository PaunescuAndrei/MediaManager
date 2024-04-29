#include "stdafx.h"
#include "FilterSettings.h"
#include <QVariantMap>
#include <QJsonArray>

FilterSettings::FilterSettings() {
	this->initJson();
}

FilterSettings::FilterSettings(QJsonObject json)
{
	this->initJson();
	QVariantMap map = this->json.toVariantMap();
	map.insert(json.toVariantMap());
	this->json = QJsonObject::fromVariantMap(map);
}

void FilterSettings::initJson()
{
	this->json.insert("rating_mode", ">=");
	this->json.insert("rating", "0.0");
	this->json.insert("views_mode", ">=");
	this->json.insert("views", "0");
	this->json.insert("authors", QJsonArray{ "All" });
	this->json.insert("tags", QJsonArray{ "All" });
	this->json.insert("types_include", QJsonArray{ "All" });
	this->json.insert("types_exclude", QJsonArray{ "All" });
	this->json.insert("watched", QJsonArray{ "No" });
	this->json.insert("ignore_defaults", "False");
	this->json.insert("include_unrated", "True");
}
void FilterSettings::setJson(QJsonObject json)
{
	this->json = json;
}

FilterSettings::~FilterSettings() {

}