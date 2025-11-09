#pragma once
#include <QSortFilterProxyModel>
#include <QCollator>
#include <QSet>
#include <QString>
#include <memory>
#include "definitions.h"

namespace rapidfuzz { namespace fuzz { template <typename T> class CachedPartialRatio; } }

class VideosProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit VideosProxyModel(QObject* parent = nullptr);

    void setSearchText(const QString& text);
    void setWatchedOption(const QString& option);
    void setVisibleOnlyChecked(bool enabled);
    void rebuildAuthorsWithUnwatched();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    QString search_text;
    QString watched_option; // "Yes", "No", "Mixed", "All"
    bool visible_only = false;
    QSet<QString> authorsWithUnwatched;
    std::shared_ptr<rapidfuzz::fuzz::CachedPartialRatio<char>> cached_ratio;
    QCollator collator;
};
