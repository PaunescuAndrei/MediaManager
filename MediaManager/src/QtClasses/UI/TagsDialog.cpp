#include "stdafx.h"
#include "TagsDialog.h"
#include "MainWindow.h"
#include "MainApp.h"
#include <QSqlRecord>
#include "stylesQt.h"

TagsDialog::TagsDialog(QSqlDatabase& db, QWidget* parent) : QDialog(parent) {
    ui.setupUi(this);
    this->setWindowModality(Qt::WindowModal);
    this->ui.prioritySpinBox->setStyleSheet(get_stylesheet("spinbox"));
    this->ui.tableView->setSortingEnabled(true);
    this->model = new QSqlTableModel(this, db);
    this->model->setTable("tags");
    this->model->setEditStrategy(QSqlTableModel::OnManualSubmit);
    this->model->select();
    this->ui.tableView->setModel(model);
    if (this->ui.tableView->horizontalHeader()->count() >= 3){
        this->ui.tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    }
    connect(this->ui.addTagButton, &QPushButton::clicked, this, [this] {this->addTag(this->ui.tagNameLineEdit->text(), this->ui.prioritySpinBox->value()); });
    this->ui.tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this->ui.tableView, &QTableView::customContextMenuRequested, this, &TagsDialog::contextMenu);
}

void TagsDialog::contextMenu(const QPoint &point) {
    QModelIndex index = this->ui.tableView->indexAt(point);
    if (!index.isValid())
        return;

    QModelIndexList selected_items = this->ui.tableView->selectionModel()->selectedIndexes();

    if (selected_items.isEmpty())
        return;

    QMenu menu = QMenu(this);
    QAction* delete_action = new QAction("Delete", &menu);
    menu.addAction(delete_action);
    QAction* menu_click = menu.exec(this->ui.tableView->viewport()->mapToGlobal(point));

    if (!menu_click)
        return;

    if (menu_click == delete_action) {
        for(QModelIndex &item : selected_items) {
            this->model->removeRow(item.row());
        }
        if (not this->model->submitAll()) {
            if (qApp)
                qMainApp->showErrorMessage("Database error: " + this->model->lastError().text());
            this->model->revertAll();
        }
    }
}

bool TagsDialog::addTag(QString name, int priority) {
    QSqlRecord tag = this->model->record();
    tag.setValue("name", name);
    tag.setValue("display_priority", priority);
    bool ok = this->model->insertRecord(-1, tag);
    if (not ok)
        return false;
    if (this->model->submitAll()) {
        return true;
    }
    else {
        this->model->revertAll();
        if (qApp)
            qMainApp->showErrorMessage("Database error: " + this->model->lastError().text());
        return false;
    }
    return true;
}

TagsDialog::~TagsDialog() {

}