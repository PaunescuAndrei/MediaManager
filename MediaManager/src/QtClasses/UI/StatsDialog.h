#pragma once
#include <QDialog>
#include "ui_StatsDialog.h"

class StatsDialog :
    public QDialog
{
    Q_OBJECT
public:
    StatsDialog(QWidget* parent = nullptr);
    ~StatsDialog();
    Ui::StatsDialog ui;
};

