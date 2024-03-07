#pragma once
#include <QDialog>
#include "ui_setCounterDialog.h"
#include <string>
#include "stylesQt.h"

class setCounterDialog : public QDialog
{
    Q_OBJECT
public:
    std::string flag = "";
    setCounterDialog(QWidget* parent = nullptr);
    void set();
    void add();
    void substract();
    ~setCounterDialog();
    Ui::setCounterDialog ui;
};