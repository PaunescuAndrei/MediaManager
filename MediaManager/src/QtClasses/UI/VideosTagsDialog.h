#pragma once
#include <QDialog>
#include "ui_VideosTagsDialog.h"
#include <QPair>

class MainWindow;

class VideosTagsDialog :
    public QDialog
{
    Q_OBJECT
public:
    MainWindow* MW = nullptr;
    QList<QPair<int, QString>> items = QList<QPair<int, QString>>();
    bool show_empty_included = false;
    VideosTagsDialog(QList<QTreeWidgetItem*> items, MainWindow* MW, QWidget* parent = nullptr);
    VideosTagsDialog(const QList<int> ids, MainWindow* MW, QWidget* parent = nullptr);
    void insertTags(QList<QListWidgetItem*> items);
    void removeTags(QList<QListWidgetItem*> items);
    void initTags();
    void updateTags();
    ~VideosTagsDialog();
    Ui::VideosTagsDialog ui;
};

