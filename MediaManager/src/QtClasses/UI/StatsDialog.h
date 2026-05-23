#pragma once
#include <QDialog>
#include <QLabel>
#include <QGridLayout>
#include <QSqlQuery>
#include "ui_StatsDialog.h"

class QChartView;
class QChart;
class QBarSet;
class QBarSeries;
class QBarCategoryAxis;
class QValueAxis;

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
    void setupChartsTab(MainApp* app);

protected:
    void changeEvent(QEvent* event) override;
    
private slots:
    void refreshCharts();

private:
    QLabel* createStatLabel(const QString& text, const QString& value, bool isValue = false);
    void addStatToGrid(QGridLayout* layout, int row, const QString& label, const QString& value);
    void addRatingDistributionTable(QGridLayout* layout, int& row, const QMap<double, int>& ratingCounts);
    void setupContributionHeatmap(QLayout* layout);
    void createDailyBars(QLayout* layout);
    void createHourlyHistogram(QLayout* layout);
    void createDayOfWeek(QLayout* layout);
    void updateDailyBars();
    void updateHourlyHistogram();
    void updateDayOfWeek();
    void applyChartsTheme();
    bool showCounts() const;
    int selectedDays() const;

    MainApp* m_app = nullptr;
    bool m_updating = false;

    QChartView* m_dailyChartView = nullptr;
    QChart* m_dailyChart = nullptr;
    QBarSeries* m_dailySeries = nullptr;
    QBarSet* m_dailyBarSet = nullptr;
    QBarCategoryAxis* m_dailyAxisX = nullptr;
    QValueAxis* m_dailyAxisY = nullptr;

    QChartView* m_hourlyChartView = nullptr;
    QChart* m_hourlyChart = nullptr;
    QBarSeries* m_hourlySeries = nullptr;
    QBarSet* m_hourlyBarSet = nullptr;
    QBarCategoryAxis* m_hourlyAxisX = nullptr;
    QValueAxis* m_hourlyAxisY = nullptr;

    QChartView* m_dowChartView = nullptr;
    QChart* m_dowChart = nullptr;
    QBarSeries* m_dowSeries = nullptr;
    QBarSet* m_dowBarSet = nullptr;
    QBarCategoryAxis* m_dowAxisX = nullptr;
    QValueAxis* m_dowAxisY = nullptr;
};
