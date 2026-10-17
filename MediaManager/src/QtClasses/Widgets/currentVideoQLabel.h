#pragma once
#include "customQLabel.h"
#include <QString>
#include <tuple>
#include <QMutex>

class currentVideoQLabel :public customQLabel
{
    Q_OBJECT

public:
    int id = -1;
    QString name = "";
    // GUI thread only: written under path_lock by setValues(); safe to read directly on
    // the GUI. Other threads must use currentPath().
    QString path = "";
    QString author = "";
    QString tags = "";
    bool toggled_path = false;
    currentVideoQLabel(QWidget* parent = nullptr);
    void setValues(int id, QString path, QString name = "", QString author = "", QString tags = "");
    void setValues(std::tuple<int, QString, QString, QString, QString> values);
    void updateText();
    void mousePressEvent(QMouseEvent* e) override;
    // Locked copy of `path` for readers on other threads (the watcher).
    QString currentPath() const;
    ~currentVideoQLabel();
private:
    mutable QMutex path_lock;
};

