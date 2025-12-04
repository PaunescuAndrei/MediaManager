#include "stdafx.h"
#include "VideosSortProxyModel.h"

VideosSortProxyModel::VideosSortProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
}

bool VideosSortProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const int col = left.column();

    if (col == ListColumns["RATING_COLUMN"]) {
        const double l = left.data(CustomRoles::rating).toDouble();
        const double r = right.data(CustomRoles::rating).toDouble();
        return l < r;
    }

    const QString lKey = left.data(Qt::DisplayRole).toString();
    const QString rKey = right.data(Qt::DisplayRole).toString();
    const int cmp = collator.compare(lKey, rKey);
    if (cmp != 0) {
        return cmp < 0;
    }

    static const int pathCol = ListColumns["PATH_COLUMN"];
    const QString lPath = left.siblingAtColumn(pathCol).data(Qt::DisplayRole).toString();
    const QString rPath = right.siblingAtColumn(pathCol).data(Qt::DisplayRole).toString();
    return collator.compare(lPath, rPath) < 0;
}
