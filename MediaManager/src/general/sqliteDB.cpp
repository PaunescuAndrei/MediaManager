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

sqliteDB::sqliteDB(QString location,std::string conname) {
    // In the constructor or initialization function of that object      
    this->db = QSqlDatabase::addDatabase("QSQLITE", conname.c_str());
    this->location = location;
    this->db.setDatabaseName(location);
    if (!this->db.open())  // open it and keep it opened
    {
        qDebug() << "Error oppening database.";
        if (qApp)
            qMainApp->showErrorMessage("Error oppening database.");
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
        qDebug() << "getVideos " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getVideos " + query.lastError().text());
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

QSet<Tag> sqliteDB::getAllTags() {
    QSqlQuery query = QSqlQuery(this->db);
    QString query_string = "SELECT id, name, display_priority from tags";
    QSet<Tag> tags = QSet<Tag>();

    query.prepare(query_string);
    if (!query.exec()) {
        qDebug() << "getAllTags " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getAllTags " + query.lastError().text());
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
        qDebug() << "getTags " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getTags " + query.lastError().text());
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
        qDebug() << "insertTag " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("insertTag " + query.lastError().text());
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
        qDebug() << "deleteExtraTags " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("deleteExtraTags " + query.lastError().text());
    }
}

QStringList sqliteDB::getAuthors(QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT DISTINCT author from videodetails where category = ? ORDER BY author"));
    query.addBindValue(category);
    if (!query.exec()) {
        qDebug() << "getAuthors " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getAuthors " + query.lastError().text());
    }
    QStringList itemlist = QStringList();
    while (query.next()) {
        QString author = query.value(0).toString();
        itemlist.append(author);
    }
    return itemlist;
}

QStringList sqliteDB::getTypes(QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT DISTINCT type from videodetails where category = ? ORDER BY type"));
    query.addBindValue(category);
    if (!query.exec()) {
        qDebug() << "getTypes " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getTypes " + query.lastError().text());
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
        qDebug() << category << value;
        qDebug() << "setFilterSettings " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("setFilterSettings " + category + value + " " + query.lastError().text());
    }
}

QJsonObject sqliteDB::getFilterSettings(QString category) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT value from maininfo WHERE name == \"FilterSettings\" AND category == ?"));
    query.addBindValue(category);
    if (!query.exec()) {
        qDebug() << "getFilterSettings " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getFilterSettings " + query.lastError().text());
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
        qDebug() << "getVideoProgress " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getVideoProgress " + query.lastError().text());
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
        qDebug() << "getVideoId " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getVideoId " + query.lastError().text());
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
        qDebug() << "checkIfVideoInDB " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("checkIfVideoInDB " + query.lastError().text());
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
        qDebug() << "getVideosWatched " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getVideosWatched " + query.lastError().text());
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
        qDebug() << "getAverageRating " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getAverageRating " + query.lastError().text());
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
        qDebug() << "getAverageRatingAuthor " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getAverageRatingAuthor " + query.lastError().text());
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
        qDebug() << name << category << value;
        qDebug() << "setMainInfoValue " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("setMainInfoValue " + name + category + value + " " + query.lastError().text());
    }
}

void sqliteDB::deleteVideo(int video_id) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("DELETE FROM videodetails WHERE id = ?"));
    query.addBindValue(video_id);
    if (!query.exec()) {
        qDebug() << "deleteVideo " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("deleteVideo " + query.lastError().text());
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
        qDebug() << "updateWatchedState " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("updateWatchedState " + query.lastError().text());
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
        qDebug() << "updateWatchedState " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("updateWatchedState " + query.lastError().text());
    }
}

void sqliteDB::updateType(int video_id, QString type_val) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE videodetails SET type=? WHERE id = ?"));
    query.addBindValue(type_val);
    query.addBindValue(video_id);
    if (!query.exec()) {
        qDebug() << "updateType " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("updateType " + query.lastError().text());
    }
}

void sqliteDB::updateAuthor(int video_id, QString author) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE videodetails SET author=? WHERE id = ?"));
    query.addBindValue(author);
    query.addBindValue(video_id);
    if (!query.exec()) {
        qDebug() << "updateAuthor " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("updateAuthor " + query.lastError().text());
    }
}

void sqliteDB::updateName(int video_id, QString name) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE videodetails SET name=? WHERE id = ?"));
    query.addBindValue(name);
    query.addBindValue(video_id);
    if (!query.exec()) {
        qDebug() << "updateName " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("updateName " + query.lastError().text());
    }
}

void sqliteDB::updateRating(int video_id, double rating) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE videodetails SET rating=? WHERE id = ?"));
    query.addBindValue(QString::number(rating));
    query.addBindValue(video_id);
    if (!query.exec()) {
        qDebug() << "updateRating " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("updateRating " + query.lastError().text());
    }
}

void sqliteDB::setViews(int video_id, int value) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE videodetails SET views = ? WHERE id = ?"));
    query.addBindValue(QString::number(value));
    query.addBindValue(video_id);
    if (!query.exec()) {
        qDebug() << "setViews " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("setViews " + query.lastError().text());
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
        qDebug() << "incrementVideoViews " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("incrementVideoViews " + query.lastError().text());
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
        qDebug() << "insertVideo " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("insertVideo " + query.lastError().text());
    }
}

QString sqliteDB::getMainInfoValue(QString name, QString category, QString fallback) {
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("SELECT value from maininfo WHERE name = ? AND category = ?"));
    query.addBindValue(name);
    query.addBindValue(category);
    if (!query.exec()) {
        qDebug() << "getMainInfoValue " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getMainInfoValue " + query.lastError().text());
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
        qDebug() << "getCurrentVideo " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getCurrentVideo " + query.lastError().text());
    }
    bool found = query.first();
    if (found == true)
        return std::tuple<int, QString, QString, QString, QString>{query.value(0).toInt(), query.value(1).toString(), query.value(2).toString(), query.value(3).toString(), query.value(4).toString()};
    else
        return fallback;
}

QString sqliteDB::getRandomVideo(QString category, QString watched, QStringList vid_type_include, QStringList vid_type_exclude) {
    QSqlQuery query = QSqlQuery(this->db);
    QString main_query_string = "SELECT path FROM videodetails WHERE category = :cat_bound_value and watched = :watched_bound_value";
    QString types_query_string = "";
    QString end_query_string = " ORDER BY RANDOM() LIMIT 1";

    QStringList types_include;
    QStringList types_exclude;
    for (QString value : vid_type_include) {
        if (value == "All") {
            types_include.clear();
            break;
        }
        types_include << QString("\'%1\'").arg(value);
    }
    for (QString value : vid_type_exclude) {
        types_exclude << QString("\'%1\'").arg(value);
    }
    if (types_include.isEmpty())
        types_query_string = "";
    else
        types_query_string = QString(" AND type in (%1)").arg(types_include.join(","));
    if (not types_exclude.isEmpty()) {
        types_query_string += QString(" AND type not in (%1)").arg(types_exclude.join(","));
    }

    QString final_query_string = main_query_string % types_query_string % end_query_string;
    query.prepare(final_query_string);
    query.bindValue(":cat_bound_value", category);
    query.bindValue(":watched_bound_value", watched);
    if (!query.exec()) {
        qDebug() << "getRandomVideo " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getRandomVideo " + query.lastError().text());
    }
    bool found = query.first();
    if (found == true)
        return query.value(0).toString();
    else
        return "";
}

QString sqliteDB::getRandomVideo(QString category, QJsonObject settings) {
    QSqlQuery query = QSqlQuery(this->db);
    QString main_query_string = "SELECT v.path FROM videodetails v";
    QString category_query_string = " WHERE v.category == :cat_bound_value";
    QString rating_query_string = QString(" AND (v.rating %1 %2 %3)").arg(settings.value("rating_mode").toString(),settings.value("rating").toString(), utils::text_to_bool(settings.value("include_unrated").toString().toStdString()) ? "OR rating == 0" : "");
    QString views_query_string = QString(" AND v.views %1 %2").arg(settings.value("views_mode").toString(), settings.value("views").toString());
    QString end_query_string = " ORDER BY RANDOM() LIMIT 1";

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

    QString final_query_string = main_query_string % tags_query_string_join % category_query_string % tags_query_string_where % authors_query_string % types_query_string % watched_query_string % rating_query_string % views_query_string % end_query_string;
    query.prepare(final_query_string);
    query.bindValue(":cat_bound_value", category);
    if (!query.exec()) {
        qDebug() << "getRandomVideo " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("getRandomVideo " + query.lastError().text());
    }
    bool found = query.first();
    if (found == true)
        return query.value(0).toString();
    else
        return "";
}

void sqliteDB::updateVideoProgress(int video_id, double progress)
{
    QSqlQuery query = QSqlQuery(this->db);
    query.prepare(QString("UPDATE videodetails SET progress=? WHERE id = ?"));
    query.addBindValue(QString::number(progress,'f',4));
    query.addBindValue(video_id);
    if (!query.exec()) {
        qDebug() << "updateVideoProgress " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("updateVideoProgress " + query.lastError().text());
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
        qDebug() << "updateItem " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("updateItem " + query.lastError().text());
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
        qDebug() << "resetWatched " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("resetWatched " + query.lastError().text());
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
        qDebug() << "resetWatched " << query.lastError().text();
        if (qApp)
            qMainApp->showErrorMessage("resetWatched " + query.lastError().text());
    }
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
        "PRIMARY KEY(\"id\" AUTOINCREMENT));";

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
    query.exec("INSERT OR IGNORE INTO maininfo(name,category,value) VALUES('current', 'PLUS', '')");
    query.exec("INSERT OR IGNORE INTO maininfo(name,category,value) VALUES('currentIconPath', 'MINUS', '')");
    query.exec("INSERT OR IGNORE INTO maininfo(name,category,value) VALUES('currentIconPath', 'PLUS', '')");
    query.exec("INSERT OR IGNORE INTO maininfo(name,category,value) VALUES('counterVar', 'ALL', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name,category,value) VALUES('timeWatchedIncrement', 'ALL', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name,category,value) VALUES('timeWatchedTotal', 'ALL', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name,category,value) VALUES('sv_target_count', 'ALL', '0')");
    query.exec("INSERT OR IGNORE INTO maininfo(name,category,value) VALUES('sv_count', 'ALL', '0')");
    this->db.commit();
}

void sqliteDB::resetDB(QString category) {
    if (category == "ALL") {
        this->db.transaction();
        QSqlQuery query = QSqlQuery("DROP TABLE videodetails",this->db);
        if (!query.exec()) {
            qDebug() << "resetDB " << query.lastError().text();
            if (qApp)
                qMainApp->showErrorMessage("resetDB " + query.lastError().text());
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
            qDebug() << "resetDB " << query.lastError().text();
            if (qApp)
                qMainApp->showErrorMessage("resetDB " + query.lastError().text());
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