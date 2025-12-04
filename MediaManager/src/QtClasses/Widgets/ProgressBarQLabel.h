#pragma once
#include "customQLabel.h"
#include <QEvent>
#include <QBrush>
#include <QPen>
#include <QStyleOptionProgressBar>

class MainWindow;

class ProgressBarQLabel :public customQLabel
{
    Q_OBJECT

public:
    int minimum_ = 0;
    int maximum_ = 100;
    int progress_ = 0;
    double w;
    bool scaled_mode;
    bool highlight_mode = false;
    bool vertical_orientation = true;
    QBrush brush_ = QBrush();
    QPen pen_ = QPen();
    qreal normalFontSize_ = 18.0;
    qreal highlightFontSize_ = 28.0;
    ProgressBarQLabel(QWidget* parent = nullptr);
    void copy(ProgressBarQLabel* other);
    bool scaledOutlineMode();
    void setScaledOutlineMode(bool state);
    double outlineThickness() const;
    double outlineThickness(QFont& f) const;
    void setOutlineThickness(double value);
    void setBrush(QBrush brush);
    void setBrush(QColor color);
    void setBrush(Qt::GlobalColor color);
    void setPen(QPen pen);
    void setPen(QColor color);
    void setPen(Qt::GlobalColor color);
    void setProgress(int progress, bool update = true);
    void setMinMax(int minimum, int maximum, bool update = true);
    void setFontSizes(qreal normalSize, qreal highlightSize);
    qreal normalFontSize() const { return this->normalFontSize_; }
    qreal highlightFontSize() const { return this->highlightFontSize_; }
    int& progress();
    int& minimum();
    int& maximum();
    QBrush& brush();
    QPen& pen();
    bool highlight_check();
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void paintEvent(QPaintEvent* e) override;
private:
    void applyFontSize(bool highlightActive);
    ~ProgressBarQLabel();
};

