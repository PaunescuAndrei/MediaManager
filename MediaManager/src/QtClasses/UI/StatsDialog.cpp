#include "stdafx.h"
#include "StatsDialog.h"
#include "MainApp.h"
#include "utils.h"
#include "ContributionHeatmapWidget.h"
#include <QtCharts>
#include <QFont>
#include <QSqlQuery>
#include <QEvent>
#include <QApplication>

StatsDialog::StatsDialog(QWidget* parent) : QDialog(parent)
{
	ui.setupUi(this);
}

void StatsDialog::setupTimeStats(MainApp* app) {
    QGridLayout* layout = ui.time_grid_layout;
    int row = 0;
    
    double totalWatchedTime = app->db->getTotalWatchedTime();
    addStatToGrid(layout, row++, "Total Watched Time:", QString::fromStdString(utils::convert_time_to_text(totalWatchedTime)));
    
    double totalSessionTime = app->db->getTotalSessionTime();
    addStatToGrid(layout, row++, "Total Session Time:", QString::fromStdString(utils::convert_time_to_text(totalSessionTime)));
    
    double todayWatchedTime = app->db->getTotalWatchedTimeToday();
    addStatToGrid(layout, row++, "Watched Time Today:", QString::fromStdString(utils::convert_time_to_text(todayWatchedTime)));
    
    double todaySessionTime = app->db->getTotalSessionTimeToday();
    addStatToGrid(layout, row++, "Session Time Today:", QString::fromStdString(utils::convert_time_to_text(todaySessionTime)));
}

void StatsDialog::setupVideoStats(MainApp* app) {
    QGridLayout* layout = ui.video_grid_layout;
    int row = 0;
    
    // Videos watched by category (total views)
    int plusWatched = app->db->getVideosWatched("PLUS", 0);
    int minusWatched = app->db->getVideosWatched("MINUS", 0);
    int totalWatched = plusWatched + minusWatched;
    
    addStatToGrid(layout, row++, "Total Videos Watched:", QString::number(totalWatched));
    addStatToGrid(layout, row++, QStringLiteral("%1 Videos Watched:").arg(app->config->get("plus_category_name")), QString::number(plusWatched));
    addStatToGrid(layout, row++, QStringLiteral("%1 Videos Watched:").arg(app->config->get("minus_category_name")), QString::number(minusWatched));
    
    // Videos watched today by category
    int plusWatchedToday = app->db->getVideosWatchedToday("PLUS");
    int minusWatchedToday = app->db->getVideosWatchedToday("MINUS");
    int videosWatchedToday = plusWatchedToday + minusWatchedToday;
    
    addStatToGrid(layout, row++, "Videos Watched Today:", QString::number(videosWatchedToday));
    addStatToGrid(layout, row++, QStringLiteral("%1 Videos Watched Today:").arg(app->config->get("plus_category_name")), QString::number(plusWatchedToday));
    addStatToGrid(layout, row++, QStringLiteral("%1 Videos Watched Today:").arg(app->config->get("minus_category_name")), QString::number(minusWatchedToday));
    
    // Database totals using new functions
    int plusTotal = app->db->getVideoCount("PLUS");
    int minusTotal = app->db->getVideoCount("MINUS");
    
    addStatToGrid(layout, row++, "Total Videos in Database:", QString::number(plusTotal + minusTotal));
    addStatToGrid(layout, row++, QStringLiteral("Total %1 Videos:").arg(app->config->get("plus_category_name")), QString::number(plusTotal));
    addStatToGrid(layout, row++, QStringLiteral("Total %1 Videos:").arg(app->config->get("minus_category_name")), QString::number(minusTotal));
    
    // Completion percentages using unique videos watched
    int plusUniqueWatched = app->db->getUniqueVideosWatched("PLUS");
    int minusUniqueWatched = app->db->getUniqueVideosWatched("MINUS");
    
    if (plusTotal > 0) {
        double plusPercent = (double)plusUniqueWatched / plusTotal * 100;
        addStatToGrid(layout, row++, QStringLiteral("%1 Completion:").arg(app->config->get("plus_category_name")), QString::number(plusPercent, 'f', 1) + "%");
    }
    if (minusTotal > 0) {
        double minusPercent = (double)minusUniqueWatched / minusTotal * 100;
        addStatToGrid(layout, row++, QStringLiteral("%1 Completion:").arg(app->config->get("minus_category_name")), QString::number(minusPercent, 'f', 1) + "%");
    }
}

void StatsDialog::setupRatingStats(MainApp* app) {
    QGridLayout* layout = ui.rating_grid_layout;
    int row = 0;
    
    // Average ratings
    double plusAvgRating = app->db->getAverageRating("PLUS", 0);
    double minusAvgRating = app->db->getAverageRating("MINUS", 0);
    double totalAvgRating = (plusAvgRating + minusAvgRating) / 2;
    
    addStatToGrid(layout, row++, "Overall Average Rating:", QString::number(totalAvgRating, 'f', 2));
    addStatToGrid(layout, row++, QStringLiteral("%1 Average Rating:").arg(app->config->get("plus_category_name")), QString::number(plusAvgRating, 'f', 2));
    addStatToGrid(layout, row++, QStringLiteral("%1 Average Rating:").arg(app->config->get("minus_category_name")), QString::number(minusAvgRating, 'f', 2));
    
    // Unrated videos
    int unratedCount = app->db->getUnratedVideoCount();
    addStatToGrid(layout, row++, "Unrated Videos:", QString::number(unratedCount));

    // Rating distribution
    QMap<double, int> ratingCounts = app->db->getRatingDistribution();
    if (!ratingCounts.isEmpty()) {
        QGridLayout* distributionLayout = ui.rating_distribution_layout;
        int distributionRow = 0;
        addRatingDistributionTable(distributionLayout, distributionRow, ratingCounts);
    }
}

void StatsDialog::addRatingDistributionTable(QGridLayout* layout, int& row, const QMap<double, int>& ratingCounts) {
    // Add table header
    QLabel* distributionTitle = new QLabel("Rating Distribution:");
    QFont titleFont = distributionTitle->font();
    titleFont.setBold(true);
    distributionTitle->setFont(titleFont);
    layout->addWidget(distributionTitle, row, 0, 1, ratingCounts.size() + 1);
    row++;
    
    // Create horizontal table
    // First row: Rating values
    QLabel* ratingHeader = new QLabel("Rating:");
    QFont headerFont = ratingHeader->font();
    headerFont.setBold(true);
    ratingHeader->setFont(headerFont);
    layout->addWidget(ratingHeader, row, 0, Qt::AlignLeft);
    
    int col = 1;
    for (auto it = ratingCounts.begin(); it != ratingCounts.end(); ++it) {
        QLabel* ratingLabel = new QLabel(QString::number(it.key(), 'f', 1));
        ratingLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(ratingLabel, row, col++);
    }
    row++;
    
    // Second row: Count values
    QLabel* countHeader = new QLabel("Count:");
    countHeader->setFont(headerFont);
    layout->addWidget(countHeader, row, 0, Qt::AlignLeft);
    
    col = 1;
    for (auto it = ratingCounts.begin(); it != ratingCounts.end(); ++it) {
        QLabel* countLabel = new QLabel(QString::number(it.value()));
        QFont valueFont = countLabel->font();
        valueFont.setBold(true);
        countLabel->setFont(valueFont);
        countLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(countLabel, row, col++);
    }
    row++;
}

QLabel* StatsDialog::createStatLabel(const QString& text, const QString& value, bool isValue) {
    QLabel* label = new QLabel(isValue ? value : text);
    QFont font = label->font();
    
    if (isValue) {
        font.setBold(true);
        font.setPointSize(font.pointSize() + 1);
    }
    
    label->setFont(font);
    return label;
}

void StatsDialog::addStatToGrid(QGridLayout* layout, int row, const QString& label, const QString& value) {
    layout->addWidget(createStatLabel(label, "", false), row, 0, Qt::AlignLeft);
    layout->addWidget(createStatLabel("", value, true), row, 1, Qt::AlignRight);
}

StatsDialog::~StatsDialog()
{
}

void StatsDialog::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange) {
        refreshCharts();
    }
    QDialog::changeEvent(event);
}

void StatsDialog::setupChartsTab(MainApp* app)
{
    m_app = app;
    setupContributionHeatmap(ui.heatmapLayout);
    createDailyBars(ui.dailyBarsLayout);
    createHourlyHistogram(ui.hourlyLayout);
    createDayOfWeek(ui.dayOfWeekLayout);

    ui.dateRangeCombo->setCurrentIndex(1);
    connect(ui.dateRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StatsDialog::refreshCharts);
    connect(ui.metricCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StatsDialog::refreshCharts);
    connect(ui.refreshChartsButton, &QPushButton::clicked,
            this, &StatsDialog::refreshCharts);
    connect(qApp, &QApplication::paletteChanged,
            this, &StatsDialog::refreshCharts);
}

bool StatsDialog::showCounts() const
{
    return ui.metricCombo->currentIndex() == 1;
}

int StatsDialog::selectedDays() const
{
    switch (ui.dateRangeCombo->currentIndex()) {
        case 0: return 7;
        case 1: return 30;
        case 2: return 90;
        case 3: return 180;
        case 4: return 365;
        default: return 36500;
    }
}

void StatsDialog::refreshCharts()
{
    if (m_updating) return;
    m_updating = true;
    updateDailyBars();
    updateHourlyHistogram();
    updateDayOfWeek();
    applyChartsTheme();
    m_updating = false;
}

void StatsDialog::setupContributionHeatmap(QLayout* layout)
{
    auto data = m_app->db->getDailyWatchedHistory(186);
    auto* heatmap = new ContributionHeatmapWidget();
    heatmap->setData(data);
    layout->addWidget(heatmap);
    layout->setAlignment(heatmap, Qt::AlignCenter);
}

void StatsDialog::createDailyBars(QLayout* layout)
{
    QColor highlight = palette().color(QPalette::Highlight);
    QColor textColor = palette().color(QPalette::Text);
    QColor bgColor = palette().color(QPalette::Base);

    m_dailyChart = new QChart();
    m_dailyChart->setAnimationOptions(QChart::NoAnimation);
    m_dailyChart->legend()->hide();
    m_dailyChart->setMargins(QMargins(0, 0, 0, 0));
    m_dailyChart->setBackgroundRoundness(0);
    m_dailyChart->setBackgroundBrush(QBrush(bgColor));
    m_dailyChart->setTitleBrush(QBrush(textColor));
    m_dailyChart->setTitle("Daily Watched Time");

    m_dailyAxisX = new QBarCategoryAxis();
    m_dailyAxisX->setLabelsColor(textColor);
    m_dailyChart->addAxis(m_dailyAxisX, Qt::AlignBottom);

    m_dailyAxisY = new QValueAxis();
    m_dailyAxisY->setTitleText("Seconds");
    m_dailyAxisY->setLabelsColor(textColor);
    m_dailyAxisY->setTitleBrush(QBrush(textColor));
    m_dailyChart->addAxis(m_dailyAxisY, Qt::AlignLeft);

    m_dailyChartView = new QChartView(m_dailyChart);
    m_dailyChartView->setRenderHint(QPainter::Antialiasing);
    m_dailyChartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_dailyChartView->setMinimumHeight(240);
    m_dailyChartView->setBackgroundBrush(QBrush(bgColor));
    layout->addWidget(m_dailyChartView);

    updateDailyBars();
}

void StatsDialog::createHourlyHistogram(QLayout* layout)
{
    QColor highlight = palette().color(QPalette::Highlight);
    QColor textColor = palette().color(QPalette::Text);
    QColor bgColor = palette().color(QPalette::Base);

    m_hourlyChart = new QChart();
    m_hourlyChart->setAnimationOptions(QChart::NoAnimation);
    m_hourlyChart->legend()->hide();
    m_hourlyChart->setMargins(QMargins(0, 0, 0, 0));
    m_hourlyChart->setBackgroundRoundness(0);
    m_hourlyChart->setBackgroundBrush(QBrush(bgColor));
    m_hourlyChart->setTitleBrush(QBrush(textColor));
    m_hourlyChart->setTitle("Watched Time by Hour of Day");

    m_hourlyAxisX = new QBarCategoryAxis();
    m_hourlyAxisX->setLabelsColor(textColor);
    m_hourlyChart->addAxis(m_hourlyAxisX, Qt::AlignBottom);

    m_hourlyAxisY = new QValueAxis();
    m_hourlyAxisY->setTitleText("Seconds");
    m_hourlyAxisY->setLabelsColor(textColor);
    m_hourlyAxisY->setTitleBrush(QBrush(textColor));
    m_hourlyChart->addAxis(m_hourlyAxisY, Qt::AlignLeft);

    m_hourlyChartView = new QChartView(m_hourlyChart);
    m_hourlyChartView->setRenderHint(QPainter::Antialiasing);
    m_hourlyChartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_hourlyChartView->setMinimumHeight(240);
    m_hourlyChartView->setBackgroundBrush(QBrush(bgColor));
    layout->addWidget(m_hourlyChartView);

    updateHourlyHistogram();
}

void StatsDialog::updateDailyBars()
{
    if (!m_dailyChart) return;
    int days = selectedDays();
    QStringList categories;
    double maxVal = 1.0;

    auto* set = new QBarSet("Data");
    set->setColor(palette().color(QPalette::Highlight));

    if (showCounts()) {
        auto data = m_app->db->getDailyWatchedCount(days);
        for (const auto& pair : data) {
            *set << pair.second;
            if (pair.second > maxVal) maxVal = pair.second;
            categories << (days <= 14 ? pair.first.toString("dd") : pair.first.toString("MM-dd"));
        }
        m_dailyChart->setTitle("Daily Videos Watched");
        m_dailyAxisY->setTitleText("Videos");
    } else {
        auto data = m_app->db->getDailyWatchedHistory(days);
        double divisor = 1.0;
        const char* unit = "Seconds";
        for (const auto& pair : data)
            if (pair.second > maxVal) maxVal = pair.second;
        if (maxVal >= 7200) { divisor = 3600.0; unit = "Hours"; }
        else if (maxVal >= 120) { divisor = 60.0; unit = "Minutes"; }
        maxVal /= divisor;
        for (const auto& pair : data) {
            *set << pair.second / divisor;
            categories << (days <= 14 ? pair.first.toString("dd") : pair.first.toString("MM-dd"));
        }
        m_dailyChart->setTitle("Daily Watched Time");
        m_dailyAxisY->setTitleText(unit);
    }

    auto* series = new QBarSeries();
    series->append(set);
    m_dailyChart->addSeries(series);
    series->attachAxis(m_dailyAxisX);
    series->attachAxis(m_dailyAxisY);

    if (m_dailySeries) {
        m_dailyChart->removeSeries(m_dailySeries);
    }
    m_dailySeries = series;
    m_dailyBarSet = set;

    m_dailyAxisX->clear();
    m_dailyAxisX->append(categories);
    m_dailyAxisX->setLabelsAngle(days > 30 ? -90 : 0);

    m_dailyAxisY->setRange(0, maxVal * 1.15);
    m_dailyAxisY->applyNiceNumbers();
}

void StatsDialog::updateHourlyHistogram()
{
    if (!m_hourlyChart) return;
    int days = selectedDays();

    QColor barColor = palette().color(QPalette::Highlight);
    barColor.setAlpha(180);

    auto* set = new QBarSet("Data");
    set->setColor(barColor);
    QStringList categories;
    for (int h = 0; h < 24; h++)
        categories << QString::number(h);

    double maxVal = 1.0;
    if (showCounts()) {
        auto data = m_app->db->getHourlyWatchedCount(days > 365 ? -1 : days);
        QMap<int, int> hourMap;
        for (const auto& pair : data)
            hourMap[pair.first] = pair.second;
        for (int h = 0; h < 24; h++) {
            double v = static_cast<double>(hourMap.value(h, 0));
            *set << v;
            if (v > maxVal) maxVal = v;
        }
        m_hourlyChart->setTitle("Videos Watched by Hour of Day");
        m_hourlyAxisY->setTitleText("Videos");
    } else {
        auto data = m_app->db->getHourlyWatchedDistribution(days > 365 ? -1 : days);
        QMap<int, double> hourMap;
        for (const auto& pair : data)
            hourMap[pair.first] = pair.second;
        for (int h = 0; h < 24; h++)
            if (hourMap.value(h, 0.0) > maxVal) maxVal = hourMap.value(h, 0.0);
        double divisor = 1.0;
        const char* unit = "Seconds";
        if (maxVal >= 7200) { divisor = 3600.0; unit = "Hours"; }
        else if (maxVal >= 120) { divisor = 60.0; unit = "Minutes"; }
        maxVal /= divisor;
        for (int h = 0; h < 24; h++)
            *set << hourMap.value(h, 0.0) / divisor;
        m_hourlyChart->setTitle("Watched Time by Hour of Day");
        m_hourlyAxisY->setTitleText(unit);
    }

    auto* series = new QBarSeries();
    series->append(set);
    m_hourlyChart->addSeries(series);
    series->attachAxis(m_hourlyAxisX);
    series->attachAxis(m_hourlyAxisY);

    if (m_hourlySeries) {
        m_hourlyChart->removeSeries(m_hourlySeries);
    }
    m_hourlySeries = series;
    m_hourlyBarSet = set;

    m_hourlyAxisY->setRange(0, maxVal * 1.15);
    m_hourlyAxisY->applyNiceNumbers();
}

void StatsDialog::createDayOfWeek(QLayout* layout)
{
    QColor textColor = palette().color(QPalette::Text);
    QColor bgColor = palette().color(QPalette::Base);

    m_dowChart = new QChart();
    m_dowChart->setAnimationOptions(QChart::NoAnimation);
    m_dowChart->legend()->hide();
    m_dowChart->setMargins(QMargins(0, 0, 0, 0));
    m_dowChart->setBackgroundRoundness(0);
    m_dowChart->setBackgroundBrush(QBrush(bgColor));
    m_dowChart->setTitleBrush(QBrush(textColor));
    m_dowChart->setTitle("Watched Time by Day of Week");

    m_dowAxisX = new QBarCategoryAxis();
    m_dowAxisX->setLabelsColor(textColor);
    static const char* dowLabels[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    for (int i = 0; i < 7; i++)
        m_dowAxisX->append(dowLabels[i]);
    m_dowChart->addAxis(m_dowAxisX, Qt::AlignBottom);

    m_dowAxisY = new QValueAxis();
    m_dowAxisY->setTitleText("Seconds");
    m_dowAxisY->setLabelsColor(textColor);
    m_dowAxisY->setTitleBrush(QBrush(textColor));
    m_dowChart->addAxis(m_dowAxisY, Qt::AlignLeft);

    m_dowChartView = new QChartView(m_dowChart);
    m_dowChartView->setRenderHint(QPainter::Antialiasing);
    m_dowChartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_dowChartView->setMinimumHeight(240);
    m_dowChartView->setBackgroundBrush(QBrush(bgColor));
    layout->addWidget(m_dowChartView);

    updateDayOfWeek();
}

void StatsDialog::updateDayOfWeek()
{
    if (!m_dowChart) return;
    int days = selectedDays();

    QColor barColor = palette().color(QPalette::Highlight);
    barColor.setAlpha(180);

    auto* set = new QBarSet("Data");
    set->setColor(barColor);

    QMap<int, double> dowMap;
    double maxVal = 1.0;
    if (showCounts()) {
        auto data = m_app->db->getDayOfWeekCount(days > 365 ? -1 : days);
        for (const auto& pair : data)
            dowMap[pair.first] = static_cast<double>(pair.second);
        m_dowChart->setTitle("Videos Watched by Day of Week");
        m_dowAxisY->setTitleText("Videos");
    } else {
        auto data = m_app->db->getDayOfWeekDistribution(days > 365 ? -1 : days);
        for (const auto& pair : data)
            dowMap[pair.first] = pair.second;
        for (int d = 0; d < 7; d++)
            if (dowMap.value(d, 0.0) > maxVal) maxVal = dowMap.value(d, 0.0);
        double divisor = 1.0;
        const char* unit = "Seconds";
        if (maxVal >= 7200) { divisor = 3600.0; unit = "Hours"; }
        else if (maxVal >= 120) { divisor = 60.0; unit = "Minutes"; }
        maxVal /= divisor;
        for (int d = 0; d < 7; d++)
            dowMap[d] /= divisor;
        m_dowChart->setTitle("Watched Time by Day of Week");
        m_dowAxisY->setTitleText(unit);
    }

    for (int d = 0; d < 7; d++) {
        double v = dowMap.value(d, 0.0);
        *set << v;
        if (v > maxVal) maxVal = v;
    }

    auto* series = new QBarSeries();
    series->append(set);
    m_dowChart->addSeries(series);
    series->attachAxis(m_dowAxisX);
    series->attachAxis(m_dowAxisY);

    if (m_dowSeries) {
        m_dowChart->removeSeries(m_dowSeries);
    }
    m_dowSeries = series;
    m_dowBarSet = set;

    m_dowAxisY->setRange(0, maxVal * 1.15);
    m_dowAxisY->applyNiceNumbers();
}

void StatsDialog::applyChartsTheme()
{
    QColor textColor = palette().color(QPalette::Text);
    QColor bgColor = palette().color(QPalette::Base);

    if (m_dailyChart) {
        m_dailyChart->setBackgroundBrush(QBrush(bgColor));
        m_dailyChart->setTitleBrush(QBrush(textColor));
    }
    if (m_dailyChartView) {
        m_dailyChartView->setBackgroundBrush(QBrush(bgColor));
    }
    if (m_dailyAxisX) {
        m_dailyAxisX->setLabelsColor(textColor);
    }
    if (m_dailyAxisY) {
        m_dailyAxisY->setLabelsColor(textColor);
        m_dailyAxisY->setTitleBrush(QBrush(textColor));
    }

    if (m_hourlyChart) {
        m_hourlyChart->setBackgroundBrush(QBrush(bgColor));
        m_hourlyChart->setTitleBrush(QBrush(textColor));
    }
    if (m_hourlyChartView) {
        m_hourlyChartView->setBackgroundBrush(QBrush(bgColor));
    }
    if (m_hourlyAxisX) {
        m_hourlyAxisX->setLabelsColor(textColor);
    }
    if (m_hourlyAxisY) {
        m_hourlyAxisY->setLabelsColor(textColor);
        m_hourlyAxisY->setTitleBrush(QBrush(textColor));
    }

    if (m_dowChart) {
        m_dowChart->setBackgroundBrush(QBrush(bgColor));
        m_dowChart->setTitleBrush(QBrush(textColor));
    }
    if (m_dowChartView) {
        m_dowChartView->setBackgroundBrush(QBrush(bgColor));
    }
    if (m_dowAxisX) {
        m_dowAxisX->setLabelsColor(textColor);
    }
    if (m_dowAxisY) {
        m_dowAxisY->setLabelsColor(textColor);
        m_dowAxisY->setTitleBrush(QBrush(textColor));
    }
}
