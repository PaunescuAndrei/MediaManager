#pragma once
#include <QDialog>
#include "ui_InsertSettingsDialog.h"

class InsertSettingsDialog :
    public QDialog
{
    Q_OBJECT
public:
    InsertSettingsDialog(QWidget* parent = nullptr);
    static QString get_author(QString path);
    static QString get_namedir(QString path);
    static QString get_name(QString path, QString dirsplit = "");
    static QString get_type(QString path);
    void init_namedir(QStringList items);
    void init_namedir(QString path);
    void init_authors(QStringList items);
    void init_authors(QString path);
    void init_types(QStringList items);
    void init_types();
    void update_treewidget_author(QTreeWidgetItem* item);
    void update_treewidget_name(QTreeWidgetItem* item);
    void update_treewidget_type(QTreeWidgetItem* item);
    void update_treewidget();
    void update_treewidget(int col);
    ~InsertSettingsDialog();
    Ui::InsertSettingsDialog ui;
};

