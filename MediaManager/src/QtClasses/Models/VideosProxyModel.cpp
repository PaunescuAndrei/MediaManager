#include "stdafx.h"
#include "VideosProxyModel.h"
#pragma warning(push ,3)
#include "rapidfuzz_all.hpp"
#pragma warning(pop)

VideosProxyModel::VideosProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
}

void VideosProxyModel::setSearchText(const QString& text) {
    search_text = text;
    if (!search_text.isEmpty()) {
        cached_ratio = std::make_shared<rapidfuzz::fuzz::CachedPartialRatio<char>>(search_text.toLower().toStdString());
    } else {
        cached_ratio.reset();
    }
    invalidateFilter();
}

void VideosProxyModel::setWatchedOption(const QString& option) {
    watched_option = option; // expects one of: Yes, No, Mixed, All
    rebuildAuthorsWithUnwatched();
    invalidateFilter();
}

void VideosProxyModel::setVisibleOnlyChecked(bool enabled) {
    visible_only = enabled;
    invalidateFilter();
}

void VideosProxyModel::rebuildAuthorsWithUnwatched() {
    authorsWithUnwatched.clear();
    if (watched_option != "Mixed" || !sourceModel()) return;
    const int rows = sourceModel()->rowCount();
    for (int r = 0; r < rows; ++r) {
        const QModelIndex wIdx = sourceModel()->index(r, ListColumns["WATCHED_COLUMN"]);
        const QModelIndex aIdx = sourceModel()->index(r, ListColumns["AUTHOR_COLUMN"]);
        if (wIdx.data(Qt::DisplayRole).toString() == "No") {
            authorsWithUnwatched.insert(aIdx.data(Qt::DisplayRole).toString());
        }
    }
}

bool VideosProxyModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const {
    Q_UNUSED(source_parent);
    const QModelIndex wIdx = sourceModel()->index(source_row, ListColumns["WATCHED_COLUMN"]);
    const QModelIndex aIdx = sourceModel()->index(source_row, ListColumns["AUTHOR_COLUMN"]);
    const QModelIndex pIdx = sourceModel()->index(source_row, ListColumns["PATH_COLUMN"]);
    const QModelIndex tIdx = sourceModel()->index(source_row, ListColumns["TAGS_COLUMN"]);
    const QModelIndex tyIdx = sourceModel()->index(source_row, ListColumns["TYPE_COLUMN"]);

    // watched filter
    bool visible = false;
    const QString w = wIdx.data(Qt::DisplayRole).toString();
    if (watched_option == "Mixed") {
        visible = authorsWithUnwatched.contains(aIdx.data(Qt::DisplayRole).toString());
    } else if (watched_option == "Yes") {
        visible = (w == "Yes");
    } else if (watched_option == "No") {
        visible = (w == "No");
    } else { // All
        visible = true;
    }

    if (!visible) return false;

    // search filter
    if (!search_text.isEmpty()) {
        const std::string combined = (pIdx.data(Qt::DisplayRole).toString() + " " +
            aIdx.data(Qt::DisplayRole).toString() + " " +
            tIdx.data(Qt::DisplayRole).toString() + " " +
            tyIdx.data(Qt::DisplayRole).toString()).toLower().toStdString();
        double score = 0;
        if (cached_ratio) {
            score = cached_ratio->similarity(combined);
        } else {
            score = rapidfuzz::fuzz::partial_ratio(search_text.toLower().toStdString(), combined);
        }
        if (score <= 80) return false;
    }

    return true;
}

bool VideosProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    if (left.column() == ListColumns["RATING_COLUMN"]) {
        const double l = left.data(CustomRoles::rating).toDouble();
        const double r = right.data(CustomRoles::rating).toDouble();
        return l < r;
    }
    const QString lKey = left.data(Qt::DisplayRole).toString();
    const QString rKey = right.data(Qt::DisplayRole).toString();
    const int cmp = collator.compare(lKey, rKey);
    if (cmp == 0) {
        QString lPath = left.siblingAtColumn(ListColumns["PATH_COLUMN"]).data(Qt::DisplayRole).toString();
        QString rPath = right.siblingAtColumn(ListColumns["PATH_COLUMN"]).data(Qt::DisplayRole).toString();
        return collator.compare(lPath, rPath) < 0;
    }
    return cmp < 0;
}
