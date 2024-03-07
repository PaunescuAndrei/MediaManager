#pragma once
#include <QDialog>
#include "ui_InsertDialog.h"

class InsertDialog :
    public QDialog
{
    Q_OBJECT
public:
    InsertDialog(QWidget* parent = nullptr);
    void getFile();
    ~InsertDialog();
    Ui::InsertDialog ui;
};

