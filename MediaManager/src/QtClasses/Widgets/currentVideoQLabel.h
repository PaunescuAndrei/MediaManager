#pragma once
#include "customQLabel.h"
#include <QString>
#include <tuple>

class currentVideoQLabel :public customQLabel
{
    Q_OBJECT

public:
    int id = -1;
    QString name = "";
    QString path = "";
    QString author = "";
    QString tags = "";
    bool toggled_path = false;
    currentVideoQLabel(QWidget* parent = nullptr);
    void setValues(int id, QString path, QString name = "", QString author = "", QString tags = "");
    void setValues(std::tuple<int, QString, QString, QString, QString> values);
    void updateText();
    void mousePressEvent(QMouseEvent* e) override;
    ~currentVideoQLabel();
};

