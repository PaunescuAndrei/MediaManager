#include "stdafx.h"
#include "VideosTagsDialog.h"
#include "MainWindow.h"
#include "MainApp.h"
#include <QSqlRelation>
#include "definitions.h"
#include <QListWidgetItem>
#include <QPersistentModelIndex>
#include <QStringList>
#include <algorithm>

namespace {
QString tagsToString(const QSet<Tag>& tags) {
    if (tags.isEmpty()) return QString();
    QList<Tag> sortedTags = tags.values();
    std::sort(sortedTags.begin(), sortedTags.end(), [](const Tag& lhs, const Tag& rhs) {
        if (lhs.display_priority == rhs.display_priority) {
            return lhs.id < rhs.id;
        }
        return lhs.display_priority < rhs.display_priority;
    });
    QStringList parts;
    parts.reserve(sortedTags.size());
    for (const Tag& tag : sortedTags) {
        parts << tag.name;
    }
    return parts.join(", ");
}
}

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

VideosTagsDialog::VideosTagsDialog(const QList<int> ids, MainWindow* MW, QWidget* parent) : QDialog(parent) {
    this->MW = MW;
    for (auto id : ids) {
        QString tagsText;
        if (this->MW) {
            const QPersistentModelIndex srcIdx = this->MW->modelIndexById(id);
            if (srcIdx.isValid()) {
                QPersistentModelIndex tagsIdx = srcIdx.sibling(srcIdx.row(), ListColumns["TAGS_COLUMN"]);
                tagsText = tagsIdx.data(Qt::DisplayRole).toString();
            }
            else if (this->MW->App && this->MW->App->db) {
                tagsText = tagsToString(this->MW->App->db->getTags(id));
            }
        }
        this->items.append({ id, tagsText });
    }
    ui.setupUi(this);
    this->setWindowModality(Qt::WindowModal);
    if (!this->items.isEmpty()) {
        QString tags = this->items.first().second;
        for (auto& item : this->items) {
            if (item.second != tags) {
                this->show_empty_included = true;
                break;
            }
        }
    }
    this->initTags();
    connect(this->ui.editTagsButton, &QPushButton::clicked, this, [this] {if (this->MW) { if (this->MW->TagsDialogButton()) { this->updateTags(); } } });
    connect(this->ui.InsertButton, &QPushButton::clicked, this, [this] {if (this->MW) { this->insertTags(this->ui.listWidgetAvailable->selectedItems()); } });
    connect(this->ui.RemoveButton, &QPushButton::clicked, this, [this] {if (this->MW) { this->removeTags(this->ui.listWidgetAdded->selectedItems()); } });
    connect(this->ui.listWidgetAvailable, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { this->insertTags({ item }); });
    connect(this->ui.listWidgetAdded, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { this->removeTags({ item }); });
}
