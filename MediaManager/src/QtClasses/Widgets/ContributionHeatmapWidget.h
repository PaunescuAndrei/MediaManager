#pragma once
#include <QWidget>
#include <QMap>
#include <QDate>
#include <QVector>

class ContributionHeatmapWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ContributionHeatmapWidget(QWidget* parent = nullptr);

    void setData(const QVector<QPair<QDate, double>>& data);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QMap<QDate, double> m_dayMap;
    QDate m_today;
    QDate m_start;
    int m_totalDays = 0;
    int m_totalWeeks = 0;
    double m_maxTime = 1.0;

    static constexpr int cellSize = 14;
    static constexpr int cellGap = 2;
    static constexpr int labelWidth = 30;
    static constexpr int headerHeight = 20;
};
