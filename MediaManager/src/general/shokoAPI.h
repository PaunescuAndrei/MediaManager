#pragma once
#include <QNetworkAccessManager>

class shokoAPI
{
public:
	QNetworkAccessManager* mgr;
	QString host_ip;
	QString apikey = "";
	shokoAPI(QString host_ip);
	bool auth();
	bool is_auth();
	QJsonArray get_playlists();
	QJsonObject get_playlist(QString PlaylistName, QJsonArray playlists);
	QJsonObject get_playlist(QString PlaylistName);
	bool update_playlist(QString PlaylistName, QJsonObject playlist_obj);
	bool update_playlist_items(QString PlaylistName, QString PlaylistItems);
	bool check_if_exists(QString id);
	QPair<QString, QString> get_first_item(QString items_input);
	QPair<QString, QString> get_random_item(QString items_input);
	QString combine_items(QString old_items_input, QString new_items_input);
	int clean_playlists();
	QJsonArray get_import_folders();
	QStringList get_files(QString id, QString type);
	~shokoAPI();
};

