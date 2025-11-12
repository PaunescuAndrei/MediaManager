#include "stdafx.h"
#include "shokoDB.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include "utils.h"
#include <QSqlError>
#include <QSqlDriver>
#include <QFileInfo>
#include <QString>

shokoDB::shokoDB(std::string location, std::string conname) {
    this->db = QSqlDatabase::addDatabase("QSQLITE", conname.c_str());
    this->db.setDatabaseName(QString::fromStdString(location));
    if (QFileInfo::exists(QString::fromStdString(location))) {
        if (!this->db.open())  // open it and keep it opened
        {
            qDebug() << "Error oppening database.\n";
        }
    }
    else {
        qDebug() << "Database doesn't exist.\n";
    }
}

QMap<QString,QString> shokoDB::get_playlist(QString playlist) {
    QMap<QString, QString> results = QMap<QString, QString>();
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QStringLiteral("SELECT PlaylistID, PlaylistName, PlaylistItems, DefaultPlayOrder, PlayWatched, PlayUnwatched FROM Playlist WHERE PlaylistName = ? "));
    query.addBindValue(playlist);
    query.exec();
    bool found = query.first();
    if (found == true) {
        results["PlaylistID"] = query.value(0).toString();
        results["PlaylistName"] = query.value(1).toString();
        results["PlaylistItems"] = query.value(2).toString();
        results["DefaultPlayOrder"] = query.value(3).toString();
        results["PlayWatched"] = query.value(4).toString();
        results["PlayUnwatched"] = query.value(5).toString();
    }
    return results;
}

bool shokoDB::check_if_exists(QString id) {
    QMap<QString, QString> results = QMap<QString, QString>();
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QStringLiteral("SELECT AnimeSeriesid, anidb_id FROM AnimeSeries WHERE animeseriesid = ? ;"));
    query.addBindValue(id);
    query.exec();
    return query.first();
}

void shokoDB::update_playlist_items(QString items, QString playlist) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QStringLiteral("UPDATE Playlist SET PlaylistItems = ? WHERE PlaylistName = ? ;"));

    query.addBindValue(items);
    query.addBindValue(playlist);

    if (!query.exec()) {
        qDebug() << "update_playlist_items " << query.lastError().text();
    }
}

QPair<QString,QString> shokoDB::get_random_item(QString items_input) {
    if (items_input.isEmpty())
        return QPair<QString, QString>();
    QStringList items = items_input.split("|");
    QString new_item = items.takeAt(utils::randint(0,items.size()-1));
    return {new_item,items.join("|")};
}

QString shokoDB::combine_items(QString old_items_input,QString new_items_input) {
    QStringList old_items = old_items_input.split("|");
    QStringList new_items = new_items_input.split("|");
    old_items += new_items;
    return old_items.join("|");
}

int shokoDB::clean_playlists() {
    int count = 0;
    QSqlQuery query = QSqlQuery("SELECT PlaylistID,playlistname,playlistitems FROM Playlist;",this->db);
    query.exec();
    while (query.next()) {
        QString playlistname = query.value(1).toString();
        QString playlistitems = query.value(2).toString();
        if (!playlistitems.isEmpty()) {
            QStringList items_split = playlistitems.split("|");
            int len = items_split.size();
            for (QString& item : QStringList(items_split)) {
                QString item_id = item.section(';', 1);
                if (!this->check_if_exists(item_id)) {
                    count += items_split.removeAll(item);
                }
            }
            if(len != items_split.size())
                this->update_playlist_items(items_split.join("|"), playlistname);
        }
    }
    return count;
}

QStringList shokoDB::getFiles(std::string animegroupid) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QStringLiteral("SELECT ans.animegroupid,ans.anidb_id, ip.importfolderlocation || vlp.filepath as filepath FROM CrossRef_File_Episode cfe INNER JOIN AnimeSeries ans on cfe.AnimeID = ans.AniDB_ID INNER JOIN VideoLocal vl on vl.Hash = cfe.Hash INNER JOIN VideoLocal_Place vlp on vl.VideoLocalID = vlp.VideoLocalID INNER JOIN ImportFolder ip on ip.importfolderid = vlp.importfolderid where ans.AnimeSeriesID = ?;"));
    query.addBindValue(QString::fromStdString(animegroupid));
    if (!query.exec()) {
        qDebug() << "getFiles " << query.lastError().text();
    }
    QStringList files = QStringList();
    while (query.next()) {
        QString filepath = query.value(2).toString();
        files.append(filepath);
    }
     return files;
}

shokoDB::~shokoDB()
{
    this->db.close();
}
