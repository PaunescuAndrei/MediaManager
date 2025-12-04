#pragma once
#include <string>
#include <QSqlDatabase>
#include <QTreeWidget>
#include "VideoData.h"
#include "sqlite3.h"
#include <tuple>
#include "definitions.h"

class sqliteDB {
public:
    QSqlDatabase db;
    QString location;
    sqliteDB(QString location, std::string conname = "");
    void enableForeignKeys();
    ~sqliteDB();
    void createTables();
    QList<QTreeWidgetItem*> getVideos(QString category);
    QVector<VideoData> getVideosData(QString category);
    VideoData getVideoData(QString path, QString category);
    QSet<Tag> getAllTags();
    QSet<Tag> getTags(int video_id);
    void insertTag(int video_id, int tag_id);
    void deleteExtraTags(int video_id, QSet<int> tags_id);
    QStringList getAuthors(QString category);
    QStringList getUniqueNames(QString category);
    QStringList getUniqueTags(QString category);
    QStringList getTypes(QString category);
    void setFilterSettings(QString value, QString category);
    QJsonObject getFilterSettings(QString category);
    QString getMainInfoValue(QString name, QString category, QString fallback = "");
    std::tuple<int, QString, QString, QString, QString> getCurrentVideo(QString category, std::tuple<int, QString, QString, QString, QString> fallback = { -1,"","","","" });
    QString getVideoProgress(int video_id, QString fallback = "");
    int getVideoId(QString path, QString category);
    bool checkIfVideoInDB(QString path, QString category);
    int getVideosWatched(QString category, int fallback);
    double getAverageRating(QString category, double fallback);
    double getAverageRatingAuthor(QString author, QString category, double fallback = 0);
    int loadOrSaveDb(QSqlDatabase db, const char* zFilename, bool backup);
    void setMainInfoValue(QString name, QString category, QString value);
    void deleteVideo(int video_id);
    void updateWatchedState(int video_id, QString watched, bool increment = false, bool update_last_watched = false);
    void updateWatchedState(int video_id, double progress, QString watched, bool increment = false, bool update_last_watched = false);
    void incrementVideoViews(int video_id, int value, bool update_last_watched = false);
    void updateType(int video_id, QString type_val);
    void updateAuthor(int video_id, QString author);
    void updateName(int video_id, QString name);
    void updateRating(int video_id, double rating);
    void setViews(int video_id, int value);
    void insertVideo(QString path, QString category, QString name, QString author = "", QString type = "");
    void updateVideoProgress(int video_id, double progress);
    void updateItem(int video_id, QString new_path = "", QString new_author = "", QString new_name = "", QString new_type = "", QString new_category = "", QString new_progress = "", QString new_watched = "", QString new_views = "", QString new_rating = "");
    QList<VideoWeightedData> getVideos(QString category, QJsonObject settings);
    void resetDB(QString category = "ALL");
    void resetWatched(QString category, double progress = 0, QString watched = "No");
    void resetWatched(QString category, QJsonObject settings, double progress = 0, QString watched = "No");
    void incrementVideosWatchedToday(QString category);
    int getVideoCount(QString category);
    QMap<double, int> getRatingDistribution();
    int getUnratedVideoCount();
    int getUniqueVideosWatched(QString category);
    void incrementTimeWatchedToday(double value);
    void incrementTimeSessionToday(double value);
};
