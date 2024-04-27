#pragma once
#include <QDialog>
#include "ui_TagsDialog.h"
#include <QSqlTableModel>

class MainWindow;

class TagsDialog :
    public QDialog
{
    Q_OBJECT
public:
    QSqlTableModel* model;
    TagsDialog(QSqlDatabase &db, QWidget* parent = nullptr);
    void contextMenu(const QPoint& point);
    bool addTag(QString name, int priority);
    ~TagsDialog();
    Ui::Dialog ui;
};

