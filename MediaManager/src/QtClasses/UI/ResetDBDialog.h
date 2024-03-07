#pragma once
#include <QDialog>
#include "ui_ResetDBDialog.h"
#include <string>

class ResetDBDialog :
    public QDialog
{
    Q_OBJECT
public:
    QString flag = "";
    ResetDBDialog(QString defaultDir,QWidget * parent = nullptr);
    void getDir();
    void set();
    void add();
    ~ResetDBDialog();
    Ui::ResetDBDialog ui;
};

