#pragma once
#include <string>
#include <QSqlDatabase>
#include <QTreeWidget>
#include "sqlite3.h"
#include "videosTreeWidget.h"
#include <tuple>

class sqliteDB {
public:
    QSqlDatabase db;
    QString location;
    sqliteDB(QString location, std::string conname = "");
    ~sqliteDB();
    void createTables();
    QList<QTreeWidgetItem*> getVideos(QString category);
    QStringList getAuthors(QString category);
    QStringList getTypes(QString category);
    void setFilterSettings(QString value, QString category);
    QJsonObject getFilterSettings(QString category);
    QString getMainInfoValue(QString name, QString category, QString fallback = "");
    std::tuple<QString, QString,QString> getCurrentVideo(QString category, std::tuple<QString, QString, QString> fallback = {"","",""});
    QString getVideoProgress(QString path, QString category, QString fallback = "");
    bool checkIfVideoInDB(QString path, QString category);
    int getVideosWatched(QString category, int fallback);
    double getAverageRating(QString category, double fallback);
    double getAverageRatingAuthor(QString author, QString category, double fallback = 0);
    int loadOrSaveDb(QSqlDatabase db, const char* zFilename, bool backup);
    void setMainInfoValue(QString name, QString category, QString value);
    void deleteVideo(QString path, QString category);
    void updateWatchedState(QString path, QString category, QString watched, bool increment = false);
    void updateWatchedState(QString path, QString category, double progress, QString watched,bool increment = false);
    void incrementVideoViews(QString path, QString category, int value);
    void updateType(QString path, QString category, QString type_val);
    void updateAuthor(QString path, QString category, QString author);
    void updateName(QString path, QString category, QString name);
    void updateRating(QString path, QString category, double rating);
    void setViews(QString path, QString category, int value);
    void insertVideo(QString path, QString category, QString name , QString author = "", QString type = "");
    void updateVideoProgress(QString path, QString category, double progress);
    void updateItem(QString path, QString category, QString new_path = "", QString new_author = "", QString new_name = "", QString new_type = "", QString new_category = "", QString new_progress = "", QString new_watched = "", QString new_views = "", QString new_rating = "");
    QString getRandomVideo(QString category, QString watched, QStringList vid_type_include = {}, QStringList vid_type_exclude = {});
    QString getRandomVideo(QString category, QJsonObject settings);
    void resetDB(QString category = "ALL");
    void resetWatched(QString category, double progress = 0, QString watched = "No");
    void resetWatched(QString category, QJsonObject settings, double progress = 0, QString watched = "No");
}; 