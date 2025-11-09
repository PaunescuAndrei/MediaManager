#include "stdafx.h"
#include "VideosModel.h"
#include <QGuiApplication>
#include "stylesQt.h"

VideosModel::VideosModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int VideosModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return rows.size();
}

int VideosModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return 11; // matches ListColumns size
}

QVariant VideosModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return QVariant();
    const VideoRow& r = rows[index.row()];
    const int c = index.column();

    if (role == Qt::DisplayRole) {
        if (c == ListColumns["AUTHOR_COLUMN"]) return r.author;
        if (c == ListColumns["NAME_COLUMN"]) return r.name;
        if (c == ListColumns["PATH_COLUMN"]) return r.path;
        if (c == ListColumns["TAGS_COLUMN"]) return r.tags;
        if (c == ListColumns["TYPE_COLUMN"]) return r.type;
        if (c == ListColumns["WATCHED_COLUMN"]) return r.watched;
        if (c == ListColumns["VIEWS_COLUMN"]) return r.views;
        if (c == ListColumns["RANDOM%_COLUMN"]) return r.randomPercent > 0 ? QVariant(QString::number(r.randomPercent, 'f', 2)) : QVariant();
        if (c == ListColumns["DATE_CREATED_COLUMN"]) return r.dateCreated;
        if (c == ListColumns["LAST_WATCHED_COLUMN"]) return r.lastWatched;
    }
    if (role == CustomRoles::id && c == ListColumns["PATH_COLUMN"]) {
        return r.id;
    }
    if (role == CustomRoles::rating && c == ListColumns["RATING_COLUMN"]) {
        return r.rating;
    }
    if (role == Qt::TextAlignmentRole) {
        if (c == ListColumns["TAGS_COLUMN"] || c == ListColumns["TYPE_COLUMN"] ||
            c == ListColumns["WATCHED_COLUMN"] || c == ListColumns["VIEWS_COLUMN"] ||
            c == ListColumns["RATING_COLUMN"] || c == ListColumns["DATE_CREATED_COLUMN"] ||
            c == ListColumns["LAST_WATCHED_COLUMN"] || c == ListColumns["RANDOM%_COLUMN"]) {
            return Qt::AlignHCenter;
        }
    }
    if (role == Qt::BackgroundRole) {
        if (!highlightedPath.isEmpty() && r.path == highlightedPath) {
            return get_highlight_brush(QGuiApplication::palette());
        }
    }
    if (role == Qt::ForegroundRole) {
        if (!highlightedPath.isEmpty() && r.path == highlightedPath) {
            return get_highlighted_text_brush(QGuiApplication::palette());
        }
        if (c == ListColumns["WATCHED_COLUMN"]) {
            if (r.watched == "Yes") return QBrush(QColor("#00e640"));
            if (r.watched == "No") return QBrush(QColor("#cf000f"));
        }
    }
    return QVariant();
}

bool VideosModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid()) return false;
    VideoRow& r = rows[index.row()];
    const int c = index.column();
    bool changed = false;
    if (role == CustomRoles::rating && c == ListColumns["RATING_COLUMN"]) {
        r.rating = value.toDouble();
        changed = true;
    }
    if (role == Qt::DisplayRole) {
        if (c == ListColumns["WATCHED_COLUMN"]) {
            const QString nv = value.toString();
            if (r.watched != nv) { r.watched = nv; changed = true; }
        } else if (c == ListColumns["VIEWS_COLUMN"]) {
            const QString nv = value.toString();
            if (r.views != nv) { r.views = nv; changed = true; }
        } else if (c == ListColumns["LAST_WATCHED_COLUMN"]) {
            QDateTime nv = value.toDateTime();
            if (r.lastWatched != nv) { r.lastWatched = nv; changed = true; }
        }
    }
    if (changed) {
        emit dataChanged(index, index, { Qt::DisplayRole, Qt::ForegroundRole });
    }
    return changed;
}

Qt::ItemFlags VideosModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ListColumns["RATING_COLUMN"]) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

QVariant VideosModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) return QVariant();
    if (role == Qt::DisplayRole) {
        if (section == ListColumns["AUTHOR_COLUMN"]) return QStringLiteral("Author");
        if (section == ListColumns["NAME_COLUMN"]) return QStringLiteral("Name");
        if (section == ListColumns["PATH_COLUMN"]) return QStringLiteral("Path");
        if (section == ListColumns["TAGS_COLUMN"]) return QStringLiteral("Tags");
        if (section == ListColumns["TYPE_COLUMN"]) return QStringLiteral("Type");
        if (section == ListColumns["WATCHED_COLUMN"]) return QStringLiteral("W");
        if (section == ListColumns["VIEWS_COLUMN"]) return QStringLiteral("Views");
        if (section == ListColumns["RATING_COLUMN"]) return QStringLiteral("Rating");
        if (section == ListColumns["RANDOM%_COLUMN"]) return QStringLiteral("Random %");
        if (section == ListColumns["DATE_CREATED_COLUMN"]) return QStringLiteral("Date Created");
        if (section == ListColumns["LAST_WATCHED_COLUMN"]) return QStringLiteral("Last Watched");
    }
    if (role == Qt::TextAlignmentRole) {
        if (section == ListColumns["AUTHOR_COLUMN"] || section == ListColumns["NAME_COLUMN"] ||
            section == ListColumns["PATH_COLUMN"]) {
            return Qt::AlignLeft;
        }
        if (section == ListColumns["TAGS_COLUMN"] || section == ListColumns["TYPE_COLUMN"] ||
            section == ListColumns["WATCHED_COLUMN"] || section == ListColumns["VIEWS_COLUMN"] ||
            section == ListColumns["RATING_COLUMN"] || section == ListColumns["RANDOM%_COLUMN"] ||
            section == ListColumns["DATE_CREATED_COLUMN"] || section == ListColumns["LAST_WATCHED_COLUMN"]) {
            return Qt::AlignHCenter;
        }
    }
    return QVariant();
}

void VideosModel::clear() {
    beginResetModel();
    rows.clear();
    idToRow.clear();
    pathToRow.clear();
    endResetModel();
}

void VideosModel::setRows(const QVector<VideoRow>& data) {
    beginResetModel();
    rows = data;
    idToRow.clear();
    pathToRow.clear();
    for (int i = 0; i < rows.size(); ++i) {
        idToRow.insert(rows[i].id, i);
        pathToRow.insert(rows[i].path, i);
    }
    endResetModel();
}

void VideosModel::setRows(QVector<VideoRow>&& data) {
    beginResetModel();
    rows = std::move(data);
    idToRow.clear();
    pathToRow.clear();
    for (int i = 0; i < rows.size(); ++i) {
        idToRow.insert(rows[i].id, i);
        pathToRow.insert(rows[i].path, i);
    }
    endResetModel();
}

int VideosModel::findRowByPath(const QString& path) const {
    for (int i = 0; i < rows.size(); ++i) {
        if (rows[i].path == path) return i;
    }
    return -1;
}

int VideosModel::findRowById(int id) const {
    for (int i = 0; i < rows.size(); ++i) {
        if (rows[i].id == id) return i;
    }
    return -1;
}

int VideosModel::rowByPath(const QString& path) const {
    auto it = pathToRow.constFind(path);
    if (it != pathToRow.constEnd()) return it.value();
    int r = findRowByPath(path);
    if (r >= 0) pathToRow.insert(path, r);
    return r;
}

int VideosModel::rowById(int id) const {
    auto it = idToRow.constFind(id);
    if (it != idToRow.constEnd()) return it.value();
    int r = findRowById(id);
    if (r >= 0) idToRow.insert(id, r);
    return r;
}

void VideosModel::setRandomPercentForPath(const QString& path, double value) {
    int r = rowByPath(path);
    if (r < 0) return;
    setRandomPercentAtRow(r, value);
}

void VideosModel::setRandomPercentAtRow(int row, double value) {
    if (row < 0 || row >= rows.size()) return;
    rows[row].randomPercent = value;
    const int col = ListColumns["RANDOM%_COLUMN"];
    emit dataChanged(index(row, col), index(row, col), { Qt::DisplayRole });
}

void VideosModel::setHighlightedPath(const QString& path) {
    int prevRow = highlightedPath.isEmpty() ? -1 : rowByPath(highlightedPath);
    highlightedPath = path;
    int newRow = highlightedPath.isEmpty() ? -1 : rowByPath(highlightedPath);
    if (prevRow >= 0) {
        emit dataChanged(index(prevRow, 0), index(prevRow, columnCount() - 1), { Qt::BackgroundRole, Qt::ForegroundRole });
    }
    if (newRow >= 0) {
        emit dataChanged(index(newRow, 0), index(newRow, columnCount() - 1), { Qt::BackgroundRole, Qt::ForegroundRole });
    }
}
