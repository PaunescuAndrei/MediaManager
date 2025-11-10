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
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

private:
    QString search_text;
    QString search_text_lower;
    QString watched_option; // "Yes", "No", "Mixed", "All"
    bool visible_only = false;
    QSet<QString> authorsWithUnwatched;
    std::shared_ptr<rapidfuzz::fuzz::CachedPartialRatio<char>> cached_ratio;
    QCollator collator;
};
