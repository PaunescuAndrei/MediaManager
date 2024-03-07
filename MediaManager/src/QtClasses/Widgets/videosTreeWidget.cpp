#include "stdafx.h"
#include "videosTreeWidget.h"
#include <QMimeData>
#include <Qt>
#include "definitions.h"
#include "utils.h"
#include "TreeWidgetItem.h"
#include <QDir>

videosTreeWidget::videosTreeWidget(QWidget* parent) : QTreeWidget(parent) {
	this->setAcceptDrops(true);
	this->setIconSize(QSize(72, 72));
}
void videosTreeWidget::mousePressEvent(QMouseEvent* event)
{
	QTreeWidget::mousePressEvent(event);
	if (event->button() == Qt::MiddleButton) {
		QModelIndex index = indexAt(event->pos());
		if (index.isValid()) {
			QTreeWidgetItem *item = this->itemFromIndex(index);
			if(item != nullptr)
				emit itemMiddleClicked(item,index.column());
		}
	}
}
void videosTreeWidget::clear()
{
	QTreeWidget::clear();
	this->last_selected = nullptr;
}

QList<QTreeWidgetItem*> videosTreeWidget::findItemsCustom(const QString& text, Qt::MatchFlags flags, int column, int hits) {
	QModelIndexList indexes = this->model()->match(model()->index(0, column, QModelIndex()), Qt::DisplayRole, text, hits, flags);
	QList<QTreeWidgetItem*> items;
	const int indexesSize = indexes.size();
	items.reserve(indexesSize);
	for (int i = 0; i < indexesSize; ++i)
		items.append(this->itemFromIndex(indexes.at(i)));
	return items;
}

void videosTreeWidget::scrollToItemCustom(QTreeWidgetItem* item, bool select, QAbstractItemView::ScrollHint scrollhint) {
	//temp fix until they fix scrollToItem (probably never)
	if (item) {
		QString value = item->text(ListColumns["PATH_COLUMN"]);
		this->findAndScrollToDelayed(value, select, scrollhint);
	}
}


void videosTreeWidget::scrollToItemDelayed(QTreeWidgetItem* item, bool select, QAbstractItemView::ScrollHint scrollhint) {
	//prone to bugs if item gets destroyed
	if (item) {
		QTimer::singleShot(0, [this, item, select, scrollhint] {
			this->scrollToItem(item, scrollhint);
			if (select)
				this->setCurrentItem(item);
		});
	}
}

void videosTreeWidget::findAndScrollToDelayed(QString value, bool select, QAbstractItemView::ScrollHint scrollhint) {
		QTimer::singleShot(0, [this, value, select, scrollhint] {
			QList<QTreeWidgetItem*> items = this->findItemsCustom(value, Qt::MatchExactly, ListColumns["PATH_COLUMN"],1);
			if (!items.isEmpty()) {
				this->scrollToItem(items.first(), scrollhint);
				if (select)
					this->setCurrentItem(items.first());
			}
		});
}

void videosTreeWidget::scrollToVerticalPositionDelayed(int scroll_value)
{
	QTimer::singleShot(0, [this, scroll_value] {this->verticalScrollBar()->setSliderPosition(scroll_value); });
}


videosTreeWidget::~videosTreeWidget() {

}