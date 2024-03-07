#pragma once
#include <QDialog>
#include "ui_DeleteDialog.h"

class DeleteDialog :
    public QDialog
{
    Q_OBJECT
public:
    DeleteDialog(QWidget* parent = nullptr);
    ~DeleteDialog();
private:
    Ui::DeleteDialog ui;
};

