#include "stdafx.h"
#include "AutoToolTipDelegate.h"
#include <QToolTip>
#include <QPoint>

bool AutoToolTipDelegate::helpEvent(QHelpEvent* e, QAbstractItemView* view,
    const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (!e || !view)
        return false;

    if (e->type() == QEvent::ToolTip) {
        QRect rect = view->visualRect(index);
        QSize size = sizeHint(option, index);
        if (rect.width() < size.width()) {
            QVariant tooltip = index.data(Qt::DisplayRole);
            QRect rect = view->visualRect(index);
            QPoint localPoint = QPoint(rect.x() - 2, rect.y() + 4);
            if (tooltip.canConvert<QString>()) {
                QToolTip::showText(view->viewport()->mapToGlobal(localPoint), tooltip.toString(), view);
                return true;
            }
        }
        if (!QStyledItemDelegate::helpEvent(e, view, option, index))
            QToolTip::hideText();
        return true;
    }

    return QStyledItemDelegate::helpEvent(e, view, option, index);
}

AutoToolTipDelegate::AutoToolTipDelegate(QObject* parent) : QStyledItemDelegate(parent)
{
}

AutoToolTipDelegate::~AutoToolTipDelegate()
{
}
