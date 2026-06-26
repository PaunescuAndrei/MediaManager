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

void StatsDialog::setupAchievements(MainApp* app)
{
    int totalWatchDays = app->db->getTotalWatchDays();
    QDateTime firstWatch = app->db->getFirstWatchDate();
    QString firstWatchStr = firstWatch.isValid() ? firstWatch.toString("MMMM d, yyyy") : "N/A";
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

    int dailyVideoGoal = app->config->get("daily_video_goal").toInt();
    int videosToday = app->db->getVideosWatchedToday("PLUS") + app->db->getVideosWatchedToday("MINUS");
    QProgressBar* videoBar = new QProgressBar();
    videoBar->setRange(0, std::max(dailyVideoGoal, 1));
    videoBar->setValue(std::min(videosToday, dailyVideoGoal));
    videoBar->setFormat(QString("%1 / %2").arg(videosToday).arg(dailyVideoGoal));
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
    QVBoxLayout* overviewLayout = qobject_cast<QVBoxLayout*>(ui.overviewTab->layout());
    if (overviewLayout) {
        QFrame* line = new QFrame();
        line->setFrameShape(QFrame::HLine);
        overviewLayout->insertWidget(overviewLayout->count() - 1, line);
        overviewLayout->insertWidget(overviewLayout->count() - 1, progressTitle);
        overviewLayout->insertWidget(overviewLayout->count() - 1, goalsContainer);
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
        return d.toString(QStringLiteral("MMM d, yyyy"));
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
            longestIntervalStr = streak.longestStreakStartDate.toString("MMM d, yyyy");
        else
            longestIntervalStr = QString("%1 – %2")
                .arg(streak.longestStreakStartDate.toString("MMM d, yyyy"))
                .arg(streak.longestStreakEndDate.toString("MMM d, yyyy"));
    } else {
        longestIntervalStr = QStringLiteral("—");
    }
    QLabel* longestIntervalLabel = new QLabel(QString("Longest Streak: %1").arg(longestIntervalStr));
    longestIntervalLabel->setAlignment(Qt::AlignCenter);
    longestIntervalLabel->setFont(statFont);
    streakLayout->addWidget(longestIntervalLabel, 4, 1, Qt::AlignCenter);

    // Streak calendar heatmap (last 6 months) — cache for reuse by setupContributionHeatmap
    m_heatmapCache = app->db->getDailyWatchedHistory(186);
    ContributionHeatmapWidget* streakCalendar = new ContributionHeatmapWidget();
    streakCalendar->setData(m_heatmapCache);
    ui.streakCalendarLayout->addWidget(streakCalendar);
    ui.streakCalendarLayout->setAlignment(streakCalendar, Qt::AlignCenter);

    // Watching Since under the heatmap
    QString firstStr = firstWatch.isValid() ? firstWatch.toString("MMMM d, yyyy") : "N/A";
    QLabel* firstWatchLabel = new QLabel(QString("Watching Since: %1").arg(firstStr));
    firstWatchLabel->setAlignment(Qt::AlignCenter);
    firstWatchLabel->setFont(statFont);
    ui.streakCalendarLayout->addWidget(firstWatchLabel);

    // Daily Goals — reuse cached values from setupAchievements when available
    QGridLayout* goalsGrid = ui.goalsGridLayout;
    int row = 0;

    int dailyVideoGoal = m_cachedDailyVideoGoal >= 0
        ? m_cachedDailyVideoGoal
        : app->config->get("daily_video_goal").toInt();
    int videosToday = m_cachedVideosToday >= 0
        ? m_cachedVideosToday
        : app->db->getVideosWatchedToday("PLUS") + app->db->getVideosWatchedToday("MINUS");

    QLabel* dailyVideoGoalLabel = new QLabel(QString("Videos: %1 / %2").arg(videosToday).arg(dailyVideoGoal));
    goalsGrid->addWidget(dailyVideoGoalLabel, row, 0);

    QProgressBar* dailyVideoGoalBar = new QProgressBar();
    dailyVideoGoalBar->setRange(0, std::max(dailyVideoGoal, 1));
    dailyVideoGoalBar->setValue(std::min(videosToday, dailyVideoGoal));
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

    // Helper: remove only data rows (row >= 1) from a grid, leaving header row intact
    auto clearGridDataRows = [](QGridLayout* grid) {
        for (int i = grid->count() - 1; i >= 0; --i) {
            QLayoutItem* item = grid->itemAt(i);
            if (!item) continue;
            int row, col, rowSpan, colSpan;
            grid->getItemPosition(i, &row, &col, &rowSpan, &colSpan);
            if (row >= 1) {
                if (item->widget()) delete item->widget();
                grid->removeItem(item);
                delete item;
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
    if (event->type() == QEvent::PaletteChange) {
        applyChartsTheme();
    }
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
        m_heatmapCache = m_app->db->getDailyWatchedHistory(186);
    auto* heatmap = new ContributionHeatmapWidget();
    heatmap->setData(m_heatmapCache);
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
