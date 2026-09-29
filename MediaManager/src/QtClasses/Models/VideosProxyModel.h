#pragma once
#include <QSortFilterProxyModel>
#include <QSet>
#include <QString>
#include <memory>
#include "definitions.h"

namespace rapidfuzz { namespace fuzz { template <typename T> struct CachedPartialRatio; } }

class VideosProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit VideosProxyModel(QObject* parent = nullptr);
    void setSearchText(const QString& text);
    void setWatchedOption(const QString& option);
    void rebuildAuthorsWithUnwatched();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    // Not virtual until Qt 7 (see qsortfilterproxymodel.h), so this is Q_INVOKABLE rather
    // than an override - the proxy finds and calls it through the meta-object system.
    // Declaring it "override" here would not compile.
    Q_INVOKABLE QSortFilterProxyModel::DataChangeRelevanceFlags dataChangeRelevanceFlags(
        const QModelIndex& sourceTopLeft,
        const QModelIndex& sourceBottomRight,
        const QList<int>& roles) const;

private:
    QString search_text;
    QString search_text_lower;
    QString watched_option; // "Yes", "No", "Mixed", "All"
    QSet<QString> authorsWithUnwatched;
    std::shared_ptr<rapidfuzz::fuzz::CachedPartialRatio<char>> cached_ratio;
};
