#include "stdafx.h"
#include "VideosTreeView.h"
#include <QTimer>
#include <QScrollBar>
#include <QPersistentModelIndex>
#include "definitions.h"
#include <QFont>

VideosTreeView::VideosTreeView(QWidget* parent) : QTreeView(parent) {
    setAcceptDrops(true);
    setIconSize(QSize(72, 72));
    setAllColumnsShowFocus(true);
}

void VideosTreeView::mousePressEvent(QMouseEvent* event) {
    QTreeView::mousePressEvent(event);
    if (event->button() == Qt::MiddleButton) {
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid()) emit itemMiddleClicked(idx);
    }
}

void VideosTreeView::findAndScrollToDelayed(QString value, bool select, QAbstractItemView::ScrollHint scrollhint) {
    QTimer::singleShot(0, [this, value, select, scrollhint] {
        if (!this->model()) return;
        QModelIndexList indexes = this->model()->match(this->model()->index(0, ListColumns["PATH_COLUMN"]), Qt::DisplayRole, value, 1, Qt::MatchExactly);
        if (!indexes.isEmpty()) {
            const QModelIndex idx = indexes.first();
            this->scrollTo(idx, scrollhint);
            if (select) this->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::Clear | QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    });
}

void VideosTreeView::scrollToVerticalPositionDelayed(int scroll_value) {
    QTimer::singleShot(0, [this, scroll_value] { if (this->verticalScrollBar()) this->verticalScrollBar()->setSliderPosition(scroll_value); });
}

void VideosTreeView::scrollToDelayed(const QModelIndex& index, QAbstractItemView::ScrollHint scrollhint) {
    QPersistentModelIndex persistent(index);
    QTimer::singleShot(0, [this, persistent, scrollhint] {
        if (!persistent.isValid()) return;
        this->scrollTo(persistent, scrollhint);
    });
}

void VideosTreeView::configureHeader() {
    QHeaderView* header = this->header();
    if (!header) return;
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setStretchLastSection(false);
    header->setSectionsClickable(true);
    QFont headerFont = header->font();
    headerFont.setPointSize(11);
    header->setFont(headerFont);
    header->setSectionResizeMode(ListColumns["AUTHOR_COLUMN"], QHeaderView::Interactive);
    header->setSectionResizeMode(ListColumns["NAME_COLUMN"], QHeaderView::Stretch);
    header->setSectionResizeMode(ListColumns["PATH_COLUMN"], QHeaderView::Stretch);
    header->setSectionResizeMode(ListColumns["TAGS_COLUMN"], QHeaderView::Fixed);
    header->resizeSection(ListColumns["TAGS_COLUMN"], 150);
    header->setDefaultAlignment(Qt::AlignLeft);
    header->setSectionResizeMode(ListColumns["TYPE_COLUMN"], QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ListColumns["WATCHED_COLUMN"], QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ListColumns["VIEWS_COLUMN"], QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ListColumns["RATING_COLUMN"], QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ListColumns["RANDOM%_COLUMN"], QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ListColumns["DATE_CREATED_COLUMN"], QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ListColumns["LAST_WATCHED_COLUMN"], QHeaderView::ResizeToContents);
}
