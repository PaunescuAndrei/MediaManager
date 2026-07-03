#include "stdafx.h"
#include "AutoToolTipDelegate.h"
#include <QToolTip>
#include <QPoint>
#include <QApplication>

bool AutoToolTipDelegate::helpEvent(QHelpEvent* e, QAbstractItemView* view,
    const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (!e || !view)
        return false;

    if (e->type() == QEvent::ToolTip) {
        QString text = index.data(Qt::DisplayRole).toString();
        if (!text.isEmpty()) {
            QStyleOptionViewItem opt = option;
            initStyleOption(&opt, index);

            QRect textRect = view->style()->subElementRect(
                QStyle::SE_ItemViewItemText, &opt, view);

            // Qt's own text-eliding code (QCommonStyle::viewItemDrawText, used by
            // paint()) reserves this exact margin on each side of the text rect
            // before eliding. subElementRect() doesn't factor it in, which is the
            // real source of the dead zone -- not something to hand-tune.
            int textMargin = view->style()->pixelMetric(
                QStyle::PM_FocusFrameHMargin, &opt, view) + 1;
            int availableWidth = textRect.width() - (2 * textMargin);

            QString elided = opt.fontMetrics.elidedText(text, Qt::ElideRight, availableWidth);
            bool clipped = (elided != text);

            if (clipped) {
                QPoint pos = view->viewport()->mapToGlobal(textRect.bottomLeft());
                QToolTip::showText(pos, text, view->viewport(), textRect);
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