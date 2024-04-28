#include "stdafx.h"
#include "VideosTagsDialog.h"
#include "MainWindow.h"
#include "MainApp.h"
#include <QSqlRelation>
#include "definitions.h"
#include <QListWidgetItem>

VideosTagsDialog::VideosTagsDialog(QList<QTreeWidgetItem*> items, MainWindow* MW, QWidget* parent) : QDialog(parent) {
    this->MW = MW;
    for (auto item : items) {
        this->items.append({item->data(ListColumns["PATH_COLUMN"], CustomRoles::id).toInt(),item->text(ListColumns["TAGS_COLUMN"])});
    }
    ui.setupUi(this);
    this->setWindowModality(Qt::WindowModal);
    QString tags = this->items.first().second;
    for (auto& item : this->items) {
        if (item.second != tags) {
            this->show_empty_included = true;
            break;
        }
    }
    this->initTags();
    connect(this->ui.editTagsButton, &QPushButton::clicked, this, [this] {if (this->MW) { if (this->MW->TagsDialogButton()) { this->updateTags(); } } });
    connect(this->ui.InsertButton, &QPushButton::clicked, this, [this] {if (this->MW) { this->insertTags(this->ui.listWidgetAvailable->selectedItems()); } });
    connect(this->ui.RemoveButton, &QPushButton::clicked, this, [this] {if (this->MW) { this->removeTags(this->ui.listWidgetAdded->selectedItems()); } });
    connect(this->ui.listWidgetAvailable, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { this->insertTags({item}); });
    connect(this->ui.listWidgetAdded, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { this->removeTags({ item }); });
}

void VideosTagsDialog::insertTags(QList<QListWidgetItem*> items) {
    if (items.isEmpty())
        return;
    for (auto item : items) {
        this->ui.listWidgetAvailable->takeItem(this->ui.listWidgetAvailable->row(item));
        this->ui.listWidgetAdded->addItem(item);
    }
    this->ui.listWidgetAdded->sortItems();
}

void VideosTagsDialog::removeTags(QList<QListWidgetItem*> items) {
    if (items.isEmpty())
        return;
    for (auto item : items) {
        this->ui.listWidgetAdded->takeItem(this->ui.listWidgetAdded->row(item));
        this->ui.listWidgetAvailable->addItem(item);
    }
    this->ui.listWidgetAvailable->sortItems();
}

void VideosTagsDialog::initTags() {
    QSet<Tag> all_tags = this->MW->App->db->getAllTags();
    QSet<Tag> added_tags = QSet<Tag>();
    if (not this->show_empty_included) {
        int item_id = this->items.first().first;
        added_tags = this->MW->App->db->getTags(item_id);
    }
    QSet<Tag> available_tags = all_tags - added_tags;

    this->ui.listWidgetAvailable->clear();
    this->ui.listWidgetAdded->clear();
    for (auto& tag : available_tags) {
        QListWidgetItem* item = new QListWidgetItem(tag.name, this->ui.listWidgetAvailable);
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(Qt::UserRole, QVariant::fromValue(tag));
    }
    for (auto& tag : added_tags) {
        QListWidgetItem* item = new QListWidgetItem(tag.name, this->ui.listWidgetAdded);
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(Qt::UserRole, QVariant::fromValue(tag));
    }
    this->ui.listWidgetAvailable->sortItems();
    this->ui.listWidgetAdded->sortItems();
}

void VideosTagsDialog::updateTags()
{
    QSet<Tag> all_tags = this->MW->App->db->getAllTags();
    for (int i = 0; i < this->ui.listWidgetAvailable->count(); ++i) {
        QListWidgetItem* item = this->ui.listWidgetAvailable->item(i);
        if (not all_tags.contains(item->data(Qt::UserRole).value<Tag>())) {
            delete this->ui.listWidgetAvailable->takeItem(i);
            --i; // Since the list shrinks after deletion, adjust index
        }
        else {
            all_tags.remove(item->data(Qt::UserRole).value<Tag>());
        }
    }
    for (int i = 0; i < this->ui.listWidgetAdded->count(); ++i) {
        QListWidgetItem* item = this->ui.listWidgetAdded->item(i);
        if (not all_tags.contains(item->data(Qt::UserRole).value<Tag>())) {
            delete this->ui.listWidgetAdded->takeItem(i);
            --i; // Since the list shrinks after deletion, adjust index
        }
        else {
            all_tags.remove(item->data(Qt::UserRole).value<Tag>());
        }
    }
    for (auto& tag : all_tags) {
        QListWidgetItem* item = new QListWidgetItem(tag.name, this->ui.listWidgetAvailable);
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(Qt::UserRole, QVariant::fromValue(tag));
    }
    this->ui.listWidgetAvailable->sortItems();
    this->ui.listWidgetAdded->sortItems();
}

VideosTagsDialog::~VideosTagsDialog() {

}