#include "stdafx.h"
#include "sqliteDB.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include "utils.h"
#include <QSqlError>
#include <QSqlDriver>
#include "TreeWidgetItem.h"
#include "MainApp.h"
#include <QJsonObject>
#include <QJsonDocument>
#include "VideoData.h"

sqliteDB::sqliteDB(QString location,std::string conname) {
    // In the constructor or initialization function of that object      
    this->db = QSqlDatabase::addDatabase("QSQLITE", conname.c_str());
    this->location = location;
    this->db.setDatabaseName(location);
    if (!this->db.open())  // open it and keep it opened
    {
        if (qMainApp) {
            qMainApp->logger->log(QString("Error opening database at: %1 with connection: %2").arg(location, QString::fromStdString(conname)), "Database", location);
            qMainApp->showErrorMessage("Error oppening database.");
        }
    }

    this->enableForeignKeys();
    //QSqlQuery query = QSqlQuery("PRAGMA journal_mode = WAL;", this->db);
    //query.exec();
    this->createTables();
}

void sqliteDB::enableForeignKeys()
{
    QSqlQuery query(this->db);
    query.prepare("PRAGMA foreign_keys = ON;");
    query.exec();
}

QList<QTreeWidgetItem*> sqliteDB::getVideos(QString category) {
    QSqlQuery query = QSqlQuery(this->db);

    QString query_string = "SELECT v.id,v.path,GROUP_CONCAT(t.name, ', ' ORDER BY t.display_priority ASC, t.id) AS tags,v.type,v.watched,v.views,v.rating,v.author,v.name, datetime(v.date_created, 'localtime'), datetime(v.last_watched, 'localtime') from videodetails v "
        "LEFT JOIN tags_relations tr ON v.id = tr.video_id "
        "LEFT JOIN tags t ON tr.tag_id = t.id WHERE v.category = :category "
        "GROUP BY v.id; ";

    query.prepare(query_string);
    query.bindValue(":category",category);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getVideos (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getVideos " + category + " " + query.lastError().text());
        }
    }
    QList<QTreeWidgetItem*> itemlist = QList<QTreeWidgetItem*>();
    while (query.next()) {
        int id = query.value(0).toInt();
        QString path = query.value(1).toString();
        QString tags = query.value(2).toString();
        QString type = query.value(3).toString();
        if (type.isEmpty()) {
            type = "None";
        }
        QString watched = query.value(4).toString();
        QString views = query.value(5).toString();
        double rating = query.value(6).toDouble();
        QString author = query.value(7).toString();
        QString name = query.value(8).toString();
        QDateTime date_created = query.value(9).toDateTime();
        QDateTime last_watched = query.value(10).toDateTime();
        TreeWidgetItem *item = new TreeWidgetItem(nullptr);
        item->setData(ListColumns["AUTHOR_COLUMN"], Qt::DisplayRole, author);
        item->setData(ListColumns["NAME_COLUMN"], Qt::DisplayRole, name);
        item->setData(ListColumns["PATH_COLUMN"], Qt::DisplayRole, path);
        item->setData(ListColumns["TAGS_COLUMN"], Qt::DisplayRole, tags);
        item->setData(ListColumns["TYPE_COLUMN"], Qt::DisplayRole, type);
        item->setData(ListColumns["WATCHED_COLUMN"], Qt::DisplayRole, watched);
        item->setData(ListColumns["VIEWS_COLUMN"], Qt::DisplayRole, views);
        item->setData(ListColumns["DATE_CREATED_COLUMN"], Qt::DisplayRole, date_created);
        item->setData(ListColumns["LAST_WATCHED_COLUMN"], Qt::DisplayRole, last_watched);
        item->setData(ListColumns["PATH_COLUMN"], CustomRoles::id, id);
        item->setData(ListColumns["RATING_COLUMN"], CustomRoles::rating,rating);
        itemlist.append(item);
    }
    return itemlist;
}

QVector<VideoRow> sqliteDB::getVideosData(QString category) {
    QSqlQuery query = QSqlQuery(this->db);

    QString query_string = "SELECT v.id,v.path,GROUP_CONCAT(t.name, ', ' ORDER BY t.display_priority ASC, t.id) AS tags,v.type,v.watched,v.views,v.rating,v.author,v.name, datetime(v.date_created, 'localtime'), datetime(v.last_watched, 'localtime') from videodetails v "
        "LEFT JOIN tags_relations tr ON v.id = tr.video_id "
        "LEFT JOIN tags t ON tr.tag_id = t.id WHERE v.category = :category "
        "GROUP BY v.id; ";

    query.prepare(query_string);
    query.bindValue(":category", category);
    QVector<VideoRow> result;
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getVideosData (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getVideosData " + category + " " + query.lastError().text());
        }
        return result;
    }

    result.reserve(query.size() > 0 ? query.size() : 0);
    while (query.next()) {
        VideoRow row;
        row.id = query.value(0).toInt();
        row.path = query.value(1).toString();
        row.tags = query.value(2).toString();
        row.type = query.value(3).toString();
        if (row.type.isEmpty()) {
            row.type = "None";
        }
        row.watched = query.value(4).toString();
        row.views = query.value(5).toString();
        row.rating = query.value(6).toDouble();
        row.author = query.value(7).toString();
        row.name = query.value(8).toString();
        row.dateCreated = query.value(9).toDateTime();
        row.lastWatched = query.value(10).toDateTime();
        result.push_back(std::move(row));
    }
    return result;
}

QSet<Tag> sqliteDB::getAllTags() {
    QSqlQuery query = QSqlQuery(this->db);
    QString query_string = "SELECT id, name, display_priority from tags";
    QSet<Tag> tags = QSet<Tag>();

    query.prepare(query_string);
    if (!query.exec()) {
        if (qMainApp) {
			qMainApp->logger->log(QString("Database Error at getAllTags: %1").arg(query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getAllTags " + query.lastError().text());
        }
    }
    while (query.next())
    {
        int id = query.value(0).toInt();
        QString name = query.value(1).toString();
        int display_priority = query.value(1).toInt();
        tags.insert({ id,name,display_priority });
    }
    return tags;
}

QSet<Tag> sqliteDB::getTags(int video_id) {
    QSqlQuery query = QSqlQuery(this->db);
    QString query_string = "SELECT t.id, t.name, t.display_priority from tags t JOIN tags_relations tr ON t.id = tr.tag_id AND tr.video_id = :video_id ;";
    //QString query_string_excluded = "SELECT t.id, t.name, t.display_priority from tags t LEFT JOIN tags_relations tr ON t.id = tr.tag_id AND tr.video_id = :video_id WHERE tr.video_id IS NULL; ";
    QSet<Tag> tags = QSet<Tag>();

    query.prepare(query_string);
    query.bindValue(":video_id", video_id);
    if (!query.exec()) {
        if (qMainApp) {
			qMainApp->logger->log(QString("Database Error at getTags (%1): %2").arg(QString::number(video_id), query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getTags " + QString::number(video_id) + " " + query.lastError().text());
        }
    }
    while (query.next()) 
    {
        int id = query.value(0).toInt();
        QString name = query.value(1).toString();
        int display_priority = query.value(1).toInt();
        tags.insert({ id,name,display_priority });
    }
    return tags;
}

void sqliteDB::insertTag(int video_id, int tag_id) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("INSERT OR IGNORE INTO tags_relations(video_id,tag_id) VALUES(?, ?)"));
    query.addBindValue(video_id);
    query.addBindValue(tag_id);
    if (!query.exec()) {
        if (qMainApp) {
			qMainApp->logger->log(QString("Database Error at insertTag (%1, %2): %3").arg(QString::number(video_id), QString::number(tag_id), query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("insertTag " + QString::number(video_id) + QString::number(tag_id) + " " + query.lastError().text());
        }
    }
}

void sqliteDB::deleteExtraTags(int video_id, QSet<int> tags_id) {
    QSqlQuery query = QSqlQuery(this->db);
    QStringList tags_id_stringlist = QStringList();
    for (auto& tag_id : tags_id) {
        tags_id_stringlist << QString("\'%1\'").arg(QString::number(tag_id));
    }
    query.prepare(QString("DELETE FROM tags_relations WHERE video_id = ? AND tag_id NOT IN (%1)").arg(tags_id_stringlist.join(",")));
    query.addBindValue(video_id);
    if (!query.exec()) {
        if (qMainApp) {
			qMainApp->logger->log(QString("Database Error at deleteExtraTags (%1): %2").arg(QString::number(video_id), query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("deleteExtraTags " + QString::number(video_id) + " " + query.lastError().text());
        }
    }
}

QStringList sqliteDB::getAuthors(QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT DISTINCT author from videodetails where category = ? ORDER BY author"));
    query.addBindValue(category);
    if (!query.exec()) {
        if (qMainApp) {
			qMainApp->logger->log(QString("Database Error at getAuthors (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getAuthors " + category + " " + query.lastError().text());
        }
    }
    QStringList itemlist = QStringList();
    while (query.next()) {
        QString author = query.value(0).toString();
        itemlist.append(author);
    }
    return itemlist;
}

QStringList sqliteDB::getUniqueNames(QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT DISTINCT name from videodetails where category = ? ORDER BY name"));
    query.addBindValue(category);
    if (!query.exec()) {
        if (qMainApp) {
			qMainApp->logger->log(QString("Database Error at getUniqueNames (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getUniqueNames " + category + " " + query.lastError().text());
        }
    }
    QStringList itemlist = QStringList();
    while (query.next()) {
        QString name = query.value(0).toString();
        itemlist.append(name);
    }
    return itemlist;
}

QStringList sqliteDB::getUniqueTags(QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT DISTINCT name from tags"));
    if (!query.exec()) {
        if (qMainApp) {
			qMainApp->logger->log(QString("Database Error at getUniqueTags (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getUniqueTags " + category + " " + query.lastError().text());
        }
    }
    QStringList itemlist = QStringList();
    while (query.next()) {
        QString name = query.value(0).toString();
        itemlist.append(name);
    }
    return itemlist;
}

QStringList sqliteDB::getTypes(QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT DISTINCT type from videodetails where category = ? ORDER BY type"));
    query.addBindValue(category);
    if (!query.exec()) {
        if (qMainApp) {
			qMainApp->logger->log(QString("Database Error at getTypes (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getTypes " + category + " " + query.lastError().text());
        }
    }
    QStringList itemlist = QStringList();
    while (query.next()) {
        QString type = query.value(0).toString();
        itemlist.append(type);
    }
    return itemlist;
}

void sqliteDB::setFilterSettings(QString value,QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("INSERT OR REPLACE INTO maininfo(name, value, category) VALUES(\"FilterSettings\",?,?)"));
    if (value.isEmpty())
        query.addBindValue(QVariant(QVariant::String));
    else
        query.addBindValue(value);
    query.addBindValue(category);
    if (!query.exec()) {
        if (qMainApp) {
			qMainApp->logger->log(QString("Database Error at setFilterSettings (%2, %3): %1").arg(query.lastError().text(), category, value), "Database", query.lastError().text());
            qMainApp->showErrorMessage("setFilterSettings " + category + value + " " + query.lastError().text());
        }
    }
}

QJsonObject sqliteDB::getFilterSettings(QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT value from maininfo WHERE name == \"FilterSettings\" AND category == ?"));
    query.addBindValue(category);
    if (!query.exec()) {
        if (qMainApp) {
			qMainApp->logger->log(QString("Database Error at getFilterSettings (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getFilterSettings " + category + " " + query.lastError().text());
        }
    }
    bool found = query.first();
    if (found == true)
        return QJsonDocument::fromJson(query.value(0).toString().toUtf8()).object();
    else
        return QJsonObject();
}

QString sqliteDB::getVideoProgress(int video_id, QString fallback) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT progress from videodetails WHERE id = ?"));
    query.addBindValue(video_id);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getVideoProgress (%1): %2").arg(QString::number(video_id), query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getVideoProgress " + QString::number(video_id) + " " + query.lastError().text());
        }
    }
    bool found = query.first();
    if (found == true)
        return query.value(0).toString();
    else
        return fallback;
}

int sqliteDB::getVideoId(QString path, QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT id from videodetails WHERE path = ? and category = ?"));
    query.addBindValue(path);
    query.addBindValue(category);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getVideoId (%1, %2): %3").arg(path, category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getVideoId " + path + category + " " + query.lastError().text());
        }
    }
    bool found = query.first();
    if (found == true)
        return query.value(0).toInt();
    else
        return -1;
}

bool sqliteDB::checkIfVideoInDB(QString path, QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT path from videodetails WHERE path = ? and category = ?"));
    query.addBindValue(path);
    query.addBindValue(category);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at checkIfVideoInDB (%1, %2): %3").arg(path, category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("checkIfVideoInDB " + path + category + " " + query.lastError().text());
        }
    }
    bool found = query.first();
    if (found == true)
        return true;
    else
        return false;

}

int sqliteDB::getVideosWatched(QString category, int fallback) {
    QSqlQuery query = QSqlQuery(this->db);
    if(category == "ALL")
        query.prepare(QString("SELECT SUM(views) FROM videodetails;"));
    else {
        query.prepare(QString("SELECT SUM(views) FROM videodetails where category = ?;"));
        query.addBindValue(category);
    }
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getVideosWatched (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getVideosWatched " + category + " " + query.lastError().text());
        }
    }
    bool found = query.first();
    if (found == true)
        return query.value(0).toInt();
    else
        return fallback;
}

double sqliteDB::getAverageRating(QString category, double fallback) {
    QSqlQuery query = QSqlQuery(this->db);
    if (category == "ALL")
        query.prepare(QString("SELECT AVG(NULLIF(rating, 0)) FROM videodetails;"));
    else {
        query.prepare(QString("SELECT AVG(NULLIF(rating, 0)) FROM videodetails where category = ?;"));
        query.addBindValue(category);
    }
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getAverageRating (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getAverageRating " + category + " " + query.lastError().text());
        }
    }
    bool found = query.first();
    if (found == true)
        return query.value(0).toDouble();
    else
        return fallback;
}

double sqliteDB::getAverageRatingAuthor(QString author, QString category, double fallback) {
    QSqlQuery query = QSqlQuery(this->db);
    if (category == "ALL")
        query.prepare(QString("SELECT AVG(NULLIF(rating, 0)) FROM videodetails where rating > 0 and author = ?;"));
    else {
        query.prepare(QString("SELECT AVG(NULLIF(rating, 0)) FROM videodetails where rating > 0 and category = ? and author = ?;"));
        query.addBindValue(category);
    }
    query.addBindValue(author);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getAverageRatingAuthor (%1, %2): %3").arg(author, category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getAverageRatingAuthor " + author + category + " " + query.lastError().text());
        }
    }
    bool found = query.first();
    if (found == true)
        return query.value(0).toDouble();
    else
        return fallback;
}

void sqliteDB::setMainInfoValue(QString name, QString category, QString value) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE maininfo SET value = ? WHERE name = ? AND category = ?"));
    if(value.isEmpty())
        query.addBindValue(QVariant(QVariant::String));
    else
        query.addBindValue(value);
    query.addBindValue(name);
    query.addBindValue(category);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at setMainInfoValue (%1, %2, %3): %4").arg(name, category, value, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("setMainInfoValue " + name + category + value + " " + query.lastError().text());
        }
    }
}

void sqliteDB::deleteVideo(int video_id) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("DELETE FROM videodetails WHERE id = ?"));
    query.addBindValue(video_id);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at deleteVideo (%1): %2").arg(QString::number(video_id), query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("deleteVideo " + QString::number(video_id) + " " + query.lastError().text());
        }
    }
}

void sqliteDB::updateWatchedState(int video_id, double progress, QString watched, bool increment, bool update_last_watched) {
    QSqlQuery query = QSqlQuery(this->db);
    QString main_query_string = "UPDATE videodetails SET progress=?, watched=?";
    QString views_increment_string = increment ? ", views = views + 1" : "";
    QString update_last_watched_string = (update_last_watched) ? ", last_watched = CURRENT_TIMESTAMP" : "";
    QString end_query_string = " WHERE id = ?";
    QString final_string = main_query_string % views_increment_string % update_last_watched_string % end_query_string;
    query.prepare(final_string);
    query.addBindValue(QString::number(progress,'f',4));
    query.addBindValue(watched);
    query.addBindValue(video_id);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at updateWatchedState (%1, %2, %3, %4): %5").arg(QString::number(video_id), QString::number(progress), QString::number(increment), QString::number(update_last_watched), query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("updateWatchedState " + QString::number(video_id) + QString::number(progress) + QString::number(increment) + QString::number(update_last_watched) + " " + query.lastError().text());
        }
    }
}

void sqliteDB::updateWatchedState(int video_id, QString watched, bool increment, bool update_last_watched) {
    QSqlQuery query = QSqlQuery(this->db);
    QString main_query_string = "UPDATE videodetails SET watched=?";
    QString views_increment_string = increment ? ", views = views + 1" : "";
    QString update_last_watched_string = (update_last_watched) ? ", last_watched = CURRENT_TIMESTAMP" : "";
    QString end_query_string = " WHERE id = ?";
    QString final_string = main_query_string % views_increment_string % update_last_watched_string % end_query_string;
    query.prepare(final_string);
    query.addBindValue(watched);
    query.addBindValue(video_id);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at updateWatchedState (%1, %2, %3): %4").arg(QString::number(video_id), QString::number(increment), QString::number(update_last_watched), query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("updateWatchedState " + QString::number(video_id) + QString::number(increment) + QString::number(update_last_watched) + " " + query.lastError().text());
        }
    }
}

void sqliteDB::updateType(int video_id, QString type_val) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE videodetails SET type=? WHERE id = ?"));
    query.addBindValue(type_val);
    query.addBindValue(video_id);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at updateType (%1, %2): %3").arg(QString::number(video_id), type_val, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("updateType " + QString::number(video_id) + type_val + " " + query.lastError().text());
        }
    }
}

void sqliteDB::updateAuthor(int video_id, QString author) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE videodetails SET author=? WHERE id = ?"));
    query.addBindValue(author);
    query.addBindValue(video_id);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at updateAuthor (%1, %2): %3").arg(QString::number(video_id), author, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("updateAuthor " + QString::number(video_id) + author + " " + query.lastError().text());
        }
    }
}

void sqliteDB::updateName(int video_id, QString name) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE videodetails SET name=? WHERE id = ?"));
    query.addBindValue(name);
    query.addBindValue(video_id);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at updateName (%1, %2): %3").arg(QString::number(video_id), name, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("updateName " + QString::number(video_id) + name + " " + query.lastError().text());
        }
    }
}

void sqliteDB::updateRating(int video_id, double rating) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE videodetails SET rating=? WHERE id = ?"));
    query.addBindValue(QString::number(rating));
    query.addBindValue(video_id);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at updateRating (%1, %2): %3").arg(QString::number(video_id), QString::number(rating), query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("updateRating " + QString::number(video_id) + QString::number(rating) + " " + query.lastError().text());
        }
    }
}

void sqliteDB::setViews(int video_id, int value) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE videodetails SET views = ? WHERE id = ?"));
    query.addBindValue(QString::number(value));
    query.addBindValue(video_id);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at setViews (%1, %2): %3").arg(QString::number(video_id), QString::number(value), query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("setViews " + QString::number(video_id) + QString::number(value) + " " + query.lastError().text());
        }
    }
}

void sqliteDB::incrementVideoViews(int video_id, int value, bool update_last_watched) {
    QSqlQuery query = QSqlQuery(this->db);
    QString main_query_string = "UPDATE videodetails SET views = views + ?";
    QString update_last_watched_string = (update_last_watched) ? ", last_watched = CURRENT_TIMESTAMP" : "";
    QString end_query_string = " WHERE id = ?";
    QString final_string = main_query_string % update_last_watched_string % end_query_string;
    query.prepare(final_string);

    query.addBindValue(QString::number(value));
    query.addBindValue(video_id);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at incrementVideoViews (%1, %2, %3): %4").arg(QString::number(video_id), QString::number(value), QString::number(update_last_watched), query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("incrementVideoViews " + QString::number(video_id) + QString::number(value) + QString::number(update_last_watched) + " " + query.lastError().text());
        }
    }
}

void sqliteDB::insertVideo(QString path, QString category, QString name, QString author, QString type) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("INSERT OR IGNORE INTO videodetails(path, category, type, author, name) VALUES(?, ?, ?, ?, ?)"));
    query.addBindValue(path);
    query.addBindValue(category);
    query.addBindValue(type);
    query.addBindValue(author);
    query.addBindValue(name);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at insertVideo (%1, %2, %3, %4, %5): %6").arg(path, category, name, author, type, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("insertVideo " + path + category + name + author + type + " " + query.lastError().text());
        }
    }
}

QString sqliteDB::getMainInfoValue(QString name, QString category, QString fallback) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT value from maininfo WHERE name = ? AND category = ?"));
    query.addBindValue(name);
    query.addBindValue(category);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getMainInfoValue (%1, %2): %3").arg(name, category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getMainInfoValue " + name + category + " " + query.lastError().text());
        }
    }
    bool found = query.first();
    if (found == true)
        return query.value(0).toString();
    else
        return fallback;
}

std::tuple<int, QString, QString, QString, QString> sqliteDB::getCurrentVideo(QString category, std::tuple<int, QString, QString, QString, QString> fallback) {
    QSqlQuery query = QSqlQuery(this->db);
    
    QString query_string = "SELECT vd.id, mi.value, vd.name, vd.author, GROUP_CONCAT(t.name, ', ' ORDER BY t.display_priority ASC, t.id) AS tags FROM maininfo mi "
        "LEFT JOIN videodetails vd ON mi.value = vd.path "
        "LEFT JOIN tags_relations tr ON vd.id = tr.video_id "
        "LEFT JOIN tags t ON tr.tag_id = t.id "
        "WHERE mi.category = ? and mi.name = \'current\';";

    query.prepare(query_string);
    query.addBindValue(category);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getCurrentVideo (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getCurrentVideo " + category + " " + query.lastError().text());
        }
    }
    bool found = query.first();
    if (found == true)
        return std::tuple<int, QString, QString, QString, QString>{query.value(0).toInt(), query.value(1).toString(), query.value(2).toString(), query.value(3).toString(), query.value(4).toString()};
    else
        return fallback;
}

QList<VideoWeightedData> sqliteDB::getVideos(QString category, QJsonObject settings) {
    QSqlQuery query = QSqlQuery(this->db);
    QString main_query_string = "SELECT v.id, v.path,SUM(t.weight) AS tags, v.views, v.rating FROM videodetails v";
    QString join_query_string = " LEFT JOIN tags_relations tr ON v.id = tr.video_id "
                               "LEFT JOIN tags t ON tr.tag_id = t.id";
    QString category_query_string = " WHERE v.category == :cat_bound_value";
    QString rating_query_string = QString(" AND (v.rating %1 %2 %3)").arg(settings.value("rating_mode").toString(), settings.value("rating").toString(), utils::text_to_bool(settings.value("include_unrated").toString().toStdString()) ? "OR rating == 0" : "");
    QString views_query_string = QString(" AND v.views %1 %2").arg(settings.value("views_mode").toString(), settings.value("views").toString());
    QString end_query_string =" GROUP BY v.id; ";

    QString authors_query_string;
    QJsonArray authors_values = settings.value("authors").toArray();
    QStringList authors;
    for (QJsonValue value : authors_values) {
        if (value == "All") {
            authors.clear();
            break;
        }
        authors << QString("\'%1\'").arg(value.toString());
    }
    if (authors.isEmpty())
        authors_query_string = "";
    else
        authors_query_string = QString(" AND v.author in (%1)").arg(authors.join(","));

    QString tags_query_string_join;
    QString tags_query_string_where;
    QJsonArray tags_values = settings.value("tags").toArray();
    QStringList tags;
    bool no_tags = false;
    for (QJsonValue value : tags_values) {
        if (value == "All") {
            tags.clear();
            break;
        }
        if (value == "None") {
            no_tags = true;
            continue;
        }
        tags << QString("\'%1\'").arg(value.toString());
    }
    if (tags.isEmpty()) {
        tags_query_string_join = "";
        tags_query_string_where = "";

    }
    else {
        //tags_query_string_join = " LEFT JOIN tags_relations tr ON v.id = tr.video_id";
        QString no_tags_query = (no_tags) ? "OR tr.tag_id IS NULL" : "";
        tags_query_string_where = QString(" AND (tr.tag_id in (%1) %2)").arg(tags.join(","), no_tags_query);
    }

    QString types_query_string;
    QJsonArray types_include_values = settings.value("types_include").toArray();
    QJsonArray types_exclude_values = settings.value("types_exclude").toArray();
    QStringList types_include;
    QStringList types_exclude;
    for (QJsonValue value : types_include_values) {
        if (value == "All") {
            types_include.clear();
            break;
        }
        types_include << QString("\'%1\'").arg(value.toString());
    }
    for (QJsonValue value : types_exclude_values) {
        types_exclude << QString("\'%1\'").arg(value.toString());
    }
    if (types_include.isEmpty())
        types_query_string = "";
    else
        types_query_string = QString(" AND v.type in (%1)").arg(types_include.join(","));
    if (not types_exclude.isEmpty()) {
        types_query_string += QString(" AND v.type not in (%1)").arg(types_exclude.join(","));
    }

    QString watched_query_string;
    QJsonArray watched_values = settings.value("watched").toArray();
    QStringList watched;
    for (QJsonValue value : watched_values) {
        if (value == "All") {
            watched.clear();
            break;
        }
        watched << QString("\'%1\'").arg(value.toString());
    }
    if (watched.isEmpty())
        watched_query_string = "";
    else
        watched_query_string = QString(" AND v.watched in (%1)").arg(watched.join(","));

    QString final_query_string = main_query_string % join_query_string % tags_query_string_join % category_query_string % tags_query_string_where % authors_query_string % types_query_string % watched_query_string % rating_query_string % views_query_string % end_query_string;
    query.prepare(final_query_string);
    query.bindValue(":cat_bound_value", category);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getVideos (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getVideos " + category + " " + query.lastError().text());
        }
    }
    QList<VideoWeightedData> videolist = QList<VideoWeightedData>();
    while (query.next()) {
        VideoWeightedData video;
        video.id = query.value(0).toInt();
        video.path = query.value(1).toString();
        video.views = query.value(3).toDouble();
        video.rating = query.value(4).toDouble();
        video.tagsWeight = query.value(2).toDouble();
        videolist.append(video);
    }
    return videolist;
}

void sqliteDB::updateVideoProgress(int video_id, double progress)
{
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE videodetails SET progress=? WHERE id = ?"));
    query.addBindValue(QString::number(progress,'f',4));
    query.addBindValue(video_id);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at updateVideoProgress (%1): %2").arg(QString::number(video_id), query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("updateVideoProgress " + QString::number(video_id) + " " + query.lastError().text());
        }
    }
}

void sqliteDB::updateItem(int video_id, QString new_path, QString new_author, QString new_name, QString new_type, QString new_category, QString new_progress, QString new_watched, QString new_views, QString new_rating) {
    QSqlQuery query = QSqlQuery(this->db);
    QString main_query_string = "UPDATE videodetails SET";
    QStringList mid_string_list = QStringList();

    if (!new_path.isEmpty()) mid_string_list.append(" path=(:new_path_)");
    if (!new_author.isEmpty()) mid_string_list.append(" author=(:new_author_)");
    if (!new_name.isEmpty()) mid_string_list.append(" name=(:new_name_)");
    if (!new_type.isEmpty()) mid_string_list.append(" type=(:new_type_)");
    if (!new_category.isEmpty()) mid_string_list.append(" category=(:new_category_)");
    if (!new_progress.isEmpty()) mid_string_list.append(" progress=(:new_progress_)");
    if (!new_watched.isEmpty()) mid_string_list.append(" watched=(:new_watched_)");
    if (!new_views.isEmpty()) mid_string_list.append(" views=(:new_views_)");
    if (!new_rating.isEmpty()) mid_string_list.append(" rating=(:new_rating_)");
    QString end_query_string = " WHERE id = (:video_id_)";

    QString mid_string = mid_string_list.join(",");
    if (mid_string.isEmpty())
        return;
    QString final_query_string = main_query_string % mid_string % end_query_string;
    query.prepare(final_query_string);
    query.bindValue(":video_id_", video_id);
    if (!new_path.isEmpty()) query.bindValue(":new_path_", new_path);
    if (!new_author.isEmpty()) query.bindValue(":new_author_", new_author);
    if (!new_name.isEmpty()) query.bindValue(":new_name_", new_name);
    if (!new_type.isEmpty()) query.bindValue(":new_type_", new_type);
    if (!new_category.isEmpty()) query.bindValue(":new_category_", new_category);
    if (!new_progress.isEmpty()) query.bindValue(":new_progress_", new_progress);
    if (!new_watched.isEmpty()) query.bindValue(":new_watched_", new_watched);
    if (!new_views.isEmpty()) query.bindValue(":new_views_", new_views);
    if (!new_rating.isEmpty()) query.bindValue(":new_rating_", new_rating);

    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at updateItem (%1): %2").arg(QString::number(video_id), query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("updateItem " + QString::number(video_id) + " " + query.lastError().text());
        }
    }
}

void sqliteDB::resetWatched(QString category, double progress, QString watched) {
    QSqlQuery query = QSqlQuery(this->db);
    QString query_string = QString("UPDATE videodetails SET %1 watched = ? WHERE category = ?").arg(progress >=0 ? "progress = ? ," : "");
    query.prepare(query_string);
    if(progress >= 0)
        query.addBindValue(progress);
    query.addBindValue(watched);
    query.addBindValue(category);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at resetWatched (%1, %2, %3): %4").arg(category, QString::number(progress), watched, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("resetWatched " + category + QString::number(progress) + watched + " " + query.lastError().text());
        }
    }
}

void sqliteDB::resetWatched(QString category, QJsonObject settings, double progress, QString watched_set_value) {
    QSqlQuery query = QSqlQuery(this->db);
    QString main_query_string = "UPDATE videodetails SET progress = 0, watched = \"No\" WHERE id IN (%1);";
    QString sub_query_string = "SELECT v.id FROM videodetails v";
    QString category_query_string = " WHERE v.category == :cat_bound_value";
    QString rating_query_string = QString(" AND (v.rating %1 %2 %3)").arg(settings.value("rating_mode").toString(), settings.value("rating").toString(), utils::text_to_bool(settings.value("include_unrated").toString().toStdString()) ? "OR rating == 0" : "");
    QString views_query_string = QString(" AND v.views %1 %2").arg(settings.value("views_mode").toString(), settings.value("views").toString());

    QString authors_query_string;
    QJsonArray authors_values = settings.value("authors").toArray();
    QStringList authors;
    for (QJsonValue value : authors_values) {
        if (value == "All") {
            authors.clear();
            break;
        }
        authors << QString("\'%1\'").arg(value.toString());
    }
    if (authors.isEmpty())
        authors_query_string = "";
    else
        authors_query_string = QString(" AND v.author in (%1)").arg(authors.join(","));

    QString tags_query_string_join;
    QString tags_query_string_where;
    QJsonArray tags_values = settings.value("tags").toArray();
    QStringList tags;
    bool no_tags = false;
    for (QJsonValue value : tags_values) {
        if (value == "All") {
            tags.clear();
            break;
        }
        if (value == "None") {
            no_tags = true;
            continue;
        }
        tags << QString("\'%1\'").arg(value.toString());
    }
    if (tags.isEmpty()) {
        tags_query_string_join = "";
        tags_query_string_where = "";

    }
    else {
        tags_query_string_join = " LEFT JOIN tags_relations tr ON v.id = tr.video_id";
        QString no_tags_query = (no_tags) ? "OR tr.tag_id IS NULL" : "";
        tags_query_string_where = QString(" AND (tr.tag_id in (%1) %2)").arg(tags.join(","), no_tags_query);
    }

    QString types_query_string;
    QJsonArray types_include_values = settings.value("types_include").toArray();
    QStringList types_include;
    for (QJsonValue value : types_include_values) {
        if (value == "All") {
            types_include.clear();
            break;
        }
        types_include << QString("\'%1\'").arg(value.toString());
    }
    if (types_include.isEmpty())
        types_query_string = "";
    else
        types_query_string = QString(" AND v.type in (%1)").arg(types_include.join(","));

    QString watched_query_string;
    QJsonArray watched_values = settings.value("watched").toArray();
    QStringList watched;
    for (QJsonValue value : watched_values) {
        if (value == "All") {
            watched.clear();
            break;
        }
        watched << QString("\'%1\'").arg(value.toString());
    }
    if (watched.isEmpty())
        watched_query_string = "";
    else
        watched_query_string = QString(" AND v.watched in (%1)").arg(watched.join(","));

    QString final_sub_query_string = sub_query_string % tags_query_string_join % category_query_string % tags_query_string_where % authors_query_string % types_query_string % watched_query_string % rating_query_string % views_query_string;
    QString final_query_string = main_query_string.arg(final_sub_query_string);

    query.prepare(final_query_string);
    query.bindValue(":cat_bound_value", category);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at resetWatched (%1, %2, %3): %4").arg(category, QString::number(progress), watched_set_value, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("resetWatched " + category + QString::number(progress) + watched_set_value + " " + query.lastError().text());
        }
    }
}

void sqliteDB::incrementVideosWatchedToday(QString category)
{
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE maininfo SET value = value + 1 WHERE name = 'videosWatchedToday' AND category = ?;"));
    query.addBindValue(category);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at incrementVideosWatchedToday (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("incrementVideosWatchedToday " + category + " " + query.lastError().text());
        }
    }
}

int sqliteDB::getVideoCount(QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare("SELECT COUNT(*) FROM videodetails WHERE category = ?");
    query.bindValue(0, category);
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getVideoCount (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getVideoCount " + category + " " + query.lastError().text());
        }
        return 0;
    }
    return query.first() ? query.value(0).toInt() : 0;
}

QMap<double, int> sqliteDB::getRatingDistribution() {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare("SELECT rating, COUNT(*) FROM videodetails WHERE rating > 0 GROUP BY rating ORDER BY rating");
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getRatingDistribution: %1").arg(query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getRatingDistribution " + query.lastError().text());
        }
        return QMap<double, int>();
    }
    
    QMap<double, int> ratingCounts;
    while (query.next()) {
        double rating = query.value(0).toDouble();
        int count = query.value(1).toInt();
        ratingCounts[rating] = count;
    }
    return ratingCounts;
}

int sqliteDB::getUnratedVideoCount() {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare("SELECT COUNT(*) FROM videodetails WHERE rating = 0 OR rating IS NULL");
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getUnratedVideoCount: %1").arg(query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getUnratedVideoCount " + query.lastError().text());
        }
        return 0;
    }
    return query.first() ? query.value(0).toInt() : 0;
}

int sqliteDB::getUniqueVideosWatched(QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    if (category == "ALL")
        query.prepare("SELECT COUNT(*) FROM videodetails WHERE views > 0");
    else {
        query.prepare("SELECT COUNT(*) FROM videodetails WHERE category = ? AND views > 0");
        query.bindValue(0, category);
    }
    if (!query.exec()) {
        if (qMainApp) {
            qMainApp->logger->log(QString("Database Error at getUniqueVideosWatched (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
            qMainApp->showErrorMessage("getUniqueVideosWatched " + category + " " + query.lastError().text());
        }
        return 0;
    }
    return query.first() ? query.value(0).toInt() : 0;
}

void sqliteDB::incrementTimeWatchedToday(double value) {
    double time = this->getMainInfoValue("timeWatchedToday", "ALL", "0.0").toDouble();
    time += value;
    this->setMainInfoValue("timeWatchedToday", "ALL", QString::number(time));
}

void sqliteDB::incrementTimeSessionToday(double value) {
    double time = this->getMainInfoValue("timeSessionToday", "ALL", "0.0").toDouble();
    time += value;
    this->setMainInfoValue("timeSessionToday", "ALL", QString::number(time));
}

void sqliteDB::createTables() {
    QString videodetails = " CREATE TABLE \"videodetails\" ("
        "\"id\"	INTEGER NOT NULL,"
        "\"path\"	TEXT NOT NULL,"
        "\"author\"	TEXT DEFAULT '',"
        "\"name\"	TEXT NOT NULL,"
        "\"type\"	TEXT DEFAULT '',"
        "\"category\"	TEXT NOT NULL,"
        "\"progress\"	REAL DEFAULT 0,"
        "\"watched\"	TEXT DEFAULT No,"
        "\"views\"	INT DEFAULT 0,"
        "\"rating\"	REAL DEFAULT 0,"
        "\"date_created\"	DATE DEFAULT CURRENT_TIMESTAMP,"
        "\"last_watched\"	DATE,"
        "UNIQUE(\"path\", \"category\"),"
        "PRIMARY KEY(\"id\" AUTOINCREMENT));";

    QString maininfo = " CREATE TABLE IF NOT EXISTS maininfo ("
        "name TEXT NOT NULL,"
        "value TEXT,"
        "category TEXT NOT NULL,"
        "PRIMARY KEY(name, category));";

    QString path_category_index = " CREATE INDEX path_category_index ON videodetails (path, category);";
    QString category_index = " CREATE INDEX category_index ON videodetails (category);";

    QString tags = " CREATE TABLE \"tags\" ("
        "\"id\"	INTEGER NOT NULL,"
        "\"name\"	TEXT NOT NULL UNIQUE,"
        "\"display_priority\"	INTEGER NOT NULL DEFAULT 100,"
        "\"weight\"	REAL NOT NULL DEFAULT 0.0,"
        "PRIMARY KEY(\"id\" AUTOINCREMENT)),"
        "CHECK(weight BETWEEN 0 AND 1);";

    QString tags_relations = " CREATE TABLE \"tags_relations\" ("
        "\"id\"	INTEGER NOT NULL,"
        "\"video_id\"	INTEGER NOT NULL,"
        "\"tag_id\"	INTEGER NOT NULL,"
        "FOREIGN KEY(\"video_id\") REFERENCES \"videodetails\"(\"id\") ON DELETE CASCADE ON UPDATE CASCADE,"
        "FOREIGN KEY(\"tag_id\") REFERENCES \"tags\"(\"id\") ON DELETE CASCADE ON UPDATE CASCADE,"
        "UNIQUE(\"video_id\",\"tag_id\"),"
        "PRIMARY KEY(\"id\" AUTOINCREMENT));";

    QString tags_priority_index = "CREATE INDEX \"tags_priority_index\" ON \"tags\" (\"display_priority\"	ASC); ";

    this->db.transaction();
    QSqlQuery query = QSqlQuery(this->db);
    query.exec(videodetails);
    query.exec(maininfo);
    query.exec(path_category_index);
    query.exec(category_index);
    query.exec(tags);
    query.exec(tags_relations);
    query.exec(tags_priority_index);
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('current', 'MINUS', '')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('current', 'PLUS', '')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('currentIconPath', 'MINUS', '')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('currentIconPath', 'PLUS', '')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('counterVar', 'ALL', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('sv_target_count', 'ALL', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('sv_count', 'ALL', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('timeWatchedIncrement', 'ALL', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('lastSessionDate', 'ALL', '')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('timeWatchedTotal', 'ALL', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('timeWatchedToday', 'ALL', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('timeSessionTotal', 'ALL', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('timeSessionToday', 'ALL', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('videosWatchedToday', 'PLUS', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name, category, value) VALUES('videosWatchedToday', 'MINUS', '0')");
    this->db.commit();
}

void sqliteDB::resetDB(QString category) {
    if (category == "ALL") {
        this->db.transaction();
        QSqlQuery query = QSqlQuery("DROP TABLE videodetails",this->db);
        if (!query.exec()) {
            if (qMainApp) {
                qMainApp->logger->log(QString("Database Error at resetDB (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
                qMainApp->showErrorMessage("resetDB " + category + " " + query.lastError().text());
            }
            return;
        }
        this->createTables();
        this->setMainInfoValue("current", "MINUS", "");
        this->setMainInfoValue("current", "PLUS", "");
        this->setMainInfoValue("currentIconPath", "MINUS", "");
        this->setMainInfoValue("currentIconPath", "PLUS", "");
        this->setMainInfoValue("sv_target_count", "ALL", "0");
        this->setMainInfoValue("sv_count", "ALL", "0");
        this->db.commit();
    }
    else {
        this->db.transaction();
        QSqlQuery query = QSqlQuery(this->db);
        query.prepare(QString("DELETE FROM videodetails where category = ?"));
        query.addBindValue(category);
        if (!query.exec()) {
            if (qMainApp) {
                qMainApp->logger->log(QString("Database Error at resetDB (%1): %2").arg(category, query.lastError().text()), "Database", query.lastError().text());
                qMainApp->showErrorMessage("resetDB " + category + " " + query.lastError().text());
            }
            return;
        }
        this->setMainInfoValue("current", category, "");
        this->setMainInfoValue("currentIconPath", category, "");
        this->db.commit();
    }
}

int sqliteDB::loadOrSaveDb(QSqlDatabase db, const char* zFilename, bool backup) {
    QVariant v = db.driver()->handle();
    if (v.isValid() && (qstrcmp(v.typeName(), "sqlite3*") == 0)) {
        // v.data() returns a pointer to the handle
        sqlite3* handle = *static_cast<sqlite3**>(v.data());
        if (handle) {
            sqlite3* pInMemory = handle;
            int rc;                   /* Function return code */
            sqlite3* pFile;           /* Database connection opened on zFilename */
            sqlite3_backup* pBackup;  /* Backup object used to copy data */
            sqlite3* pTo;             /* Database to copy to (pFile or pInMemory) */
            sqlite3* pFrom;           /* Database to copy from (pFile or pInMemory) */

            /* Open the database file identified by zFilename. Exit early if this fails
            ** for any reason. */
            rc = sqlite3_open(zFilename, &pFile);
            if (rc == SQLITE_OK) {

                pFrom = (backup ? pInMemory : pFile);
                pTo = (backup ? pFile : pInMemory);

                pBackup = sqlite3_backup_init(pTo, "main", pFrom, "main");
                if (pBackup) {
                    (void)sqlite3_backup_step(pBackup, -1);
                    (void)sqlite3_backup_finish(pBackup);
                }
                rc = sqlite3_errcode(pTo);
            }

            (void)sqlite3_close(pFile);
            return rc;
        }
    }
    return -1;
}

sqliteDB::~sqliteDB() {
    this->db.close();
}

//DROP TRIGGER sv_target_count_update_trigger;
//
//CREATE TRIGGER sv_target_count_update_trigger
//AFTER UPDATE OF watched ON videodetails
//FOR EACH ROW
//WHEN OLD.watched <> NEW.watched
//BEGIN
//UPDATE maininfo SET value = IFNULL(CAST(round(nonfh.c / CAST(fh.c AS FLOAT), 0) AS INTEGER), nonfh.c) FROM(SELECT COUNT(*) as c FROM videodetails WHERE category = "MINUS" AND watched = "No" AND type < > "FH") as nonfh, (SELECT COUNT(*) as c FROM videodetails WHERE category = "MINUS" AND watched = "No" AND type = "FH") as fh WHERE name = "sv_target_count";
//END;
//
//DROP TRIGGER sv_target_count_insert_trigger;
//
//CREATE TRIGGER sv_target_count_insert_trigger
//AFTER INSERT ON videodetails
//FOR EACH ROW
//WHEN NEW.category = "MINUS"
//BEGIN
//UPDATE maininfo SET value = IFNULL(CAST(round(nonfh.c / CAST(fh.c AS FLOAT), 0) AS INTEGER), nonfh.c) FROM(SELECT COUNT(*) as c FROM videodetails WHERE category = "MINUS" AND watched = "No" AND type < > "FH") as nonfh, (SELECT COUNT(*) as c FROM videodetails WHERE category = "MINUS" AND watched = "No" AND type = "FH") as fh WHERE name = "sv_target_count";
//END;
//
//DROP TRIGGER sv_target_count_delete_trigger;
//
//CREATE TRIGGER sv_target_count_delete_trigger
//AFTER DELETE ON videodetails
//FOR EACH ROW
//WHEN OLD.category = "MINUS"
//BEGIN
//UPDATE maininfo SET value = IFNULL(CAST(round(nonfh.c / CAST(fh.c AS FLOAT), 0) AS INTEGER), nonfh.c) FROM(SELECT COUNT(*) as c FROM videodetails WHERE category = "MINUS" AND watched = "No" AND type < > "FH") as nonfh, (SELECT COUNT(*) as c FROM videodetails WHERE category = "MINUS" AND watched = "No" AND type = "FH") as fh WHERE name = "sv_target_count";
//END;
