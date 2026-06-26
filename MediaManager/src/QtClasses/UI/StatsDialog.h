#pragma once
#include <QDialog>
#include <QLabel>
#include <QGridLayout>
#include <QSqlQuery>
#include <QProgressBar>
#include "ui_StatsDialog.h"

class QChartView;
class QChart;
class QBarSet;
class QBarSeries;
class QBarCategoryAxis;
class QValueAxis;

class MainApp;
class ContributionHeatmapWidget;

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
    void setupAchievements(MainApp* app);
    void setupStreaksTab(MainApp* app);
    void setupAuthorsTab(MainApp* app);

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void refreshCharts();
    void refreshAuthors();

private:
    QLabel* createStatLabel(const QString& text, const QString& value, bool isValue = false);
    void addStatToGrid(QGridLayout* layout, int row, const QString& label, const QString& value);
    void addRatingDistributionTable(QGridLayout* layout, int& row, const QMap<double, int>& ratingCounts);
    void addAuthorRow(QGridLayout* layout, int row, int rank, const QString& author, const QString& value, const QColor& barColor = QColor());
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

    // Streaks tab — reusable grid layouts to avoid widget churn on refresh
    QGridLayout* m_topAuthorsGrid = nullptr;
    QGridLayout* m_topRatedGrid = nullptr;
    QGridLayout* m_completionGrid = nullptr;
    QGridLayout* m_hiddenGemsGrid = nullptr;

    // Shared caches to avoid redundant DB queries between setup methods
    QVector<QPair<QDate, double>> m_heatmapCache;
    int m_cachedVideosToday = -1;
    double m_cachedWatchedTodaySec = -1.0;
    int m_cachedDailyVideoGoal = -1;
    int m_cachedDailyTimeGoalSec = -1;
};
