#pragma once
#include <QDialog>
#include <QLabel>
#include <QGridLayout>
#include <QSqlQuery>
#include "ui_StatsDialog.h"

class MainApp;

class StatsDialog :
    public QDialog
{
    Q_OBJECT
public:
    StatsDialog(QWidget* parent = nullptr);
    ~StatsDialog();
    Ui::StatsDialog ui;
    
    void setupTimeStats(MainApp* app);
    void setupVideoStats(MainApp* app);
    void setupRatingStats(MainApp* app);
    
private:
    QLabel* createStatLabel(const QString& text, const QString& value, bool isValue = false);
    void addStatToGrid(QGridLayout* layout, int row, const QString& label, const QString& value);
    void addRatingDistributionTable(QGridLayout* layout, int& row, const QMap<double, int>& ratingCounts);
};

