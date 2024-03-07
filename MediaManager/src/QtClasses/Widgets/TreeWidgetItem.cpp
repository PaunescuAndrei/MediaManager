#include "stdafx.h"
#include "TreeWidgetItem.h"
#include "definitions.h"
#include "utils.h"
#include <Qt>

TreeWidgetItem::TreeWidgetItem(QTreeWidget* parent) : QTreeWidgetItem(parent) {
    this->qcollator.setNumericMode(true);
    this->qcollator.setCaseSensitivity(Qt::CaseInsensitive);
}

TreeWidgetItem::TreeWidgetItem(const QStringList& strings, int type, QTreeWidget* parent) : QTreeWidgetItem(parent,strings,type) {
    this->qcollator.setNumericMode(true);
    this->qcollator.setCaseSensitivity(Qt::CaseInsensitive);
}

bool TreeWidgetItem::operator<(const QTreeWidgetItem& other)const {
    int column = treeWidget()->sortColumn();
    if (column == ListColumns["RATING_COLUMN"]) {
        return this->data(ListColumns["RATING_COLUMN"], CustomRoles::rating).toDouble() < other.data(ListColumns["RATING_COLUMN"], CustomRoles::rating).toDouble();
    }
    QString key1 = text(column);
    QString key2 = other.text(column);
    if (key1 == key2) {
        return this->qcollator.compare(text(ListColumns["PATH_COLUMN"]), other.text(ListColumns["PATH_COLUMN"])) < 0;
    }
    return this->qcollator.compare(key1, key2) < 0;
}

TreeWidgetItem::~TreeWidgetItem() {

}