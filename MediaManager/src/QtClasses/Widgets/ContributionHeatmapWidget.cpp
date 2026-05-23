#include "stdafx.h"
#include "ContributionHeatmapWidget.h"
#include <QPainter>
#include <QToolTip>
#include <QMouseEvent>
#include <QLocale>

ContributionHeatmapWidget::ContributionHeatmapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void ContributionHeatmapWidget::setData(const QVector<QPair<QDate, double>>& data)
{
    m_dayMap.clear();
    for (const auto& pair : data) {
        m_dayMap[pair.first] = pair.second;
    }

    m_today = QDate::currentDate();
    m_start = m_today.addDays(-185);
    int startDow = m_start.dayOfWeek();
    m_start = m_start.addDays(-((startDow + 6) % 7));
    m_totalDays = m_start.daysTo(m_today) + 1;
    m_totalWeeks = (m_totalDays + 6) / 7;

    m_maxTime = 1.0;
    for (const auto& pair : data) {
        if (pair.second > m_maxTime) m_maxTime = pair.second;
    }

    int w = labelWidth + m_totalWeeks * (cellSize + cellGap);
    int h = headerHeight + 7 * (cellSize + cellGap) + 4;
    setFixedSize(w, h);
    update();
}

void ContributionHeatmapWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor highlight = palette().color(QPalette::Highlight);
    QColor labelColor = palette().color(QPalette::Text);

    QColor emptyCell = labelColor;
    emptyCell.setAlpha(20);

    QFont labelFont = painter.font();
    labelFont.setPointSize(9);
    painter.setFont(labelFont);

    const char* dayNames[4] = {"Mon", "Wed", "Fri", nullptr};
    for (int r = 0; r < 7; r++) {
        int labelIdx = (r == 0) ? 0 : (r == 2) ? 1 : (r == 4) ? 2 : -1;
        if (labelIdx >= 0 && dayNames[labelIdx]) {
            QRect labelRect(2, headerHeight + r * (cellSize + cellGap), labelWidth - 4, cellSize);
            QColor muted = labelColor;
            muted.setAlpha(128);
            painter.setPen(muted);
            painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, dayNames[labelIdx]);
        }
    }

    QDate prevMonth;
    int prevMonthCol = -1;
    for (int w = 0; w < m_totalWeeks; w++) {
        QDate weekDate = m_start.addDays(w * 7 + 3);
        if (weekDate.month() != prevMonth.month()) {
            prevMonth = QDate(weekDate.year(), weekDate.month(), 1);
            if (w != prevMonthCol || prevMonthCol == -1) {
                QRect monthRect(labelWidth + w * (cellSize + cellGap), 2, 60, headerHeight - 2);
                QColor muted = labelColor;
                muted.setAlpha(128);
                painter.setPen(muted);
                painter.drawText(monthRect, Qt::AlignLeft, QLocale::system().monthName(weekDate.month(), QLocale::ShortFormat));
                prevMonthCol = w;
            }
        }
    }

    for (int d = 0; d < m_totalDays; d++) {
        QDate date = m_start.addDays(d);
        int dow = date.dayOfWeek();
        int row = (dow + 6) % 7;
        int col = d / 7;

        int x = labelWidth + col * (cellSize + cellGap);
        int y = headerHeight + row * (cellSize + cellGap);
        QRect cellRect(x, y, cellSize, cellSize);

        double time = m_dayMap.value(date, 0.0);

        QColor color;
        if (time <= 0 || m_maxTime <= 0) {
            color = emptyCell;
        } else {
            double frac = time / m_maxTime;
            int alpha = static_cast<int>(frac * 255);
            if (alpha < 25) alpha = 25;
            if (alpha > 255) alpha = 255;
            color = highlight;
            color.setAlpha(alpha);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(cellRect, 2, 2);
    }
}
