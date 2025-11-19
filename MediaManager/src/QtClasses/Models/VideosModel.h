#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include <QHash>
#include <QBrush>
#include "definitions.h"
#include "VideoData.h"

class VideosModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit VideosModel(QObject* parent = nullptr);

    // Model impl
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    // Loading
    void clear();
    void setRows(const QVector<VideoData>& data);
    void setRows(QVector<VideoData>&& data);

    // Helpers
    const VideoData* rowAt(int r) const { return (r >= 0 && r < rows.size()) ? &rows[r] : nullptr; }
    int findRowByPath(const QString& path) const;
    int findRowById(int id) const;
    int rowByPath(const QString& path) const;
    int rowById(int id) const;

    // Update helpers
    void setRandomPercentForPath(const QString& path, double value);
    void setRandomPercentAtRow(int row, double value);
    void setRandomPercentColumn(const QMap<int, long double>& probabilities);
    void setHighlightedPath(const QString& path);

private:
    // Fast lookup caches; populated on setRows and lazily on fallback lookups
    QVector<VideoData> rows;
    mutable QHash<int, int> idToRow;
    mutable QHash<QString, int> pathToRow;
    QString highlightedPath;
};
