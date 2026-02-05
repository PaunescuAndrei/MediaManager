#pragma once
#include <QString>
#include <QDateTime>

// Shared row data structure for videos table view/model.
struct VideoData {
    int id = -1;
    QString author;
    QString name;
    QString path;
    QString tags;
    QString type;
    QString watched; // "Yes"/"No"
    QString views;
    double rating = 0.0;
    double randomPercent = 0.0;
    double bpm = -1.0;
    QDateTime dateCreated;
    QDateTime lastWatched;
};

