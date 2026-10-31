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

void VideosTreeView::setTranslucentRows(bool translucent) {
    if (this->translucentRows == translucent)
        return;
    this->translucentRows = translucent;
    // The flag decides how the rows are painted, so a change has to repaint them. The palette that
    // travels with it is set on the view by the caller; a layout-only repaint would leave the previous
    // row colours on screen until something scrolled.
    this->viewport()->update();
}

void VideosTreeView::drawRow(QPainter* painter, const QStyleOptionViewItem& options,
    const QModelIndex& index) const {
    if (!this->translucentRows) {
        // Qt's own path, unchanged -- this is the layer-off rendering, and the reason it is still
        // pixel-identical to the build before the layer existed.
        QTreeView::drawRow(painter, options, index);
        return;
    }
    // Which of the two row colours this row is. The option Qt hands to drawRow() carries no Alternate
    // flag -- the base implementation computes it per item, further down -- so it is derived here the
    // way the view alternates: on the row, from 1. Measured: with the flag read from `options` instead,
    // every row came out Base and the list lost its alternation entirely.
    const bool alternate = this->alternatingRowColors() && (index.row() % 2) == 1;
    const QColor rowColour =
        options.palette.color(alternate ? QPalette::AlternateBase : QPalette::Base);
    QStyleOptionViewItem opt(options);
    painter->fillRect(options.rect, rowColour);
    // The style fills alternate rows from the same role, and per column rather than per row. An alpha-0
    // copy makes that second fill a no-op, so the rectangle above is the row's only fill.
    opt.palette.setColor(QPalette::AlternateBase, QColor(Qt::transparent));
    QTreeView::drawRow(painter, opt, index);
}

void VideosTreeView::mousePressEvent(QMouseEvent* event) {
    if ((event->button() == Qt::LeftButton || event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) && (event->modifiers() & Qt::AltModifier)) {
        this->altPreviewPressed = true;
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid()) emit itemAltClicked(idx);
        event->accept();
        return;
    }
    this->altPreviewPressed = false;
    QTreeView::mousePressEvent(event);
    if (event->button() == Qt::MiddleButton) {
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid()) emit itemMiddleClicked(idx);
    }
}

void VideosTreeView::mouseReleaseEvent(QMouseEvent* event) {
    if (this->altPreviewPressed) {
        this->altPreviewPressed = false;
        event->accept();
        return;
    }
    QTreeView::mouseReleaseEvent(event);
}

void VideosTreeView::mouseMoveEvent(QMouseEvent* event) {
    if (this->altPreviewPressed) {
        if (event->buttons() == Qt::NoButton) {
            // The release may have gone to the preview window; drop the stale guard on
            // the first button-less move instead of swallowing hover forever.
            this->altPreviewPressed = false;
        }
        else {
            event->accept();
            return;
        }
    }
    QTreeView::mouseMoveEvent(event);
}

void VideosTreeView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (this->altPreviewPressed || (event->modifiers() & Qt::AltModifier)) {
        event->accept();
        return;
    }
    QTreeView::mouseDoubleClickEvent(event);
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
