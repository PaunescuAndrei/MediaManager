#pragma once
#include <QStyledItemDelegate>
#include <QAbstractItemView>
#include <QHelpEvent>
#include <QStyleOptionViewItem>
#include <QModelIndex>
#include <QObject>

class AutoToolTipDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    AutoToolTipDelegate(QObject* parent);
    ~AutoToolTipDelegate();

public slots:
    bool helpEvent(QHelpEvent* e, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index);
};

