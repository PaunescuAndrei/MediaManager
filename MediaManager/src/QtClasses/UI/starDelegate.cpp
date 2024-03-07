#include "stdafx.h"
#include <QtWidgets>
#include "starDelegate.h"
#include "starEditorWidget.h"
#include "starRating.h"
#include "definitions.h"
#include "utils.h"
#include "MainWindow.h"
#include "MainApp.h"

StarDelegate::StarDelegate(QIcon*& active, QIcon*& halfactive, QIcon*& inactive, QWidget* parent, MainWindow* mw) : QStyledItemDelegate(parent)
{
    this->active = &active;
    this->halfactive = &halfactive;
    this->inactive = &inactive;
    this->mw = mw;
}

void StarDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
     QStyledItemDelegate::paint(painter, option, index);
    if (index.data(CustomRoles::rating).canConvert<double>()) {
        StarRating starRating = StarRating(*this->active, *this->halfactive, *this->inactive,index.data(CustomRoles::rating).value<double>(),5.0);
        starRating.paint(painter, option.rect, option.palette, option.state, StarRating::EditMode::ReadOnly);
    } 
}

QSize StarDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (index.data(CustomRoles::rating).canConvert<double>()) {
        StarRating starRating = StarRating(*this->active, *this->halfactive, *this->inactive, index.data(CustomRoles::rating).value<double>(), 5.0);
        return starRating.sizeHint();
    }
    return QStyledItemDelegate::sizeHint(option, index);
}

QWidget *StarDelegate::createEditor(QWidget *parent,  const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (index.data(CustomRoles::rating).canConvert<double>()) {
        starEditorWidget* editor = new starEditorWidget(parent, index);
        editor->setEditMode(starEditorWidget::EditMode::HoverClick);
        editor->editing_finished = false;
        connect(editor, &starEditorWidget::editingFinished,this, &StarDelegate::commitAndCloseEditor);
        return editor;
    }
    return QStyledItemDelegate::createEditor(parent, option, index);
}

void StarDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    if (index.data(CustomRoles::rating).canConvert<double>()) {
        StarRating starRating = StarRating(*this->active, *this->halfactive, *this->inactive, index.data(CustomRoles::rating).value<double>(), 5.0);
        starEditorWidget*starEditor = qobject_cast<starEditorWidget*>(editor);
        starEditor->setStarRating(starRating);
    } else {
        QStyledItemDelegate::setEditorData(editor, index);
    }
}

void StarDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    if (index.data(CustomRoles::rating).canConvert<double>()) {
        starEditorWidget* starEditor = qobject_cast<starEditorWidget*>(editor);
        if(starEditor->edit_mode == starEditorWidget::EditMode::HoverClick && starEditor->editing_finished)
            model->setData(index, QVariant::fromValue(starEditor->starRating().starCount()), CustomRoles::rating);
        else
            model->setData(index, QVariant::fromValue(starEditor->original_value), CustomRoles::rating);
    } else {
        QStyledItemDelegate::setModelData(editor, model, index);
    }
}

void StarDelegate::commitAndCloseEditor()
{
    starEditorWidget* editor = qobject_cast<starEditorWidget*>(sender());
    if (this->mw and editor)
        this->mw->updateRating(editor->item_index, editor->original_value, editor->starRating().starCount());
    emit commitData(editor);
    emit closeEditor(editor);
}
