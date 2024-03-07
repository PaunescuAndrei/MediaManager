#pragma once
#include <QSqlDatabase>

//not used
class shokoDB
{
public:
	QSqlDatabase db;
	shokoDB(std::string location, std::string conname = "");
	QMap<QString, QString> get_playlist(QString playlist);
	bool check_if_exists(QString id);
	void update_playlist_items(QString items, QString playlist);
	QPair<QString, QString> get_random_item(QString items_input);
	QString combine_items(QString old_items_input, QString new_items_input);
	int clean_playlists();
	QStringList getFiles(std::string animegroupid);
	~shokoDB();
};

