#pragma once
#include <QDialog>
#include <QPair>
#include <QSet>
#include "ui_VideosTagsDialog.h"

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
    // Picker mode: edits a plain set of tag ids without touching the database.
    // Used by the insert dialog, whose rows have no video id yet.
    VideosTagsDialog(const QSet<int>& tagIds, MainWindow* MW, QWidget* parent = nullptr);
    void insertTags(QList<QListWidgetItem*> items);
    void removeTags(QList<QListWidgetItem*> items);
    void initTags();
    void updateTags();
    QSet<int> selectedTagIds() const;
    ~VideosTagsDialog();
    Ui::VideosTagsDialog ui;
private:
    void setupConnections();
    bool tagPickerMode = false;
    QSet<int> initialTagIds;
};
