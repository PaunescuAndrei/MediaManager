#pragma once
#include <QDialog>
#include <QTreeWidgetItem>
#include "ui_UpdatePathsDialog.h"
#include "WheelStrongFocusEventFilter.h"

class MainWindow;

class UpdatePathsDialog : public QDialog
{
    Q_OBJECT

public:
    UpdatePathsDialog(QWidget* parent, MainWindow* mainWindow);
    static QString normalizeFilename(const QString& filepath);
    ~UpdatePathsDialog();
    void addItems(QList<QTreeWidgetItem*> items);

private:
    Ui::UpdatePathsDialog ui;
    MainWindow* mainWindow;
    QHash<QPair<QString, QString>, double> similarity_cache;
    WheelStrongFocusEventFilter* filter;
    QMimeDatabase mimeDb;

    void updateSimilarityText(int row);
    void repopulateAllCombos();
    void repopulateCombo(int row);
    void acceptDialog();
    double calculateSimilarity(const QString& name1, const QString& name2);
    void recalculateSimilarities();
    void autoMatchItems();
    void populateComboBox(QComboBox* pathCombo, const QString& currentPath, const QString& name, const QDir& main_path, bool preserveSelection = false);
    bool isVideoFile(const QString& path);
};
