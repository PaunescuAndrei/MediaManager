#pragma once
#include <QScrollBar>
#include <QScrollArea>

class customScrollArea : public QScrollArea
{
    Q_OBJECT

public:
    customScrollArea(QWidget* parent = nullptr) : QScrollArea(){

    }
    ~customScrollArea() {

    }
    QSize minimumSizeHint() const override {
        return QSize(0,0);
    }
    //QSize sizeHint() const override
    //{
    //    return QSize(0,0);
    //}
    //QSize viewportSizeHint() const override
    //{
    //    return QSize(0, 0);
    //}
};