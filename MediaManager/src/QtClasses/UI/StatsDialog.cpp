#include "stdafx.h"
#include "StatsDialog.h"
#include "MainApp.h"
#include "utils.h"
#include <QFont>
#include <QSqlQuery>

StatsDialog::StatsDialog(QWidget* parent) : QDialog(parent)
{
	ui.setupUi(this);
}

void StatsDialog::setupTimeStats(MainApp* app) {
    QGridLayout* layout = ui.time_grid_layout;
    int row = 0;
    
    // Total watched time
    double totalWatchedTime = app->db->getMainInfoValue("timeWatchedTotal", "ALL", "0.0").toDouble();
    addStatToGrid(layout, row++, "Total Watched Time:", QString::fromStdString(utils::convert_time_to_text(totalWatchedTime)));
    
    // Total session time
    double totalSessionTime = app->db->getMainInfoValue("timeSessionTotal", "ALL", "0.0").toDouble();
    addStatToGrid(layout, row++, "Total Session Time:", QString::fromStdString(utils::convert_time_to_text(totalSessionTime)));
    
    // Today's watched time
    double todayWatchedTime = app->db->getMainInfoValue("timeWatchedToday", "ALL", "0.0").toDouble();
    addStatToGrid(layout, row++, "Watched Time Today:", QString::fromStdString(utils::convert_time_to_text(todayWatchedTime)));
    
    // Today's session time
    double todaySessionTime = app->db->getMainInfoValue("timeSessionToday", "ALL", "0.0").toDouble();
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
    int plusWatchedToday = app->db->getMainInfoValue("videosWatchedToday", "PLUS", "0").toInt();
    int minusWatchedToday = app->db->getMainInfoValue("videosWatchedToday", "MINUS", "0").toInt();
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
