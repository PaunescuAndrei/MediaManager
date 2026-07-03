#include "stdafx.h"
#include "StatsDialog.h"
#include "MainApp.h"
#include "utils.h"
#include "ContributionHeatmapWidget.h"
#include "AutoToolTipDelegate.h"
#include <QtCharts>
#include <QFont>
#include <QSqlQuery>
#include <QEvent>
#include <QApplication>
#include <QProgressBar>
#include <QPushButton>

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

    int totalWatchDays = app->db->getTotalWatchDays();
    if (totalWatchDays > 0) {
        double avgDailyTime = totalWatchedTime / totalWatchDays;
        addStatToGrid(layout, row++, "Avg Daily Watch Time:", QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(avgDailyTime))));
    }
    if (m_cachedAvgSessionTime < 0.0)
        m_cachedAvgSessionTime = app->db->getAverageSessionTime();
    addStatToGrid(layout, row++, "Avg Session Length:", QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(m_cachedAvgSessionTime))));
    if (m_cachedAvgSessionTimePerDay < 0.0)
        m_cachedAvgSessionTimePerDay = app->db->getAverageSessionTimePerDay();
    addStatToGrid(layout, row++, "Avg Daily Session Time:", QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(m_cachedAvgSessionTimePerDay))));
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

    // Averages (from watch_history for accuracy)
    if (m_cachedAvgCompletedPerDay < 0.0)
        m_cachedAvgCompletedPerDay = app->db->getAverageCompletedPerDay();
    addStatToGrid(layout, row++, "Avg Videos Per Day:", QString::number(m_cachedAvgCompletedPerDay, 'f', 1));
    int totalVids = plusTotal + minusTotal;
    if (totalVids > 0) {
        int totalViews = app->db->getTotalViews("PLUS") + app->db->getTotalViews("MINUS");
        double avgViewsPerVideo = (double)totalViews / totalVids;
        addStatToGrid(layout, row++, "Avg Views Per Video:", QString::number(avgViewsPerVideo, 'f', 1));
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

    int ratedCount = app->db->getRatedVideoCount();
    addStatToGrid(layout, row++, "Ratings Given:", QString::number(ratedCount));

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

void StatsDialog::setupAchievements(MainApp* app)
{
    int totalWatchDays = app->db->getTotalWatchDays();
    QDateTime firstWatch = app->db->getFirstWatchDate();
    QString firstWatchStr = firstWatch.isValid() ? firstWatch.toString("d MMMM yyyy") : "N/A";
    int totalVideos = app->db->getVideoCount("PLUS") + app->db->getVideoCount("MINUS");

    QGridLayout* layout = qobject_cast<QGridLayout*>(ui.overviewTab->findChild<QGridLayout*>("video_grid_layout"));
    if (!layout) return;

    // Find the row after existing video stats and add achievements
    int row = layout->rowCount();
    addStatToGrid(layout, row++, "Total Active Days:", QString::number(totalWatchDays));
    addStatToGrid(layout, row++, "First Video Watched:", firstWatchStr);
    addStatToGrid(layout, row++, "Total Videos in Library:", QString::number(totalVideos));

    // Today's Progress section
    QGridLayout* goalsLayout = new QGridLayout();
    QWidget* goalsContainer = new QWidget();
    goalsContainer->setLayout(goalsLayout);

    QLabel* progressTitle = new QLabel("Today's Progress");
    QFont titleFont = progressTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSize(14);
    progressTitle->setFont(titleFont);

    int goalRow = 0;
    QLabel* videoGoalLabel = new QLabel("Video Goal:");
    goalsLayout->addWidget(videoGoalLabel, goalRow, 0);

    double dailyVideoGoal = app->config->get("daily_video_goal").toDouble();
    int videosToday = app->db->getVideosWatchedToday("PLUS") + app->db->getVideosWatchedToday("MINUS");
    int goalCeil = std::max(static_cast<int>(std::ceil(dailyVideoGoal)), 1);
    QProgressBar* videoBar = new QProgressBar();
    videoBar->setRange(0, goalCeil);
    videoBar->setValue(std::min(videosToday, goalCeil));
    videoBar->setFormat(QString("%1 / %2").arg(videosToday).arg(dailyVideoGoal, 0, 'f', 1));
    videoBar->setTextVisible(true);
    videoBar->setMinimumHeight(22);
    goalsLayout->addWidget(videoBar, goalRow++, 1);

    QLabel* timeGoalLabel = new QLabel("Watch Time Goal:");
    goalsLayout->addWidget(timeGoalLabel, goalRow, 0);

    int dailyTimeGoalMin = app->config->get("daily_time_goal_minutes").toInt();
    int dailyTimeGoalSec = dailyTimeGoalMin * 60;
    double watchedToday = app->db->getTotalWatchedTimeToday();
    QProgressBar* timeBar = new QProgressBar();
    timeBar->setRange(0, std::max(dailyTimeGoalSec, 1));
    timeBar->setValue(std::min(static_cast<int>(watchedToday), dailyTimeGoalSec));
    QString timeProgressStr = QString("%1 / %2").arg(
        QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(watchedToday))),
        QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(dailyTimeGoalSec))));
    timeBar->setFormat(timeProgressStr);
    timeBar->setTextVisible(true);
    timeBar->setMinimumHeight(22);
    goalsLayout->addWidget(timeBar, goalRow++, 1);

    // Insert goals after the video stats
    QVBoxLayout* progressGoalsLayout = qobject_cast<QVBoxLayout*>(ui.progress_goals_container->layout());
    if (progressGoalsLayout) {
        progressGoalsLayout->insertWidget(progressGoalsLayout->count(), progressTitle);
        progressGoalsLayout->insertWidget(progressGoalsLayout->count(), goalsContainer);
    }

    // Cache shared values so setupStreaksTab can reuse them
    m_cachedVideosToday = videosToday;
    m_cachedWatchedTodaySec = watchedToday;
    m_cachedDailyVideoGoal = dailyVideoGoal;
    m_cachedDailyTimeGoalSec = dailyTimeGoalSec;
}

void StatsDialog::setupStreaksTab(MainApp* app)
{
    m_app = app;

    // Initialize heatmap range from config
    m_heatmapDays = qBound(1, app->config->get("stats_heatmap_months").toInt(), 24) * 31;

    // Streak display
    QGridLayout* streakLayout = ui.streakDisplayLayout;
    WatchStreak streak = app->db->getWatchStreak();
    int totalDays = app->db->getTotalWatchDays();
    QDateTime firstWatch = app->db->getFirstWatchDate();

    // Relative date helper (avoids duplicated logic for streakStart / lastWatched)
    auto relativeDate = [](const QDate& d) -> QString {
        QDate today = QDate::currentDate();
        if (d == today) return QStringLiteral("Today");
        if (d == today.addDays(-1)) return QStringLiteral("Yesterday");
        return d.toString(QStringLiteral("d MMM yyyy"));
    };

    // Current Streak - large prominent display
    QLabel* currentLabel = new QLabel("Current Streak");
    QFont currentTitleFont = currentLabel->font();
    currentTitleFont.setPointSize(12);
    currentTitleFont.setBold(true);
    currentLabel->setFont(currentTitleFont);
    streakLayout->addWidget(currentLabel, 0, 0, Qt::AlignCenter);

    QLabel* streakCurrentLabel = new QLabel(QString::number(streak.currentStreak));
    QFont streakFont = streakCurrentLabel->font();
    streakFont.setPointSize(48);
    streakFont.setBold(true);
    streakCurrentLabel->setFont(streakFont);
    streakCurrentLabel->setAlignment(Qt::AlignCenter);
    streakLayout->addWidget(streakCurrentLabel, 1, 0, Qt::AlignCenter);

    QLabel* currentUnit = new QLabel(streak.currentStreak == 1 ? "day" : "days");
    currentUnit->setAlignment(Qt::AlignCenter);
    streakLayout->addWidget(currentUnit, 2, 0, Qt::AlignCenter);

    // Longest Streak
    QLabel* longestLabel = new QLabel("Longest Streak");
    QFont longestTitleFont = longestLabel->font();
    longestTitleFont.setPointSize(12);
    longestTitleFont.setBold(true);
    longestLabel->setFont(longestTitleFont);
    streakLayout->addWidget(longestLabel, 0, 1, Qt::AlignCenter);

    QLabel* streakLongestLabel = new QLabel(QString::number(streak.longestStreak));
    QFont longestStreakFont = streakLongestLabel->font();
    longestStreakFont.setPointSize(48);
    longestStreakFont.setBold(true);
    streakLongestLabel->setFont(longestStreakFont);
    streakLongestLabel->setAlignment(Qt::AlignCenter);
    streakLayout->addWidget(streakLongestLabel, 1, 1, Qt::AlignCenter);

    QLabel* longestUnit = new QLabel(streak.longestStreak == 1 ? "day" : "days");
    longestUnit->setAlignment(Qt::AlignCenter);
    streakLayout->addWidget(longestUnit, 2, 1, Qt::AlignCenter);

    // Bottom stats — 2x2 grid
    QFont statFont;
    statFont.setPointSize(11);

    // Row 3, Col 0: Streak Since (under Current Streak)
    QString streakStartStr = (streak.currentStreak > 0 && streak.streakStartDate.isValid())
        ? relativeDate(streak.streakStartDate) : QStringLiteral("—");
    QLabel* streakStartLabel = new QLabel(QString("Streak Since: %1").arg(streakStartStr));
    streakStartLabel->setAlignment(Qt::AlignCenter);
    streakStartLabel->setFont(statFont);
    streakLayout->addWidget(streakStartLabel, 3, 0, Qt::AlignCenter);

    // Row 3, Col 1: Total Active Days (under Longest Streak)
    QLabel* totalActiveDaysLabel = new QLabel(QString("Total Active Days: %1").arg(totalDays));
    totalActiveDaysLabel->setAlignment(Qt::AlignCenter);
    totalActiveDaysLabel->setFont(statFont);
    streakLayout->addWidget(totalActiveDaysLabel, 3, 1, Qt::AlignCenter);

    // Row 4, Col 0: Last Watched
    QString lastWatchedStr = streak.lastWatchedDate.isValid()
        ? relativeDate(streak.lastWatchedDate) : QStringLiteral("N/A");
    QLabel* lastWatchedLabel = new QLabel(QString("Last Watched: %1").arg(lastWatchedStr));
    lastWatchedLabel->setAlignment(Qt::AlignCenter);
    lastWatchedLabel->setFont(statFont);
    streakLayout->addWidget(lastWatchedLabel, 4, 0, Qt::AlignCenter);

    // Row 4, Col 1: Longest Streak date interval (under Total Active Days)
    QString longestIntervalStr;
    if (streak.longestStreak > 0 && streak.longestStreakStartDate.isValid() && streak.longestStreakEndDate.isValid()) {
        if (streak.longestStreakStartDate == streak.longestStreakEndDate)
            longestIntervalStr = streak.longestStreakStartDate.toString("d MMM yyyy");
        else
            longestIntervalStr = QString("%1 – %2")
                .arg(streak.longestStreakStartDate.toString("d MMM yyyy"))
                .arg(streak.longestStreakEndDate.toString("d MMM yyyy"));
    } else {
        longestIntervalStr = QStringLiteral("—");
    }
    QLabel* longestIntervalLabel = new QLabel(QString("Longest Streak: %1").arg(longestIntervalStr));
    longestIntervalLabel->setAlignment(Qt::AlignCenter);
    longestIntervalLabel->setFont(statFont);
    streakLayout->addWidget(longestIntervalLabel, 4, 1, Qt::AlignCenter);

    // Streak calendar heatmap — cache for reuse by setupContributionHeatmap
    m_heatmapCache = app->db->getDailyWatchedHistory(m_heatmapDays);
    ContributionHeatmapWidget* streakCalendar = new ContributionHeatmapWidget();
    streakCalendar->setDayRange(m_heatmapDays);
    streakCalendar->setData(m_heatmapCache);
    ui.streakCalendarLayout->addWidget(streakCalendar);
    ui.streakCalendarLayout->setAlignment(streakCalendar, Qt::AlignCenter);

    // Watching Since under the heatmap
    QString firstStr = firstWatch.isValid() ? firstWatch.toString("d MMMM yyyy") : "N/A";
    QLabel* firstWatchLabel = new QLabel(QString("Watching Since: %1").arg(firstStr));
    firstWatchLabel->setAlignment(Qt::AlignCenter);
    firstWatchLabel->setFont(statFont);
    ui.streakCalendarLayout->addWidget(firstWatchLabel);

    // Daily Goals — reuse cached values from setupAchievements when available
    QGridLayout* goalsGrid = ui.goalsGridLayout;
    int row = 0;

    double dailyVideoGoal = m_cachedDailyVideoGoal >= 0.0
        ? m_cachedDailyVideoGoal
        : app->config->get("daily_video_goal").toDouble();
    int videosToday = m_cachedVideosToday >= 0
        ? m_cachedVideosToday
        : app->db->getVideosWatchedToday("PLUS") + app->db->getVideosWatchedToday("MINUS");

    QLabel* dailyVideoGoalLabel = new QLabel(QString("Videos: %1 / %2").arg(videosToday).arg(dailyVideoGoal, 0, 'f', 1));
    goalsGrid->addWidget(dailyVideoGoalLabel, row, 0);

    QProgressBar* dailyVideoGoalBar = new QProgressBar();
    int goalCeil2 = std::max(static_cast<int>(std::ceil(dailyVideoGoal)), 1);
    dailyVideoGoalBar->setRange(0, goalCeil2);
    dailyVideoGoalBar->setValue(std::min(videosToday, goalCeil2));
    dailyVideoGoalBar->setTextVisible(true);
    dailyVideoGoalBar->setMinimumHeight(22);
    goalsGrid->addWidget(dailyVideoGoalBar, row++, 1);

    int dailyTimeGoalMin = m_cachedDailyTimeGoalSec >= 0
        ? m_cachedDailyTimeGoalSec / 60
        : app->config->get("daily_time_goal_minutes").toInt();
    int dailyTimeGoalSec = m_cachedDailyTimeGoalSec >= 0
        ? m_cachedDailyTimeGoalSec
        : dailyTimeGoalMin * 60;
    double watchedToday = m_cachedWatchedTodaySec >= 0.0
        ? m_cachedWatchedTodaySec
        : app->db->getTotalWatchedTimeToday();

    QLabel* dailyTimeGoalLabel = new QLabel(QString("Time: %1 / %2").arg(
        QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(watchedToday))),
        QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(dailyTimeGoalSec)))));
    goalsGrid->addWidget(dailyTimeGoalLabel, row, 0);

    QProgressBar* dailyTimeGoalBar = new QProgressBar();
    dailyTimeGoalBar->setRange(0, std::max(dailyTimeGoalSec, 1));
    dailyTimeGoalBar->setValue(std::min(static_cast<int>(watchedToday), dailyTimeGoalSec));
    dailyTimeGoalBar->setTextVisible(true);
    dailyTimeGoalBar->setMinimumHeight(22);
    goalsGrid->addWidget(dailyTimeGoalBar, row++, 1);

    // Heatmap range combo — load default from config, save on change
    int heatmapMonths = qBound(1, app->config->get("stats_heatmap_months").toInt(), 24);
    ui.streaksHeatmapRangeCombo->setCurrentIndex(heatmapMonthsToComboIndex(heatmapMonths));
    connect(ui.streaksHeatmapRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, app]() {
            int idx = ui.streaksHeatmapRangeCombo->currentIndex();
            m_heatmapDays = kHeatmapMonthOptions[idx] * 31;
            app->config->set("stats_heatmap_months", QString::number(kHeatmapMonthOptions[idx]));
            // Sync charts combo
            ui.chartsHeatmapRangeCombo->blockSignals(true);
            ui.chartsHeatmapRangeCombo->setCurrentIndex(idx);
            ui.chartsHeatmapRangeCombo->blockSignals(false);
            m_heatmapCache = app->db->getDailyWatchedHistory(m_heatmapDays);
            // Update heatmap widgets in both tabs
            auto updateHeatmap = [&](QLayout* layout) {
                QLayoutItem* item = layout->itemAt(0);
                if (item) {
                    ContributionHeatmapWidget* heatmap = qobject_cast<ContributionHeatmapWidget*>(item->widget());
                    if (heatmap) {
                        heatmap->setDayRange(m_heatmapDays);
                        heatmap->setData(m_heatmapCache);
                    }
                }
            };
            updateHeatmap(ui.streakCalendarLayout);
            updateHeatmap(ui.heatmapLayout);
        });
}

void StatsDialog::addAuthorRow(QGridLayout* layout, int row, int rank, const QString& author, const QString& value, const QColor& barColor)
{
    QLabel* rankLabel = new QLabel(QString("#%1").arg(rank));
    QFont rankFont = rankLabel->font();
    if (rank <= 3) rankFont.setBold(true);
    rankLabel->setFont(rankFont);
    if (rank <= 3) {
        QColor medalColor = rank == 1 ? QColor("#FFD700") : rank == 2 ? QColor("#C0C0C0") : QColor("#CD7F32");
        rankLabel->setStyleSheet(QString("color: %1;").arg(medalColor.name()));
    }
    rankLabel->setFixedWidth(32);
    layout->addWidget(rankLabel, row, 0, Qt::AlignCenter);

    QLabel* authorLabel = new QLabel(author);
    authorLabel->setFont(rankFont);
    authorLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(authorLabel, row, 1, Qt::AlignLeft | Qt::AlignVCenter);

    if (barColor.isValid()) {
        // value is the display text (e.g. "45.3%"), extract the number for the bar fill
        QString numStr = value;
        numStr.remove('%').remove(" ★");
        int barVal = qBound(0, qRound(numStr.toDouble()), 100);

        QProgressBar* bar = new QProgressBar();
        bar->setRange(0, 100);
        bar->setValue(barVal);
        bar->setFormat(QString(" %1 ").arg(value)); // padding so text doesn't hug edges
        bar->setTextVisible(true);
        bar->setMinimumHeight(20);
        bar->setStyleSheet(QString(
            "QProgressBar { background: palette(base); border: 1px solid palette(mid);"
            " border-radius: 2px; text-align: center; }"
            "QProgressBar::chunk { background: %1; border-radius: 1px; }").arg(barColor.name()));
        layout->addWidget(bar, row, 2);
    } else {
        QLabel* valLabel = new QLabel(value);
        valLabel->setAlignment(Qt::AlignCenter);
        QFont valFont = valLabel->font();
        valFont.setBold(true);
        valLabel->setFont(valFont);
        layout->addWidget(valLabel, row, 2, Qt::AlignCenter);
    }
}

void StatsDialog::setupAuthorsTab(MainApp* app)
{
    m_app = app;

    // Update category dropdown to use config names
    QString plusName = app->config->get("plus_category_name");
    QString minusName = app->config->get("minus_category_name");
    ui.authorsCategoryCombo->setItemText(1, plusName);
    ui.authorsCategoryCombo->setItemText(2, minusName);

    connect(ui.authorsRefreshBtn, &QPushButton::clicked, this, &StatsDialog::refreshAuthors);
    refreshAuthors();
}

void StatsDialog::refreshAuthors()
{
    if (!m_app) return;
    // Map combo index back to internal category name
    static const char* catKeys[] = {"ALL", "PLUS", "MINUS"};
    int catIdx = qBound(0, ui.authorsCategoryCombo->currentIndex(), 2);
    QString category = catKeys[catIdx];

    // Resolve category display name from config
    QString catName = category;
    if (category == "PLUS")
        catName = m_app->config->get("plus_category_name");
    else if (category == "MINUS")
        catName = m_app->config->get("minus_category_name");

    // Helper: clear a layout completely, deleting all child widgets (first-call only)
    auto clearLayout = [](QLayout* layout) {
        if (!layout) return;
        QLayoutItem* item;
        while ((item = layout->takeAt(0)) != nullptr) {
            if (item->widget()) delete item->widget();
            if (item->layout()) {
                // Recurse into sub-layouts
                QLayout* sub = item->layout();
                QLayoutItem* subItem;
                while ((subItem = sub->takeAt(0)) != nullptr) {
                    if (subItem->widget()) delete subItem->widget();
                    delete subItem;
                }
            }
            delete item;
        }
    };

    // Helper: remove only data rows (row >= 1) from a grid, leaving header row intact.
    // Deleting the widget triggers Qt to auto-remove and delete the layout item — don't double-free.
    auto clearGridDataRows = [](QGridLayout* grid) {
        for (int i = grid->count() - 1; i >= 0; --i) {
            QLayoutItem* item = grid->itemAt(i);
            if (!item) continue;
            int row, col, rowSpan, colSpan;
            grid->getItemPosition(i, &row, &col, &rowSpan, &colSpan);
            if (row >= 1) {
                if (QWidget* w = item->widget()) {
                    delete w;
                } else {
                    grid->removeItem(item);
                    delete item;
                }
            }
        }
    };

    // Helper: update the first widget (title label) in a VBoxLayout
    auto updateTitle = [](QVBoxLayout* layout, const QString& text) {
        if (QLayoutItem* first = layout->itemAt(0)) {
            if (QLabel* label = qobject_cast<QLabel*>(first->widget()))
                label->setText(text);
        }
    };

    // Helper: create a centered section title label
    auto makeTitle = [](const QString& text) {
        QLabel* label = new QLabel(text);
        QFont f = label->font();
        f.setPointSize(12);
        f.setBold(true);
        label->setFont(f);
        label->setAlignment(Qt::AlignCenter);
        return label;
    };

    // --- Top Authors by Views ---
    {
        QVBoxLayout* existing = ui.topAuthorsLayout;
        QString titleText = QString("Top %1 Authors by Views").arg(catName);
        QGridLayout* grid;

        if (!m_topAuthorsGrid) {
            clearLayout(existing);
            existing->addWidget(makeTitle(titleText));
            grid = new QGridLayout();
            QLabel* headerRank = new QLabel("#");
            QLabel* headerAuthor = new QLabel("Author");
            QLabel* headerViews = new QLabel("Views");
            QFont headerFont = headerRank->font();
            headerFont.setBold(true);
            headerRank->setFont(headerFont); headerAuthor->setFont(headerFont); headerViews->setFont(headerFont);
            grid->addWidget(headerRank, 0, 0, Qt::AlignCenter);
            grid->addWidget(headerAuthor, 0, 1, Qt::AlignLeft);
            grid->addWidget(headerViews, 0, 2, Qt::AlignCenter);
            grid->setColumnStretch(0, 0);
            grid->setColumnStretch(1, 1);
            grid->setColumnStretch(2, 0);
            existing->addLayout(grid);
            m_topAuthorsGrid = grid;
        } else {
            updateTitle(existing, titleText);
            grid = m_topAuthorsGrid;
            clearGridDataRows(grid);
        }

        auto topAuthors = m_app->db->getTopAuthors(10, category);
        int row = 1;
        for (const auto& pair : topAuthors) {
            addAuthorRow(grid, row, row, pair.first, QString::number(pair.second));
            row++;
        }
        if (topAuthors.isEmpty())
            grid->addWidget(new QLabel("No data available"), 1, 0, 1, 3, Qt::AlignCenter);
    }

    // --- Top Authors by Rating ---
    {
        QVBoxLayout* existing = ui.topRatedAuthorsLayout;
        QString titleText = QString("Top %1 Authors by Rating").arg(catName);
        QGridLayout* grid;

        if (!m_topRatedGrid) {
            clearLayout(existing);
            existing->addWidget(makeTitle(titleText));
            grid = new QGridLayout();
            QLabel* headerRank = new QLabel("#");
            QLabel* headerAuthor = new QLabel("Author");
            QLabel* headerRating = new QLabel("Avg Rating");
            QFont headerFont = headerRank->font();
            headerFont.setBold(true);
            headerRank->setFont(headerFont); headerAuthor->setFont(headerFont); headerRating->setFont(headerFont);
            grid->addWidget(headerRank, 0, 0, Qt::AlignCenter);
            grid->addWidget(headerAuthor, 0, 1, Qt::AlignLeft);
            grid->addWidget(headerRating, 0, 2, Qt::AlignCenter);
            grid->setColumnStretch(0, 0);
            grid->setColumnStretch(1, 1);
            grid->setColumnStretch(2, 0);
            existing->addLayout(grid);
            m_topRatedGrid = grid;
        } else {
            updateTitle(existing, titleText);
            grid = m_topRatedGrid;
            clearGridDataRows(grid);
        }

        auto topRated = m_app->db->getTopAuthorsByRating(10, category);
        int row = 1;
        for (const auto& pair : topRated) {
            addAuthorRow(grid, row, row, pair.first, QString::number(pair.second, 'f', 2));
            row++;
        }
        if (topRated.isEmpty())
            grid->addWidget(new QLabel("No data available (min 3 rated videos required)"), 1, 0, 1, 3, Qt::AlignCenter);
    }

    // --- Author Completion Rates (scrollable, uncompleted first) ---
    {
        QVBoxLayout* existing = ui.completionLayout;
        QString titleText = QString("%1 Author Completion Rates").arg(catName);
        QGridLayout* grid;

        if (!m_completionGrid) {
            clearLayout(existing);
            existing->addWidget(makeTitle(titleText));

            QScrollArea* scroll = new QScrollArea();
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setMaximumHeight(320);

            QWidget* scrollContent = new QWidget();
            grid = new QGridLayout(scrollContent);
            grid->setContentsMargins(0, 0, 0, 0);

            QLabel* headerRank = new QLabel("#");
            QLabel* headerAuthor = new QLabel("Author");
            QLabel* headerCompletion = new QLabel("Completion %");
            QFont headerFont = headerRank->font();
            headerFont.setBold(true);
            headerRank->setFont(headerFont); headerAuthor->setFont(headerFont); headerCompletion->setFont(headerFont);
            grid->addWidget(headerRank, 0, 0, Qt::AlignCenter);
            grid->addWidget(headerAuthor, 0, 1, Qt::AlignLeft);
            grid->addWidget(headerCompletion, 0, 2, Qt::AlignCenter);
            grid->setColumnStretch(0, 0);
            grid->setColumnStretch(1, 1);
            grid->setColumnStretch(2, 0);

            scroll->setWidget(scrollContent);
            existing->addWidget(scroll);
            m_completionGrid = grid;
        } else {
            updateTitle(existing, titleText);
            grid = m_completionGrid;
            clearGridDataRows(grid);
        }

        auto completion = m_app->db->getAuthorCompletion(category);
        int row = 1;
        for (const auto& pair : completion) {
            QColor barColor = pair.second >= 75.0 ? QColor("#4CAF50") :
                              pair.second >= 50.0 ? QColor("#FF9800") : QColor("#F44336");
            addAuthorRow(grid, row, row, pair.first,
                         QString::number(pair.second, 'f', 1) + "%", barColor);
            row++;
        }
        if (completion.isEmpty())
            grid->addWidget(new QLabel("No data available (min 3 videos required)"), 1, 0, 1, 3, Qt::AlignCenter);
    }

    // --- Hidden Gems (scrollable) ---
    {
        QVBoxLayout* existing = ui.hiddenGemsLayout;
        QString titleText = QString("%1 Hidden Gems — Top Rated & Unwatched").arg(catName);
        QGridLayout* grid;

        if (!m_hiddenGemsGrid) {
            clearLayout(existing);
            existing->addWidget(makeTitle(titleText));

            QScrollArea* scroll = new QScrollArea();
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setMaximumHeight(320);

            QWidget* scrollContent = new QWidget();
            grid = new QGridLayout(scrollContent);
            grid->setContentsMargins(0, 0, 0, 0);

            QLabel* headerAuthor = new QLabel("Author");
            QLabel* headerCount = new QLabel("Unwatched");
            QLabel* headerRating = new QLabel("Avg Rating");
            QFont headerFont = headerAuthor->font();
            headerFont.setBold(true);
            headerAuthor->setFont(headerFont); headerCount->setFont(headerFont); headerRating->setFont(headerFont);
            grid->addWidget(headerAuthor, 0, 0, Qt::AlignLeft);
            grid->addWidget(headerCount, 0, 1, Qt::AlignCenter);
            grid->addWidget(headerRating, 0, 2, Qt::AlignCenter);
            grid->setColumnStretch(0, 1);
            grid->setColumnStretch(1, 0);
            grid->setColumnStretch(2, 0);

            scroll->setWidget(scrollContent);
            existing->addWidget(scroll);
            m_hiddenGemsGrid = grid;
        } else {
            updateTitle(existing, titleText);
            grid = m_hiddenGemsGrid;
            clearGridDataRows(grid);
        }

        auto hiddenGems = m_app->db->getTopRatedUnwatched(20, category);
        int row = 1;
        for (const auto& gem : hiddenGems) {
            QLabel* authorLabel = new QLabel(gem.author);
            authorLabel->setTextFormat(Qt::PlainText);
            grid->addWidget(authorLabel, row, 0, Qt::AlignLeft | Qt::AlignVCenter);

            QLabel* countLabel = new QLabel(QString::number(gem.count));
            countLabel->setAlignment(Qt::AlignCenter);
            grid->addWidget(countLabel, row, 1, Qt::AlignCenter);

            QLabel* ratingLabel = new QLabel(QString::number(gem.avgRating, 'f', 1) + " ★");
            ratingLabel->setAlignment(Qt::AlignCenter);
            QFont ratingFont = ratingLabel->font();
            ratingFont.setBold(true);
            ratingLabel->setFont(ratingFont);
            grid->addWidget(ratingLabel, row, 2, Qt::AlignCenter);
            row++;
        }
        if (hiddenGems.isEmpty()) {
            QLabel* empty = new QLabel("No hidden gems found — all rated videos have been watched!");
            empty->setStyleSheet("color: palette(highlight);");
            QFont emptyFont = empty->font();
            emptyFont.setBold(true);
            empty->setFont(emptyFont);
            grid->addWidget(empty, 1, 0, 1, 3, Qt::AlignCenter);
        }
    }
}

StatsDialog::~StatsDialog()
{
}

void StatsDialog::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange)
        applyChartsTheme();
    QDialog::changeEvent(event);
}

void StatsDialog::setupChartsTab(MainApp* app)
{
    m_app = app;
    setupContributionHeatmap(ui.heatmapLayout);
    ui.dateRangeCombo->setCurrentIndex(2);  // "Last 30 days"
    createDailyBars(ui.dailyBarsLayout);
    createHourlyHistogram(ui.hourlyLayout);
    createDayOfWeek(ui.dayOfWeekLayout);

    connect(ui.dateRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StatsDialog::refreshCharts);
    connect(ui.metricCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StatsDialog::refreshCharts);
    connect(ui.refreshChartsButton, &QPushButton::clicked,
            this, &StatsDialog::refreshCharts);
    connect(qApp, &QApplication::paletteChanged,
            this, &StatsDialog::applyChartsTheme);
}

bool StatsDialog::showCounts() const
{
    return ui.metricCombo->currentIndex() == 1;
}

int StatsDialog::selectedDays() const
{
    switch (ui.dateRangeCombo->currentIndex()) {
        case 0: return 1;    // Today
        case 1: return 7;
        case 2: return 30;
        case 3: return 90;
        case 4: return 180;
        case 5: return 365;
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
    // Reuse cached heatmap data if setupStreaksTab already fetched it
    if (m_heatmapCache.isEmpty())
        m_heatmapCache = m_app->db->getDailyWatchedHistory(m_heatmapDays);
    auto* heatmap = new ContributionHeatmapWidget();
    heatmap->setDayRange(m_heatmapDays);
    heatmap->setData(m_heatmapCache);
    layout->addWidget(heatmap);
    layout->setAlignment(heatmap, Qt::AlignCenter);

    // Heatmap range combo — load default from config, save on change
    int heatmapMonths = qBound(1, m_app->config->get("stats_heatmap_months").toInt(), 24);
    ui.chartsHeatmapRangeCombo->setCurrentIndex(heatmapMonthsToComboIndex(heatmapMonths));
    connect(ui.chartsHeatmapRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, layout]() {
            int idx = ui.chartsHeatmapRangeCombo->currentIndex();
            m_heatmapDays = kHeatmapMonthOptions[idx] * 31;
            m_app->config->set("stats_heatmap_months", QString::number(kHeatmapMonthOptions[idx]));
            // Sync streaks combo
            ui.streaksHeatmapRangeCombo->blockSignals(true);
            ui.streaksHeatmapRangeCombo->setCurrentIndex(idx);
            ui.streaksHeatmapRangeCombo->blockSignals(false);
            m_heatmapCache = m_app->db->getDailyWatchedHistory(m_heatmapDays);
            // Update heatmap widgets in both tabs
            QLayoutItem* item = layout->itemAt(0);
            if (item) {
                ContributionHeatmapWidget* hmap = qobject_cast<ContributionHeatmapWidget*>(item->widget());
                if (hmap) {
                    hmap->setDayRange(m_heatmapDays);
                    hmap->setData(m_heatmapCache);
                }
            }
            QLayoutItem* streakItem = ui.streakCalendarLayout->itemAt(0);
            if (streakItem) {
                ContributionHeatmapWidget* streakHmap = qobject_cast<ContributionHeatmapWidget*>(streakItem->widget());
                if (streakHmap) {
                    streakHmap->setDayRange(m_heatmapDays);
                    streakHmap->setData(m_heatmapCache);
                }
            }
        });
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

    // Pick date format based on range
    QString dateFormat = days <= 14 ? QStringLiteral("dd")
                       : QStringLiteral("MM-dd");

    if (showCounts()) {
        auto data = m_app->db->getDailyWatchedCount(days);
        for (const auto& pair : data) {
            *set << pair.second;
            if (pair.second > maxVal) maxVal = pair.second;
            categories << pair.first.toString(dateFormat);
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
            categories << pair.first.toString(dateFormat);
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
    m_dailyAxisX->setLabelsAngle(days > 14 ? -90 : 0);

    // Give each bar enough width so all date labels are readable; horizontal scrollbar appears when needed
    m_dailyChartView->setMinimumWidth(categories.size() * 24);

    m_dailyAxisY->setRange(0, maxVal * 1.15);
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

    QColor barColor = palette().color(QPalette::Highlight);
    if (m_dailyBarSet) {
        m_dailyBarSet->setColor(barColor);
    }
    QColor barColorAlpha = barColor;
    barColorAlpha.setAlpha(180);
    if (m_hourlyBarSet) {
        m_hourlyBarSet->setColor(barColorAlpha);
    }
    if (m_dowBarSet) {
        m_dowBarSet->setColor(barColorAlpha);
    }
}


void StatsDialog::setupRecordsTab(MainApp* app)
{
    auto addSection = [](QGridLayout* layout, const QString& title) -> int {
        QLabel* label = new QLabel(title);
        QFont f = label->font();
        f.setPointSize(14);
        f.setBold(true);
        label->setFont(f);
        layout->addWidget(label, 0, 0, 1, 2, Qt::AlignLeft);
        return 1; // start data at row 1
    };

    // Most videos in a day (all-time, including today)
    {
        QGridLayout* grid = ui.mostVideosGridLayout;
        int row = addSection(grid, "Most Videos in a Day");
        auto rec = app->db->getMostVideosInDay();
        if (rec.date.isValid() && rec.count > 0) {
            addStatToGrid(grid, row++, "Record:", QString("%1 videos").arg(rec.count));
            addStatToGrid(grid, row++, "Date:", rec.date.toString("d MMMM yyyy"));
        } else {
            addStatToGrid(grid, row++, "Status:", "N/A");
        }
    }

    // Most time in a day
    {
        QGridLayout* grid = ui.mostTimeGridLayout;
        int row = addSection(grid, "Most Watch Time in a Day");
        auto rec = app->db->getMostTimeInDay();
        if (rec.date.isValid() && rec.seconds > 0) {
            addStatToGrid(grid, row++, "Record:", QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(rec.seconds))));
            addStatToGrid(grid, row++, "Date:", rec.date.toString("d MMMM yyyy"));
        } else {
            addStatToGrid(grid, row++, "Status:", "N/A");
        }
    }

    // Longest session
    {
        QGridLayout* grid = ui.longestSessionGridLayout;
        int row = addSection(grid, "Longest Session");
        auto rec = app->db->getLongestSession();
        if (rec.sessionTime > 0) {
            addStatToGrid(grid, row++, "Duration:", QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(rec.sessionTime))));
            addStatToGrid(grid, row++, "Date:", rec.date);
            addStatToGrid(grid, row++, "Videos watched:", QString::number(rec.videoCount));
        } else {
            addStatToGrid(grid, row++, "Status:", "N/A");
        }
    }

    // Most viewed video
    {
        QGridLayout* grid = ui.mostViewedGridLayout;
        int row = addSection(grid, "Most Viewed Video");
        auto rec = app->db->getMostViewedVideo();
        if (rec.views > 0) {
            addStatToGrid(grid, row++, "Video:", rec.name);
            addStatToGrid(grid, row++, "Author:", rec.author);
            addStatToGrid(grid, row++, "Views:", QString::number(rec.views));
        } else {
            addStatToGrid(grid, row++, "Status:", "N/A");
        }
    }

    // Most time spent on a video
    {
        QGridLayout* grid = ui.mostTimeVideoGridLayout;
        int row = addSection(grid, "Most Time on a Single Video");
        auto rec = app->db->getMostTimeSpentVideo();
        if (rec.totalTime > 0) {
            addStatToGrid(grid, row++, "Video:", rec.name);
            addStatToGrid(grid, row++, "Author:", rec.author);
            addStatToGrid(grid, row++, "Total Time:", QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(rec.totalTime))));
        } else {
            addStatToGrid(grid, row++, "Status:", "N/A");
        }
    }

    // Most diverse day
    {
        QGridLayout* grid = ui.mostDiverseGridLayout;
        int row = addSection(grid, "Most Diverse Day");
        auto rec = app->db->getMostDiverseDay();
        if (rec.date.isValid() && rec.authorCount > 0) {
            addStatToGrid(grid, row++, "Authors:", QString::number(rec.authorCount));
            addStatToGrid(grid, row++, "Date:", rec.date.toString("d MMMM yyyy"));
        } else {
            addStatToGrid(grid, row++, "Status:", "N/A");
        }
    }

}

void StatsDialog::setupLibraryTab(MainApp* app)
{
    // Shared values computed once, reused by Completion and Rating Coverage sections
    int plusTotal = app->db->getVideoCount("PLUS");
    int minusTotal = app->db->getVideoCount("MINUS");

    // --- Completion section ---
    {
        QGridLayout* grid = ui.completionGridLayout;
        QLabel* title = new QLabel("Library Completion");
        QFont f = title->font();
        f.setPointSize(14);
        f.setBold(true);
        title->setFont(f);
        grid->addWidget(title, 0, 0, 1, 2, Qt::AlignLeft);
        int row = 1;

        int totalVids = plusTotal + minusTotal;
        int plusWatched = app->db->getUniqueVideosWatched("PLUS");
        int minusWatched = app->db->getUniqueVideosWatched("MINUS");
        int totalWatched = plusWatched + minusWatched;

        if (totalVids > 0) {
            double overallPct = (double)totalWatched / totalVids * 100.0;
            QProgressBar* bar = new QProgressBar();
            bar->setRange(0, 100);
            bar->setValue(static_cast<int>(overallPct));
            bar->setFormat(QString("Overall: %1 / %2 (%3%)")
                .arg(totalWatched).arg(totalVids).arg(overallPct, 0, 'f', 1));
            bar->setTextVisible(true);
            bar->setMinimumHeight(22);
            grid->addWidget(new QLabel("Overall:"), row, 0);
            grid->addWidget(bar, row++, 1);
        }

        auto addCatBar = [&](const QString& cat, int watched, int total) {
            if (total > 0) {
                double pct = (double)watched / total * 100.0;
                QProgressBar* bar = new QProgressBar();
                bar->setRange(0, 100);
                bar->setValue(static_cast<int>(pct));
                bar->setFormat(QString("%1 / %2 (%3%)").arg(watched).arg(total).arg(pct, 0, 'f', 1));
                bar->setTextVisible(true);
                bar->setMinimumHeight(22);
                grid->addWidget(new QLabel(cat + ":"), row, 0);
                grid->addWidget(bar, row++, 1);
            }
        };
        addCatBar(app->config->get("plus_category_name"), plusWatched, plusTotal);
        addCatBar(app->config->get("minus_category_name"), minusWatched, minusTotal);
    }

    // --- Rating coverage ---
    {
        QGridLayout* grid = ui.coverageGridLayout;
        QLabel* title = new QLabel("Rating Coverage");
        QFont f = title->font();
        f.setPointSize(14);
        f.setBold(true);
        title->setFont(f);
        grid->addWidget(title, 0, 0, 1, 2, Qt::AlignLeft);
        int row = 1;

        // Reuse plusTotal/minusTotal already computed above in the Completion section
        int rated = app->db->getRatedVideoCount();
        int totalVids = plusTotal + minusTotal;

        if (totalVids > 0) {
            double pct = (double)rated / totalVids * 100.0;
            QProgressBar* bar = new QProgressBar();
            bar->setRange(0, 100);
            bar->setValue(static_cast<int>(pct));
            bar->setFormat(QString("%1 / %2 (%3%)").arg(rated).arg(totalVids).arg(pct, 0, 'f', 1));
            bar->setTextVisible(true);
            bar->setMinimumHeight(22);
            grid->addWidget(new QLabel("Rated:"), row, 0);
            grid->addWidget(bar, row++, 1);
        }
        addStatToGrid(grid, row++, "Unrated:", QString::number(app->db->getUnratedVideoCount()));
    }

    // --- Diversity ---
    {
        QGridLayout* grid = ui.diversityGridLayout;
        QLabel* title = new QLabel("Content Diversity");
        QFont f = title->font();
        f.setPointSize(14);
        f.setBold(true);
        title->setFont(f);
        grid->addWidget(title, 0, 0, 1, 2, Qt::AlignLeft);
        int row = 1;

        addStatToGrid(grid, row++, "Distinct Authors:", QString::number(app->db->getDistinctAuthorCount("ALL")));
        addStatToGrid(grid, row++, "Distinct Types:", QString::number(app->db->getDistinctTypeCount("ALL")));
        addStatToGrid(grid, row++, "Distinct Tags:", QString::number(app->db->getDistinctTagCount()));
    }

    // --- Additions ---
    {
        QGridLayout* grid = ui.additionsGridLayout;
        QLabel* title = new QLabel("New Additions");
        QFont f = title->font();
        f.setPointSize(14);
        f.setBold(true);
        title->setFont(f);
        grid->addWidget(title, 0, 0, 1, 2, Qt::AlignLeft);
        int row = 1;

        addStatToGrid(grid, row++, "Added This Week (7d):", QString::number(app->db->getVideosAddedSince(7)));
        addStatToGrid(grid, row++, "Added This Month (30d):", QString::number(app->db->getVideosAddedSince(30)));
    }

    // --- Most Neglected ---
    {
        QVBoxLayout* layout = ui.neglectedLayout;

        auto makeTitle = [](const QString& text) {
            QLabel* label = new QLabel(text);
            QFont f = label->font();
            f.setPointSize(12);
            f.setBold(true);
            label->setFont(f);
            label->setAlignment(Qt::AlignCenter);
            return label;
        };

        layout->addWidget(makeTitle("Most Neglected — Oldest Unwatched"));

        QString cat = "ALL"; // Show all categories
        auto oldest = app->db->getMostNeglectedOldest(10, cat);
        QGridLayout* oldestGrid = new QGridLayout();
        oldestGrid->addWidget(new QLabel("#"), 0, 0, Qt::AlignCenter);
        oldestGrid->addWidget(new QLabel("Video"), 0, 1, Qt::AlignLeft);
        oldestGrid->addWidget(new QLabel("Author"), 0, 2, Qt::AlignLeft);
        oldestGrid->addWidget(new QLabel("Added"), 0, 3, Qt::AlignCenter);
        oldestGrid->setColumnStretch(0, 0);
        oldestGrid->setColumnStretch(1, 1);
        oldestGrid->setColumnStretch(2, 1);
        oldestGrid->setColumnStretch(3, 0);
        int row = 1;
        for (const auto& v : oldest) {
            oldestGrid->addWidget(new QLabel(QString("#%1").arg(row)), row, 0, Qt::AlignCenter);
            oldestGrid->addWidget(new QLabel(v.name), row, 1, Qt::AlignLeft);
            oldestGrid->addWidget(new QLabel(v.author), row, 2, Qt::AlignLeft);
            oldestGrid->addWidget(new QLabel(v.dateCreated.toString("d MMM yyyy")), row, 3, Qt::AlignCenter);
            row++;
        }
        if (oldest.isEmpty()) {
            oldestGrid->addWidget(new QLabel("No neglected videos found"), 1, 0, 1, 4, Qt::AlignCenter);
        }
        layout->addLayout(oldestGrid);
    }
}

void StatsDialog::setupTagsTab(MainApp* app)
{
    m_app = app;
    ui.tagsCategoryCombo->setItemText(1, app->config->get("plus_category_name"));
    ui.tagsCategoryCombo->setItemText(2, app->config->get("minus_category_name"));
    connect(ui.tagsRefreshBtn, &QPushButton::clicked, this, &StatsDialog::refreshTags);
    refreshTags();
}

void StatsDialog::refreshTags()
{
    if (!m_app) return;

    QString category = "ALL";
    int comboIdx = ui.tagsCategoryCombo->currentIndex();
    if (comboIdx == 1) category = "PLUS";
    else if (comboIdx == 2) category = "MINUS";

    std::function<void(QLayout*)> clearLayout = [&](QLayout* layout) {
        if (!layout) return;
        QLayoutItem* item;
        while ((item = layout->takeAt(0)) != nullptr) {
            if (item->widget()) { item->widget()->deleteLater(); }
            if (item->layout()) { clearLayout(item->layout()); }
            delete item;
        }
    };

    auto clearGridDataRows = [](QGridLayout* grid) {
        if (!grid) return;
        // Remove all widgets from row 1 onwards, keep row 0 (header)
        for (int r = grid->rowCount() - 1; r >= 1; --r) {
            for (int c = 0; c < grid->columnCount(); ++c) {
                QLayoutItem* item = grid->itemAtPosition(r, c);
                if (item && item->widget()) {
                    item->widget()->deleteLater();
                }
            }
        }
    };

    auto makeTitle = [](const QString& text) {
        QLabel* label = new QLabel(text);
        QFont f = label->font();
        f.setPointSize(12);
        f.setBold(true);
        label->setFont(f);
        label->setAlignment(Qt::AlignCenter);
        return label;
    };

    int limit = 10;

    // Top Tags by Views
    {
        QVBoxLayout* layout = ui.topTagsByViewsLayout;
        if (!m_topTagsByViewsGrid) {
            clearLayout(layout);
            layout->addWidget(makeTitle("Top Tags by Views"));
            QGridLayout* grid = new QGridLayout();
            grid->addWidget(new QLabel("#"), 0, 0, Qt::AlignCenter);
            grid->addWidget(new QLabel("Tag"), 0, 1, Qt::AlignLeft);
            grid->addWidget(new QLabel("Views"), 0, 2, Qt::AlignCenter);
            grid->setColumnStretch(0, 0);
            grid->setColumnStretch(1, 1);
            grid->setColumnStretch(2, 0);
            layout->addLayout(grid);
            m_topTagsByViewsGrid = grid;
        } else {
            clearGridDataRows(m_topTagsByViewsGrid);
        }

        auto tags = m_app->db->getTopTagsByViews(limit, category);
        int row = 1;
        for (const auto& pair : tags) {
            addAuthorRow(m_topTagsByViewsGrid, row, row, pair.first, QString::number(pair.second));
            row++;
        }
        if (tags.isEmpty())
            m_topTagsByViewsGrid->addWidget(new QLabel("No data available"), 1, 0, 1, 3, Qt::AlignCenter);
    }

    // Top Tags by Watch Time
    {
        QVBoxLayout* layout = ui.topTagsByWatchTimeLayout;
        if (!m_topTagsByWatchTimeGrid) {
            clearLayout(layout);
            layout->addWidget(makeTitle("Top Tags by Watch Time"));
            QGridLayout* grid = new QGridLayout();
            grid->addWidget(new QLabel("#"), 0, 0, Qt::AlignCenter);
            grid->addWidget(new QLabel("Tag"), 0, 1, Qt::AlignLeft);
            grid->addWidget(new QLabel("Time"), 0, 2, Qt::AlignCenter);
            grid->setColumnStretch(0, 0);
            grid->setColumnStretch(1, 1);
            grid->setColumnStretch(2, 0);
            layout->addLayout(grid);
            m_topTagsByWatchTimeGrid = grid;
        } else {
            clearGridDataRows(m_topTagsByWatchTimeGrid);
        }

        auto tags = m_app->db->getTopTagsByWatchTime(limit, category);
        int row = 1;
        for (const auto& pair : tags) {
            addAuthorRow(m_topTagsByWatchTimeGrid, row, row, pair.first,
                QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(pair.second))));
            row++;
        }
        if (tags.isEmpty())
            m_topTagsByWatchTimeGrid->addWidget(new QLabel("No data available"), 1, 0, 1, 3, Qt::AlignCenter);
    }

    // Tag Completion Rates
    {
        QVBoxLayout* layout = ui.tagCompletionLayout;
        if (!m_tagCompletionGrid) {
            clearLayout(layout);
            layout->addWidget(makeTitle("Tag Completion Rates"));
            QGridLayout* grid = new QGridLayout();
            grid->addWidget(new QLabel("#"), 0, 0, Qt::AlignCenter);
            grid->addWidget(new QLabel("Tag"), 0, 1, Qt::AlignLeft);
            grid->addWidget(new QLabel("Completion"), 0, 2, Qt::AlignCenter);
            grid->setColumnStretch(0, 0);
            grid->setColumnStretch(1, 1);
            grid->setColumnStretch(2, 0);
            layout->addLayout(grid);
            m_tagCompletionGrid = grid;
        } else {
            clearGridDataRows(m_tagCompletionGrid);
        }

        auto tags = m_app->db->getTagCompletion(category);
        int row = 1;
        for (const auto& pair : tags) {
            int barVal = static_cast<int>(pair.second);
            QColor barColor = barVal >= 75 ? QColor("#4CAF50") : barVal >= 50 ? QColor("#FF9800") : QColor("#F44336");
            addAuthorRow(m_tagCompletionGrid, row, row, pair.first,
                QString("%1%").arg(pair.second, 0, 'f', 1), barColor);
            row++;
        }
        if (tags.isEmpty())
            m_tagCompletionGrid->addWidget(new QLabel("No data available"), 1, 0, 1, 3, Qt::AlignCenter);
    }

    // Average Rating by Tag
    {
        QVBoxLayout* layout = ui.avgRatingByTagLayout;
        if (!m_avgRatingByTagGrid) {
            clearLayout(layout);
            layout->addWidget(makeTitle("Average Rating by Tag"));
            QGridLayout* grid = new QGridLayout();
            grid->addWidget(new QLabel("#"), 0, 0, Qt::AlignCenter);
            grid->addWidget(new QLabel("Tag"), 0, 1, Qt::AlignLeft);
            grid->addWidget(new QLabel("Avg Rating"), 0, 2, Qt::AlignCenter);
            grid->setColumnStretch(0, 0);
            grid->setColumnStretch(1, 1);
            grid->setColumnStretch(2, 0);
            layout->addLayout(grid);
            m_avgRatingByTagGrid = grid;
        } else {
            clearGridDataRows(m_avgRatingByTagGrid);
        }

        auto tags = m_app->db->getAverageRatingByTag(limit, category);
        int row = 1;
        for (const auto& pair : tags) {
            addAuthorRow(m_avgRatingByTagGrid, row, row, pair.first,
                QString::number(pair.second, 'f', 2));
            row++;
        }
        if (tags.isEmpty())
            m_avgRatingByTagGrid->addWidget(new QLabel("No data available"), 1, 0, 1, 3, Qt::AlignCenter);
    }

    // Untapped Tags
    {
        QVBoxLayout* layout = ui.untappedTagsLayout;
        // Clear and rebuild each time (simple text list)
        QLayoutItem* item;
        while ((item = layout->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        layout->addWidget(makeTitle("Untapped Tags (No Watched Videos)"));
        QStringList untapped = m_app->db->getUntappedTags(category);
        QString text = untapped.isEmpty() ? "All tags have been explored!" : untapped.join(", ");
        QLabel* label = new QLabel(text);
        label->setWordWrap(true);
        label->setStyleSheet("padding: 4px;");
        layout->addWidget(label);
    }
}

void StatsDialog::setupSessionsTab(MainApp* app)
{
    // --- Summary stats ---
    {
        QGridLayout* grid = ui.sessionSummaryGridLayout;
        QLabel* title = new QLabel("Session Summary");
        QFont f = title->font();
        f.setPointSize(14);
        f.setBold(true);
        title->setFont(f);
        grid->addWidget(title, 0, 0, 1, 2, Qt::AlignLeft);
        int row = 1;

        // Reuse cached averages when available to avoid redundant DB queries
        if (m_cachedAvgSessionTime < 0.0)
            m_cachedAvgSessionTime = app->db->getAverageSessionTime();
        addStatToGrid(grid, row++, "Avg Session Length:",
            QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(m_cachedAvgSessionTime))));

        double avgWatchedTime = app->db->getAverageWatchedTime();
        addStatToGrid(grid, row++, "Avg Watched Time:",
            QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(avgWatchedTime))));

        double avgSessionsPerDay = app->db->getAverageSessionsPerDay();
        addStatToGrid(grid, row++, "Avg Sessions Per Day:", QString::number(avgSessionsPerDay, 'f', 1));

        double avgWatchTimePerDay = app->db->getAverageWatchTimePerDay();
        addStatToGrid(grid, row++, "Avg Watch Time Per Day:",
            QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(avgWatchTimePerDay))));

        if (m_cachedAvgSessionTimePerDay < 0.0)
            m_cachedAvgSessionTimePerDay = app->db->getAverageSessionTimePerDay();
        addStatToGrid(grid, row++, "Avg Session Time Per Day:",
            QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(m_cachedAvgSessionTimePerDay))));

        if (m_cachedAvgCompletedPerDay < 0.0)
            m_cachedAvgCompletedPerDay = app->db->getAverageCompletedPerDay();
        addStatToGrid(grid, row++, "Avg Completed Per Day:", QString::number(m_cachedAvgCompletedPerDay, 'f', 1));
    }

    // --- Recent sessions table ---
    QTableWidget* table = ui.sessionsTableWidget;
    table->setItemDelegate(new AutoToolTipDelegate(table));
    auto sessions = app->db->getRecentSessions(50);
    table->setRowCount(sessions.size());

    for (int i = 0; i < sessions.size(); ++i) {
        const auto& s = sessions[i];

        QTableWidgetItem* dateItem = new QTableWidgetItem(s.date.toString("yyyy-MM-dd"));
        dateItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(i, 0, dateItem);

        table->setItem(i, 1, new QTableWidgetItem(s.videoName));
        table->setItem(i, 2, new QTableWidgetItem(s.author));

        QTableWidgetItem* watchedItem = new QTableWidgetItem(
            QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(s.watchedTime))));
        watchedItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(i, 3, watchedItem);

        QTableWidgetItem* sessionItem = new QTableWidgetItem(
            QString::fromStdString(utils::convert_time_to_text(static_cast<unsigned long>(s.sessionTime))));
        sessionItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(i, 4, sessionItem);

        QTableWidgetItem* completedItem = new QTableWidgetItem(s.completed ? "Yes" : "No");
        completedItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(i, 5, completedItem);
    }

    table->resizeColumnsToContents();
    // Ensure reasonable minimum column widths
    if (table->columnWidth(1) < 150) table->setColumnWidth(1, 150);
    if (table->columnWidth(2) < 100) table->setColumnWidth(2, 100);
    // Video column stretches to fill remaining width; others take minimal space
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    // Let the table expand vertically to fill the remaining space
    QSpacerItem* spacer = ui.sessionsMainLayout->itemAt(ui.sessionsMainLayout->count() - 1)->spacerItem();
    if (spacer) spacer->changeSize(0, 0, QSizePolicy::Minimum, QSizePolicy::Minimum);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
