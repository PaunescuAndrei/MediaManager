#pragma once
#include <QTreeWidgetItem>
#include <QCollator>

class TreeWidgetItem :
    public QTreeWidgetItem
{
public:
    QCollator qcollator = QCollator();
    TreeWidgetItem(QTreeWidget* parent = nullptr);
    TreeWidgetItem(const QStringList& strings, int type = 0, QTreeWidget* parent = nullptr);
    bool operator<(const QTreeWidgetItem& other) const;
    ~TreeWidgetItem();
};

