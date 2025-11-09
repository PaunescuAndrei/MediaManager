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
    search_text_lower = text.toLower();
    if (!search_text.isEmpty()) {
        cached_ratio = std::make_shared<rapidfuzz::fuzz::CachedPartialRatio<char>>(search_text_lower.toStdString());
    }
    else {
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

bool VideosProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const int col = left.column();

    // Special handling for rating column
    if (col == ListColumns["RATING_COLUMN"]) {
        const double l = left.data(CustomRoles::rating).toDouble();
        const double r = right.data(CustomRoles::rating).toDouble();
        return l < r;
    }

    // Standard string comparison with collator
    const QString lKey = left.data(Qt::DisplayRole).toString();
    const QString rKey = right.data(Qt::DisplayRole).toString();
    const int cmp = collator.compare(lKey, rKey);

    if (cmp != 0) {
        return cmp < 0;
    }

    // Tie-breaker: compare by path
    static const int pathCol = ListColumns["PATH_COLUMN"];
    const QString lPath = left.siblingAtColumn(pathCol).data(Qt::DisplayRole).toString();
    const QString rPath = right.siblingAtColumn(pathCol).data(Qt::DisplayRole).toString();
    return collator.compare(lPath, rPath) < 0;
}