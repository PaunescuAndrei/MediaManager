#include "stdafx.h"
#include "VideosProxyModel.h"
#pragma warning(push ,3)
#include "rapidfuzz_all.hpp"
#pragma warning(pop)
#include <QAbstractProxyModel>

VideosProxyModel::VideosProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

void VideosProxyModel::setSearchText(const QString& text) {
    beginFilterChange();
    search_text = text;
    search_text_lower = text.toLower();
    if (!search_text.isEmpty()) {
        cached_ratio = std::make_shared<rapidfuzz::fuzz::CachedPartialRatio<char>>(search_text_lower.toStdString());
    }
    else {
        cached_ratio.reset();
    }
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void VideosProxyModel::setWatchedOption(const QString& option) {
    beginFilterChange();
    watched_option = option; // expects one of: Yes, No, Mixed, All
    rebuildAuthorsWithUnwatched();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void VideosProxyModel::rebuildAuthorsWithUnwatched() {
    authorsWithUnwatched.clear();
    if (watched_option != "Mixed" || !sourceModel()) return;

    // Cache column indices
    const int watchedCol = ListColumns["WATCHED_COLUMN"];
    const int authorCol = ListColumns["AUTHOR_COLUMN"];
    const int rows = sourceModel()->rowCount();

    // Pre-allocate hash set
    authorsWithUnwatched.reserve(rows / 10); // Estimate

    for (int r = 0; r < rows; ++r) {
        const QModelIndex wIdx = sourceModel()->index(r, watchedCol);
        if (wIdx.data(Qt::DisplayRole).toString() == "No") {
            const QModelIndex aIdx = sourceModel()->index(r, authorCol);
            authorsWithUnwatched.insert(aIdx.data(Qt::DisplayRole).toString());
        }
    }
}

// filterAcceptsRow() reads only the columns below, so a dataChanged() touching none of
// them cannot change the filter result for any row. Without this the default answer
// ("any change is relevant for filtering") re-runs filterAcceptsRow() over the whole
// model - including a fuzzy match per row while a search is active - for nothing.
// The base's verdict on sorting is passed through untouched, so ordering cannot go stale.
QSortFilterProxyModel::DataChangeRelevanceFlags VideosProxyModel::dataChangeRelevanceFlags(
    const QModelIndex& sourceTopLeft, const QModelIndex& sourceBottomRight, const QList<int>& roles) const
{
    Q_UNUSED(roles);

    static const int watchedCol = ListColumns["WATCHED_COLUMN"];
    static const int authorCol = ListColumns["AUTHOR_COLUMN"];
    static const int pathCol = ListColumns["PATH_COLUMN"];
    static const int tagsCol = ListColumns["TAGS_COLUMN"];
    static const int typeCol = ListColumns["TYPE_COLUMN"];

    const DataChangeRelevanceFlags base = QSortFilterProxyModel::dataChangeRelevanceFlags(sourceTopLeft, sourceBottomRight, roles);

    const int first = sourceTopLeft.column();
    const int last = sourceBottomRight.column();
    const bool touchesFilterColumn =
        (watchedCol >= first && watchedCol <= last) ||
        (authorCol >= first && authorCol <= last) ||
        (pathCol >= first && pathCol <= last) ||
        (tagsCol >= first && tagsCol <= last) ||
        (typeCol >= first && typeCol <= last);

    if (touchesFilterColumn) return base | DataChangeRelevanceFlag::RelevantForFiltering;
    return base & DataChangeRelevanceFlag::RelevantForSorting;
}

bool VideosProxyModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const {
    Q_UNUSED(source_parent);

    // Cache column indices (compiler will optimize these as constants)
    static const int watchedCol = ListColumns["WATCHED_COLUMN"];
    static const int authorCol = ListColumns["AUTHOR_COLUMN"];
    static const int pathCol = ListColumns["PATH_COLUMN"];
    static const int tagsCol = ListColumns["TAGS_COLUMN"];
    static const int typeCol = ListColumns["TYPE_COLUMN"];

    // Get all indices at once
    QAbstractItemModel* srcModel = sourceModel();
    const QModelIndex wIdx = srcModel->index(source_row, watchedCol);

    // Watched filter - early exit if fails
    const QString w = wIdx.data(Qt::DisplayRole).toString();

    if (watched_option == "Mixed") {
        const QModelIndex aIdx = srcModel->index(source_row, authorCol);
        if (!authorsWithUnwatched.contains(aIdx.data(Qt::DisplayRole).toString())) {
            return false;
        }
    }
    else if (watched_option == "Yes") {
        if (w != "Yes") return false;
    }
    else if (watched_option == "No") {
        if (w != "No") return false;
    }
    // else: All - always passes

    // Search filter - only if search text exists
    if (!search_text.isEmpty()) {
        const QModelIndex pIdx = srcModel->index(source_row, pathCol);
        const QModelIndex aIdx = srcModel->index(source_row, authorCol);
        const QModelIndex tIdx = srcModel->index(source_row, tagsCol);
        const QModelIndex tyIdx = srcModel->index(source_row, typeCol);

        // Build combined string - reserve space to avoid reallocations
        QString combined;
        combined.reserve(200); // Reasonable estimate
        combined.append(pIdx.data(Qt::DisplayRole).toString());
        combined.append(' ');
        combined.append(aIdx.data(Qt::DisplayRole).toString());
        combined.append(' ');
        combined.append(tIdx.data(Qt::DisplayRole).toString());
        combined.append(' ');
        combined.append(tyIdx.data(Qt::DisplayRole).toString());

        const std::string combined_std = combined.toLower().toStdString();

        double score = 0;
        if (cached_ratio) {
            score = cached_ratio->similarity(combined_std);
        }
        else {
            score = rapidfuzz::fuzz::partial_ratio(search_text_lower.toStdString(), combined_std);
        }

        if (score <= 80) return false;
    }

    return true;
}

void VideosProxyModel::sort(int column, Qt::SortOrder order) {
    if (auto chainedProxy = qobject_cast<QAbstractProxyModel*>(sourceModel())) {
        chainedProxy->sort(column, order);
    }
    else if (sourceModel()) {
        sourceModel()->sort(column, order);
    }
}
