#pragma once
#include <QSortFilterProxyModel>
#include <QCollator>
#include "definitions.h"

class VideosSortProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit VideosSortProxyModel(QObject* parent = nullptr);

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    mutable QCollator collator;
};
