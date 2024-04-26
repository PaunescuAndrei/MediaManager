#pragma once
#include <QLabel>
#include <QString>
#include <tuple>

class currentVideoQLabel :public QLabel
{
    Q_OBJECT

public:
    int id = -1;
    QString name = "";
    QString path = "";
    QString author = "";
    bool toggled_path = false;
    currentVideoQLabel(QWidget* parent = nullptr);
    void setValues(int id, QString path, QString name = "", QString author = "", bool update = true);
    void setValues(std::tuple<int, QString, QString, QString> values, bool update = true);
    void updateText();
    void mousePressEvent(QMouseEvent* e) override;
    ~currentVideoQLabel();
};

