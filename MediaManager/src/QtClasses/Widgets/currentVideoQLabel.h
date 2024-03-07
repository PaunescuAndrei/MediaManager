#pragma once
#include <QLabel>
#include <QString>
#include <tuple>

class currentVideoQLabel :public QLabel
{
    Q_OBJECT

public:
    QString name = "";
    QString path = "";
    QString author = "";
    bool toggled_path = false;
    currentVideoQLabel(QWidget* parent = nullptr);
    void setValues(QString path, QString name = "", QString author = "", bool update = true);
    void setValues(std::tuple<QString, QString, QString> values, bool update = true);
    void updateText();
    void mousePressEvent(QMouseEvent* e) override;
    ~currentVideoQLabel();
};

